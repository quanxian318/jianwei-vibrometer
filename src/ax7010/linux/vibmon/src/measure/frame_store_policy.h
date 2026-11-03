#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>

namespace vm {
// Only completed, still readable stores may be paired. Never make an old
// frame look recent by substituting the CPU read time for its timestamp.
inline int nearestCompletedStore(const std::array<int64_t, 3>& completed,
                                 int writing, int64_t target, int64_t now, int64_t lifetime) {
  int best = -1;
  int64_t distance = INT64_MAX;
  for (int i = 0; i < 3; ++i) {
    if (i == writing || completed[i] <= 0 || now < completed[i] || now - completed[i] >= lifetime) continue;
    const int64_t d = target > 0 ? std::llabs(completed[i] - target) : now - completed[i];
    if (d < distance) { best = i; distance = d; }
  }
  return best;
}
}  // namespace vm
