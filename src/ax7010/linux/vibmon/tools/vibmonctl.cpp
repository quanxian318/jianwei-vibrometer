// vibmonctl - command-line client of vibmond's control socket.
//   vibmonctl [-s /run/vibmon.sock] <cmd> [key=value ...]
// e.g. vibmonctl status | measure_start duration_s=10 | print | light on=true percent=80
//      laser on=true | roi x=0.5 y=0.4 w=80 h=48 | voice asset=measure_done
//      set_net ssid=lab password=secret
//      settings | set duration_s=30 warn_pp=0.5 alarm_pp=1 light_percent=80 | shutdown
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "util/json_util.h"

using vm::JsonValue;

namespace {
JsonValue typed(const std::string& v) {
  if (v == "true") return JsonValue(true);
  if (v == "false") return JsonValue(false);
  char* end = nullptr;
  const double d = strtod(v.c_str(), &end);
  if (!v.empty() && end && *end == 0) {
    if (d == static_cast<long long>(d) && v.find('.') == std::string::npos) return JsonValue(static_cast<long long>(d));
    return JsonValue(d);
  }
  return JsonValue(v);
}
}  // namespace

int main(int argc, char** argv) {
  std::string path = "/run/vibmon.sock";
  int i = 1;
  if (i + 1 < argc && !strcmp(argv[i], "-s")) {
    path = argv[i + 1];
    i += 2;
  }
  if (i >= argc) {
    fprintf(stderr, "usage: %s [-s socket] <cmd> [key=value ...]\n", argv[0]);
    return 2;
  }
  JsonValue cmd = JsonValue::makeObject();
  cmd.set("cmd", argv[i++]);
  for (; i < argc; ++i) {
    const char* eq = strchr(argv[i], '=');
    if (!eq) {
      fprintf(stderr, "argument %s is not key=value\n", argv[i]);
      return 2;
    }
    const std::string key(argv[i], static_cast<size_t>(eq - argv[i]));
    const std::string val(eq + 1);
    cmd.set(key, typed(val));
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  if (fd < 0 || connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    fprintf(stderr, "cannot connect to %s (is vibmond running?)\n", path.c_str());
    return 1;
  }
  const std::string line = vp::jsonSerialize(cmd) + "\n";
  if (write(fd, line.data(), line.size()) != static_cast<ssize_t>(line.size())) return 1;
  std::string reply;
  char buf[4096];
  ssize_t n;
  while (reply.find('\n') == std::string::npos && (n = read(fd, buf, sizeof(buf))) > 0) reply.append(buf, n);
  close(fd);
  JsonValue r;
  if (!vp::jsonParse(reply, r)) {
    fputs(reply.c_str(), stdout);
    return 1;
  }
  fputs(vm::jsonPretty(r).c_str(), stdout);
  return vm::jBool(r, "ok", false) ? 0 : 1;
}
