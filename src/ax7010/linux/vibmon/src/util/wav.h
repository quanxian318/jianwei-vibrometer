#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vm {

// 16-bit PCM RIFF/WAVE. Writing: mono at the given (integer) rate. Reading:
// mono, or the average of the channels.
std::string wavMono16(const int16_t* pcm, size_t n, int rate);
bool readWavMono16(const std::string& path, std::vector<int16_t>& pcm, int& rate, std::string& err);

}  // namespace vm
