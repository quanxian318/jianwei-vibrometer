// 见微测振 for Windows: the same web app as the Android build, in an Edge
// WebView2 window (runtime preinstalled on Windows 10/11), with the native
// bridge window.VibmonNative the pages expect:
//   discover(timeoutMs, cb)  UDP "VIBMON_DISCOVER" to port 48300 on every
//                            adapter's broadcast address and 192.168.4.1
//   saveFile(name, mime, text)  writes into the user's Downloads folder
//   shareText(title, text)   copies to the clipboard
//   setTheme(dark)           dark title bar
//   keepScreenOn(on)         keeps the display awake during a capture
// The page itself is loaded from the web folder next to the executable.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>

#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "webview/webview.h"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace {

const unsigned short kDiscoveryPort = 48300;

std::string utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

// file:///C:/.../web/index.html, percent-encoding everything outside a safe set
// (the install path may hold spaces or Chinese characters).
std::string pageUrl() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  std::wstring exe(buf, n);
  std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
  std::string path = utf8(dir + L"\\web\\index.html");
  std::string url = "file:///";
  const char* hex = "0123456789ABCDEF";
  for (unsigned char c : path) {
    if (c == '\\') url += '/';
    else if (isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-' || c == '_' || c == '~') url += (char)c;
    else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
  }
  return url;
}

// Directed broadcast address of every IPv4 adapter that is up.
std::set<std::string> broadcastTargets() {
  std::set<std::string> out = {"255.255.255.255", "192.168.4.1"};
  ULONG len = 16 * 1024;
  std::vector<unsigned char> mem(len);
  auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(mem.data());
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, aa, &len) == ERROR_BUFFER_OVERFLOW) {
    mem.resize(len);
    aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(mem.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, aa, &len) != NO_ERROR) return out;
  }
  for (auto* a = aa; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
      if (sin->sin_family != AF_INET) continue;
      unsigned long ip = ntohl(sin->sin_addr.s_addr);
      unsigned long mask = u->OnLinkPrefixLength ? (0xFFFFFFFFul << (32 - u->OnLinkPrefixLength)) & 0xFFFFFFFFul : 0;
      in_addr b;
      b.s_addr = htonl((ip & mask) | (~mask & 0xFFFFFFFFul));
      char s[INET_ADDRSTRLEN];
      if (inet_ntop(AF_INET, &b, s, sizeof s)) out.insert(s);
    }
  }
  return out;
}

// Same protocol and answer handling as the Android shell: a few probe rounds
// (UDP may drop one), answers whose "service" is "vibmon", one per ip:port,
// each with the sender's address added as "ip". Returns a JSON array.
std::string discover(int timeoutMs) {
  std::string arr = "[";
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return "[]";
  BOOL yes = TRUE;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
  const std::set<std::string> targets = broadcastTargets();
  const char probe[] = "VIBMON_DISCOVER";
  auto now = [] { return std::chrono::steady_clock::now(); };
  auto end = now() + std::chrono::milliseconds(timeoutMs < 400 ? 400 : timeoutMs > 6000 ? 6000 : timeoutMs);
  auto nextProbe = now();
  std::set<std::string> seen;
  char buf[2048];
  while (now() < end) {
    if (now() >= nextProbe) {
      for (const auto& t : targets) {
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(kDiscoveryPort);
        if (inet_pton(AF_INET, t.c_str(), &to.sin_addr) == 1)
          sendto(s, probe, (int)sizeof(probe) - 1, 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
      }
      nextProbe = now() + std::chrono::milliseconds(600);
    }
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{0, 250000};
    if (select(0, &rd, nullptr, nullptr, &tv) <= 0) continue;
    sockaddr_in from{};
    int fl = sizeof from;
    int n = recvfrom(s, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fl);
    if (n <= 1) continue;
    std::string msg(buf, n);
    if (msg[0] != '{' || webview::detail::json_parse(msg, "service", 0) != "vibmon") continue;
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
    std::string port = webview::detail::json_parse(msg, "http_port", 0);
    if (!seen.insert(std::string(ip) + ":" + (port.empty() ? "80" : port)).second) continue;
    if (arr.size() > 1) arr += ',';
    arr += "{\"ip\":\"" + std::string(ip) + "\"," + msg.substr(1);
  }
  closesocket(s);
  return arr + "]";
}

std::string safeName(const std::string& name) {
  std::string out;
  for (char c : name) out += (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') ? c : '_';
  return out.empty() || out[0] == '.' ? "jianwei_" + out : out;
}

bool saveToDownloads(const std::string& name, const std::string& text) {
  PWSTR dir = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &dir))) return false;
  std::wstring path = std::wstring(dir) + L"\\" + wide(safeName(name));
  CoTaskMemFree(dir);
  FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return false;
  bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  return fclose(f) == 0 && ok;
}

