#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "measure/field_spectrum.h"
#include "measure/frame_source.h"
#include "measure/lk_tracker.h"
#include "measure/multipoint.h"
#include "measure/spectrum.h"
#include "measure/tracker.h"
#include "measure/vision.h"

namespace vm {

// Measuring box size limits (pixels): smaller boxes hold too little texture,
// larger ones make the per-frame NCC search too slow. The LK tracker takes at
// most kLkMaxPixels (64 x 64).
constexpr int kRoiMinPx = 16, kRoiMaxPx = 256, kRoiMaxPxLk = 64;
// The measuring box of size w x h centred at (cx, cy) (normalised) in a width x height frame.
inline Roi boxAt(int width, int height, double cx, double cy, int w, int h) {
  return Roi{static_cast<int>(cx * width) - w / 2, static_cast<int>(cy * height) - h / 2, w, h};
}

struct MeasureConfig {
  int roiW = 64, roiH = 64;  // measuring box (pixels)
  int searchRadius = 8;
  double durationS = 10;
  char axis = 'y';          // displacement axis in image coordinates
  double minScore = 0.6;    // NCC below this counts as a lost frame
  double mmPerPx = 0;       // 0 = not calibrated: results stay in pixels
  std::string calibrationId;
  std::string calibrationLayoutHash;  // optional fixed-pose multipoint layout binding
  double fMinHz = 0.5;
  std::string tracker = "ncc";  // "ncc": current PS baseline; "lk": PL lk_accum engine model
  // Optional reference ROI on static background (same tracker): the result is
  // then the target minus the reference, i.e. camera translation removed.
  bool referenceEnabled = false;
  double refCx = 0.1, refCy = 0.9;  // normalised centre of the reference ROI
  double minPeakSnr = 5;            // peak / median of the searched band; below: no clear dominant line
  double maxFinalStepPx = 0.05;     // LK: last update larger than this counts as not converged
  bool fieldSpectrum = true;        // whole-field brightness line detection (independent cross-check)
  // "roi": the box tracked by `tracker`; "multipoint": the team's multi-point
  // plate measurement (measure/multipoint.h): grid in the box, references
  // around (refCx, refCy), motor-off baseline.
  std::string method = "roi";
  MultiPointConfig multipoint;
  // Per capture, set by the application for "multipoint": collect a baseline
  // instead of measuring; the baseline file to read, or to create.
  bool baselineCapture = false;
  std::string baselineFile;
};

// Largest box side the configured method and tracker take.
inline int roiMaxPx(const MeasureConfig& m) {
  return m.method == "roi" && m.tracker == "lk" ? kRoiMaxPxLk : kRoiMaxPx;
}

struct CaptureSample {
  int64_t tNs = 0;
  uint32_t seq = 0;
  double dx = 0, dy = 0, score = 0;
  bool timestampValid = true;
  bool ok = false, edge = false, saturated = false;
  bool converged = true;  // LK only
};

struct MeasureResult {
  bool valid = false;
  std::string reason;  // why the result is not valid
  int64_t startMonoNs = 0;
  double durationS = 0;
  uint32_t frames = 0, dropped = 0, badFrames = 0, edgeHits = 0, saturatedFrames = 0, unconvergedFrames = 0;
  double fpsMeasured = 0, fpsNominal = 0, jitterMs = 0, minScore = 1;
  char axis = 'y';
  double dominantHz = 0;
  double peakAmpPx = 0;     // spectral peak amplitude (0-peak) at dominantHz
  double rmsPx = 0, ppPx = 0;  // time domain, linear trend removed
  double mmPerPx = 0;
  std::string calibrationId;
  Roi roi;
  int frameWidth = 0, frameHeight = 0;
  std::vector<double> t, disp, dispOther;  // seconds, pixels
  Spectrum spectrum;
  double peakSnr = 0;       // spectral peak / median amplitude of the searched band
  bool peakClear = false;   // peakSnr >= minPeakSnr; otherwise dominantHz is only the largest bin
  std::string tracker;
  // Background reference. When used, t/disp/spectrum and the metrics above
  // describe target minus reference; raw* keep the uncompensated target.
  struct Reference {
    bool used = false, valid = false;
    std::string reason;
    Roi roi;
    double ppPx = 0, rmsPx = 0, dominantHz = 0, peakAmpPx = 0;
  } reference;
  double rawPpPx = 0, rawRmsPx = 0, rawDominantHz = 0;
  FieldSpectrumResult field;  // brightness-based source frequency, not a displacement
  std::string method = "roi";
  // method "multipoint". t/disp/spectrum and the pixel metrics above describe
  // the representative point (largest gate ratio), dominantHz is the core's.
  struct MultiPoint {
    bool baseline = false;  // this capture collected a baseline
    std::string baselineFile;
    int baselineWindows = 0;
    uint32_t layoutHash = 0;
    int windows = 0, signalWindows = 0, quietWindows = 0, rejectedWindows = 0, mismatchWindows = 0;
    double frequencyHz = 0, frequencyMinHz = 0, frequencyMaxHz = 0, fs = 0;
    std::vector<std::array<float, 2>> points, refs;
    // Last signal window (else the last window), per point.
    std::vector<float> rmsPx, gateRatio;
    std::vector<uint8_t> valid, gate;
    int passed = 0;
    int representative = -1;
    // The representative point's own analysis (gaps, tracking, spectrum):
    // decides its amplitudes and plots, not the windows' frequency.
    bool waveformValid = false;
    std::string waveformReason;
  } multipoint;
};

// One multi-point capture: per-frame tracking and the analysed windows.
struct MultiPointCapture {
  MultiPointLayout layout;
  std::vector<int64_t> tNs;
  std::vector<uint32_t> seq;
  std::vector<uint8_t> timestampValid;
  std::vector<std::vector<float>> u, v;     // [point][frame], reference motion removed
  std::vector<std::vector<uint8_t>> ok;     // [point][frame]
  std::vector<uint8_t> refOk;               // [frame] reference group tracked
  std::vector<MultiPointWindow> windows;
  void reset(const MultiPointLayout& l);
  void add(const GrayFrame& f, const MultiPointTracker& t, bool tracked);
};
// What made windows of a capture invalid, from its frames: "dropped_frames"
// (sequence gaps), "irregular_sampling" (an interval more than 10 % off the
// median, the team's window check), "reference_lost" or "tracking_lost",
// whichever hit most frames; empty when none did.
std::string multiPointWindowCause(const MultiPointCapture& c);
// Result of a multi-point measurement (not a baseline) from its windows.
MeasureResult analyzeMultiPoint(const MultiPointCapture& c, const MeasureConfig& cfg, double nominalFps);

// Target minus reference sample by sample (same frames); a frame is good only if both are.
std::vector<CaptureSample> relativeSamples(const std::vector<CaptureSample>& target,
                                           const std::vector<CaptureSample>& reference);
// analyzeCapture plus the optional reference compensation.
MeasureResult analyzeWithReference(const std::vector<CaptureSample>& target, const std::vector<CaptureSample>* reference,
                                   const MeasureConfig& cfg, double nominalFps);

// Pure analysis of a finished capture (unit-tested on synthetic data).
MeasureResult analyzeCapture(const std::vector<CaptureSample>& samples, const MeasureConfig& cfg, double nominalFps);

// Camera image for display at full resolution: colour (BGR888, byte 0 = blue)
// when the source delivers it, otherwise gray.
struct PreviewImage {
  int width = 0, height = 0;
  uint64_t frame = 0;         // engine frame count when taken: a new image has a new number
  std::vector<uint8_t> bgr;   // width * height * 3, or empty
  std::vector<uint8_t> gray;  // width * height when bgr is empty
};

class MeasurementEngine {
 public:
  enum class State { kStopped, kPreview, kCapturing, kAnalyzing };
  struct Live {
    State state = State::kStopped;
    bool cameraOk = false;
    std::string source;
    std::string error;
    double elapsedS = 0, durationS = 0, fps = 0;
    double procMs = 0;  // measurement work per frame while capturing (mean over the last second), else 0
    double lastDisp = 0, lastScore = 0;
    std::vector<float> recent;  // recent displacement samples (pixels)
    int frameW = 0, frameH = 0;
    std::shared_ptr<const PreviewImage> preview;  // shared, not copied, with every reader
    Roi roi;
    uint64_t frameCount = 0;
    VisionLive vision;
    // Running multi-point capture: layout, baseline or measurement, last window.
    std::vector<std::array<float, 2>> mpPoints, mpRefs;
    bool mpBaseline = false;
    MultiPointWindow mpWindow;
    int mpWindows = 0;
  };
  using DoneFn = std::function<void(const MeasureResult&)>;

