#include "measure/session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "measure";

// One measurement point with the configured tracker ("ncc" or "lk").
class PointTracker {
 public:
  bool init(const GrayFrame& f, const Roi& roi, const MeasureConfig& cfg) {
    lk_ = cfg.tracker == "lk";
    maxStep_ = cfg.maxFinalStepPx;
    return lk_ ? lkt_.init(f, roi, cfg.searchRadius) : ncc_.init(f, roi, cfg.searchRadius);
  }
  // Result reason when init() failed.
  const char* initFailure() const {
    if (!lk_) return "roi_not_trackable";
    switch (lkt_.initStatus()) {
      case LkTracker::InitStatus::kTooLarge: return "roi_too_large";
      case LkTracker::InitStatus::kFlat: return "roi_flat";
      case LkTracker::InitStatus::kWeakGradient: return "roi_weak_texture";
      case LkTracker::InitStatus::kAperture: return "roi_single_edge";
      default: return "roi_outside_frame";
    }
  }
  CaptureSample track(const GrayFrame& f) {
    CaptureSample s;
    s.tNs = f.timestampNs;
    s.seq = f.sequence;
    s.timestampValid = f.timestampValid;
    TrackResult tr;
    if (lk_) {
      const LkTracker::Result r = lkt_.track(f);
      tr = r;
      s.converged = r.lastStep <= maxStep_;
    } else {
      tr = ncc_.track(f);
    }
    s.ok = tr.ok;
    s.dx = tr.dx;
    s.dy = tr.dy;
    s.score = tr.score;
    s.edge = tr.atEdge;
    s.saturated = tr.saturated;
    return s;
  }
  const Roi& roi() const { return lk_ ? lkt_.roi() : ncc_.roi(); }

 private:
  bool lk_ = false;
  double maxStep_ = 0.05;
  NccTracker ncc_;
  LkTracker lkt_;
};

CaptureSample firstSample(const GrayFrame& f) {
  CaptureSample s;
  s.tNs = f.timestampNs;
  s.seq = f.sequence;
  s.timestampValid = f.timestampValid;
  s.ok = true;
  s.score = 1;
  return s;
}
}  // namespace

