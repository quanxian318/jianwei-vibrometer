#pragma once
#include <Arduino.h>

#include <deque>
#include <string>
#include <vector>

#include <vibproto.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Bounded, thread-safe queue of protocol events (protocol V1 "事件与IRQ").
// Events stay queued until AX acknowledges them; DRAIN only reads. When the
// queue is full the oldest event is dropped and queue_overflow increments.
class EventStore {
 public:
  void begin(const std::string& bootId);

  // Creates an event with a new id; safe from any task (not from ISRs).
  // Returns the event id, or 0 when the event could not be encoded.
  uint32_t post(const char* eventType, const vp::JsonValue& data);

  bool hasUnacked() const;
  uint32_t lastId() const;
  uint32_t ackedThrough() const;
  uint32_t overflowCount() const;
  size_t pendingCount() const;
  const std::string& bootId() const { return bootId_; }

  // Next event not yet pushed proactively over UART.
  bool nextUnsent(std::string& payloadJson, uint32_t& id);
  void markSent(uint32_t id);

  // Collects raw event JSON objects with id > after, honouring limit and a
  // byte budget for the joined array content.
  void collect(uint32_t after, size_t limit, size_t byteBudget, std::vector<std::string>& out,
               uint32_t& nextAfter, bool& more) const;

  // Removes events with id <= through. Fails when through > lastId().
  bool ack(uint32_t through);

  // Adds {"last_event_id","acked_through","pending"} to obj.
  void describe(vp::JsonValue& obj) const;

 private:
  struct Entry {
    uint32_t id;
    std::string json;
  };
  mutable SemaphoreHandle_t mutex_ = nullptr;
  std::string bootId_;
  std::deque<Entry> queue_;
  uint32_t nextId_ = 1;
  uint32_t ackedThrough_ = 0;
  uint32_t sentThrough_ = 0;
  uint32_t overflow_ = 0;
};

extern EventStore g_events;
