#include "app/acoustic_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "drv/pdm_mic.h"
#include "drv/pdm_regs.h"
#include "hw/phys_window.h"
#include "measure/pdm_model.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {

constexpr const char* kTag = "acoustic";
constexpr double kPi = 3.14159265358979323846;

// The PL receiver through /dev/mem, or through given registers (tests): raw
// PDM words from the FIFO, decimated here (bit-exact with the reference model).
class PlPdmSource : public PcmSource {
 public:
  PlPdmSource(uint64_t base, const std::string& phase, int warmupMs) : base_(base), phase_(phase), warmupMs_(warmupMs) {}
  PlPdmSource(RegIo& io, const std::string& phase, int warmupMs) : io_(&io), phase_(phase), warmupMs_(warmupMs) {}
  ~PlPdmSource() override { stop(); }

  bool start(StartError& e) override {
    char b[128];
    snprintf(b, sizeof(b), "no PDM receiver at 0x%llx (bitstream without it)", static_cast<unsigned long long>(base_));
    if (!io_) {
      if (!win_.isOpen() && !win_.open(base_, 0x1000)) {
        snprintf(b, sizeof(b), "cannot map 0x%llx", static_cast<unsigned long long>(base_));
        e.text = b;
        e.permanent = true;
        return false;
      }
      uint32_t id = 0;
      if (!win_.tryRead32(pdm_reg::kId, id) || id != pdm_reg::kIdValue) {
        e.text = b;
        e.permanent = e.absent = true;
        win_.close();
        return false;
      }
      io_ = &win_;
    } else if (io_->read32(pdm_reg::kId) != pdm_reg::kIdValue) {
      e.text = b;
      e.permanent = e.absent = true;
      return false;
    }
    PdmMic mic(*io_);
    if (!mic.probe(e.text)) {
      e.permanent = true;
      return false;
    }
    // The PL clock against CLOCK_MONOTONIC: sample rate and times follow it.
    const int64_t t0 = monoNs();
    const uint32_t c0 = mic.now();
    sleepMs(100);
    const int64_t t1 = monoNs();
    const uint32_t c1 = mic.now();
    plHz_ = t1 > t0 && c1 != c0 ? static_cast<uint32_t>(c1 - c0) * 1e9 / (t1 - t0) : mic.clockHz();

    // Which half of the clock carries data: the other one reads the board
    // pull-up (all ones), a missing module reads it in both.
    const auto data = [](double ones) { return ones > 0.02 && ones < 0.98; };
    bool high = phase_ == "high";
    double ones = 0;
    if (!sampleHalf(mic, high, ones)) {
      e.text = "PDM clock not running";
      stop();
      return false;
    }
    if (!data(ones)) {
      if (phase_ != "auto") {
        e.text = high ? "no PDM data while the clock is high (L/R = VDD set)" : "no PDM data while the clock is low (L/R = GND set)";
        stop();
        return false;
      }
      high = !high;
      if (!sampleHalf(mic, high, ones) || !data(ones)) {
        e.text = "no PDM data (microphone missing or DAT stuck)";
        e.absent = true;
        stop();
        return false;
      }
    }
    high_ = high;
    // The stream proper, from a fresh start: the microphone's turn-on and the
    // filter's start-up are dropped.
    PdmMic::Setup s;
    s.phaseHigh = high_;
    mic.start(s);
    halfDiv_ = mic.halfDiv();
    dec_.reset();
    next_ = 0;
    discard_ = static_cast<uint32_t>(warmupMs_ * 1e-3 * sampleRate());
    clipped_ = 0;
    LOGI(kTag, "PDM receiver at 0x%llx: PL clock %.0f Hz, PDM %.0f Hz, %.2f S/s, data while the clock is %s",
         static_cast<unsigned long long>(base_), plHz_, pdmClockHz(), sampleRate(),
         high_ ? "high (L/R = VDD)" : "low (L/R = GND)");
    return true;
  }