MeasureResult analyzeCapture(const std::vector<CaptureSample>& samples, const MeasureConfig& cfg, double nominalFps) {
  MeasureResult r;
  r.axis = cfg.axis;
  r.mmPerPx = cfg.mmPerPx;
  r.calibrationId = cfg.calibrationId;
  r.fpsNominal = nominalFps;
  r.frames = static_cast<uint32_t>(samples.size());
  if (samples.size() < 32) {
    r.reason = "too_few_frames";
    return r;
  }
  r.startMonoNs = samples.front().tNs;
  std::vector<double> t, x, xo, dts;
  bool badTiming = false;
  uint32_t maxMissing = 0;
  for (size_t i = 0; i < samples.size(); ++i) {
    const CaptureSample& s = samples[i];
    if (i > 0) {
      const uint32_t step = s.seq - samples[i - 1].seq;
      if (step == 0 || step >= 100000 || s.tNs <= samples[i - 1].tNs) badTiming = true;
      if (step > 1 && step < 100000) { r.dropped += step - 1; maxMissing = std::max(maxMissing, step - 1); }
      dts.push_back((s.tNs - samples[i - 1].tNs) * 1e-9 / std::max<uint32_t>(1, step));
    }
    if (s.edge) r.edgeHits++;
    if (s.saturated) r.saturatedFrames++;
    if (!s.timestampValid) badTiming = true;
    if (!s.ok || !std::isfinite(s.score) || !std::isfinite(s.dx) || !std::isfinite(s.dy) || s.score < cfg.minScore) {
      r.badFrames++;
      continue;
    }
    if (!s.converged) {
      r.unconvergedFrames++;
      continue;
    }
    r.minScore = std::min(r.minScore, s.score);
    t.push_back((s.tNs - r.startMonoNs) * 1e-9);
    x.push_back(cfg.axis == 'x' ? s.dx : s.dy);
    xo.push_back(cfg.axis == 'x' ? s.dy : s.dx);
  }
  r.durationS = (samples.back().tNs - samples.front().tNs) * 1e-9;
  if (r.durationS <= 0) {
    r.reason = "bad_timestamps";
    return r;
  }
  r.fpsMeasured = (samples.size() - 1 + r.dropped) / r.durationS;
  double mean = 0, sq = 0;
  for (double d : dts) mean += d;
  mean /= std::max<size_t>(1, dts.size());
  for (double d : dts) sq += (d - mean) * (d - mean);
  r.jitterMs = std::sqrt(sq / std::max<size_t>(1, dts.size())) * 1000;
  r.t = t;
  r.disp = x;
  r.dispOther = xo;
  const bool excessiveDrops = maxMissing > 2 ||
      r.dropped > 0.05 * (samples.size() + r.dropped);
  if (badTiming || excessiveDrops || r.badFrames || r.unconvergedFrames || r.edgeHits || r.saturatedFrames) {
    r.reason = badTiming ? "bad_timestamps" : (excessiveDrops ? "dropped_frames" :
               (r.badFrames ? "tracking_lost" : (r.unconvergedFrames ? "not_converged" :
               (r.edgeHits ? "search_edge" : "image_saturated"))));
    return r;
  }
  // Bound timestamp jitter before correcting the small nonuniform sample intervals.
  if (std::any_of(dts.begin(), dts.end(), [mean](double dt) { return std::abs(dt - mean) > mean * 0.1; })) {
    r.reason = "irregular_sampling";
    return r;
  }
  if (t.size() < 32) {
    r.reason = "too_few_valid_frames";
    return r;
  }
  // Short gaps (at most 2 frames, total <=5%) are timestamp-resampled.
  // Raw samples and dropped counts remain unchanged; amplitudes are estimates.
  // Use measured timestamps rather than nominal UI/camera FPS.
  std::vector<double> u = resampleUniform(t, x, r.fpsMeasured);
  std::vector<double> d = u;
  removeLinearTrend(d);
  SeriesStats st = seriesStats(d);
  r.rmsPx = st.rms;
  r.ppPx = st.peakToPeak;
  r.spectrum = amplitudeSpectrum(u, r.fpsMeasured, cfg.fMinHz, 0);
  if (!r.spectrum.valid) {
    r.reason = "no_spectrum";
    return r;
  }
  r.dominantHz = r.spectrum.peakHz;
  r.peakAmpPx = r.spectrum.peakAmp;
  // Confidence of the dominant line: peak over the median of the searched band.
  std::vector<double> band;
  const double fHi = 0.8 * r.fpsMeasured / 2;
  for (size_t k = 0; k < r.spectrum.freq.size(); ++k)
    if (r.spectrum.freq[k] >= cfg.fMinHz && r.spectrum.freq[k] <= fHi) band.push_back(r.spectrum.amp[k]);
  if (!band.empty()) {
    std::nth_element(band.begin(), band.begin() + band.size() / 2, band.end());
    const double med = band[band.size() / 2];
    r.peakSnr = med > 0 ? r.peakAmpPx / med : 0;
  }
  r.peakClear = r.peakSnr >= cfg.minPeakSnr;
  r.valid = true;
  return r;
}

std::vector<CaptureSample> relativeSamples(const std::vector<CaptureSample>& target,
                                           const std::vector<CaptureSample>& reference) {
  std::vector<CaptureSample> out;
  const size_t n = std::min(target.size(), reference.size());
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    CaptureSample s = target[i];
    const CaptureSample& q = reference[i];
    if (q.seq != s.seq || q.tNs != s.tNs) {  // not the same frame: unusable
      s.ok = false;
    } else {
      s.dx -= q.dx;
      s.dy -= q.dy;
      s.score = std::min(s.score, q.score);
      s.ok = s.ok && q.ok;
      s.edge = s.edge || q.edge;
      s.saturated = s.saturated || q.saturated;
      s.converged = s.converged && q.converged;
      s.timestampValid = s.timestampValid && q.timestampValid;
    }
    out.push_back(s);
  }
  return out;
}

