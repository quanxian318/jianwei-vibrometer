#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "util/event_loop.h"

namespace vm {

struct HttpRequest {
  std::string method, path, query;  // path without the query string, not decoded
  std::map<std::string, std::string> headers;  // names lower-case
  std::string body;
  std::string peer;  // client IPv4 address
  std::string header(const char* name) const;
  std::string param(const char* name) const;  // decoded query parameter, "" when absent
};

struct HttpResponse {
  int status = 200;
  std::string contentType = "application/json; charset=utf-8";
  std::string body;
  std::map<std::string, std::string> headers;
};

// Minimal HTTP/1.1 server for the phone app on the main event loop: one
// request per connection (Connection: close), bodies by Content-Length only,
// responses buffered and written as the socket accepts them. Every response
// carries permissive CORS headers so the app also runs in a desktop browser.
class HttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;
  HttpServer(EventLoop& loop, Handler handler) : loop_(loop), handler_(std::move(handler)) {}
  ~HttpServer();
  bool listen(int port);
  int port() const { return port_; }
  void sweep(int64_t nowMs);  // drops connections idle for more than 10 s

  static const char* reason(int status);
  static std::string urlDecode(const std::string& s);

 private:
  struct Conn {
    std::string in, out;
    size_t sent = 0;
    bool responding = false;
    int64_t lastMs = 0;
    std::string peer;
  };
  void accept();
  void onEvent(int fd, uint32_t ev);
  void onReadable(int fd, Conn& c);
  void flush(int fd, Conn& c);
  void close(int fd);
  // 0 = incomplete, > 0 = request parsed (bytes used), < 0 = HTTP error status.
  static int parse(const std::string& in, HttpRequest& req);

  EventLoop& loop_;
  Handler handler_;
  int listenFd_ = -1;
  int port_ = 0;
  std::map<int, Conn> conns_;
};

}  // namespace vm