  void stop() override {
    if (io_) PdmMic(*io_).stop();
  }

  bool read(std::vector<Block>& out, std::string& err) override {
    PdmMic mic(*io_);
    const PdmMic::Status st = mic.status();
    if (st.killed || !st.running) {
      err = st.killed ? "PDM clock stopped by power fail" : "PDM clock not running";
      return false;
    }
    std::vector<uint32_t> words;
    const PdmMic::Snapshot snap = mic.read(words);
    const int64_t ta = monoNs();
    const uint32_t tsNow = mic.now();
    const int64_t tb = monoNs();
    // 64-bit word indices; a jump is a gap (words dropped while the FIFO was full).
    const uint64_t first = next_ + static_cast<int64_t>(static_cast<int32_t>(snap.first() - static_cast<uint32_t>(next_)));
    if (first != next_) {
      dec_.reset();
      discard_ = std::max<uint32_t>(discard_, 64);  // the filter starts again
    }
    next_ = first;
    Block* cur = nullptr;
    uint64_t lastWord = 0, lastPcm = 0;
    bool have = false;
    for (const uint32_t w : words) {
      const uint64_t wi = next_++;
      int16_t pcm;
      bool sat;
      if (!dec_.push(w, pcm, sat)) continue;
      clipped_ += sat;
      if (discard_ > 0) {
        --discard_;
        continue;
      }
      const uint64_t pi = wi / 2;  // a sample completes on every second word
      if (!cur || pi != cur->firstIndex + cur->pcm.size()) {
        out.emplace_back();
        cur = &out.back();
        cur->firstIndex = pi;
      }
      cur->pcm.push_back(pcm);
      lastWord = wi;
      lastPcm = pi;
      have = true;
    }
    // Time of the newest sample: the latch saw the newest bit at snap.ts.
    if (have) {
      const uint64_t produced = next_ + static_cast<uint32_t>(snap.words - static_cast<uint32_t>(next_));
      const uint64_t newestBit = 32 * produced + snap.bits - 1;
      const double bitS = 2.0 * halfDiv_ / plHz_;
      const int64_t latchNs = (ta + tb) / 2 - static_cast<int64_t>(static_cast<uint32_t>(tsNow - snap.ts) / plHz_ * 1e9);
      Block& a = out.back();
      a.anchored = true;
      a.anchorIndex = lastPcm;
      a.anchorNs = latchNs - static_cast<int64_t>((newestBit - (32 * lastWord + 31)) * bitS * 1e9) -
                   static_cast<int64_t>(kPdmFilterDelayS * 2e6 / pdmClockHz() * 1e9);
    }
    return true;
  }

  double sampleRate() const override { return pdmClockHz() / kPdmDecimation; }
  double pdmClockHz() const override { return halfDiv_ ? plHz_ / (2.0 * halfDiv_) : 0; }
  std::string describe() const override {
    char b[64];
    snprintf(b, sizeof(b), "pl pdm_mic 0x%llx", static_cast<unsigned long long>(base_));
    return base_ ? std::string(b) : std::string("pl pdm_mic");
  }
  std::string lr() const override { return halfDiv_ ? (high_ ? "VDD" : "GND") : std::string(); }
  uint32_t clipped() override { return clipped_; }

 private:
  // Runs the clock on one half for a moment; ones = share of 1 bits after
  // the microphone's turn-on.
  bool sampleHalf(PdmMic& mic, bool high, double& ones) {
    PdmMic::Setup s;
    s.phaseHigh = high;
    mic.start(s);
    halfDiv_ = mic.halfDiv();
    sleepMs(20);
    mic.clear();
    sleepMs(10);
    std::vector<uint32_t> w;
    mic.read(w);
    if (w.empty()) return false;
    size_t n = 0;
    for (const uint32_t x : w) n += static_cast<size_t>(__builtin_popcount(x));
    ones = static_cast<double>(n) / (32.0 * w.size());
    return true;
  }

