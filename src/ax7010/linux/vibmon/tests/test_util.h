#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_testFailures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_testFailures;                                                      \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                                          \
  do {                                                                                                 \
    double _a = (a), _b = (b);                                                                         \
    if (!(std::fabs(_a - _b) <= (tol))) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, _a, _b, \
                   static_cast<double>(tol));                                                          \
      ++g_testFailures;                                                                                \
    }                                                                                                  \
  } while (0)

inline int testSummary(const char* name) {
  if (g_testFailures) {
    std::printf("%s: FAILED %d check(s)\n", name, g_testFailures);
    return 1;
  }
  std::printf("%s: all checks passed\n", name);
  return 0;
}
