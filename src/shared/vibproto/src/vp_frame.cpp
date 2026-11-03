#include "vp_frame.h"

#include <string.h>

namespace vp {

uint16_t crc16CcittFalse(const uint8_t* data, size_t len, uint16_t crc) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

bool frameHeaderValid(uint8_t version, uint8_t kind, uint8_t opcode, uint8_t flags, uint16_t seq,
                      uint16_t length) {
  (void)opcode;  // any 0..255 value is structurally valid
  if (version != kProtocolVersion || flags != 0 || length > kMaxPayload) return false;
  if (kind == kKindEvent) return seq == 0;
  if (kind == kKindRequest || kind == kKindResponse) return seq >= 1;
  return false;
}

bool encodeFrameRaw(uint8_t kind, uint8_t opcode, uint16_t seq, const std::string& payloadJson,
                    std::string& out) {
  if (payloadJson.size() > kMaxPayload) return false;
  const uint16_t length = static_cast<uint16_t>(payloadJson.size());
  if (!frameHeaderValid(kProtocolVersion, kind, opcode, 0, seq, length)) return false;
  out.clear();
  out.reserve(kFrameOverhead + length);
  out += static_cast<char>(kSof0);
  out += static_cast<char>(kSof1);
  out += static_cast<char>(kProtocolVersion);
  out += static_cast<char>(kind);
  out += static_cast<char>(opcode);
  out += static_cast<char>(0);
  out += static_cast<char>(seq & 0xFF);
  out += static_cast<char>(seq >> 8);
  out += static_cast<char>(length & 0xFF);
  out += static_cast<char>(length >> 8);
  out += payloadJson;
  const uint16_t crc = crc16CcittFalse(reinterpret_cast<const uint8_t*>(out.data()) + 2, out.size() - 2);
  out += static_cast<char>(crc & 0xFF);
  out += static_cast<char>(crc >> 8);
  return true;
}

bool encodeFrame(uint8_t kind, uint8_t opcode, uint16_t seq, const JsonValue& payload, std::string& out) {
  if (!payload.isObject()) return false;
  std::string body;
  if (!jsonSerializeChecked(payload, body)) return false;
  return encodeFrameRaw(kind, opcode, seq, body, out);
}

DecodeResult decodeFrame(const uint8_t* data, size_t len, Frame& out) {
  if (len < kFrameOverhead || data[0] != kSof0 || data[1] != kSof1) return kDecodeShort;
  const uint8_t version = data[2], kind = data[3], opcode = data[4], flags = data[5];
  const uint16_t seq = static_cast<uint16_t>(data[6] | (data[7] << 8));
  const uint16_t length = static_cast<uint16_t>(data[8] | (data[9] << 8));
  if (!frameHeaderValid(version, kind, opcode, flags, seq, length)) return kDecodeHeader;
  if (len != kFrameOverhead + length) return kDecodeSize;
  const uint16_t crc = crc16CcittFalse(data + 2, 8 + length);
  const uint16_t got = static_cast<uint16_t>(data[len - 2] | (data[len - 1] << 8));
  if (crc != got) return kDecodeCrc;
  const char* text = reinterpret_cast<const char*>(data + kHeaderBytes);
  JsonValue parsed;
  if (!jsonParse(text, length, parsed, nullptr, kPayloadMaxDepth) || !parsed.isObject()) return kDecodeJson;
  out.kind = kind;
  out.opcode = opcode;
  out.seq = seq;
  out.payloadJson.assign(text, length);
  out.payload = parsed;
  return kDecodeOk;
}

StreamDecoder::StreamDecoder() : len_(0) { resetStats(); }

void StreamDecoder::resetStats() { memset(&stats_, 0, sizeof(stats_)); }

uint32_t StreamDecoder::totalErrors() const {
  return stats_.headerErrors + stats_.crcErrors + stats_.jsonErrors + stats_.partialTimeouts;
}

bool StreamDecoder::expirePartial() {
  if (len_ == 0) return false;
  stats_.partialTimeouts++;
  stats_.discardedBytes += static_cast<uint32_t>(len_);
  len_ = 0;
  return true;
}

void StreamDecoder::discard(size_t n) {
  if (n >= len_) {
    len_ = 0;
    return;
  }
  memmove(buf_, buf_ + n, len_ - n);
  len_ -= n;
}

size_t StreamDecoder::feed(const uint8_t* data, size_t len, const Handler& onFrame) {
  size_t count = 0;
  // Bounded chunks keep the retained buffer small even under continuous noise.
  for (size_t off = 0; off < len; off += kChunk) {
    size_t n = len - off < kChunk ? len - off : kChunk;
    memcpy(buf_ + len_, data + off, n);
    len_ += n;
    process(onFrame, count);
  }
  return count;
}

void StreamDecoder::process(const Handler& onFrame, size_t& count) {
  for (;;) {
    size_t start = len_;
    for (size_t i = 0; i + 1 < len_; ++i) {
      if (buf_[i] == kSof0 && buf_[i + 1] == kSof1) {
        start = i;
        break;
      }
    }
    if (start == len_) {
      // Keep a trailing A5 that may be the first half of the next SOF.
      bool keep = len_ > 0 && buf_[len_ - 1] == kSof0;
      size_t drop = keep ? len_ - 1 : len_;
      stats_.discardedBytes += static_cast<uint32_t>(drop);
      if (keep) {
        buf_[0] = kSof0;
        len_ = 1;
      } else {
        len_ = 0;
      }
      return;
    }
    if (start > 0) {
      stats_.discardedBytes += static_cast<uint32_t>(start);
      discard(start);
    }
    if (len_ < kHeaderBytes) return;
    const uint16_t seq = static_cast<uint16_t>(buf_[6] | (buf_[7] << 8));
    const uint16_t length = static_cast<uint16_t>(buf_[8] | (buf_[9] << 8));
    if (!frameHeaderValid(buf_[2], buf_[3], buf_[4], buf_[5], seq, length)) {
      stats_.headerErrors++;
      stats_.discardedBytes++;
      discard(1);
      continue;
    }
    const size_t size = kFrameOverhead + length;
    if (len_ < size) return;
    Frame frame;
    DecodeResult r = decodeFrame(buf_, size, frame);
    if (r != kDecodeOk) {
      if (r == kDecodeCrc) {
        stats_.crcErrors++;
      } else if (r == kDecodeJson) {
        stats_.jsonErrors++;
      } else {
        stats_.headerErrors++;
      }
      stats_.discardedBytes++;
      discard(1);
      continue;
    }
    discard(size);
    stats_.frames++;
    count++;
    if (onFrame) onFrame(frame);
  }
}

}  // namespace vp
