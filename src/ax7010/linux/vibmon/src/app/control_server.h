#pragma once

#include <functional>
#include <map>
#include <string>

#include "util/event_loop.h"
#include "util/json_util.h"

namespace vm {

// Local control socket: one JSON object per line in, one per line out.
// Runs on the main loop; used by vibmonctl and for scripted acceptance tests.
class ControlServer {
 public:
  using Handler = std::function<JsonValue(const JsonValue& cmd)>;
  ControlServer(EventLoop& loop, Handler handler) : loop_(loop), handler_(std::move(handler)) {}
  ~ControlServer();
  bool listen(const std::string& path);

 private:
  void accept();
  void onClient(int fd);
  void closeClient(int fd);

  EventLoop& loop_;
  Handler handler_;
  int listenFd_ = -1;
  std::string path_;
  std::map<int, std::string> buffers_;
};

}  // namespace vm