MeasureResult analyzeWithReference(const std::vector<CaptureSample>& target, const std::vector<CaptureSample>* reference,
                                   const MeasureConfig& cfg, double nominalFps) {
  MeasureResult raw = analyzeCapture(target, cfg, nominalFps);
  raw.tracker = cfg.tracker;
  raw.rawPpPx = raw.ppPx;
  raw.rawRmsPx = raw.rmsPx;
  raw.rawDominantHz = raw.dominantHz;
  if (!reference) return raw;
  const MeasureResult ref = analyzeCapture(*reference, cfg, nominalFps);
  MeasureResult r = analyzeCapture(relativeSamples(target, *reference), cfg, nominalFps);
  r.tracker = cfg.tracker;
  r.rawPpPx = raw.ppPx;
  r.rawRmsPx = raw.rmsPx;
  r.rawDominantHz = raw.dominantHz;
  r.reference.used = true;
  r.reference.valid = ref.valid;
  r.reference.reason = ref.reason;
  r.reference.ppPx = ref.ppPx;
  r.reference.rmsPx = ref.rmsPx;
  r.reference.dominantHz = ref.dominantHz;
  r.reference.peakAmpPx = ref.peakAmpPx;
  // A failed reference invalidates the compensated result even if the
  // combined series happens to pass (e.g. both lost on different frames).
  if (r.valid && !ref.valid) {
    r.valid = false;
    r.reason = "reference_invalid";
  } else if (!r.valid) {
    // Report the target's own failure first; otherwise the reference caused it.
    r.reason = !raw.valid ? raw.reason : "reference_" + (ref.reason.empty() ? std::string("mismatch") : ref.reason);
  }
  return r;
}

void MultiPointCapture::reset(const MultiPointLayout& l) {
  layout = l;
  tNs.clear();
  seq.clear();
  timestampValid.clear();
  refOk.clear();
  windows.clear();
  u.assign(l.points.size(), std::vector<float>());
  v.assign(l.points.size(), std::vector<float>());
  ok.assign(l.points.size(), std::vector<uint8_t>());
}

void MultiPointCapture::add(const GrayFrame& f, const MultiPointTracker& t, bool tracked) {
  tNs.push_back(f.timestampNs);
  seq.push_back(f.sequence);
  timestampValid.push_back(f.timestampValid);
  refOk.push_back(tracked && t.referenceValid());
  for (size_t i = 0; i < u.size(); ++i) {
    u[i].push_back(t.pointU(static_cast<int>(i)));
    v[i].push_back(t.pointV(static_cast<int>(i)));
    ok[i].push_back(tracked && t.pointValid(static_cast<int>(i)));
  }
}

std::string multiPointWindowCause(const MultiPointCapture& c) {
  const size_t n = c.tNs.size();
  if (n < 2) return std::string();
  std::vector<int64_t> dt(n - 1);
  for (size_t i = 1; i < n; ++i) dt[i - 1] = c.tNs[i] - c.tNs[i - 1];
  std::vector<int64_t> sorted = dt;
  std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
  const int64_t median = sorted[sorted.size() / 2];
  int dropped = 0, irregular = 0, refLost = 0, pointLost = 0;
  for (size_t i = 1; i < n; ++i) {
    if (c.seq[i] != c.seq[i - 1] + 1) {
      ++dropped;
    } else if (std::llabs(dt[i - 1] - median) * 10 > median) {
      ++irregular;
    }
  }
  for (size_t f = 0; f < n; ++f) {
    if (f < c.refOk.size() && !c.refOk[f]) {
      ++refLost;
      continue;
    }
    for (const auto& p : c.ok) {
      if (f < p.size() && !p[f]) {
        ++pointLost;
        break;
      }
    }
  }
  LOGI(kTag, "multi-point frames %zu: dropped %d, irregular intervals %d, reference lost %d, points lost %d", n,
       dropped, irregular, refLost, pointLost);
  const int most = std::max(std::max(dropped, irregular), std::max(refLost, pointLost));
  if (!most) return std::string();
  if (most == dropped) return "dropped_frames";
  if (most == irregular) return "irregular_sampling";
  if (most == refLost) return "reference_lost";
  return "tracking_lost";
}

