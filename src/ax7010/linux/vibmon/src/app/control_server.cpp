#include "app/control_server.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "util/log.h"

namespace vm {

ControlServer::~ControlServer() {
  for (auto& b : buffers_) {
    loop_.removeFd(b.first);
    ::close(b.first);
  }
  if (listenFd_ >= 0) {
    loop_.removeFd(listenFd_);
    ::close(listenFd_);
    ::unlink(path_.c_str());
  }
}

bool ControlServer::listen(const std::string& path) {
  path_ = path;
  listenFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listenFd_ < 0) return false;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  ::unlink(path.c_str());
  if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listenFd_, 4) != 0) {
    LOGE("ctl", "cannot listen on %s: %s", path.c_str(), strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  loop_.addFd(listenFd_, EPOLLIN, [this](uint32_t) { accept(); });
  return true;
}

void ControlServer::accept() {
  for (;;) {
    int fd = accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return;
    buffers_[fd] = std::string();
    loop_.addFd(fd, EPOLLIN | EPOLLRDHUP, [this, fd](uint32_t ev) {
      if (ev & (EPOLLERR | EPOLLHUP)) {
        closeClient(fd);
        return;
      }
      onClient(fd);
    });
  }
}

void ControlServer::closeClient(int fd) {
  loop_.removeFd(fd);
  ::close(fd);
  buffers_.erase(fd);
}

void ControlServer::onClient(int fd) {
  char buf[1024];
  for (;;) {
    ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n == 0) {
      closeClient(fd);
      return;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EINTR) break;
      closeClient(fd);
      return;
    }
    std::string& b = buffers_[fd];
    b.append(buf, static_cast<size_t>(n));
    if (b.size() > 65536) {
      closeClient(fd);
      return;
    }
  }
  std::string& b = buffers_[fd];
  size_t pos;
  while ((pos = b.find('\n')) != std::string::npos) {
    std::string line = b.substr(0, pos);
    b.erase(0, pos + 1);
    JsonValue cmd, reply;
    std::string err;
    if (!vp::jsonParse(line, cmd, &err, 16) || !cmd.isObject()) {
      reply = JsonValue::makeObject();
      reply.set("ok", false);
      reply.set("error", "bad_json: " + err);
    } else {
      reply = handler_(cmd);
    }
    std::string out = vp::jsonSerialize(reply) + "\n";
    // Replies are small; a client that does not read them is dropped.
    if (::write(fd, out.data(), out.size()) != static_cast<ssize_t>(out.size())) {
      closeClient(fd);
      return;
    }
  }
}

}  // namespace vm
