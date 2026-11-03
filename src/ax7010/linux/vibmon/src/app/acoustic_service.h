#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hw/uio.h"
#include "measure/acoustic.h"

namespace vm {

// Where the acoustic PCM comes from: the PL receiver (pdm_mic.v) or a
// synthetic machine sound for hosts without the hardware.
class PcmSource {
 public:
  virtual ~PcmSource() = default;
  struct StartError {
    std::string text;
    bool permanent = false;  // retrying cannot help
    bool absent = false;     // nothing connected: no receiver in the bitstream, or no microphone on the pins
  };
  // Probes, sets up and starts the clock; blocks through the warm-up and the
  // L/R check (about 0.2 s).
  virtual bool start(StartError& e) = 0;
  virtual void stop() = 0;
  struct Block {
    std::vector<int16_t> pcm;
    uint64_t firstIndex = 0;  // continuous sample index of pcm[0]; a jump from the last block is a gap
    // A sample index and the CLOCK_MONOTONIC time its sound reached the
    // microphone (production time minus the filter delay).
    uint64_t anchorIndex = 0;
    int64_t anchorNs = 0;
    bool anchored = false;
  };
  // New samples, one block per continuous run; false: fault (e.g. the clock was killed).
  virtual bool read(std::vector<Block>& out, std::string& err) = 0;
  virtual double sampleRate() const = 0;
  virtual double pdmClockHz() const = 0;
  virtual std::string describe() const = 0;
  virtual std::string lr() const = 0;  // "GND" / "VDD" half of the clock with data, "" unknown
  virtual uint32_t clipped() = 0;      // samples saturated in the decimator since start
};

// The PL receiver behind given registers (tests; the service maps /dev/mem itself).
std::unique_ptr<PcmSource> plPdmSource(RegIo& io, const std::string& phase, int warmupMs);

struct AcousticOptions {
  std::string source = "pl";  // pl | synthetic
  uint64_t base = 0x44030000; // pdm_mic AXI base
  std::string phase = "auto"; // auto | low (L/R = GND) | high (L/R = VDD)
  int warmupMs = 50;          // PL warm-up after the clock starts (chip 10 ms + filter)
  double ringS = 60;          // PCM kept; a longer capture grows the ring when it starts
  int pollMs = 10;
  double synthHz = 10.84;     // synthetic source: modulation frequency
  AcousticConfig analysis;
};

// MP34DT01 acoustic reference: continuous PCM into a ring buffer, sample
// times on CLOCK_MONOTONIC, and the window of a measurement.
// States: disabled -> starting -> warmup -> ready <-> capturing; absent (no
// receiver in the bitstream, or no microphone on the pins: not connected
// rather than a fault) and fault, both retried every 5 s unless permanent;
// stopped on power fail.
class AcousticService {
 public:
  enum class State { kDisabled, kStarting, kWarmup, kReady, kCapturing, kAbsent, kFault };
  explicit AcousticService(const AcousticOptions& o);
  // For tests: a given source instead of the configured one.
  AcousticService(const AcousticOptions& o, std::unique_ptr<PcmSource> source);
  ~AcousticService();
  void start();
  void stop();
  void setPowerFail(bool active);

  struct Status {
    State state = State::kDisabled;
    std::string error, source, lr;
    double sampleRate = 0, pdmClockHz = 0;
    double levelDbfs = -200;   // last 0.2 s, full-scale sine = 0 dBFS
    uint64_t samples = 0;      // received since start
    uint32_t gaps = 0;         // continuity breaks since start
    double bufferedS = 0;
  };
  Status status();
  static const char* stateName(State s);

  // A measurement of durationS starts: the ring grows if it could not hold the
  // whole window until the capture is analysed; the state is "capturing".
  void beginCapture(double durationS);
  void cancelCapture();
  struct Window {
    bool available = false;
    std::string reason;  // not_ready | not_buffered | gap | late
    std::vector<int16_t> pcm;
    double sampleRate = 0, pdmClockHz = 0;
    std::string source, lr;
    uint64_t firstIndex = 0;
    int64_t firstNs = 0;  // CLOCK_MONOTONIC of pcm[0] at the microphone
  };
  // The PCM of [t0Ns, t1Ns) (CLOCK_MONOTONIC), waiting up to waitMs for its
  // end to arrive. Ends the capture.
  Window endCapture(int64_t t0Ns, int64_t t1Ns, int waitMs = 300);

 private:
  void run();
  void setState(State s, const std::string& err = std::string());
  void append(const PcmSource::Block& b);

  AcousticOptions opt_;
  std::unique_ptr<PcmSource> src_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> pfail_{false};

  std::mutex mutex_;
  State state_ = State::kDisabled;
  std::string error_;
  bool capturing_ = false;
  std::vector<int16_t> ring_;
  uint64_t end_ = 0;      // index after the newest sample
  uint64_t first_ = 0;    // oldest index of the current stream
  uint64_t base_ = 0;     // source index 0 of the current start
  bool haveData_ = false;
  std::deque<std::pair<uint64_t, uint64_t>> gaps_;  // [from, to) of missing samples
  uint32_t gapCount_ = 0;
  uint64_t received_ = 0;
  uint64_t anchorIndex_ = 0;
  int64_t anchorNs_ = 0;
  bool anchored_ = false;
  double levelDbfs_ = -200;
};

}  // namespace vm
