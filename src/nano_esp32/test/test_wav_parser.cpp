// Host test for wav_parser.cpp:
//   g++ -std=c++11 -Wall -Wextra -I../vibmon_nano test_wav_parser.cpp ../vibmon_nano/wav_parser.cpp -o t && ./t
#include <stdio.h>
#include <string.h>

#include <string>

#include "wav_parser.h"

static int g_fail = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);       \
      ++g_fail;                                                   \
    }                                                             \
  } while (0)

static void put16(std::string& s, unsigned v) {
  s += static_cast<char>(v & 0xFF);
  s += static_cast<char>((v >> 8) & 0xFF);
}
static void put32(std::string& s, unsigned long v) {
  for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}
static void chunk(std::string& s, const char* id, const std::string& body) {
  s += std::string(id, 4);
  put32(s, body.size());
  s += body;
  if (body.size() & 1) s += '\0';
}
static std::string fmtBody(unsigned format, unsigned ch, unsigned long rate, unsigned bits, bool extensible) {
  std::string b;
  put16(b, extensible ? 0xFFFE : format);
  put16(b, ch);
  put32(b, rate);
  put32(b, rate * ch * bits / 8);
  put16(b, ch * bits / 8);
  put16(b, bits);
  if (extensible) {
    put16(b, 22);
    put16(b, bits);
    put32(b, 0x4);
    put16(b, format);  // first two bytes of the sub-format GUID
    b += std::string(14, '\x11');
  }
  return b;
}
static std::string riff(const std::string& chunks) {
  std::string s = "RIFF";
  put32(s, chunks.size() + 4);
  s += "WAVE";
  s += chunks;
  return s;
}

static size_t readMem(void* ctx, uint32_t off, uint8_t* buf, size_t len) {
  const std::string* s = static_cast<const std::string*>(ctx);
  if (off >= s->size()) return 0;
  size_t n = s->size() - off < len ? s->size() - off : len;
  memcpy(buf, s->data() + off, n);
  return n;
}

static WavResult parse(const std::string& f, WavInfo& info) {
  return wavParse(readMem, const_cast<std::string*>(&f), static_cast<uint32_t>(f.size()), info);
}

int main() {
  WavInfo info;
  std::string samples(3200, '\x01');  // 100 ms of 16 kHz mono

  std::string c;
  chunk(c, "fmt ", fmtBody(1, 1, 16000, 16, false));
  chunk(c, "data", samples);
  std::string canonical = riff(c);
  CHECK(parse(canonical, info) == kWavOk);
  CHECK(info.dataOffset == 44 && info.dataSize == 3200 && info.sampleRate == 16000 && info.channels == 1);

  // Unknown odd-sized chunk before fmt and a LIST chunk before data.
  c.clear();
  chunk(c, "junk", std::string(5, 'x'));
  chunk(c, "fmt ", fmtBody(1, 2, 22050, 16, false));
  chunk(c, "LIST", std::string(26, 'i'));
  chunk(c, "data", samples);
  std::string extra = riff(c);
  CHECK(parse(extra, info) == kWavOk);
  CHECK(info.channels == 2 && info.blockAlign == 4 && info.dataSize == 3200);
  CHECK(info.dataOffset == 12 + (8 + 6) + (8 + 16) + (8 + 26) + 8);

  // WAVE_FORMAT_EXTENSIBLE with PCM sub-format.
  c.clear();
  chunk(c, "fmt ", fmtBody(1, 1, 16000, 16, true));
  chunk(c, "data", samples);
  CHECK(parse(riff(c), info) == kWavOk);

  // data chunk before fmt.
  c.clear();
  chunk(c, "data", samples);
  chunk(c, "fmt ", fmtBody(1, 1, 8000, 16, false));
  CHECK(parse(riff(c), info) == kWavOk && info.dataOffset == 20 && info.sampleRate == 8000);

  // Declared data size larger than the file: clipped to whole frames.
  std::string truncated = canonical.substr(0, canonical.size() - 101);
  CHECK(parse(truncated, info) == kWavOk && info.dataSize == 3200 - 102);

  // Rejections.
  c.clear();
  chunk(c, "fmt ", fmtBody(1, 1, 16000, 8, false));
  chunk(c, "data", samples);
  CHECK(parse(riff(c), info) == kWavUnsupported);
  c.clear();
  chunk(c, "fmt ", fmtBody(3, 1, 16000, 16, false));  // IEEE float
  chunk(c, "data", samples);
  CHECK(parse(riff(c), info) == kWavUnsupported);
  c.clear();
  chunk(c, "fmt ", fmtBody(1, 1, 16000, 16, false));
  CHECK(parse(riff(c), info) == kWavNoData);
  CHECK(parse(std::string("RIFX....WAVE"), info) == kWavNotRiff);
  CHECK(parse(std::string("RIFF"), info) == kWavIoError);

  if (g_fail) {
    printf("FAILED %d\n", g_fail);
    return 1;
  }
  printf("wav parser checks passed\n");
  return 0;
}
