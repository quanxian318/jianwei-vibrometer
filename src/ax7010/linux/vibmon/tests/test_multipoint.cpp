// Multi-point vibration wrapper around the team's core (third_party/axvibration).
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "app/storage.h"
#include "app/alarm_service.h"
#include "measure/frame_source.h"
#include "measure/multipoint.h"
#include "measure/session.h"
#include "test_util.h"
#include "util/json_util.h"
#include "vb_runtime.h"

using namespace vm;

namespace {

SyntheticConfig scene(double amplitude) {
  SyntheticConfig c;
  c.width = 640;
  c.height = 480;
  c.fps = 60;
  c.freqHz = 7;
  c.amplitudePx = amplitude;
  c.axis = 'y';
  c.noise = 1;
  c.realtime = false;
  return c;
}

// The moving centre third of the synthetic scene; references bottom left.
MultiPointLayout layout(const MultiPointConfig& mc = MultiPointConfig()) {
  return multiPointLayout(640, 480, Roi{272, 208, 96, 64}, 0.1, 0.9, mc);
}

void testLayout() {
  const MultiPointLayout l = layout();
  CHECK(l.error.empty());
  CHECK(l.points.size() == 6);  // 3 x 2 at 32 px pitch
  CHECK(l.refs.size() == 4);
  CHECK_NEAR(l.points[0][0], 272 + 16, 1e-4);
  CHECK_NEAR(l.points[5][1], 208 + 48, 1e-4);
  MultiPointConfig few;
  few.maxPoints = 4;
  CHECK(layout(few).points.size() == 4);
  MultiPointConfig horizontal;
  horizontal.maxPoints = 12;
  horizontal.gridCols = 6;
  horizontal.gridRows = 2;
  const auto wide = multiPointLayout(1280, 720, Roi{500, 300, 384, 160}, .15, .2, horizontal);
  CHECK(wide.error.empty());
  CHECK(wide.points.size() == 12);
  CHECK_NEAR(wide.points[0][0], 532, 1e-4);
  CHECK_NEAR(wide.points[5][0], 852, 1e-4);
  CHECK_NEAR(wide.points[5][1], wide.points[0][1], 1e-4);
  CHECK_NEAR(wide.points[6][1] - wide.points[0][1], 80, 1e-4);
  const auto small = multiPointLayout(1280, 720, Roi{500, 300, 96, 64}, .15, .2, horizontal);
  CHECK(small.points.size() == 6);  // do not overlap 25x25 patches to force 12 points
  CHECK(multiPointHash(wide) != multiPointHash(small));
  horizontal.gridCols = 4;
  horizontal.gridRows = 3;
  const auto fourColumns = multiPointLayout(1280, 720, Roi{500, 300, 384, 192}, .15, .2, horizontal);
  CHECK(fourColumns.error.empty() && fourColumns.points.size() == 12);
  CHECK_NEAR(fourColumns.points[0][0], 548, 1e-4);
  CHECK_NEAR(fourColumns.points[3][0], 836, 1e-4);
  CHECK_NEAR(fourColumns.points[3][1], fourColumns.points[0][1], 1e-4);
  CHECK_NEAR(fourColumns.points[4][1] - fourColumns.points[0][1], 64, 1e-4);
  CHECK(multiPointHash(wide) != multiPointHash(fourColumns));
  CHECK(multiPointLayout(640, 480, Roi{0, 200, 20, 64}, 0.1, 0.9, MultiPointConfig()).error == "roi_outside_frame");
  CHECK(multiPointLayout(640, 480, Roi{40, 400, 64, 64}, 0.1, 0.9, MultiPointConfig()).error == "reference_in_box");
  CHECK(multiPointLayout(640, 480, Roi{272, 208, 96, 64}, 0.01, 0.9, MultiPointConfig()).error ==
        "reference_outside_frame");
  CHECK(multiPointHash(l) != multiPointHash(multiPointLayout(640, 480, Roi{276, 208, 96, 64}, 0.1, 0.9,
                                                             MultiPointConfig())));
}

// Region-limited blur against the team's bridge, which blurs every whole frame.
void testSameAsWholeFrameBlur() {
  const MultiPointLayout l = layout();
  MultiPointTracker t;
  std::string err;
  CHECK(t.start(l, true, "", err));
  VRConfig c;
  memset(&c, 0, sizeof(c));
  c.width = 640;
  c.height = 480;
  c.nroi = static_cast<int>(l.points.size());
  c.nref = static_cast<int>(l.refs.size());
  for (size_t i = 0; i < l.points.size(); ++i) {
    c.xy[i][0] = l.points[i][0];
    c.xy[i][1] = l.points[i][1];
  }
  for (size_t i = 0; i < l.refs.size(); ++i) {
    c.xy[l.points.size() + i][0] = l.refs[i][0];
    c.xy[l.points.size() + i][1] = l.refs[i][1];
  }
  std::unique_ptr<VRSession> s(new VRSession());
  std::unique_ptr<VRBaseline> b(new VRBaseline());
  memset(b.get(), 0, sizeof(*b));
  vr_init(s.get(), &c);
  std::vector<float> gray(640 * 480), blur(640 * 480);
  auto src = makeSyntheticSource(scene(3));  // several pixels: the patches really move
  src->start();
  GrayFrame f;
  int mismatches = 0, windows = 0;
  for (int k = 0; k < 200; ++k) {
    src->next(f, 0);
    if (k == 0) {
      CHECK(t.prime(f));
      CHECK(t.frames() == 0);
      CHECK(!t.prime(f));
    }
    const bool analysed = t.push(f);
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = f.pixels[i];
    vb_blur3(gray.data(), 640, 480, blur.data());
    vr_frame(s.get(), blur.data(), f.sequence, f.timestampNs * 1e-9);
    for (int i = 0; i < c.nroi; ++i) {
      if (t.pointU(i) != s->last[i][0] || t.pointV(i) != s->last[i][1] || t.pointValid(i) != (s->last_valid[i] != 0))
        ++mismatches;
    }
    if (s->frames >= VR_N && s->frames % 8 == 0) {
      VRResult r;
      vr_analyze(s.get(), b.get(), 1, &r);
      CHECK(analysed && t.lastWindow().status == r.status);
      ++windows;
    } else {
      CHECK(!analysed);
    }
  }
  CHECK(mismatches == 0);
  CHECK(windows == (200 - 128) / 8 + 1);
  CHECK(t.baselineWindows() == static_cast<int>(b->windows));
}

void testBaselineThenSignal() {
  char dir[] = "/tmp/vm_mp_XXXXXX";
  CHECK(mkdtemp(dir) != nullptr);
  const MultiPointLayout l = layout();
  const std::string path = multiPointBaselinePath(dir, multiPointHash(l), "test");
  std::string err;
  GrayFrame f;
  {
    MultiPointTracker t;
    CHECK(t.start(l, true, "", err));
    auto src = makeSyntheticSource(scene(0));  // motor off: noise only
    src->start();
    for (int k = 0; k < 6 * 60; ++k) {
      src->next(f, 0);
      t.push(f);
    }
    CHECK(t.lastWindow().status == 2);
    CHECK(t.baselineWindows() >= 5);
    CHECK(t.saveBaseline(path));
    CHECK(!t.saveBaseline(path));  // never overwritten
  }
  int windows = 0;
  CHECK(findMultiPointBaseline(dir, l, &windows) == path);
  CHECK(windows >= 5);
  // An old valid file must not become a freshly collected baseline after restart.
  std::set<std::string> thisRun;
  windows = 99;
  CHECK(findMultiPointBaseline(dir, l, &windows, &thisRun).empty() && windows == 0);
  thisRun.insert(path);
  CHECK(findMultiPointBaseline(dir, l, &windows, &thisRun) == path && windows >= 5);
  const std::set<std::string> nextRun;
  CHECK(findMultiPointBaseline(dir, l, nullptr, &nextRun).empty());
  CHECK(findMultiPointBaseline(dir, l) == path);  // history remains intact on disk
  CHECK(findMultiPointBaseline(dir, multiPointLayout(640, 480, Roi{276, 208, 96, 64}, 0.1, 0.9,
                                                     MultiPointConfig())).empty());
  MultiPointTracker none;
  CHECK(!none.start(l, false, std::string(dir) + "/missing.vbb", err) && err == "baseline_required");

  // Motor on: 7 Hz, 1 px on the plate.
  MultiPointTracker t;
  CHECK(t.start(l, false, path, err));
  auto src = makeSyntheticSource(scene(1));
  src->start();
  int signal = 0, analysed = 0;
  for (int k = 0; k < 6 * 60; ++k) {
    src->next(f, 0);
    if (!t.push(f)) continue;
    ++analysed;
    const MultiPointWindow& w = t.lastWindow();
    if (w.status != 1) continue;
    ++signal;
    CHECK_NEAR(w.frequencyHz, 7, 0.25);
    CHECK(w.passed == static_cast<int>(l.points.size()));
    CHECK_NEAR(w.rmsPx[0], 1 / std::sqrt(2.0), 0.15);
  }
  CHECK(analysed == (6 * 60 - 128) / 32 + 1);
  CHECK(signal == analysed);

  // Motor off again: nothing above the baseline.
  MultiPointTracker quiet;
  CHECK(quiet.start(l, false, path, err));
  auto still = makeSyntheticSource(scene(0));
  still->start();
  int passedWindows = 0;
  for (int k = 0; k < 6 * 60; ++k) {
    still->next(f, 0);
    if (quiet.push(f) && quiet.lastWindow().status == 1) ++passedWindows;
  }
  CHECK(passedWindows == 0);
  unlink(path.c_str());
  rmdir(dir);
}

// Windows found the frequency but the representative waveform has a gap:
// the result stays valid (its frequency), the waveform does not, and the
// record leaves the waveform's amplitudes unknown instead of zero.
void testWaveformValidity() {
  MultiPointCapture c;
  c.layout.width = 640;
  c.layout.height = 480;
  c.layout.points.push_back({320, 240});
  c.u.resize(1);
  c.v.resize(1);
  c.ok.resize(1);
  for (int i = 0; i < 160; ++i) {
    const int k = i + (i >= 150 ? 3 : 0);  // three frames missing near the end
    c.tNs.push_back(1000000000LL + static_cast<int64_t>(k) * 1000000000LL / 60);
    c.seq.push_back(static_cast<uint32_t>(k));
    c.timestampValid.push_back(1);
    c.u[0].push_back(0);
    c.v[0].push_back(static_cast<float>(std::sin(2 * M_PI * 7 * i / 60)));
    c.ok[0].push_back(1);
  }
  MultiPointWindow w;
  w.status = 1;
  w.frequencyHz = 7;
  w.fs = 60;
  w.rmsPx = {0.707f};
  w.gateRatio = {5};
  w.valid = {1};
  w.gate = {1};
  w.passed = 1;
  c.windows.push_back(w);
  const MeasureResult r = analyzeMultiPoint(c, MeasureConfig(), 60);
  CHECK(r.valid && r.reason.empty() && r.peakClear);
  CHECK_NEAR(r.dominantHz, 7, 1e-9);
  CHECK(!r.multipoint.waveformValid && r.multipoint.waveformReason == "dropped_frames");
  MultiPointCapture good = c;
  for (size_t i = 0; i < good.tNs.size(); ++i) {
    good.tNs[i] = 1000000000LL + static_cast<int64_t>(i) * 1000000000LL / 60;
    good.seq[i] = static_cast<uint32_t>(i);
  }
  MeasureConfig metric;
  metric.axis = 'y'; metric.mmPerPx = 0.20308513971015377;
  metric.calibrationId = "trial-ruler";
  char layoutId[9]; snprintf(layoutId, sizeof(layoutId), "%08x", multiPointHash(good.layout));
  metric.calibrationLayoutHash = layoutId;
  const auto metricResult = analyzeMultiPoint(good, metric, 60);
  CHECK(metricResult.valid && metricResult.multipoint.waveformValid);
  CHECK_NEAR(metricResult.mmPerPx, metric.mmPerPx, 1e-12);
  CHECK(metricResult.calibrationId == metric.calibrationId);
  CHECK(metricResult.ppPx * metricResult.mmPerPx > .05);
  metric.calibrationLayoutHash = "mismatch";
  CHECK(analyzeMultiPoint(good, metric, 60).mmPerPx == 0);
  metric.calibrationLayoutHash.clear();
  CHECK(analyzeMultiPoint(good, metric, 60).mmPerPx == 0);
  CHECK(AlarmService::classify(.049, .05, .05, true) == AlarmLevel::kNormal);
  CHECK(AlarmService::classify(.05, .05, .05, true) == AlarmLevel::kNormal);
  CHECK(AlarmService::classify(.050001, .05, .05, true) == AlarmLevel::kAlarm);


  char tmpl[] = "/tmp/vm_mp_rec_XXXXXX";
  const std::string dir = mkdtemp(tmpl);
  RecordMeta m;
  m.measurementId = "vibmon-01-gap";
  std::string err;
  CHECK(saveRecord(dir, m, r, {}, {}, err));
  JsonValue rec;
  CHECK(loadJsonFile(dir + "/vibmon-01-gap/record.json", rec, &err));
  const JsonValue& res = jObj(rec, "result");
  CHECK(jBool(res, "valid", false) && jNum(res, "dominant_frequency_hz", 0) == 7);
  const JsonValue* rms = res.find("displacement_rms_px");
  CHECK(rms && rms->isNull());
  const JsonValue& mp = jObj(res, "multipoint");
  CHECK(!jBool(mp, "waveform_valid", true) && jStr(mp, "waveform_reason", "") == "dropped_frames");
  ResultSummary s;
  readMultiPointSummary(res, s);
  CHECK(!s.waveformValid && s.waveformReason == "dropped_frames");
  const std::vector<RecordSummary> list = listRecords(dir, 5);
  CHECK(list.size() == 1 && list[0].valid && list[0].peakClear && !list[0].amplitudes);
  CHECK(std::system(("rm -rf " + dir).c_str()) == 0);
}

}  // namespace

int main() {
  testLayout();
  testSameAsWholeFrameBlur();
  testBaselineThenSignal();
  testWaveformValidity();
  return testSummary("multipoint");
}