  uint64_t base_ = 0;
  RegIo* io_ = nullptr;  // win_ once mapped, or the registers given
  std::string phase_;
  int warmupMs_;
  PhysWindow win_;
  double plHz_ = 0;
  uint32_t halfDiv_ = 0;
  bool high_ = false;
  PdmWordDecimator dec_;
  uint64_t next_ = 0;     // index of the next word expected
  uint32_t discard_ = 0;  // samples still to drop (turn-on, filter start-up)
  uint32_t clipped_ = 0;
};

// Machine-like sound for hosts without the hardware: broadband noise and a
// 2.4 kHz tone, both modulated at hz, produced in real time.
class SyntheticPcmSource : public PcmSource {
 public:
  explicit SyntheticPcmSource(double hz) : hz_(hz) {}
  bool start(StartError&) override {
    t0_ = monoNs();
    produced_ = 0;
    return true;
  }
  void stop() override {}
  bool read(std::vector<Block>& out, std::string&) override {
    const uint64_t target = static_cast<uint64_t>((monoNs() - t0_) * 1e-9 * kRate);
    if (target <= produced_) return true;
    out.emplace_back();
    Block& b = out.back();
    b.firstIndex = produced_;
    for (; produced_ < target; ++produced_) {
      const double t = produced_ / kRate;
      const double am = 1 + 0.5 * std::sin(2 * kPi * hz_ * t);
      const double v = am * (900 * std::sin(2 * kPi * 2400 * t) + noise_(rng_)) + 0.1 * noise_(rng_);
      b.pcm.push_back(static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, v))));
    }
    b.anchored = true;
    b.anchorIndex = produced_ - 1;
    b.anchorNs = t0_ + static_cast<int64_t>((produced_ - 1) * 1e9 / kRate);
    return true;
  }
  double sampleRate() const override { return kRate; }
  double pdmClockHz() const override { return kRate * kPdmDecimation; }
  std::string describe() const override { return "synthetic"; }
  std::string lr() const override { return "GND"; }
  uint32_t clipped() override { return 0; }

 private:
  static constexpr double kRate = 31250;
  double hz_;
  int64_t t0_ = 0;
  uint64_t produced_ = 0;
  std::mt19937 rng_{11};
  std::normal_distribution<double> noise_{0.0, 300.0};
};

}  // namespace

std::unique_ptr<PcmSource> plPdmSource(RegIo& io, const std::string& phase, int warmupMs) {
  return std::unique_ptr<PcmSource>(new PlPdmSource(io, phase, warmupMs));
}

AcousticService::AcousticService(const AcousticOptions& o) : opt_(o) {
  if (o.source == "synthetic") {
    src_.reset(new SyntheticPcmSource(o.synthHz));
  } else {
    src_.reset(new PlPdmSource(o.base, o.phase, o.warmupMs));
  }
}

AcousticService::AcousticService(const AcousticOptions& o, std::unique_ptr<PcmSource> source)
    : opt_(o), src_(std::move(source)) {}

AcousticService::~AcousticService() { stop(); }

const char* AcousticService::stateName(State s) {
  switch (s) {
    case State::kDisabled: return "disabled";
    case State::kStarting: return "starting";
    case State::kWarmup: return "warmup";
    case State::kReady: return "ready";
    case State::kCapturing: return "capturing";
    case State::kAbsent: return "absent";
    case State::kFault: return "fault";
  }
  return "unknown";
}

void AcousticService::start() {
  if (running_) return;
  ring_.assign(static_cast<size_t>(std::max(1.0, opt_.ringS) * 31250), 0);
  setState(State::kStarting);
  running_ = true;
  thread_ = std::thread([this] { run(); });
}

void AcousticService::stop() {
  if (!running_) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  setState(State::kDisabled);
}

void AcousticService::setPowerFail(bool active) { pfail_ = active; }

