#include "util/event_loop.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

EventLoop::EventLoop() {
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = wakeFd_;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeFd_, &ev);
  loopThread_ = std::this_thread::get_id();
}

EventLoop::~EventLoop() {
  if (wakeFd_ >= 0) close(wakeFd_);
  if (epfd_ >= 0) close(epfd_);
}

bool EventLoop::addFd(int fd, uint32_t events, FdHandler handler) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
    LOGE("loop", "epoll add fd %d failed: errno %d", fd, errno);
    return false;
  }
  handlers_[fd] = std::move(handler);
  return true;
}

void EventLoop::removeFd(int fd) {
  epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
  handlers_.erase(fd);
}

int EventLoop::addTimer(int64_t intervalMs, Task task, bool repeat) {
  Timer t;
  t.id = nextTimerId_++;
  t.intervalMs = intervalMs < 1 ? 1 : intervalMs;
  t.dueMs = monoMs() + t.intervalMs;
  t.repeat = repeat;
  t.task = std::move(task);
  timers_.push_back(std::move(t));
  return timers_.back().id;
}

void EventLoop::cancelTimer(int id) {
  timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; }),
                timers_.end());
}

void EventLoop::post(Task task) {
  {
    std::lock_guard<std::mutex> lock(taskMutex_);
    tasks_.push_back(std::move(task));
  }
  wake();
}

void EventLoop::wake() {
  uint64_t one = 1;
  ssize_t n = write(wakeFd_, &one, sizeof(one));
  (void)n;
}

void EventLoop::stop() {
  post([this]() { running_ = false; });
}

int EventLoop::nextTimeoutMs() const {
  if (timers_.empty()) return 1000;
  int64_t now = monoMs();
  int64_t best = INT64_MAX;
  for (const auto& t : timers_) best = std::min(best, t.dueMs);
  int64_t wait = best - now;
  if (wait < 0) wait = 0;
  if (wait > 1000) wait = 1000;
  return static_cast<int>(wait);
}

void EventLoop::runTimers() {
  int64_t now = monoMs();
  // Collect due ids first: callbacks may add or cancel timers.
  std::vector<int> due;
  for (const auto& t : timers_) {
    if (t.dueMs <= now) due.push_back(t.id);
  }
  for (int id : due) {
    auto it = std::find_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; });
    if (it == timers_.end()) continue;
    Task task = it->task;
    if (it->repeat) {
      it->dueMs += it->intervalMs;
      if (it->dueMs <= now) it->dueMs = now + it->intervalMs;  // skip missed periods
    } else {
      timers_.erase(it);
    }
    task();
  }
}

void EventLoop::run() {
  loopThread_ = std::this_thread::get_id();
  running_ = true;
  epoll_event events[16];
  while (running_) {
    int n = epoll_wait(epfd_, events, 16, nextTimeoutMs());
    if (n < 0 && errno != EINTR) {
      LOGE("loop", "epoll_wait failed: errno %d", errno);
      break;
    }
    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;
      if (fd == wakeFd_) {
        uint64_t v;
        while (read(wakeFd_, &v, sizeof(v)) > 0) {
        }
        continue;
      }
      auto it = handlers_.find(fd);
      if (it != handlers_.end()) {
        FdHandler h = it->second;  // copy: the handler may remove itself
        h(events[i].events);
      }
    }
    std::vector<Task> tasks;
    {
      std::lock_guard<std::mutex> lock(taskMutex_);
      tasks.swap(tasks_);
    }
    for (auto& t : tasks) t();
    runTimers();
  }
}

}  // namespace vm
