// Acoustic reference (MP34DT01 on J706): the decimator's response, the word
// decimator against the reference, pdm_regs.h against pdm_mic.v, the PL
// source and service against an emulated core, the analysis on synthetic
// sound, WAV files and the record.
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "app/acoustic_service.h"
#include "app/storage.h"
#include "drv/pdm_mic.h"
#include "drv/pdm_regs.h"
#include "measure/acoustic.h"
#include "measure/pdm_model.h"
#include "test_util.h"
#include "util/clock.h"
#include "util/json_util.h"
#include "util/wav.h"

using namespace vm;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Amplitude of the component at hz (least squares on sin/cos).
double toneAmplitude(const std::vector<double>& x, double fs, double hz) {
  double ss = 0, sc = 0, cc = 0, xs = 0, xc = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    const double s = std::sin(2 * kPi * hz * i / fs), c = std::cos(2 * kPi * hz * i / fs);
    ss += s * s;
    sc += s * c;
    cc += c * c;
    xs += x[i] * s;
    xc += x[i] * c;
  }
  const double det = ss * cc - sc * sc;
  const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
  return std::hypot(a, b);
}

// PCM of the reference decimator for a signal (fraction of full scale).
std::vector<double> modelPcm(double seconds, const std::function<double(double)>& sig) {
  SigmaDelta2 sd;
  PdmDecimator dec;
  std::vector<double> out;
  const long bits = static_cast<long>(seconds * 2e6);
  for (long i = 0; i < bits; ++i) {
    int16_t p;
    bool sat;
    if (dec.push(sd.step(sig(i / 2e6)), p, sat)) out.push_back(p);
  }
  return out;
}

void testModelResponse() {
  const double fs = 31250;
  // DC and a 1 kHz sine come out at their PDM full-scale fraction.
  std::vector<double> dc = modelPcm(0.05, [](double) { return 0.25; });
  double mean = 0;
  for (size_t i = 200; i < dc.size(); ++i) mean += dc[i];
  mean /= dc.size() - 200;
  CHECK_NEAR(mean / 32768, 0.25, 0.003);
  auto tone = [&](double hz) {
    std::vector<double> y = modelPcm(0.12, [hz](double t) { return 0.4 * std::sin(2 * kPi * hz * t); });
    y.erase(y.begin(), y.begin() + 200);  // filter start-up
    return toneAmplitude(y, fs, hz) / 32768;
  };
  const double a1k = tone(1000);
  CHECK_NEAR(a1k, 0.4, 0.004);
  // CIC droop equalised: flat to 10 kHz.
  CHECK_NEAR(20 * std::log10(tone(9000) / a1k), 0.0, 0.1);
  CHECK_NEAR(20 * std::log10(tone(200) / a1k), 0.0, 0.1);
  // 20 kHz (above the 15.6 kHz output Nyquist frequency) would alias to 11.25 kHz.
  std::vector<double> y = modelPcm(0.12, [](double t) { return 0.4 * std::sin(2 * kPi * 20000 * t); });
  y.erase(y.begin(), y.begin() + 200);
  CHECK(toneAmplitude(y, fs, 31250 - 20000) / 32768 < 0.4e-3);  // < -60 dB
  // All-ones PDM saturates at +32767 (PDM full scale is +32768).
  PdmDecimator dec;
  int16_t p = 0;
  bool sat = false, any = false;
  for (int i = 0; i < 64 * 200; ++i)
    if (dec.push(1, p, sat)) any = any || sat;
  CHECK(any && p == 32767);
}