void AcousticService::setState(State s, const std::string& err) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (s == State::kReady && capturing_) s = State::kCapturing;
  if (s != state_ || err != error_) {
    if (s == State::kFault) LOGW(kTag, "%s", err.c_str());
  }
  state_ = s;
  error_ = err;
}

void AcousticService::run() {
  bool active = false, permanent = false, waiting = false;
  int64_t retryAt = 0;
  while (running_) {
    State s;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      s = state_;
    }
    if (pfail_) {
      if (active) src_->stop();
      active = false;
      if (!permanent) setState(State::kFault, "power_fail");  // a missing core stays the reason
      retryAt = 0;
      sleepMs(50);
      continue;
    }
    // (A retry keeps the absent or fault state until it succeeds: no flicker.)
    if (s == State::kStarting || ((s == State::kFault || s == State::kAbsent) && !permanent && monoMs() >= retryAt)) {
      PcmSource::StartError e;
      if (src_->start(e)) {
        active = true;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          base_ = haveData_ ? end_ + 1 : 0;  // a restart is a break in the stream
          if (!haveData_) first_ = 0;
        }
        setState(State::kWarmup);
        waiting = true;  // ready with the first samples past the warm-up
      } else {
        permanent = e.permanent;
        setState(e.absent ? State::kAbsent : State::kFault, e.text);
        retryAt = monoMs() + 5000;
      }
      continue;
    }
    if (active && (s == State::kWarmup || s == State::kReady || s == State::kCapturing)) {
      std::vector<PcmSource::Block> blocks;
      std::string err;
      if (!src_->read(blocks, err)) {
        src_->stop();
        active = false;
        setState(State::kFault, err);
        retryAt = monoMs() + 5000;
        continue;
      }
      bool got = false;
      for (const PcmSource::Block& b : blocks) {
        append(b);
        got = got || !b.pcm.empty();
      }
      if (waiting && got) {
        waiting = false;
        setState(State::kReady);
      }
    }
    sleepMs(opt_.pollMs);
  }
  if (active) src_->stop();
}

void AcousticService::append(const PcmSource::Block& b) {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t cap = ring_.size();
  const uint64_t first = base_ + b.firstIndex;
  if (!b.pcm.empty()) {
    if (!haveData_) {
      first_ = first;
    } else if (first > end_) {
      gaps_.emplace_back(end_, first);
      ++gapCount_;
    }
    if (haveData_ && first < end_) return;  // never rewrite what is there
    for (size_t i = 0; i < b.pcm.size(); ++i) ring_[(first + i) % cap] = b.pcm[i];
    end_ = first + b.pcm.size();
    haveData_ = true;
    received_ += b.pcm.size();
    while (!gaps_.empty() && gaps_.front().second + cap < end_) gaps_.pop_front();
    // Level of the last 0.2 s.
    const uint64_t span = std::min<uint64_t>(end_ - std::max(first_, end_ > cap ? end_ - cap : 0),
                                             static_cast<uint64_t>(0.2 * src_->sampleRate()));
    double sum = 0, sq = 0;
    for (uint64_t i = end_ - span; i < end_; ++i) sum += ring_[i % cap];
    const double mean = span ? sum / span : 0;
    for (uint64_t i = end_ - span; i < end_; ++i) {
      const double v = ring_[i % cap] - mean;
      sq += v * v;
    }
    levelDbfs_ = span && sq > 0 ? 20 * std::log10(std::sqrt(sq / span) / (32768 / std::sqrt(2.0))) : -200;
  }
  if (b.anchored) {
    anchorIndex_ = base_ + b.anchorIndex;
    anchorNs_ = b.anchorNs;
    anchored_ = true;
  }
}

AcousticService::Status AcousticService::status() {
  Status s;
  std::lock_guard<std::mutex> lock(mutex_);
  s.state = state_;
  s.error = error_;
  s.source = src_ ? src_->describe() : std::string();
  s.lr = src_ ? src_->lr() : std::string();
  s.sampleRate = src_ ? src_->sampleRate() : 0;
  s.pdmClockHz = src_ ? src_->pdmClockHz() : 0;
  s.levelDbfs = levelDbfs_;
  s.samples = received_;
  s.gaps = gapCount_;
  if (haveData_ && s.sampleRate > 0) {
    s.bufferedS = std::min<uint64_t>(end_ - first_, ring_.size()) / s.sampleRate;
  }
  return s;
}

