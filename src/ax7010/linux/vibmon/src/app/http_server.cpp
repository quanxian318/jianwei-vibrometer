#include "app/http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "http";
constexpr size_t kMaxHeader = 8192;
constexpr size_t kMaxBody = 65536;
constexpr size_t kMaxConns = 16;

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

std::string HttpRequest::header(const char* name) const {
  auto it = headers.find(name);
  return it == headers.end() ? std::string() : it->second;
}

std::string HttpRequest::param(const char* name) const {
  const std::string key = name;
  size_t pos = 0;
  while (pos <= query.size()) {
    size_t amp = query.find('&', pos);
    if (amp == std::string::npos) amp = query.size();
    const std::string kv = query.substr(pos, amp - pos);
    const size_t eq = kv.find('=');
    if (HttpServer::urlDecode(kv.substr(0, eq)) == key)
      return eq == std::string::npos ? std::string() : HttpServer::urlDecode(kv.substr(eq + 1));
    pos = amp + 1;
  }
  return std::string();
}

const char* HttpServer::reason(int status) {
  switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

std::string HttpServer::urlDecode(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      o += ' ';
    } else if (s[i] == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) &&
               isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      o += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

HttpServer::~HttpServer() {
  while (!conns_.empty()) close(conns_.begin()->first);
  if (listenFd_ >= 0) {
    loop_.removeFd(listenFd_);
    ::close(listenFd_);
  }
}

bool HttpServer::listen(int port) {
  listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listenFd_ < 0) return false;
  int one = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(listenFd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(listenFd_, 8) != 0) {
    LOGE(kTag, "cannot listen on port %d: %s", port, strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  socklen_t len = sizeof(a);
  getsockname(listenFd_, reinterpret_cast<sockaddr*>(&a), &len);
  port_ = ntohs(a.sin_port);
  loop_.addFd(listenFd_, EPOLLIN, [this](uint32_t) { accept(); });
  return true;
}

void HttpServer::accept() {
  for (;;) {
    sockaddr_in a{};
    socklen_t len = sizeof(a);
    int fd = accept4(listenFd_, reinterpret_cast<sockaddr*>(&a), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return;
    if (conns_.size() >= kMaxConns) {
      ::close(fd);
      continue;
    }
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    Conn& c = conns_[fd];
    c.lastMs = monoMs();
    c.peer = ip;
    loop_.addFd(fd, EPOLLIN | EPOLLRDHUP, [this, fd](uint32_t ev) { onEvent(fd, ev); });
  }
}

void HttpServer::close(int fd) {
  loop_.removeFd(fd);
  ::close(fd);
  conns_.erase(fd);
}

void HttpServer::sweep(int64_t nowMs) {
  std::vector<int> stale;
  for (const auto& c : conns_)
    if (nowMs - c.second.lastMs > 10000) stale.push_back(c.first);
  for (int fd : stale) close(fd);
}

void HttpServer::onEvent(int fd, uint32_t ev) {
  auto it = conns_.find(fd);
  if (it == conns_.end()) return;
  Conn& c = it->second;
  if (ev & EPOLLERR) {
    close(fd);
    return;
  }
  c.lastMs = monoMs();
  if (c.responding) {
    if (ev & (EPOLLOUT | EPOLLHUP)) flush(fd, c);
    return;
  }
  if (ev & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) onReadable(fd, c);
}

int HttpServer::parse(const std::string& in, HttpRequest& req) {
  const size_t end = in.find("\r\n\r\n");
  if (end == std::string::npos) return in.size() > kMaxHeader ? -413 : 0;
  size_t lineEnd = in.find("\r\n");
  const std::string first = in.substr(0, lineEnd);
  const size_t sp1 = first.find(' '), sp2 = first.rfind(' ');
  if (sp1 == std::string::npos || sp2 == sp1) return -400;
  req.method = first.substr(0, sp1);
  std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
  const size_t q = target.find('?');
  req.path = target.substr(0, q);
  req.query = q == std::string::npos ? std::string() : target.substr(q + 1);
  size_t pos = lineEnd + 2;
  while (pos < end) {
    size_t e = in.find("\r\n", pos);
    const std::string line = in.substr(pos, e - pos);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    pos = e + 2;
  }
  size_t length = 0;
  const std::string cl = req.header("content-length");
  if (!cl.empty()) {
    char* tail = nullptr;
    const unsigned long n = strtoul(cl.c_str(), &tail, 10);
    if (*tail != 0) return -400;
    if (n > kMaxBody) return -413;
    length = n;
  }
  if (in.size() < end + 4 + length) return 0;
  req.body = in.substr(end + 4, length);
  return static_cast<int>(end + 4 + length);
}

void HttpServer::onReadable(int fd, Conn& c) {
  char buf[4096];
  for (;;) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n == 0) {
      close(fd);  // the client gave up before a complete request
      return;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR) break;
      close(fd);
      return;
    }
    c.in.append(buf, static_cast<size_t>(n));
    if (c.in.size() > kMaxHeader + kMaxBody) break;
  }
  HttpRequest req;
  req.peer = c.peer;
  const int r = parse(c.in, req);
  if (r == 0) return;
  HttpResponse resp;
  if (r < 0) {
    resp.status = -r;
    resp.body = std::string("{\"ok\":false,\"error\":\"") + reason(resp.status) + "\"}";
  } else if (req.method == "OPTIONS") {
    resp.status = 204;  // CORS pre-flight
  } else {
    resp = handler_(req);
  }
  std::string& o = c.out;
  o = "HTTP/1.1 " + std::to_string(resp.status) + " " + reason(resp.status) + "\r\n";
  if (!resp.body.empty() || resp.status != 204) o += "Content-Type: " + resp.contentType + "\r\n";
  o += "Content-Length: " + std::to_string(resp.body.size()) + "\r\n";
  o += "Connection: close\r\nCache-Control: no-store\r\n";
  o += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Authorization, Content-Type\r\n";
  o += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  for (const auto& h : resp.headers) o += h.first + ": " + h.second + "\r\n";
  o += "\r\n";
  o += resp.body;
  c.responding = true;
  c.in.clear();
  flush(fd, c);
}

void HttpServer::flush(int fd, Conn& c) {
  while (c.sent < c.out.size()) {
    const ssize_t n = ::send(fd, c.out.data() + c.sent, c.out.size() - c.sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        // Wait for room in the socket buffer.
        loop_.removeFd(fd);
        loop_.addFd(fd, EPOLLOUT | EPOLLRDHUP, [this, fd](uint32_t ev) { onEvent(fd, ev); });
        return;
      }
      close(fd);
      return;
    }
    c.sent += static_cast<size_t>(n);
  }
  shutdown(fd, SHUT_WR);
  close(fd);
}

}  // namespace vm