MeasureResult analyzeMultiPoint(const MultiPointCapture& c, const MeasureConfig& cfg, double nominalFps) {
  MeasureResult::MultiPoint m;
  m.baselineFile = cfg.baselineFile;
  m.layoutHash = multiPointHash(c.layout);
  m.points = c.layout.points;
  m.refs = c.layout.refs;
  m.windows = static_cast<int>(c.windows.size());
  std::vector<double> freqs;
  const MultiPointWindow* summary = nullptr;
  for (const MultiPointWindow& w : c.windows) {
    if (w.status == 1) {
      ++m.signalWindows;
      freqs.push_back(w.frequencyHz);
      summary = &w;
    } else if (w.status == 0) {
      ++m.quietWindows;
    } else if (w.status == -2) {
      ++m.mismatchWindows;
    } else {
      ++m.rejectedWindows;
    }
    if (w.fs > 0) m.fs = w.fs;
  }
  if (!summary && !c.windows.empty()) summary = &c.windows.back();
  if (!freqs.empty()) {
    std::sort(freqs.begin(), freqs.end());
    const size_t n = freqs.size();
    m.frequencyHz = n % 2 ? freqs[n / 2] : 0.5 * (freqs[n / 2 - 1] + freqs[n / 2]);
    m.frequencyMinHz = freqs.front();
    m.frequencyMaxHz = freqs.back();
  }
  if (summary) {
    m.rmsPx = summary->rmsPx;
    m.gateRatio = summary->gateRatio;
    m.valid = summary->valid;
    m.gate = summary->gate;
    m.passed = summary->passed;
  }
  float best = -1;
  for (size_t i = 0; i < m.valid.size(); ++i) {
    if (m.valid[i] && m.gateRatio[i] > best) {
      best = m.gateRatio[i];
      m.representative = static_cast<int>(i);
    }
  }
  if (m.representative < 0 && !c.u.empty()) m.representative = 0;

  // The representative point through the single-point analysis: waveform,
  // spectrum and timing checks; the core's amplitudes are projected pixels.
  std::vector<CaptureSample> s(c.tNs.size());
  for (size_t k = 0; k < s.size(); ++k) {
    s[k].tNs = c.tNs[k];
    s[k].seq = c.seq[k];
    s[k].timestampValid = c.timestampValid[k] != 0;
    s[k].score = 1;
    if (m.representative >= 0) {
      s[k].dx = c.u[m.representative][k];
      s[k].dy = c.v[m.representative][k];
      s[k].ok = c.ok[m.representative][k] != 0;
    }
  }
  MeasureConfig pc = cfg;
  char layoutId[9];
  snprintf(layoutId, sizeof(layoutId), "%08x", multiPointHash(c.layout));
  if (cfg.calibrationLayoutHash.empty() || cfg.calibrationLayoutHash != layoutId) {
    // Multipoint metric conversion is enabled only for an explicitly bound layout.
    pc.mmPerPx = 0;
    pc.calibrationId.clear();
  }
  MeasureResult r = analyzeCapture(s, pc, nominalFps);
  m.waveformValid = r.valid;
  m.waveformReason = r.valid ? std::string() : r.reason;
  r.method = "multipoint";
  r.tracker = "multipoint";
  // Valid with signal windows, whose frequency is the result; the waveform
  // keeps its own validity above.
  r.valid = m.signalWindows > 0;
  r.peakClear = r.valid;
  if (r.valid) {
    r.reason.clear();
    r.dominantHz = m.frequencyHz;
  } else {
    r.dominantHz = 0;
    if (c.windows.empty()) {
      r.reason = "too_few_frames";
    } else if (m.quietWindows) {
      r.reason = "no_signal";
    } else if (m.mismatchWindows) {
      r.reason = "baseline_mismatch";
    } else {
      r.reason = multiPointWindowCause(c);
      if (r.reason.empty()) r.reason = "invalid_windows";
    }
  }
  r.multipoint = m;
  return r;
}