  explicit MeasurementEngine(std::unique_ptr<FrameSource> source);
  ~MeasurementEngine();

  bool startCamera();
  void stopCamera();
  // roi centre in normalised image coordinates (0..1).
  // Display image processing (vision page); runs beside the tracker.
  void setVision(const VisionConfig& cfg);
  // Called on the camera thread with a preview frame (not during a capture)
  // at most rateHz times a second, e.g. to feed the stereo service the left image.
  void setFrameTap(std::function<void(GrayFrame&)> tap, double rateHz);
  void setFrameTapRate(double rateHz);  // 0 stops the tap
  // What the display shows. Outside a capture a frame is converted only when
  // something uses it: the preview (when shown), the vision chain (when its
  // output is shown and it is idle) or the frame tap; the vision chain pauses
  // while its output is not shown. Both default to true (no screen).
  void setDisplayNeeds(bool preview, bool vision);
  bool beginCapture(const MeasureConfig& cfg, double cx, double cy, DoneFn done);
  void endCapture();  // analyse what was captured so far
  void cancel();      // drop the capture without a result
  Live live();
  State state() const { return state_.load(); }

 private:
  void run();
  void updatePreview(GrayFrame& f);

  std::unique_ptr<FrameSource> src_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<State> state_{State::kStopped};
  std::mutex mutex_;
  Live live_;
  MeasureConfig cfg_;
  double cx_ = 0.5, cy_ = 0.5;
  DoneFn done_;
  bool startRequested_ = false, endRequested_ = false, cancelRequested_ = false;
  std::vector<CaptureSample> samples_, refSamples_;
  MultiPointCapture mp_;
  FieldSpectrum field_;
  int64_t lastPreviewNs_ = 0, lastTapNs_ = 0;
  std::atomic<bool> previewWanted_{true}, visionWanted_{true};
  VisionPipeline vision_;
  std::function<void(GrayFrame&)> tap_;
  double tapRateHz_ = 0;
};

}  // namespace vm
