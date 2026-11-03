#include "event_store.h"

#include "app_config.h"
#include "esp_timer.h"
#include "log.h"

EventStore g_events;

namespace {
class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};
}  // namespace

void EventStore::begin(const std::string& bootId) {
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
  bootId_ = bootId;
}

uint32_t EventStore::post(const char* eventType, const vp::JsonValue& data) {
  Lock lock(mutex_);
  const uint32_t id = nextId_;
  vp::JsonValue ev = vp::JsonValue::makeObject();
  ev.set("nano_boot_id", bootId_);
  ev.set("event_id", static_cast<unsigned long>(id));
  ev.set("event_type", eventType);
  ev.set("uptime_ms", static_cast<long long>(esp_timer_get_time() / 1000));
  ev.set("data", data.isObject() ? data : vp::JsonValue::makeObject());
  std::string json;
  if (!vp::jsonSerializeChecked(ev, json) || json.size() > cfg::kEventMaxBytes) {
    vp::JsonValue small = vp::JsonValue::makeObject();
    small.set("truncated", true);
    ev.set("data", small);
    if (!vp::jsonSerializeChecked(ev, json) || json.size() > cfg::kEventMaxBytes) return 0;
  }
  nextId_++;
  if (queue_.size() >= cfg::kEventQueueCapacity) {
    queue_.pop_front();
    overflow_++;
  }
  Entry e;
  e.id = id;
  e.json.swap(json);
  queue_.push_back(e);
  return id;
}

bool EventStore::hasUnacked() const {
  Lock lock(mutex_);
  return !queue_.empty();
}

uint32_t EventStore::lastId() const {
  Lock lock(mutex_);
  return nextId_ - 1;
}

uint32_t EventStore::ackedThrough() const {
  Lock lock(mutex_);
  return ackedThrough_;
}

uint32_t EventStore::overflowCount() const {
  Lock lock(mutex_);
  return overflow_;
}

size_t EventStore::pendingCount() const {
  Lock lock(mutex_);
  return queue_.size();
}

bool EventStore::nextUnsent(std::string& payloadJson, uint32_t& id) {
  Lock lock(mutex_);
  for (size_t i = 0; i < queue_.size(); ++i) {
    if (queue_[i].id > sentThrough_) {
      payloadJson = queue_[i].json;
      id = queue_[i].id;
      return true;
    }
  }
  return false;
}

void EventStore::markSent(uint32_t id) {
  Lock lock(mutex_);
  if (id > sentThrough_) sentThrough_ = id;
}

void EventStore::collect(uint32_t after, size_t limit, size_t byteBudget, std::vector<std::string>& out,
                         uint32_t& nextAfter, bool& more) const {
  Lock lock(mutex_);
  out.clear();
  nextAfter = after;
  more = false;
  size_t used = 0;
  for (size_t i = 0; i < queue_.size(); ++i) {
    const Entry& e = queue_[i];
    if (e.id <= after) continue;
    const size_t cost = e.json.size() + (out.empty() ? 0 : 1);
    if (out.size() >= limit || used + cost > byteBudget) {
      more = true;
      break;
    }
    out.push_back(e.json);
    used += cost;
    nextAfter = e.id;
  }
}

bool EventStore::ack(uint32_t through) {
  Lock lock(mutex_);
  if (through > nextId_ - 1) return false;
  while (!queue_.empty() && queue_.front().id <= through) queue_.pop_front();
  if (through > ackedThrough_) ackedThrough_ = through;
  if (through > sentThrough_) sentThrough_ = through;
  return true;
}

void EventStore::describe(vp::JsonValue& obj) const {
  Lock lock(mutex_);
  obj.set("last_event_id", static_cast<unsigned long>(nextId_ - 1));
  obj.set("acked_through", static_cast<unsigned long>(ackedThrough_));
  obj.set("pending", static_cast<unsigned long>(queue_.size()));
}
