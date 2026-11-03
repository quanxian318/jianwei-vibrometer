#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vm {

// Display image processing for the vision page. The same integer algorithm
// is the reference for the PL implementation (ax7010/pl/vision) and the PC
// model (pc_sim/vision_model.py); all three must agree bit for bit.
//
//   camera gray --REDUCE x levels--> G0 (working image)
//   G1 = REDUCE(G0), G2 = REDUCE(G1), L1 = G1 - EXPAND(G2)
//   per L1 pixel: two first-order low-passes (Q6 states), band = lpH - lpL
//   magnified = clamp(G0 + EXPAND(clamp(gain * band >> 6, +-kDeltaLimit)))
//   energy    = running mean of |band| (Q6)
//
// The output is for display only and never enters the displacement result.
struct VisionConfig {
  bool enabled = true;
  int levels = 0;  // REDUCE steps to the working image; 0 = until it is at most 200 px wide
  double lowHz = 2, highHz = 5;
  int gain = 20;
};

struct VisionLive {
  int w = 0, h = 0;                // working image size, 0 = not running
  std::vector<uint8_t> source;     // working image G0
  std::vector<uint8_t> magnified;  // motion-magnified G0
  int ew = 0, eh = 0;              // band (L1) size
  std::vector<uint8_t> energy;     // band amplitude scaled to 0..255
  std::string backend;             // "PS" (software) or "PL" (FPGA)
  double procMs = 0, fps = 0;
  bool settled = false;            // temporal filter past its start-up transient
  bool paused = false;             // PS processing paused (a measurement needs the CPU)
  int peakX = -1, peakY = -1;      // strongest band amplitude (working coordinates)
};

namespace vision {

constexpr int kStateFrac = 6;      // fractional bits of the temporal filter states
constexpr int kDeltaLimit = 96;    // |amplified band| limit in gray levels
constexpr int kEnergyShift = 4;    // running mean of |band| over ~16 frames

// 5-tap binomial low-pass ([1 4 6 4 1]/16 per axis, edge replicate) and 2x
// decimation keeping even rows/columns: out = (sum + 128) >> 8.
void pyrReduce(const uint8_t* in, int w, int h, std::vector<uint8_t>& out, int& ow, int& oh);
// 2x up-sampling to ow x oh with the matching interpolation kernel: per axis
// even = (a + 6b + c), odd = 4(b + c), out = (sum + 32) >> 6 (floor).
void pyrExpand(const int16_t* in, int w, int h, int ow, int oh, std::vector<int16_t>& out);
// Q16 coefficient of a first-order low-pass: round(65536 * (1 - exp(-2 pi f / fs))).
int lowpassCoef(double fHz, double fs);

// One temporal step for every band pixel; updates the states in place.
// state += (coef * (x - state) + 32768) >> 16 stalls within a few Q6 LSB of
// a constant input (dead band), so a still scene leaves |band| <= 3/64 gray.
struct BandState {
  std::vector<int16_t> lpH, lpL;
  std::vector<uint16_t> amp;
};
void bandStep(const int16_t* lap, size_t n, int coefH, int coefL, BandState& st, std::vector<int16_t>& band);

// The complete per-frame computation that the PL implements: no timing
// policy, the coefficients come from outside (PS registers in hardware).
struct CoreOut {
  int w0 = 0, h0 = 0, w1 = 0, h1 = 0;  // working image (G0) and band (L1) sizes
  std::vector<uint8_t> g0, mag;
  std::vector<int16_t> lap, band;
  std::vector<uint16_t> amp;            // copy of the running |band| mean after this frame
};
// levels: REDUCE steps from the input to G0 (0 = until G0 is at most 200 px wide).
int workingLevels(int w, int levels);
void core(const uint8_t* gray, int w, int h, int levels, int coefH, int coefL, int gain, BandState& st, CoreOut& out);

}  // namespace vision

// Software (PS) implementation running in its own thread: frames are offered
// without blocking; when the processor is busy a frame is skipped and the
// filter coefficients follow the rate actually processed.
class VisionPipeline {
 public:
  VisionPipeline() = default;
  ~VisionPipeline();
  void start();
  void stop();
  void configure(const VisionConfig& cfg);
  void setPaused(bool paused);  // e.g. while a measurement needs the CPU
  // Running, not paused and no frame waiting: offer() would be processed.
  bool wantsFrame();
  void offer(const uint8_t* gray, int w, int h, int64_t tNs);
  VisionLive live();

  // Synchronous processing of one frame (tests and the PC model comparison).
  void process(const uint8_t* gray, int w, int h, int64_t tNs);

 private:
  void run();

  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  VisionConfig cfg_;
  std::vector<uint8_t> pending_;
  int pw_ = 0, ph_ = 0;
  int64_t pendingNs_ = 0;
  bool havePending_ = false;

  // Processing state (worker thread).
  vision::BandState band_;
  int bw_ = 0, bh_ = 0;
  int64_t lastNs_ = 0;
  double fps_ = 0;
  int framesSinceReset_ = 0;
  double lastLow_ = 0, lastHigh_ = 0;
  VisionLive out_;
};

}  // namespace vm