// The word decimator (what the PS runs on the PL's words) against the
// reference, bit for bit: sigma-delta sound, random bits, all ones / zeros.
void testWordDecimator() {
  auto compare = [](const std::vector<uint32_t>& words) {
    PdmDecimator ref;
    PdmWordDecimator fast;
    std::vector<int16_t> a, b;
    for (uint32_t w : words) {
      int16_t p;
      bool s1, s2;
      for (int i = 0; i < 32; ++i)
        if (ref.push((w >> i) & 1, p, s1)) a.push_back(p);
      if (fast.push(w, p, s2)) b.push_back(p);
    }
    return a.size() == words.size() / 2 && a == b;
  };
  std::vector<uint32_t> w(20000, 0);
  SigmaDelta2 sd;
  for (size_t i = 0; i < w.size(); ++i)
    for (int b = 0; b < 32; ++b) {
      const double t = (32.0 * i + b) / 2e6;
      if (sd.step(0.5 * std::sin(2 * kPi * 1000 * t) + 0.2 * std::sin(2 * kPi * 7300 * t))) w[i] |= 1u << b;
    }
  CHECK(compare(w));
  std::mt19937 rng(9);
  for (uint32_t& x : w) x = rng();
  CHECK(compare(w));
  std::vector<uint32_t> edge(400, 0xFFFFFFFFu);
  std::fill(edge.begin() + 200, edge.end(), 0u);
  CHECK(compare(edge));
}

