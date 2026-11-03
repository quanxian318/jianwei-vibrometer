#pragma once
#include <Arduino.h>

#include <string>

#include <vibproto.h>

// Protocol V1 request handling: HELLO session, seq replay cache (32 entries,
// same seq + same payload -> cached response, same seq + other payload ->
// SEQ_CONFLICT), session check and the command table.
namespace router {

void begin(const std::string& bootId);
void onFrame(const vp::Frame& frame);
// Pushes not-yet-sent events to the AX as kind=3 frames while the TX queue has room.
void pumpEvents();
bool sessionActive();
const std::string& sessionId();
void describe(vp::JsonValue& obj);

}  // namespace router
