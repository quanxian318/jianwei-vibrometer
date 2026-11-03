// PS-Nano protocol V1 binary framing (see 06_通信协议与配置/PS_Nano协议V1.md).
//
//   offset 0  : A5 5A
//   offset 2  : version(1) kind(1) opcode(1) flags(1) seq(2 LE) length(2 LE)
//   offset 10 : JSON object payload, `length` bytes (<= 1024)
//   end       : CRC-16/CCITT-FALSE (LE) over offset 2 .. end of payload
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

#include "vp_json.h"

namespace vp {

const uint8_t kSof0 = 0xA5;
const uint8_t kSof1 = 0x5A;
const uint8_t kProtocolVersion = 1;
const size_t kMaxPayload = 1024;
const size_t kHeaderBytes = 10;    // SOF + 8 header bytes
const size_t kFrameOverhead = 12;  // header bytes + CRC
const size_t kMaxFrame = kMaxPayload + kFrameOverhead;
const int kPayloadMaxDepth = 16;

enum FrameKind : uint8_t { kKindRequest = 1, kKindResponse = 2, kKindEvent = 3 };

enum Opcode : uint8_t {
  kOpHello = 0x01,
  kOpGetStatus = 0x02,
  kOpSetNet = 0x10,
  kOpGetTime = 0x11,
  kOpPlayVoice = 0x20,
  kOpStopVoice = 0x21,
  kOpGetSdInfo = 0x30,
  kOpDrainEvents = 0x40,
  kOpAckEvents = 0x41,
  kOpPreparePowerOff = 0x50,
  // App gateway extension (protocol V1.1, AX -> Nano, see 15_手机APP_20261025):
  // AX publishes snapshots that the Nano serves to the phone app over Wi-Fi
  // and answers the APP_COMMAND events the Nano queued for it.
  kOpPublish = 0x60,
  kOpCommandResult = 0x62,
  kOpEvent = 0x40,  // opcode used by kind=3 event frames
};

uint16_t crc16CcittFalse(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);

bool frameHeaderValid(uint8_t version, uint8_t kind, uint8_t opcode, uint8_t flags, uint16_t seq,
                      uint16_t length);

// payloadJson must already be a serialized JSON object (<= kMaxPayload bytes).
bool encodeFrameRaw(uint8_t kind, uint8_t opcode, uint16_t seq, const std::string& payloadJson,
                    std::string& out);
// Serializes `payload` (must be an object with finite numbers) and frames it.
bool encodeFrame(uint8_t kind, uint8_t opcode, uint16_t seq, const JsonValue& payload,
                 std::string& out);

struct Frame {
  uint8_t kind = 0;
  uint8_t opcode = 0;
  uint16_t seq = 0;
  std::string payloadJson;  // payload bytes exactly as received
  JsonValue payload;        // parsed JSON object
};

enum DecodeResult { kDecodeOk = 0, kDecodeShort, kDecodeHeader, kDecodeSize, kDecodeCrc, kDecodeJson };

// Decodes one complete frame (len must equal 12 + payload length).
DecodeResult decodeFrame(const uint8_t* data, size_t len, Frame& out);

// Incremental decoder that tolerates arbitrary fragmentation, concatenation and
// noise. Invalid candidates drop their first byte and the search continues,
// exactly as StreamDecoder in protocol_reference.py.
class StreamDecoder {
 public:
  struct Stats {
    uint32_t frames;
    uint32_t headerErrors;
    uint32_t crcErrors;
    uint32_t jsonErrors;
    uint32_t partialTimeouts;
    uint32_t discardedBytes;
  };
  typedef std::function<void(const Frame&)> Handler;

  StreamDecoder();
  size_t feed(const uint8_t* data, size_t len, const Handler& onFrame);
  // Call after the inter-byte deadline (200 ms) passed; returns true if a partial frame was dropped.
  bool expirePartial();
  bool hasPartial() const { return len_ > 0; }
  const Stats& stats() const { return stats_; }
  uint32_t totalErrors() const;
  void resetStats();
  void clear() { len_ = 0; }

 private:
  void process(const Handler& onFrame, size_t& count);
  void discard(size_t n);

  static const size_t kChunk = 512;
  uint8_t buf_[kMaxFrame + kChunk + 16];
  size_t len_;
  Stats stats_;
};

}  // namespace vp
