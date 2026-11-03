#include "wav_parser.h"

#include <string.h>

namespace {
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

WavResult wavParse(WavReadFn read, void* ctx, uint32_t fileSize, WavInfo& out) {
  uint8_t hdr[12];
  if (fileSize < 12 || read(ctx, 0, hdr, 12) != 12) return kWavIoError;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return kWavNotRiff;

  bool haveFmt = false, haveData = false;
  uint16_t format = 0;
  WavInfo info;
  uint32_t pos = 12;
  // A RIFF file has a handful of chunks; the bound protects against loops on corrupt sizes.
  for (int guard = 0; guard < 64 && static_cast<uint64_t>(pos) + 8 <= fileSize; ++guard) {
    uint8_t ch[8];
    if (read(ctx, pos, ch, 8) != 8) return kWavIoError;
    const uint32_t size = le32(ch + 4);
    const uint32_t body = pos + 8;
    if (memcmp(ch, "fmt ", 4) == 0) {
      if (size < 16) return kWavUnsupported;
      uint8_t f[40];
      size_t n = size < sizeof(f) ? size : sizeof(f);
      if (read(ctx, body, f, n) != n) return kWavIoError;
      format = le16(f);
      info.channels = le16(f + 2);
      info.sampleRate = le32(f + 4);
      info.blockAlign = le16(f + 12);
      info.bitsPerSample = le16(f + 14);
      if (format == 0xFFFE) {
        // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts at offset 24.
        if (n < 26) return kWavUnsupported;
        format = le16(f + 24);
      }
      haveFmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      info.dataOffset = body;
      uint32_t avail = fileSize > body ? fileSize - body : 0;
      info.dataSize = size < avail ? size : avail;
      haveData = true;
      if (haveFmt) break;
    }
    // Next chunk: size plus the pad byte that keeps chunks word aligned.
    const uint64_t next = static_cast<uint64_t>(body) + size + (size & 1u);
    if (next > 0xFFFFFFFFull) break;
    pos = static_cast<uint32_t>(next);
  }
  if (!haveFmt) return kWavNoFmt;
  if (!haveData) return kWavNoData;
  if (format != 1 || info.bitsPerSample != 16 || (info.channels != 1 && info.channels != 2) ||
      info.blockAlign != info.channels * 2 || info.sampleRate < 8000 || info.sampleRate > 48000) {
    return kWavUnsupported;
  }
  info.dataSize -= info.dataSize % info.blockAlign;
  out = info;
  return kWavOk;
}

const char* wavResultName(WavResult r) {
  switch (r) {
    case kWavOk: return "ok";
    case kWavIoError: return "io_error";
    case kWavNotRiff: return "not_riff_wave";
    case kWavNoFmt: return "no_fmt_chunk";
    case kWavNoData: return "no_data_chunk";
    case kWavUnsupported: return "unsupported_format";
  }
  return "unknown";
}