void AcousticService::beginCapture(double durationS) {
  std::lock_guard<std::mutex> lock(mutex_);
  capturing_ = true;
  if (state_ == State::kReady) state_ = State::kCapturing;
  // The window must still be held when the capture has been analysed: the
  // capture plus 10 % and 15 s (reader lead, video analysis).
  const double fs = src_->sampleRate() > 0 ? src_->sampleRate() : 31250;
  const uint64_t need = static_cast<uint64_t>((durationS * 1.1 + 15) * fs);
  const uint64_t cap = ring_.size();
  if (need <= cap) return;
  std::vector<int16_t> bigger(static_cast<size_t>(need), 0);
  if (haveData_) {
    const uint64_t oldest = std::max<uint64_t>(first_, end_ > cap ? end_ - cap : 0);
    for (uint64_t i = oldest; i < end_; ++i) bigger[i % need] = ring_[i % cap];
    first_ = oldest;  // nothing older is held, however large the ring now is
  }
  ring_.swap(bigger);
  LOGI(kTag, "ring grown to %.0f s for a %.0f s capture", need / fs, durationS);
}

void AcousticService::cancelCapture() {
  std::lock_guard<std::mutex> lock(mutex_);
  capturing_ = false;
  if (state_ == State::kCapturing) state_ = State::kReady;
}

AcousticService::Window AcousticService::endCapture(int64_t t0Ns, int64_t t1Ns, int waitMs) {
  Window w;
  const int64_t until = monoMs() + waitMs;
  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      w.sampleRate = src_->sampleRate();
      w.pdmClockHz = src_->pdmClockHz();
      w.source = src_->describe();
      w.lr = src_->lr();
      if (!haveData_ || !anchored_ || w.sampleRate <= 0) {
        w.reason = "not_ready";
      } else {
        const double fs = w.sampleRate;
        const int64_t i0 = static_cast<int64_t>(anchorIndex_) + std::llround((t0Ns - anchorNs_) * 1e-9 * fs);
        const int64_t i1 = static_cast<int64_t>(anchorIndex_) + std::llround((t1Ns - anchorNs_) * 1e-9 * fs);
        const uint64_t cap = ring_.size();
        const int64_t oldest = static_cast<int64_t>(std::max<uint64_t>(first_, end_ > cap ? end_ - cap : 0));
        if (i1 <= i0) {
          w.reason = "not_ready";
        } else if (i0 < oldest) {
          w.reason = "not_buffered";
        } else if (i1 > static_cast<int64_t>(end_)) {
          w.reason = "late";  // the end has not arrived yet
        } else {
          w.reason.clear();
          for (const auto& g : gaps_)
            if (static_cast<int64_t>(g.first) < i1 && static_cast<int64_t>(g.second) > i0) w.reason = "gap";
          if (w.reason.empty()) {
            w.pcm.resize(static_cast<size_t>(i1 - i0));
            for (int64_t i = i0; i < i1; ++i) w.pcm[static_cast<size_t>(i - i0)] = ring_[static_cast<uint64_t>(i) % cap];
            w.available = true;
            w.firstIndex = static_cast<uint64_t>(i0);
            w.firstNs = anchorNs_ + static_cast<int64_t>((i0 - static_cast<int64_t>(anchorIndex_)) * 1e9 / fs);
          }
        }
      }
      if (w.reason != "late" || monoMs() >= until) {
        capturing_ = false;
        if (state_ == State::kCapturing) state_ = State::kReady;
        return w;
      }
    }
    sleepMs(10);  // the reader brings the end
  }
}

}  // namespace vm