MeasurementEngine::MeasurementEngine(std::unique_ptr<FrameSource> source) : src_(std::move(source)) {}

MeasurementEngine::~MeasurementEngine() { stopCamera(); }

bool MeasurementEngine::startCamera() {
  if (running_) return true;
  if (!src_ || !src_->start()) {
    std::lock_guard<std::mutex> lock(mutex_);
    live_.cameraOk = false;
    live_.error = "camera_start_failed";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    live_.cameraOk = true;
    live_.error.clear();
    live_.source = src_->describe();
  }
  running_ = true;
  state_ = State::kPreview;
  thread_ = std::thread(&MeasurementEngine::run, this);
  vision_.start();
  return true;
}

void MeasurementEngine::stopCamera() {
  if (!running_) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  vision_.stop();
  src_->stop();
  state_ = State::kStopped;
}

void MeasurementEngine::setVision(const VisionConfig& cfg) { vision_.configure(cfg); }

void MeasurementEngine::setFrameTap(std::function<void(GrayFrame&)> tap, double rateHz) {
  std::lock_guard<std::mutex> lock(mutex_);
  tap_ = std::move(tap);
  tapRateHz_ = rateHz;
}

void MeasurementEngine::setFrameTapRate(double rateHz) {
  std::lock_guard<std::mutex> lock(mutex_);
  tapRateHz_ = rateHz;
}

void MeasurementEngine::setDisplayNeeds(bool preview, bool vision) {
  previewWanted_ = preview;
  visionWanted_ = vision;
}

bool MeasurementEngine::beginCapture(const MeasureConfig& cfg, double cx, double cy, DoneFn done) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_ || state_ != State::kPreview || startRequested_ || !live_.cameraOk) return false;
  cfg_ = cfg;
  cx_ = std::max(0.0, std::min(1.0, cx));
  cy_ = std::max(0.0, std::min(1.0, cy));
  done_ = std::move(done);
  startRequested_ = true;
  state_ = State::kCapturing;  // reserve the capture before the camera thread consumes the request
  endRequested_ = cancelRequested_ = false;
  return true;
}

void MeasurementEngine::endCapture() {
  std::lock_guard<std::mutex> lock(mutex_);
  endRequested_ = true;
}

void MeasurementEngine::cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  cancelRequested_ = true;
}

MeasurementEngine::Live MeasurementEngine::live() {
  std::lock_guard<std::mutex> lock(mutex_);
  Live l = live_;
  l.state = state_;
  l.vision = vision_.live();
  return l;
}

