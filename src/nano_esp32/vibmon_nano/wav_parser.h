#pragma once
#include <stddef.h>
#include <stdint.h>

// RIFF/WAVE header walker. Unknown chunks and odd-size padding are skipped,
// so files written by any editor work, not only 44-byte canonical headers.
struct WavInfo {
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint16_t blockAlign = 0;
  uint32_t sampleRate = 0;
  uint32_t dataOffset = 0;  // file offset of the first sample byte
  uint32_t dataSize = 0;    // bytes of sample data (clipped to the file size)
};

enum WavResult {
  kWavOk = 0,
  kWavIoError,
  kWavNotRiff,
  kWavNoFmt,
  kWavNoData,
  kWavUnsupported,
};

// read(ctx, offset, buf, len) must return the number of bytes read.
typedef size_t (*WavReadFn)(void* ctx, uint32_t offset, uint8_t* buf, size_t len);

// Accepts PCM (or WAVE_FORMAT_EXTENSIBLE PCM), 16-bit, mono or stereo,
// 8000..48000 Hz.
WavResult wavParse(WavReadFn read, void* ctx, uint32_t fileSize, WavInfo& out);
const char* wavResultName(WavResult r);
