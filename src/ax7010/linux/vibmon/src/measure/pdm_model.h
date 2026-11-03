#pragma once

#include <cstdint>

namespace vm {

// PDM microphone chain: the PL receiver (ax7010/pl/audio/rtl/pdm_mic.v)
// delivers 2 MHz PDM bits, the PS decimates them: CIC (order 5, /32) ->
// 63-tap FIR (/2) -> 31.25 kS/s, 16-bit PCM with the PDM full scale at +/-32768.
constexpr int kPdmDecimation = 64;
// CIC 77.5 PDM periods plus FIR 31 input samples (62.5 kHz) at a 2 MHz PDM clock.
constexpr double kPdmFilterDelayS = 534.75e-6;

// Reference decimator, one bit at a time: the five CIC integrators update
// together (each adds the previous value of the one before it), the comb
// runs after every 32nd bit, every second CIC sample starts an output and the
// FIR delay line starts at zero. (It was also checked bit for bit against an
// RTL version of the filter in the PL.)
class PdmDecimator {
 public:
  PdmDecimator() { reset(); }
  void reset();
  // Feeds one PDM bit (0/1); true when it completes a PCM sample.
  bool push(int bit, int16_t& pcm, bool& saturated);

 private:
  uint32_t integ_[5], comb_[5];
  int bits_ = 0;
  int32_t line_[64];
  int wp_ = 0;
  bool half_ = false;
};

// The same decimator fed 32 bits at a time (bit 0 = oldest), as the PL
// delivers them: the CIC as its 156-tap integer FIR, evaluated with byte
// tables. Bit-exact with PdmDecimator, about 20 table look-ups per word.
class PdmWordDecimator {
 public:
  PdmWordDecimator() { reset(); }
  void reset();
  // Feeds one word; true when it completes a PCM sample (every second word).
  bool push(uint32_t word, int16_t& pcm, bool& saturated);

 private:
  uint32_t hist_[4];  // the four previous words, newest first
  int32_t line_[64];
  int wp_ = 0;
  bool half_ = false;
};

// Second-order sigma-delta modulator for test stimulus: x in [-1, 1] of the
// PDM full scale (stable up to about 0.8), one bit per call.
class SigmaDelta2 {
 public:
  int step(double x);

 private:
  double s1_ = 0, s2_ = 0;
  int last_ = 0;
};

}  // namespace vm
