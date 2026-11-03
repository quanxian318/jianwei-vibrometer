#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace vm {

// Single-threaded epoll loop with millisecond timers and a thread-safe task
// queue. Services that are not thread-safe run all their work on this loop.
class EventLoop {
 public:
  using FdHandler = std::function<void(uint32_t events)>;
  using Task = std::function<void()>;

  EventLoop();
  ~EventLoop();
  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;

  bool addFd(int fd, uint32_t events, FdHandler handler);
  void removeFd(int fd);

  int addTimer(int64_t intervalMs, Task task, bool repeat = true);
  void cancelTimer(int id);

  void post(Task task);  // any thread
  // Runs f on the loop thread and waits for the result (must not be called from the loop thread).
  template <typename R>
  R call(std::function<R()> f) {
    if (inLoopThread()) return f();
    auto p = std::make_shared<std::promise<R>>();
    std::future<R> fut = p->get_future();
    post([p, f]() { p->set_value(f()); });
    return fut.get();
  }
  void runSync(Task f) {
    if (inLoopThread()) {
      f();
      return;
    }
    auto p = std::make_shared<std::promise<void>>();
    std::future<void> fut = p->get_future();
    post([p, f]() {
      f();
      p->set_value();
    });
    fut.get();
  }

  void run();
  void stop();  // any thread
  bool inLoopThread() const { return std::this_thread::get_id() == loopThread_; }

 private:
  struct Timer {
    int id;
    int64_t dueMs;
    int64_t intervalMs;
    bool repeat;
    Task task;
  };
  void wake();
  void runTimers();
  int nextTimeoutMs() const;

  int epfd_ = -1;
  int wakeFd_ = -1;
  bool running_ = false;
  std::thread::id loopThread_;
  std::map<int, FdHandler> handlers_;
  std::vector<Timer> timers_;
  int nextTimerId_ = 1;
  std::mutex taskMutex_;
  std::vector<Task> tasks_;
};

}  // namespace vm