// pdm_regs.h against pdm_mic.v (read multiplexer, write decoder, constants).
void testRegisterMapMatchesRtl() {
  const std::string here = __FILE__;
  const std::string path = here.substr(0, here.rfind('/') + 1) + "../../../pl/audio/rtl/pdm_mic.v";
  std::ifstream f(path);
#if defined(__arm__)
  if (!f.good()) {
    std::printf("  register map check skipped: %s not found\n", path.c_str());
    return;
  }
#endif
  CHECK(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string v = ss.str();
  std::map<uint32_t, std::string> rd;
  {
    const std::regex re("10'h([0-9A-Fa-f]{3}): s_axi_rdata <= ([^;]+);");
    for (auto it = std::sregex_iterator(v.begin(), v.end(), re); it != std::sregex_iterator(); ++it)
      rd[std::stoul((*it)[1].str(), nullptr, 16) * 4] = (*it)[2].str();
  }
  using namespace pdm_reg;
  const std::pair<uint32_t, const char*> want[] = {
      {kId, "ID_VALUE"}, {kVersion, "VERSION"}, {kClkHz, "CLK_HZ"}, {kCtrl, "{30'd0, phase, en}"},
      {kHalfDiv, "{24'd0, half_div}"}, {kSampleAt, "{24'd0, sample_at}"},
      {kStatus, "{29'd0, killed, overflow, running}"}, {kLevel, "level_q"}, {kLevelL, "level_l"},
      {kBitsL, "{27'd0, bits_l}"}, {kTsL, "ts_l"}, {kTs, "ts"}, {kDropped, "dropped"}, {kDroppedL, "dropped_l"},
      {kConfig, "CONFIG"}};
  for (const auto& w : want) {
    const bool ok = rd.count(w.first) && rd[w.first] == w.second;
    if (!ok) std::fprintf(stderr, "  register 0x%03x: RTL '%s', expected '%s'\n", w.first, rd[w.first].c_str(), w.second);
    CHECK(ok);
  }
  auto has = [&](const char* s) { return v.find(s) != std::string::npos; };
  // DATA pops; WORDS latches the level, the partial word, the clock and DROPPED.
  CHECK(has("10'h008: begin") && has("s_axi_rdata <= level_q != 0 ? head : 32'd0;") && has("pop <= level_q != 0;"));
  CHECK(has("10'h009: begin") && has("s_axi_rdata <= words;") && has("level_l <= level;") && has("bits_l <= bits;") &&
        has("ts_l <= ts;") && has("dropped_l <= dropped;"));
  // Writes.
  CHECK(has("10'h003: begin  // CTRL") && has("en <= wd[0];") && has("phase <= wd[1];") && has("clear <= wd[8];"));
  CHECK(has("10'h004: if (s_axi_wstrb[0]) half_div <=") && has("10'h005: if (s_axi_wstrb[0]) sample_at <="));
  CHECK(has("10'h006: if (s_axi_wstrb[0]) ovf_w1c <= wd[1];"));
  CHECK(has("ID_VALUE = 32'h5044_4D31") && kIdValue == 0x50444D31);
  CHECK(has("VERSION  = 32'h0002_0000") && kVersionValue == 0x00020000);
  CHECK(has("{16'd0, FIFO_LOG2_B, 8'd32}") && has("parameter FIFO_LOG2 = 13"));
}

// pdm_mic.v at the register level: the microphone (a sigma-delta modulator
// of machine-like sound) runs in real time at 2 MHz, words go into an
// 8192-word FIFO that freezes on overflow, WORDS latches the snapshot. The
// half without data reads the pull-up (all ones).
struct FakePdm : RegIo {
  uint32_t id = pdm_reg::kIdValue;
  bool micPresent = true, micHigh = false;  // the module's L/R pin
  double amHz = 10.84;
  bool en = false, phase = false;
  uint32_t half = 25, sampleAt = 18;
  int64_t enNs = 0, tsBaseNs = monoNs();
  uint64_t produced = 0, bitIdx = 0;
  uint32_t dropped = 0;
  bool overflow = false, dropNext = false;
  bool overflowAtLatch = false;  // overflow just after the driver has looked at STATUS
  std::deque<uint32_t> fifo;
  SigmaDelta2 sd;
  std::mt19937 rng{3};
  std::normal_distribution<double> noise{0.0, 0.02};
  uint32_t lWords = 0, lLevel = 0, lBits = 0, lTs = 0, lDropped = 0;
  int starts = 0;

  double bitRate() const { return 1e8 / (2.0 * half); }
  uint32_t ts(int64_t ns) const { return static_cast<uint32_t>((ns - tsBaseNs) / 10); }
  uint32_t nextWord() {
    uint32_t w = 0;
    for (int b = 0; b < 32; ++b, ++bitIdx) {
      int bit = 1;  // the pull-up
      if (micPresent && phase == micHigh) {
        const double t = bitIdx / bitRate();
        const double am = 1 + 0.5 * std::sin(2 * kPi * amHz * t);
        bit = sd.step(am * (0.25 * std::sin(2 * kPi * 2400 * t) + noise(rng)));
      }
      if (bit) w |= 1u << b;
    }
    return w;
  }
  uint64_t bitsTaken(int64_t now) const { return en ? static_cast<uint64_t>((now - enNs) * 1e-9 * bitRate()) : 0; }
  void advance(int64_t now) {
    if (!en) return;
    const uint64_t done = bitsTaken(now) / 32;
    while (produced < done) {
      const uint32_t w = nextWord();
      if (fifo.size() >= 8192 || overflow || dropNext) {
        ++dropped;
        overflow = true;
        dropNext = false;
      } else {
        fifo.push_back(w);
      }
      ++produced;
    }
  }
  uint32_t read32(uint32_t o) override {
    using namespace pdm_reg;
    const int64_t now = monoNs();
    advance(now);
    switch (o) {
      case kId: return id;
      case kVersion: return kVersionValue;
      case kClkHz: return 100000000;
      case kCtrl: return (en ? kCtrlEn : 0) | (phase ? kCtrlPhaseHigh : 0);
      case kHalfDiv: return half;
      case kSampleAt: return sampleAt;
      case kStatus: return (en ? kStRunning : 0) | (overflow ? kStOverflow : 0);
      case kLevel: return static_cast<uint32_t>(fifo.size());
      case kData: {
        if (fifo.empty()) return 0;
        const uint32_t w = fifo.front();
        fifo.pop_front();
        return w;
      }
      case kWords:
        if (overflowAtLatch) {
          overflow = true;
          overflowAtLatch = false;
        }
        lWords = static_cast<uint32_t>(produced);
        lLevel = static_cast<uint32_t>(fifo.size());
        lBits = static_cast<uint32_t>(bitsTaken(now) - 32 * produced);
        lTs = ts(now);
        lDropped = dropped;
        return lWords;
      case kLevelL: return lLevel;
      case kBitsL: return lBits;
      case kTsL: return lTs;
      case kTs: return ts(now);
      case kDropped: return dropped;
      case kDroppedL: return lDropped;
      case kConfig: return (13u << 8) | 32u;
      default: return 0;
    }
  }
  void write32(uint32_t o, uint32_t v) override {
    using namespace pdm_reg;
    advance(monoNs());
    switch (o) {
      case kCtrl:
        if ((v & kCtrlEn) && !en) {  // a fresh start
          enNs = monoNs();
          produced = bitIdx = 0;
          dropped = 0;
          overflow = false;
          fifo.clear();
          sd = SigmaDelta2();
          ++starts;
        }
        en = v & kCtrlEn;
        phase = v & kCtrlPhaseHigh;
        if (v & kCtrlClear) {
          fifo.clear();
          dropped = 0;
          overflow = false;
        }
        break;
      case kHalfDiv: half = v < 4 ? 4 : v; break;
      case kSampleAt: sampleAt = v; break;
      case kStatus:
        if (v & kStOverflow) {
          overflow = false;
          dropped = 0;
        }
        break;
      default: break;
    }
  }
};

void waitState(AcousticService& s, AcousticService::State want, int ms) {
  const int64_t until = monoMs() + ms;
  while (s.status().state != want && monoMs() < until) sleepMs(10);
}

void testDriver() {
  FakePdm hw;
  PdmMic mic(hw);
  std::string err;
  CHECK(mic.probe(err));
  mic.start(PdmMic::Setup());
  CHECK(hw.half == 25 && hw.sampleAt == 18 && hw.en && !hw.phase);  // 2 MHz at 100 MHz, 3/4 into the half
  sleepMs(30);
  std::vector<uint32_t> w;
  const PdmMic::Snapshot s = mic.read(w);
  CHECK(w.size() > 1500 && w.size() == s.level && s.first() == 0 && s.dropped == 0);
  CHECK(s.words >= s.level && s.bits < 32);
  CHECK(static_cast<int32_t>(mic.now() - s.ts) >= 0);
  // An overflow after the driver looked at STATUS stays set: what is still in
  // the FIFO belongs to the next poll, which takes it and releases the FIFO.
  w.clear();
  hw.overflowAtLatch = true;
  const PdmMic::Snapshot s2 = mic.read(w);
  CHECK(hw.overflow && w.size() == s2.level && s2.first() == s.first() + s.level);
  sleepMs(2);
  w.clear();
  const PdmMic::Snapshot s3 = mic.read(w);
  CHECK(!hw.overflow && w.empty() && s3.dropped > 0 && s3.first() == s2.first() + s2.level);
  hw.id = 0x12345678;
  CHECK(!mic.probe(err) && err.find("ID") != std::string::npos);
}

void testService() {
  // L/R = VDD module: the low half reads the pull-up, the auto phase moves to the high half.
  {
    FakePdm hw;
    hw.micHigh = true;
    AcousticOptions o;
    o.ringS = 10;
    AcousticService svc(o, plPdmSource(hw, "auto", 20));
    svc.start();
    waitState(svc, AcousticService::State::kReady, 3000);
    AcousticService::Status st = svc.status();
    CHECK(st.state == AcousticService::State::kReady);
    CHECK(st.lr == "VDD" && hw.phase);
    CHECK_NEAR(st.sampleRate, 31250, 30);  // the PL clock measured against CLOCK_MONOTONIC
    CHECK_NEAR(st.pdmClockHz, 2e6, 2000);
    svc.beginCapture(2);
    CHECK(svc.status().state == AcousticService::State::kCapturing);
    const int64_t t0 = monoNs();
    sleepMs(2400);
    const int64_t t1 = monoNs();
    AcousticService::Window w = svc.endCapture(t0, t1);
    if (!w.available) std::fprintf(stderr, "  window not available: %s\n", w.reason.c_str());
    CHECK(w.available);
    CHECK(svc.status().state == AcousticService::State::kReady);
    CHECK_NEAR(static_cast<double>(w.pcm.size()), 2.4 * 31250, 400);
    CHECK_NEAR((w.firstNs - t0) * 1e-6, 0.0, 1.0);  // the window starts where asked
    st = svc.status();
    CHECK(st.levelDbfs > -20 && st.levelDbfs < -5);
    const AcousticAnalysis a = analyzeAcoustic(w.pcm.data(), w.pcm.size(), w.sampleRate, 10.84, AcousticConfig());
    CHECK(a.valid && a.relation == "agree");
    CHECK_NEAR(a.acousticHz, 10.84, 0.3);
    // An overflow drops words until the PS has taken the ones before the gap;
    // a window across the gap is refused.
    const int64_t g0 = monoNs();
    sleepMs(100);
    hw.dropNext = true;
    sleepMs(150);
    st = svc.status();
    CHECK(st.gaps >= 1);
    const AcousticService::Window g = svc.endCapture(g0, monoNs() - 20000000);
    CHECK(!g.available && g.reason == "gap");
    // A window older than the stream.
    const AcousticService::Window old = svc.endCapture(t0 - 20000000000LL, t0 - 18000000000LL);
    CHECK(!old.available && old.reason == "not_buffered");
    svc.stop();
  }
  // A 60 s capture with the default 60 s ring: the ring grows when the capture
  // starts, so the whole window is still held when it ends.
  {
    struct Burst : PcmSource {
      std::atomic<bool>* go;
      bool sent = false;
      explicit Burst(std::atomic<bool>* g) : go(g) {}
      bool start(StartError&) override { return true; }
      void stop() override {}
      bool read(std::vector<Block>& out, std::string&) override {
        if (sent || !*go) return true;
        Block b;
        b.pcm.assign(1878125, 200);  // 60.1 s at once
        b.anchored = true;
        b.anchorNs = 1000000000LL;
        out.push_back(std::move(b));
        sent = true;
        return true;
      }
      double sampleRate() const override { return 31250; }
      double pdmClockHz() const override { return 2e6; }
      std::string describe() const override { return "burst"; }
      std::string lr() const override { return "GND"; }
      uint32_t clipped() override { return 0; }
    };
    std::atomic<bool> go{false};
    AcousticOptions o;
    AcousticService svc(o, std::unique_ptr<PcmSource>(new Burst(&go)));
    svc.start();
    svc.beginCapture(60);
    go = true;
    for (int n = 0; n < 200 && svc.status().samples < 1878125; ++n) sleepMs(10);
    const AcousticService::Window w = svc.endCapture(1000000000LL, 61000000000LL, 0);
    CHECK(w.available && w.pcm.size() == 1875000);
    svc.stop();
  }
  // No module: the pull-up in both halves; not connected rather than a fault.
  {
    FakePdm hw;
    hw.micPresent = false;
    AcousticOptions o;
    AcousticService svc(o, plPdmSource(hw, "auto", 20));
    svc.start();
    waitState(svc, AcousticService::State::kAbsent, 3000);
    const AcousticService::Status st = svc.status();
    CHECK(st.state == AcousticService::State::kAbsent);
    CHECK(st.error.find("microphone missing") != std::string::npos);
    CHECK(!hw.en);  // clock stopped again
    svc.stop();
  }
  // Wrong fixed phase: the configured half has no data.
  {
    FakePdm hw;
    hw.micHigh = true;
    AcousticOptions o;
    AcousticService svc(o, plPdmSource(hw, "low", 20));
    svc.start();
    waitState(svc, AcousticService::State::kFault, 3000);
    CHECK(svc.status().error.find("L/R = GND") != std::string::npos);
    svc.stop();
  }
  // No core in the bitstream: not connected for good, no retries; a power
  // fail does not hide that.
  {
    FakePdm hw;
    hw.id = 0;
    AcousticOptions o;
    AcousticService svc(o, plPdmSource(hw, "auto", 20));
    svc.start();
    waitState(svc, AcousticService::State::kAbsent, 3000);
    CHECK(svc.status().state == AcousticService::State::kAbsent);
    CHECK(svc.status().error.find("no PDM receiver") != std::string::npos);
    CHECK(hw.starts == 0);
    svc.setPowerFail(true);
    sleepMs(120);
    CHECK(svc.status().state == AcousticService::State::kAbsent);
    svc.stop();
  }
  // Power fail stops the clock; the service restarts once it is released.
  {
    FakePdm hw;
    AcousticOptions o;
    AcousticService svc(o, plPdmSource(hw, "auto", 20));
    svc.start();
    waitState(svc, AcousticService::State::kReady, 3000);
    svc.setPowerFail(true);
    waitState(svc, AcousticService::State::kFault, 1000);
    CHECK(svc.status().error == "power_fail" && !hw.en);
    svc.setPowerFail(false);
    waitState(svc, AcousticService::State::kReady, 3000);
    CHECK(svc.status().state == AcousticService::State::kReady && hw.en);
    svc.stop();
  }
}

std::vector<int16_t> machine(double seconds, double fs, double amHz, double depth, double toneHz, double lowHz) {
  std::mt19937 rng(5);
  std::normal_distribution<double> n(0.0, 400.0);
  std::vector<int16_t> out(static_cast<size_t>(seconds * fs));
  for (size_t i = 0; i < out.size(); ++i) {
    const double t = i / fs;
    const double am = 1 + depth * std::sin(2 * kPi * amHz * t);
    double v = am * n(rng) + 1500 * std::sin(2 * kPi * toneHz * t);
    if (lowHz > 0) v += 3000 * std::sin(2 * kPi * lowHz * t);
    out[i] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, v)));
  }
  return out;
}

