#include "util/wav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vm {

namespace {

void put32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

void put16(std::string& s, uint16_t v) {
  s.push_back(static_cast<char>(v & 0xFF));
  s.push_back(static_cast<char>(v >> 8));
}

uint32_t get32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t get16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

}  // namespace

std::string wavMono16(const int16_t* pcm, size_t n, int rate) {
  std::string s;
  s.reserve(44 + 2 * n);
  const uint32_t data = static_cast<uint32_t>(2 * n);
  s += "RIFF";
  put32(s, 36 + data);
  s += "WAVEfmt ";
  put32(s, 16);
  put16(s, 1);  // PCM
  put16(s, 1);  // mono
  put32(s, static_cast<uint32_t>(rate));
  put32(s, static_cast<uint32_t>(rate) * 2);
  put16(s, 2);
  put16(s, 16);
  s += "data";
  put32(s, data);
  for (size_t i = 0; i < n; ++i) put16(s, static_cast<uint16_t>(pcm[i]));
  return s;
}

bool readWavMono16(const std::string& path, std::vector<int16_t>& pcm, int& rate, std::string& err) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    err = "cannot open";
    return false;
  }
  std::string buf;
  char chunk[65536];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) buf.append(chunk, got);
  fclose(f);
  const unsigned char* p = reinterpret_cast<const unsigned char*>(buf.data());
  if (buf.size() < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) {
    err = "not a RIFF/WAVE file";
    return false;
  }
  int channels = 0, bits = 0;
  rate = 0;
  for (size_t pos = 12; pos + 8 <= buf.size();) {
    const uint32_t len = get32(p + pos + 4);
    const size_t body = pos + 8;
    if (body + len > buf.size() && memcmp(p + pos, "data", 4)) break;
    if (!memcmp(p + pos, "fmt ", 4) && len >= 16) {
      if (get16(p + body) != 1) {
        err = "not PCM";
        return false;
      }
      channels = get16(p + body + 2);
      rate = static_cast<int>(get32(p + body + 4));
      bits = get16(p + body + 14);
    } else if (!memcmp(p + pos, "data", 4)) {
      if (channels < 1 || bits != 16 || rate <= 0) {
        err = "needs 16-bit PCM";
        return false;
      }
      const size_t frames = std::min<size_t>(len, buf.size() - body) / (2 * channels);
      pcm.resize(frames);
      for (size_t i = 0; i < frames; ++i) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) sum += static_cast<int16_t>(get16(p + body + 2 * (i * channels + c)));
        pcm[i] = static_cast<int16_t>(sum / channels);
      }
      return true;
    }
    pos = body + len + (len & 1);
  }
  err = "no data chunk";
  return false;
}

}  // namespace vm