bool copyToClipboard(HWND hwnd, const std::string& text) {
  std::wstring w = wide(text);
  if (!OpenClipboard(hwnd)) return false;
  EmptyClipboard();
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  bool ok = false;
  if (h) {
    memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(h);
    ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
    if (!ok) GlobalFree(h);
  }
  CloseClipboard();
  return ok;
}

std::string computerName() {
  wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
  return GetComputerNameW(buf, &n) ? utf8(std::wstring(buf, n)) : "PC";
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  try {
    webview::webview w(false, nullptr);
    w.set_title("见微测振");
    w.set_size(360, 600, WEBVIEW_HINT_MIN);
    w.set_size(440, 900, WEBVIEW_HINT_NONE);
    HWND hwnd = static_cast<HWND>(w.window().value());

    w.bind("__jwDiscover", [&](const std::string& seq, const std::string& req, void*) {
      int ms = atoi(webview::detail::json_parse(req, "", 0).c_str());
      std::thread([&w, seq, ms] { w.resolve(seq, 0, discover(ms)); }).detach();
    }, nullptr);
    w.bind("__jwSave", [&](const std::string& req) -> std::string {
      return saveToDownloads(webview::detail::json_parse(req, "", 0), webview::detail::json_parse(req, "", 2)) ? "true" : "false";
    });
    w.bind("__jwShare", [&](const std::string& req) -> std::string {
      return copyToClipboard(hwnd, webview::detail::json_parse(req, "", 1)) ? "true" : "false";
    });
    w.bind("__jwTheme", [&](const std::string& req) -> std::string {
      BOOL dark = webview::detail::json_parse(req, "", 0) == "true";
      DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
      return "true";
    });
    w.bind("__jwAwake", [&](const std::string& req) -> std::string {
      bool on = webview::detail::json_parse(req, "", 0) == "true";
      SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
      return "true";
    });
    // The pages call the bridge synchronously (Android's addJavascriptInterface);
    // these wrappers forward to the asynchronous bindings above.
    w.init(std::string(
        "window.VibmonNative = {"
        "discover: function (ms, cb) { window.__jwDiscover(ms).then(function (a) { var f = window.__jw && window.__jw[cb]; if (f) f(JSON.stringify(a || [])); },"
        "  function () { var f = window.__jw && window.__jw[cb]; if (f) f('[]'); }); },"
        "saveFile: function (name, mime, text) { window.__jwSave(name, mime, text).then(function (ok) { if (!ok && window.U) U.toast('保存失败', 'err'); }); return true; },"
        "shareText: function (title, text) { window.__jwShare(title, text).then(function (ok) { if (window.U) U.toast(ok ? '已复制到剪贴板' : '复制失败', ok ? undefined : 'err'); }); },"
        "setTheme: function (dark) { window.__jwTheme(!!dark); },"
        "keepScreenOn: function (on) { window.__jwAwake(!!on); },"
        "vibrate: function () {},"
        "appVersion: function () { return '" APP_VERSION "'; },"
        "deviceName: function () { return ") + webview::detail::json_escape("Windows · " + computerName()) + "; }"
        "};");
    // --selftest <file>: load the app, let the page report what it rendered
    // (plus a PNG of the 3D hero) into <file>, then quit. Used by CI on a real
    // Windows machine; exits 2 if the page never reports.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && std::wstring(argv[1]) == L"--selftest") {
      std::wstring out = argv[2];
      w.bind("__jwSelftest", [&w, out](const std::string& req) -> std::string {
        FILE* f = _wfopen(out.c_str(), L"wb");
        std::string body = webview::detail::json_parse(req, "", 0);
        if (f) { fwrite(body.data(), 1, body.size(), f); fclose(f); }
        w.terminate();
        return "true";
      });
      w.init("window.addEventListener('load', function () { setTimeout(function () {"
             "  var c = document.getElementById('heroTwin');"
             "  VibmonNative.discover(1500, '__st'); window.__jw = window.__jw || {};"
             "  window.__jw.__st = function (found) { window.__jwSelftest(JSON.stringify({"
             "    version: window.App && App.version, title: document.title, connect: !!document.getElementById('view-connect'),"
             "    hero: !!c, heroSize: c ? [c.width, c.height] : null, boards: Object.keys(window.PCB_BOARDS || {}),"
             "    bridge: Object.keys(window.VibmonNative || {}), discover: JSON.parse(found),"
             "    png: c ? c.toDataURL('image/png') : null })); };"
             "}, 3000); });");
      SetTimer(hwnd, 1, 30000, [](HWND, UINT, UINT_PTR, DWORD) { ExitProcess(2); });
    }
    LocalFree(argv);
    w.navigate(pageUrl());
    w.run();
  } catch (const webview::exception& e) {
    MessageBoxW(nullptr, wide(std::string("无法启动：") + e.what() + "\n\n需要 Microsoft Edge WebView2 运行时（Windows 10/11 通常已自带）。").c_str(), L"见微测振", MB_ICONERROR);
    return 1;
  }
  WSACleanup();
  return 0;
}