void testAnalysis() {
  const double fs = 31250;
  const AcousticConfig cfg;
  // Broadband machine noise modulated at the rotation frequency.
  const std::vector<int16_t> m = machine(8, fs, 10.84, 0.6, 1250, 0);
  AcousticAnalysis a = analyzeAcoustic(m.data(), m.size(), fs, 10.84, cfg);
  CHECK(a.valid && a.relation == "agree");
  CHECK_NEAR(a.acousticHz, 10.84, 0.1);
  CHECK(!a.envelopePeaks.empty() && a.envelopePeaks[0].prominenceDb > 15);
  CHECK_NEAR(a.tone.hz, 1250, 5);
  CHECK(!a.envFreq.empty() && a.envFreq.size() == a.envAmp.size());
  CHECK(a.carrierHighHz > a.carrierLowHz && a.bands.size() == cfg.carrierBands.size());
  a = analyzeAcoustic(m.data(), m.size(), fs, 5.42, cfg);
  CHECK(a.relation == "harmonic" && a.harmonic == 2);
  a = analyzeAcoustic(m.data(), m.size(), fs, 21.68, cfg);
  CHECK(a.relation == "harmonic" && a.harmonic == -2);
  a = analyzeAcoustic(m.data(), m.size(), fs, 7.3, cfg);
  CHECK(a.relation == "differ");
  a = analyzeAcoustic(m.data(), m.size(), fs, 0, cfg);
  CHECK(a.relation == "no_visual" && std::fabs(a.acousticHz - 10.84) < 0.1);
  // Steady sound: no envelope period (noise alone must not make one).
  const std::vector<int16_t> steady = machine(6, fs, 10, 0.0, 800, 0);
  a = analyzeAcoustic(steady.data(), steady.size(), fs, 10, cfg);
  CHECK(a.valid && a.relation == "no_peak");
  // A low-frequency sound shows as a direct peak.
  const std::vector<int16_t> low = machine(6, fs, 10, 0.0, 800, 60);
  a = analyzeAcoustic(low.data(), low.size(), fs, 0, cfg);
  CHECK(!a.directPeaks.empty() && std::fabs(a.directPeaks[0].hz - 60) < 0.3);
  // Not valid: stuck line, silence, clipping, too short.
  std::vector<int16_t> stuck(4 * 31250, 32767);
  CHECK(analyzeAcoustic(stuck.data(), stuck.size(), fs, 10, cfg).reason == "stuck");
  std::vector<int16_t> silent(4 * 31250, 3);
  CHECK(analyzeAcoustic(silent.data(), silent.size(), fs, 10, cfg).reason == "no_signal");
  std::vector<int16_t> loud = machine(4, fs, 10, 0.5, 900, 0);
  for (size_t i = 0; i < loud.size(); i += 20) loud[i] = 32767;
  CHECK(analyzeAcoustic(loud.data(), loud.size(), fs, 10, cfg).reason == "clipped");
  CHECK(analyzeAcoustic(m.data(), 31250, fs, 10, cfg).reason == "too_short");
  // dBFS: a full-scale sine is 0 dBFS.
  std::vector<int16_t> sine(4 * 31250);
  for (size_t i = 0; i < sine.size(); ++i) sine[i] = static_cast<int16_t>(16384 * std::sin(2 * kPi * 1000 * i / fs));
  CHECK_NEAR(analyzeAcoustic(sine.data(), sine.size(), fs, 0, cfg).rmsDbfs, -6.02, 0.05);
}