void MeasurementEngine::updatePreview(GrayFrame& f) {
  auto p = std::make_shared<PreviewImage>();
  p->width = f.width;
  p->height = f.height;
  if (f.bgr.size() == static_cast<size_t>(f.width) * f.height * 3) {
    p->bgr.swap(f.bgr);
  } else {
    p->gray = f.pixels;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  p->frame = live_.frameCount;
  live_.preview = std::move(p);
}

void MeasurementEngine::run() {
  GrayFrame frame;
  PointTracker tracker, refTracker;
  MultiPointTracker mpt;
  bool refActive = false, mpActive = false;
  Roi box;
  int64_t captureStartNs = 0;
  int64_t fpsWindowStart = monoNs();
  int fpsFrames = 0;
  int64_t procNs = 0;  // measurement work in the current fps window
  int procFrames = 0;
  int failures = 0;
  while (running_) {
    // Cancellation must work even when the camera has stopped delivering frames.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (cancelRequested_) {
        cancelRequested_ = startRequested_ = endRequested_ = false;
        samples_.clear();
        refSamples_.clear();
        mpActive = false;
        done_ = nullptr;
        state_ = State::kPreview;
      }
    }
    // Outside a capture a frame is converted only when something uses it.
    // The software vision chain is display work: it runs while its output is
    // shown and pauses while a capture needs the CPU.
    const bool measuring = state_ != State::kPreview;
    vision_.setPaused(measuring || !visionWanted_);
    const int64_t due = monoNs();
    // Idle preview follows delivered frames. During capture retain the last
    // image so full-colour DDR reads cannot interrupt metrology samples.
    const bool previewDue = !measuring && previewWanted_;
    const bool visionDue = !measuring && vision_.wantsFrame();
    std::function<void(GrayFrame&)> tap;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!measuring && tap_ && tapRateHz_ > 0 && due - lastTapNs_ >= static_cast<int64_t>(1e9 / tapRateHz_))
        tap = tap_;
    }
    frame.skipPixels = !measuring && !previewDue && !visionDue && !tap;
    frame.wantColor = previewDue || static_cast<bool>(tap);  // same-frame stereo colour
    // Stereo-only view: hand off the BGR snapshot immediately. The stereo
    // worker obtains its right frame before deriving gray from cached BGR.
    frame.colorOnly = !measuring && !visionDue && ((previewDue && !tap) || (!previewDue && tap));
    // A running multi-point capture reads only around its templates.
    frame.regions.clear();
    if (mpActive && !previewDue) frame.regions = mpt.regions();
    if (!src_->next(frame, 500)) {
      if (++failures == 10) {
        DoneFn done;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          live_.cameraOk = false;
          live_.error = "no_frames";
          if (state_ == State::kCapturing || startRequested_) {
            done = done_;
            done_ = nullptr;
            startRequested_ = endRequested_ = false;
            samples_.clear();
            mpActive = false;
            state_ = State::kPreview;
          }
        }
        LOGW(kTag, "no frames from %s", src_->describe().c_str());
        MeasureResult result;
        result.reason = "camera_timeout";
        if (done) done(result);
      }
      continue;
    }
    if (failures >= 10) {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.cameraOk = true;
      live_.error.clear();
    }
    failures = 0;
    const int64_t now = monoNs();
    fpsFrames++;
    if (now - fpsWindowStart >= 1000000000LL) {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.fps = fpsFrames * 1e9 / static_cast<double>(now - fpsWindowStart);
      live_.procMs = procFrames ? procNs * 1e-6 / procFrames : 0;
      fpsWindowStart = now;
      fpsFrames = 0;
      procNs = 0;
      procFrames = 0;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.frameCount++;
      live_.frameW = frame.width;
      live_.frameH = frame.height;
    }
    if (!frame.skipPixels) {
      if (previewDue) {  // first: it takes the colour copy, which the others do not need
        lastPreviewNs_ = now;
        updatePreview(frame);
      }
      if (visionDue) vision_.offer(frame.pixels.data(), frame.width, frame.height, frame.timestampNs);
      if (tap) {
        lastTapNs_ = now;
        tap(frame);
      }
    }

    bool start = false, end = false, cancel = false;
    MeasureConfig cfg;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // A capture starts on a converted frame: one requested while this frame
      // was read without pixels starts with the next (the state already says
      // capturing, so nothing below may run for this frame).
      if (startRequested_ && (frame.skipPixels || frame.colorOnly) && !cancelRequested_) continue;
      start = startRequested_;
      end = endRequested_;
      cancel = cancelRequested_;
      startRequested_ = endRequested_ = cancelRequested_ = false;
      cfg = cfg_;
    }
    if (cancel) {
      samples_.clear();
      mpActive = false;
      state_ = State::kPreview;
      continue;
    }
    if (start) {
      auto refuse = [&](const std::string& reason) {
        MeasureResult r;
        r.reason = reason;
        state_ = State::kPreview;
        DoneFn done;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          done = done_;
        }
        if (done) done(r);
      };
      const Roi roi = boxAt(frame.width, frame.height, cx_, cy_, cfg.roiW, cfg.roiH);
      samples_.clear();
      refSamples_.clear();
      if (cfg.method == "multipoint") {
        const MultiPointLayout layout =
            multiPointLayout(frame.width, frame.height, roi, cfg.refCx, cfg.refCy, cfg.multipoint);
        std::string err;
        if (!mpt.start(layout, cfg.baselineCapture, cfg.baselineFile, err)) {
          LOGW(kTag, "multi-point capture refused: %s", err.c_str());
          refuse(err);
          continue;
        }
        mp_.reset(layout);
        mpActive = true;
        box = roi;
        // Template allocation, whole-frame input and gradient initialization
        // can exceed one frame period. Finish that work before the timed series.
        if (!mpt.prime(frame)) {
          mpActive = false;
          refuse("invalid_frame");
          continue;
        }
        captureStartNs = 0;
        state_ = State::kCapturing;
        std::lock_guard<std::mutex> lock(mutex_);
        live_.roi = roi;
        live_.durationS = cfg.durationS;
        live_.recent.clear();
        live_.mpPoints = layout.points;
        live_.mpRefs = layout.refs;
        live_.mpBaseline = cfg.baselineCapture;
        live_.mpWindow = MultiPointWindow();
        live_.mpWindows = 0;
        continue;
      }
      if (!tracker.init(frame, roi, cfg)) {
        LOGW(kTag, "ROI %dx%d not trackable: %s", roi.w, roi.h, tracker.initFailure());
        refuse(tracker.initFailure());
        continue;
      }
      refActive = cfg.referenceEnabled;
      if (refActive) {
        Roi rr = roi;
        rr.x = static_cast<int>(cfg.refCx * frame.width) - rr.w / 2;
        rr.y = static_cast<int>(cfg.refCy * frame.height) - rr.h / 2;
        if (!refTracker.init(frame, rr, cfg)) {
          LOGW(kTag, "reference ROI not trackable: %s", refTracker.initFailure());
          refuse(std::string("reference_") + refTracker.initFailure());
          continue;
        }
        refSamples_.push_back(firstSample(frame));
      }
      if (cfg.fieldSpectrum) {
        field_.reset(frame.width, frame.height);
        field_.add(frame.pixels.data(), frame.width, frame.height);
      }
      captureStartNs = frame.timestampNs;
      samples_.push_back(firstSample(frame));
      state_ = State::kCapturing;
      std::lock_guard<std::mutex> lock(mutex_);
      live_.roi = tracker.roi();
      live_.durationS = cfg.durationS;
      live_.recent.clear();
      continue;
    }
    if (state_ != State::kCapturing) continue;
    if (mpActive) {
      const int64_t t0 = monoNs();
      const uint32_t before = mpt.frames();
      if (!before) captureStartNs = frame.timestampNs;
      const bool analysed = mpt.push(frame);
      mp_.add(frame, mpt, mpt.frames() != before);
      if (analysed) mp_.windows.push_back(mpt.lastWindow());
      procNs += monoNs() - t0;
      procFrames++;
      const double elapsed = (frame.timestampNs - captureStartNs) * 1e-9;
      {
        int tracked = 0;
        for (size_t i = 0; i < mp_.u.size(); ++i) tracked += mpt.pointValid(static_cast<int>(i));
        std::lock_guard<std::mutex> lock(mutex_);
        live_.elapsedS = elapsed;
        live_.lastDisp = cfg.axis == 'x' ? mpt.pointU(0) : mpt.pointV(0);
        live_.lastScore = mp_.u.empty() ? 0 : static_cast<double>(tracked) / mp_.u.size();
        live_.recent.push_back(static_cast<float>(live_.lastDisp));
        if (live_.recent.size() > 600) live_.recent.erase(live_.recent.begin(), live_.recent.begin() + 100);
        if (analysed) {
          live_.mpWindow = mpt.lastWindow();
          live_.mpWindows = static_cast<int>(mp_.windows.size());
        }
      }
      if (end || elapsed >= cfg.durationS) {
        state_ = State::kAnalyzing;
        MeasureResult r;
        if (cfg.baselineCapture) {
          MeasureResult::MultiPoint& m = r.multipoint;
          m.baseline = true;
          m.layoutHash = multiPointHash(mp_.layout);
          m.points = mp_.layout.points;
          m.refs = mp_.layout.refs;
          m.baselineWindows = mpt.baselineWindows();
          m.windows = static_cast<int>(mp_.windows.size());
          for (const MultiPointWindow& w : mp_.windows) m.rejectedWindows += w.status != 2;
          r.method = "multipoint";
          r.frames = static_cast<uint32_t>(mp_.tNs.size());
          r.startMonoNs = mp_.tNs.front();
          r.durationS = (mp_.tNs.back() - mp_.tNs.front()) * 1e-9;
          for (size_t k = 1; k < mp_.seq.size(); ++k) {
            const uint32_t step = mp_.seq[k] - mp_.seq[k-1];
            if (step > 1 && step < 100000) r.dropped += step - 1;
          }
          r.fpsMeasured = r.durationS > 0 ? (r.frames - 1 + r.dropped) / r.durationS : 0;
          r.fpsNominal = src_->nominalFps();
          r.valid = mpt.saveBaseline(cfg.baselineFile);
          if (r.valid) {
            m.baselineFile = cfg.baselineFile;
          } else if (r.durationS < 5) {
            r.reason = "baseline_too_short";
          } else if (m.baselineWindows < 5) {
            // Long enough, but too few windows passed the team's check: say why.
            r.reason = multiPointWindowCause(mp_);
            if (r.reason.empty()) r.reason = "invalid_windows";
          } else {
            r.reason = "baseline_not_saved";
          }
        } else {
          r = analyzeMultiPoint(mp_, cfg, src_->nominalFps());
        }
        if (r.valid) multiPointWindowCause(mp_);  // retain capture-quality diagnostics on success too
        r.roi = box;
        r.frameWidth = frame.width;
        r.frameHeight = frame.height;
        mpActive = false;
        mp_ = MultiPointCapture();
        DoneFn done;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          done = done_;
        }
        state_ = State::kPreview;
        if (done) done(r);
      }
      continue;
    }
    const int64_t t0 = monoNs();
    const CaptureSample s = tracker.track(frame);
    samples_.push_back(s);
    if (refActive) refSamples_.push_back(refTracker.track(frame));
    if (cfg.fieldSpectrum) field_.add(frame.pixels.data(), frame.width, frame.height);
    procNs += monoNs() - t0;
    procFrames++;
    const double elapsed = (frame.timestampNs - captureStartNs) * 1e-9;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      live_.elapsedS = elapsed;
      live_.lastDisp = cfg.axis == 'x' ? s.dx : s.dy;
      live_.lastScore = s.score;
      live_.recent.push_back(static_cast<float>(live_.lastDisp));
      if (live_.recent.size() > 600) live_.recent.erase(live_.recent.begin(), live_.recent.begin() + 100);
    }
    if (end || elapsed >= cfg.durationS) {
      state_ = State::kAnalyzing;
      MeasureResult r = analyzeWithReference(samples_, refActive ? &refSamples_ : nullptr, cfg, src_->nominalFps());
      r.roi = tracker.roi();
      if (refActive) r.reference.roi = refTracker.roi();
      r.frameWidth = frame.width;
      r.frameHeight = frame.height;
      if (cfg.fieldSpectrum) {
        // Needs the same uniform sampling as the displacement spectrum.
        if (r.fpsMeasured > 0 && !r.dropped && r.reason != "bad_timestamps" && r.reason != "irregular_sampling")
          r.field = field_.analyze(r.fpsMeasured, cfg.fMinHz, 0);
        else
          r.field.reason = "sampling_invalid";
      }
      samples_.clear();
      refSamples_.clear();
      DoneFn done;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        done = done_;
      }
      state_ = State::kPreview;
      if (done) done(r);
    }
  }
}

}  // namespace vm
