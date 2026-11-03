// pdm_vectors - stimulus for the PL PDM receiver test bench
// (ax7010/pl/audio/sim/tb_pdm_mic.v): the bits a microphone would send, from
// a second-order sigma-delta modulator.
//
//   pdm_vectors <out.txt> [words]
//
// Signal: 1 kHz at -8 dBFS, 125 Hz at -16 dBFS, a small DC offset and noise.
// File: the number of 32-bit words, then the words in hex (bit 0 first).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "measure/pdm_model.h"

using namespace vm;

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <out.txt> [words]\n", argv[0]);
    return 2;
  }
  const int nwords = argc > 2 ? std::atoi(argv[2]) : 3000;
  std::vector<uint32_t> words(static_cast<size_t>(nwords), 0);
  SigmaDelta2 sd;
  std::mt19937 rng(7);
  std::normal_distribution<double> noise(0.0, 0.01);
  const double kPi = 3.14159265358979323846;
  for (long i = 0; i < 32L * nwords; ++i) {
    const double t = i / 2.0e6;
    const double x = 0.4 * std::sin(2 * kPi * 1000 * t) + 0.16 * std::sin(2 * kPi * 125 * t + 0.3) + 0.02 + noise(rng);
    if (sd.step(x)) words[static_cast<size_t>(i / 32)] |= 1u << (i % 32);
  }
  FILE* f = fopen(argv[1], "w");
  if (!f) return 1;
  fprintf(f, "%d\n", nwords);
  for (uint32_t w : words) fprintf(f, "%08x\n", w);
  fclose(f);
  printf("%d words -> %s\n", nwords, argv[1]);
  return 0;
}