void testWavAndRecord() {
  char tmpl[] = "/tmp/vibmon_acoustic_XXXXXX";
  const std::string dir = mkdtemp(tmpl);
  const std::vector<int16_t> pcm = machine(3, 31250, 10.84, 0.6, 1250, 0);
  CHECK(writeFileAtomic(dir + "/x.wav", wavMono16(pcm.data(), pcm.size(), 31250)));
  std::vector<int16_t> back;
  int rate = 0;
  std::string err;
  CHECK(readWavMono16(dir + "/x.wav", back, rate, err));
  CHECK(rate == 31250 && back == pcm);

  RecordMeta m;
  m.measurementId = "vibmon-01-test";
  m.acoustic.enabled = true;
  m.acoustic.available = true;
  m.acoustic.source = "synthetic";
  m.acoustic.lr = "GND";
  m.acoustic.sampleRate = 31250;
  m.acoustic.pdmClockHz = 2e6;
  m.acoustic.pcm = pcm;
  m.acoustic.bandLowHz = 10;
  m.acoustic.bandHighHz = 100;
  m.acoustic.analysis = analyzeAcoustic(pcm.data(), pcm.size(), 31250, 10.84, AcousticConfig());
  MeasureResult r;
  CHECK(saveRecord(dir, m, r, {}, {}, err));
  JsonValue rec;
  CHECK(loadJsonFile(dir + "/vibmon-01-test/record.json", rec, &err));
  const JsonValue& a = jObj(rec, "acoustic");
  CHECK(jBool(a, "valid", false) && jStr(a, "wav_file", "") == "acoustic.wav");
  CHECK_NEAR(jNum(a, "sample_rate_hz", 0), 31250, 1e-6);
  CHECK(jStr(jObj(a, "visual"), "relation", "") == "agree");
  ResultSummary s;
  readAcousticSummary(rec, s);
  CHECK(s.acousticEnabled && s.acousticValid && s.acousticRelation == "agree" && std::fabs(s.acousticHz - 10.84) < 0.2);
  std::vector<double> f, amp;
  loadAcousticSpectrum(dir, "vibmon-01-test", f, amp);
  CHECK(!f.empty() && f.size() == amp.size());
  CHECK(readWavMono16(dir + "/vibmon-01-test/acoustic.wav", back, rate, err) && back.size() == pcm.size());
  // Not available: the reason is kept, no audio files.
  RecordMeta n;
  n.measurementId = "vibmon-01-gap";
  n.acoustic.enabled = true;
  n.acoustic.reason = "gap";
  CHECK(saveRecord(dir, n, r, {}, {}, err));
  CHECK(loadJsonFile(dir + "/vibmon-01-gap/record.json", rec, &err));
  CHECK(!jBool(jObj(rec, "acoustic"), "available", true) && jStr(jObj(rec, "acoustic"), "reason", "") == "gap");
  CHECK(access((dir + "/vibmon-01-gap/acoustic.wav").c_str(), F_OK) != 0);
  readAcousticSummary(rec, s);
  CHECK(s.acousticEnabled && !s.acousticValid && s.acousticReason == "gap");
  CHECK(std::system(("rm -rf " + dir).c_str()) == 0);
}

}  // namespace

int main() {
  testModelResponse();
  testWordDecimator();
  testRegisterMapMatchesRtl();
  testDriver();
  testService();
  testAnalysis();
  testWavAndRecord();
  return testSummary("acoustic");
}
