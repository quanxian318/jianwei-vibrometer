// Stereo matcher, rectification/calibration loading and the depth service on synthetic pairs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "stereo/stereo_match.h"
#include "stereo/stereo_rectify.h"
#include "stereo/stereo_service.h"
#include "test_util.h"
#include "app/config.h"
#include "util/json_util.h"

using namespace vm;

namespace {

// Band-limited random texture, exact at sub-pixel positions.
struct Texture {
  struct Wave { double kx, ky, ph, a; };
  std::vector<Wave> waves;
  explicit Texture(unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < 48; ++i) {
      const double k = 0.1 + 0.9 * u(rng), th = 2 * M_PI * u(rng);
      waves.push_back({k * std::cos(th), k * std::sin(th), 2 * M_PI * u(rng), 1.0 / (1 + 3 * k)});
    }
  }
  double at(double x, double y) const {
    double s = 0;
    for (const auto& w : waves) s += w.a * std::sin(w.kx * x + w.ky * y + w.ph);
    return s;
  }
};

// Left/right rectified pair of fronto-parallel surfaces given by a piecewise
// constant disparity(x, y) of the left image. The right pixel xr shows the
// nearest surface whose left position xr + d has that same disparity, so
// occlusions follow the real geometry.
template <typename F>
void renderPair(const Texture& t, int w, int h, F disparity, StereoImage& l, StereoImage& r, double gainR = 1,
                double offR = 0) {
  l.w = r.w = w;
  l.h = r.h = h;
  l.px.resize(static_cast<size_t>(w) * h);
  r.px.resize(l.px.size());
  for (int y = 0; y < h; ++y) {
    std::vector<double> cand;
    for (int x = 0; x < w; ++x)
      if (std::find(cand.begin(), cand.end(), disparity(x, y)) == cand.end()) cand.push_back(disparity(x, y));
    std::sort(cand.rbegin(), cand.rend());  // nearest first
    for (int x = 0; x < w; ++x) {
      const double vl = 128 + 30 * t.at(x, y);
      double xl = x + cand.back();
      for (double dd : cand) {
        const int xi = static_cast<int>(std::lround(x + dd));
        if (xi >= 0 && xi < w && disparity(xi, y) == dd) {
          xl = x + dd;
          break;
        }
      }
      const double vr = gainR * (128 + 30 * t.at(xl, y)) + offR;
      l.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, vl + 0.5)));
      r.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, vr + 0.5)));
    }
  }
}

struct Score {
  double density = 0, bad = 0, medianErr = 0;
};
template <typename F> Score score(const DisparityMap& m, F truth, int border = 12, bool measuredOnly = false) {
  int known = 0, valid = 0, bad = 0;
  std::vector<double> err;
  for (int y = border; y < m.h - border; ++y)
    for (int x = border + 64; x < m.w - border; ++x) {
      ++known;
      const int16_t v = m.q4[static_cast<size_t>(y) * m.w + x];
      if (v < 0 || (measuredOnly && !m.measured(static_cast<size_t>(y) * m.w + x))) continue;
      ++valid;
      const double e = std::fabs(v / 16.0 - truth(x, y));
      err.push_back(e);
      bad += e > 1.0;
    }
  Score s;
  s.density = known ? static_cast<double>(valid) / known : 0;
  s.bad = valid ? static_cast<double>(bad) / valid : 1;
  if (!err.empty()) {
    std::nth_element(err.begin(), err.begin() + err.size() / 2, err.end());
    s.medianErr = err[err.size() / 2];
  }
  return s;
}

void testPlane() {
  Texture tex(3);
  StereoImage l, r;
  auto d = [](int, int) { return 18.25; };
  renderPair(tex, 320, 180, d, l, r);
  const Score s = score(matchSgm(l, r, SgmParams()), d);
  std::printf("  plane: density %.3f, bad>1px %.4f, median err %.3f px\n", s.density, s.bad, s.medianErr);
  CHECK(s.density > 0.95);
  CHECK(s.bad < 0.01);
  CHECK(s.medianErr < 0.1);
}

// Sub-pixel bias over a sweep of fractional disparities.
void testSubpixelBias() {
  Texture tex(11);
  for (int mode = 0; mode <= 2; ++mode) {
    double worst = 0, sum = 0;
    for (int k = 0; k < 8; ++k) {
      const double dt = 14 + k / 8.0;
      StereoImage l, r;
      auto d = [dt](int, int) { return dt; };
      renderPair(tex, 240, 120, d, l, r);
      SgmParams p;
      p.subpixel = mode == 0 ? 0 : 1;
      p.refineLk = mode == 2;
      std::vector<double> e;
      const DisparityMap m = matchSgm(l, r, p);
      for (int y = 12; y < m.h - 12; ++y)
        for (int x = 80; x < m.w - 12; ++x) {
          const int16_t v = m.q4[static_cast<size_t>(y) * m.w + x];
          if (v >= 0) e.push_back(v / 16.0 - dt);
        }
      double mean = 0;
      for (double v : e) mean += v;
      mean /= std::max<size_t>(1, e.size());
      worst = std::max(worst, std::fabs(mean));
      sum += std::fabs(mean);
    }
    const char* names[] = {"parabola", "equiangular", "equiangular + LK refine"};
    std::printf("  sub-pixel %s: mean |bias| %.3f px, worst %.3f px\n", names[mode], sum / 8, worst);
    if (mode == 2) CHECK(worst < 0.05);
  }
}

void testStepAndExposure() {
  Texture tex(8);
  StereoImage l, r;
  auto d = [](int x, int) { return x < 190 ? 10.0 : 31.5; };  // two planes, depth step
  renderPair(tex, 320, 180, d, l, r, 0.8, 12);                 // right camera darker, offset
  SgmParams p;
  p.census = 9;
  const DisparityMap sm = matchSgm(l, r, p);
  const Score s = score(sm, d);
  const Score sMeas = score(sm, d, 12, true);
  BmParams bp;
  const Score b = score(matchBmBaseline(l, r, bp), d);
  std::printf("  step+exposure: sgm (measured + filled) density %.3f bad %.4f | team bm density %.3f bad %.4f\n",
              s.density, s.bad, b.density, b.bad);
  std::printf("  step+exposure: measured only density %.3f bad %.4f\n", sMeas.density, sMeas.bad);
  CHECK(sMeas.density > 0.85);
  CHECK(sMeas.bad < 0.025);
  CHECK(s.density > 0.95);
  CHECK(s.bad < 0.05);
}

void testFlatRejected() {
  StereoImage l, r;
  l.w = r.w = 160;
  l.h = r.h = 90;
  l.px.assign(160 * 90, 120);
  r.px = l.px;
  CHECK(matchSgm(l, r, SgmParams()).valid() == 0);  // no texture: nothing invented
  CHECK(matchBmBaseline(l, r, BmParams()).valid() == 0);
}

std::string writeCalibration(double f, double baseline) {
  JsonValue root = JsonValue::makeObject();
  root.set("id", "unit-test");
  JsonValue size = JsonValue::makeArray();
  size.push(320);
  size.push(180);
  root.set("image_size", size);
  for (const char* side : {"left", "right"}) {
    JsonValue cam = JsonValue::makeObject();
    auto arr = [](std::initializer_list<double> v) {
      JsonValue a = JsonValue::makeArray();
      for (double x : v) a.push(x);
      return a;
    };
    cam.set("K", arr({f, 0, 160, 0, f, 90, 0, 0, 1}));
    cam.set("D", arr({0, 0, 0, 0, 0}));
    cam.set("R", arr({1, 0, 0, 0, 1, 0, 0, 0, 1}));
    const double tx = std::string(side) == "right" ? -f * baseline : 0;
    cam.set("P", arr({f, 0, 160, tx, 0, f, 90, 0, 0, 0, 1, 0}));
    root.set(side, cam);
  }
  const std::string path = "/tmp/vibmon_test_stereo_calibration.json";
  writeFileAtomic(path, jsonPretty(root));
  return path;
}

GrayFrame toFrame(const StereoImage& im, int64_t t) {
  GrayFrame f;
  f.width = im.w;
  f.height = im.h;
  f.pixels = im.px;
  f.timestampNs = t;
  return f;
}

void testColourRectification() {
  GrayFrame f; f.width = f.height = 12; f.bgr.resize(12 * 12 * 3);
  for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
    const int i = y * 12 + x;
    f.bgr[3*i] = 10+x; f.bgr[3*i+1] = 40+y; f.bgr[3*i+2] = 180+x+y;
  }
  StereoCalibration c;
  auto map = buildRectifyMap(c, false, 12, 12, 6, 6);
  for (auto& x : map.sx) x += .25f;
  auto bgr = rectifyBgrImage(f, map, 2);
  CHECK(bgr.size() == 6 * 6 * 3);
  f.pixels.resize(12 * 12);
  for (int channel = 0; channel < 3; ++channel) {
    for (size_t i = 0; i < f.pixels.size(); ++i) f.pixels[i] = f.bgr[3*i+channel];
    auto gray = rectifyImage(f, map, 2);
    for (size_t i = 0; i < gray.px.size(); ++i) CHECK(bgr[3*i+channel] == gray.px[i]);
  }
  f.bgr.pop_back(); CHECK(rectifyBgrImage(f, map, 2).empty());
}

void testExtendedPlRange() {
  Texture texture(921); StereoImage l,r;
  renderPair(texture,256,144,[](int,int){return 83.25;},l,r);
  auto match=[&](int offset) {
    auto shifted=offset ? shiftStereoRight(r,offset) : r;
    auto d=fuseVerticalPasses(plCoreModel(l,shifted),plCoreModel(flipStereoRows(l),flipStereoRows(shifted)));
    return offset ? offsetDisparities(std::move(d),offset) : d;
  };
  auto low=match(0), high=match(64), wide=mergeDisparityRanges(l,r,low,high);
  int near=0,bad=0,lowNear=0,availableNear=0;
  for(int y=16;y<128;++y)for(int x=165;x<242;++x) {
    const size_t i=static_cast<size_t>(y)*256+x;
    availableNear+=high.measured(i)&&std::abs(high.q4[i]/16.-83.25)<1;
    lowNear+=low.measured(i)&&std::abs(low.q4[i]/16.-83.25)<1;
    if(wide.measured(i)) {++near;bad+=std::abs(wide.q4[i]/16.-83.25)>1;}
  }
  CHECK(lowNear==0); CHECK(near>5000); CHECK(bad<near*.02);
  std::printf("  extended PL: near plane %d valid, %d wrong, %d independently supported\n",near,bad,availableNear);
  // Check that adding the near band also preserves a separate far surface.
  renderPair(texture,256,144,[](int,int){return 12.25;},l,r);
  low=match(0); high=match(64); wide=mergeDisparityRanges(l,r,low,high);
  int far=0,farBad=0;
  for(int y=16;y<128;++y)for(int x=165;x<242;++x) {
    const size_t i=static_cast<size_t>(y)*256+x;
    if(wide.measured(i)) {++far; farBad+=std::abs(wide.q4[i]/16.-12.25)>1;}
  }
  CHECK(far>5000 && farBad<far*.02);
  l.px.assign(l.px.size(),128); r.px=l.px;
  low.q4.assign(low.q4.size(),-1); high.q4.assign(high.q4.size(),1024);
  CHECK(mergeDisparityRanges(l,r,low,high).valid()==0); // textureless area is never fabricated
}

void testSearchRangeSettingsOverlay() {
  AppConfig config;
  const std::string path=writeCalibration(321.5,40); std::string err;
  CHECK(writeFileAtomic(path,"{\"stereo\":{\"pl_search_ranges\":2}}"));
  CHECK(loadConfig(path,config,err) && config.stereo.service.plSearchRanges==2);
  CHECK(writeFileAtomic(path,"{\"measurement\":{\"duration_s\":10}}"));
  CHECK(loadConfig(path,config,err) && config.stereo.service.plSearchRanges==2);
  std::remove(path.c_str());
}

void testTwentyCentimetreProfile() {
  const std::string path = writeCalibration(321.5, 40);
  StereoConfig cfg; cfg.calibrationFile = path;
  cfg.workWidth = 256; cfg.workHeight = 144;
  cfg.temporal = false; cfg.sgm.refineLk = false;
  Texture texture(271);
  StereoImage l, r;
  const double disparity = (321.5 * 256 / 320) * 40 / 200;
  CHECK(disparity < 63);  // 20 cm stays inside the existing PL search range
  renderPair(texture, 256, 144, [=](int,int){ return disparity; }, l, r);
  StereoService service(cfg, nullptr, nullptr);
  auto d = service.process(toFrame(l,0),toFrame(r,0));
  double scale=0, z=0;
  CHECK(stereoScaleAt(*d,.6,.5,.1,.1,1286,scale,z,16));
  CHECK_NEAR(z,200,8);
  CHECK(d->w==256 && d->h==144);
  std::remove(path.c_str());
}

void testCalibrationAndService() {
  StereoCalibration c;
  std::string err;
  CHECK(!loadStereoCalibration("/nonexistent.json", c, err));
  const std::string path = writeCalibration(300, 60);
  CHECK(loadStereoCalibration(path, c, err));
  CHECK_NEAR(c.baselineMm, 60, 1e-9);
  CHECK_NEAR(rectifiedFocalPx(c, 320), 300, 1e-9);

  // Plane at Z = 1000 mm -> d = f B / Z = 18 px.
  Texture tex(5);
  StereoImage l, r;
  renderPair(tex, 320, 180, [](int, int) { return 18.0; }, l, r);
  StereoConfig sc;
  sc.calibrationFile = path;
  std::vector<std::shared_ptr<const DepthMap>> published;
  StereoService svc(sc, nullptr, [&](std::shared_ptr<const DepthMap> d) { published.push_back(d); });
  CHECK(svc.stats().calibrated);
  auto colour = toFrame(l, 0);
  colour.bgr.resize(colour.pixels.size() * 3);
  for (size_t i = 0; i < colour.pixels.size(); ++i) {
    colour.bgr[3*i] = colour.pixels[i];
    colour.bgr[3*i+1] = colour.pixels[i];
    colour.bgr[3*i+2] = colour.pixels[i];
  }
  colour.pixels.clear(); colour.colorOnly = true;  // fast capture path: gray derived only after pairing
  auto first = svc.process(colour, toFrame(r, 0));
  CHECK(first->leftBgr.size() == first->mm.size()*3);
  CHECK(first->validFraction == 0);  // temporal confirmation still required for measurements
  CHECK(std::count_if(first->displayMm.begin(), first->displayMm.end(), [](uint16_t z){return z>0;}) > 1000);
  CHECK(first->at(.5,.5) == 0);  // a colourful preview cannot fabricate a distance reading
  auto d = svc.process(toFrame(l, 1000), toFrame(r, 1000));
  CHECK(d->backend == "PL-model");
  CHECK(d->validFraction > 0.6);
  double mmPerPx = 0, z = 0;
  CHECK(stereoScaleAt(*d, 0.6, 0.5, 0.1, 0.1, 300, mmPerPx, z));
  std::printf("  service: valid %.3f, Z %.1f mm, mm/px %.4f, %.1f ms\n", d->validFraction, z, mmPerPx, d->procMs);
  CHECK_NEAR(z, 1000, 10);
  CHECK_NEAR(mmPerPx, 1000.0 / 300, 0.04);

  // A 4 px lens-centring offset reads 1000 mm as 818 mm. The distance
  // correction at a point known to be 1000 mm away removes it from the next
  // pair on and keeps it in the calibration file.
  renderPair(tex, 320, 180, [](int, int) { return 22.0; }, l, r);
  StereoService off(sc, nullptr, nullptr);
  off.process(toFrame(l, 0), toFrame(r, 0));
  auto o = off.process(toFrame(l, 1000), toFrame(r, 1000));
  CHECK(stereoScaleAt(*o, 0.6, 0.5, 0.1, 0.1, 300, mmPerPx, z));
  CHECK_NEAR(z, 300.0 * 60 / 22, 15);
  double shift = 0;
  CHECK(distanceOffsetPx(*o, off.calibration().width, 0.6, 0.5, 1000, shift, err));
  CHECK_NEAR(shift, 4, 0.2);
  CHECK(!distanceOffsetPx(*o, off.calibration().width, 0.6, 0.5, 0, shift, err));
  CHECK(off.correctOffset(shift, o->calibrationVersion, "1000", err));
  // The same map again (a second tap before a new pair): it does not show the
  // correction yet, so the offset is not applied twice.
  CHECK(!off.correctOffset(shift, o->calibrationVersion, "1000", err) && err == "depth_stale");
  off.process(toFrame(l, 2000), toFrame(r, 2000));
  auto fixed = off.process(toFrame(l, 3000), toFrame(r, 3000));
  CHECK(fixed->calibrationVersion == off.calibrationVersion() && fixed->calibrationVersion != o->calibrationVersion);
  CHECK(stereoScaleAt(*fixed, 0.6, 0.5, 0.1, 0.1, 300, mmPerPx, z));
  CHECK_NEAR(z, 1000, 15);
  StereoCalibration saved;
  CHECK(loadStereoCalibration(path, saved, err));
  CHECK_NEAR(saved.right.K[2], 160 - shift, 1e-6);
  CHECK(saved.id.size() >= 9 && saved.id.compare(saved.id.size() - 9, 9, "-dist1000") == 0);
  // On a map of the corrected calibration the next correction is accepted (and small).
  double again = 0;
  CHECK(distanceOffsetPx(*fixed, off.calibration().width, 0.6, 0.5, 1000, again, err));
  CHECK(std::fabs(again) < 0.5 && off.correctOffset(again, fixed->calibrationVersion, "1000", err));

  // Without calibration the service never publishes millimetres.
  StereoConfig nc;
  nc.calibrationFile = "/nonexistent.json";
  StereoService none(nc, nullptr, nullptr);
  CHECK(!none.stats().calibrated);
  none.process(toFrame(l, 0), toFrame(r, 0));
  auto u = none.process(toFrame(l, 0), toFrame(r, 0));
  CHECK(std::all_of(u->mm.begin(), u->mm.end(), [](uint16_t v) { return v == 0; }));
  CHECK(!stereoScaleAt(*u, 0.5, 0.5, 0.1, 0.1, 300, mmPerPx, z));
}

// A confirmed pixel that drops out keeps its value: one frame as a measurement, then for display.
void testTemporalHold() {
  DisparityMap m;
  m.w = 4;
  m.h = 1;
  m.q4.assign(4, 160);
  TemporalDisparityFilter tf;
  tf.apply(m);
  DisparityMap out = tf.apply(m);
  CHECK(out.measured(1) && out.q4[1] == 160);
  m.q4[1] = kInvalidDisparity;
  for (int k = 1; k <= TemporalDisparityFilter::kHoldFrames + 1; ++k) {
    out = tf.apply(m);
    if (k <= TemporalDisparityFilter::kHoldFrames) {
      CHECK(!out.measured(1) && out.q4[1] == 160);
    } else {
      CHECK(out.q4[1] < 0);
    }
  }
}

// Display filling also closes short column gaps between agreeing measured pixels.
void testFillColumns() {
  StereoImage im;
  im.w = im.h = 10;
  im.px.assign(100, 128);
  DisparityMap m;
  m.w = m.h = 10;
  m.q4.assign(100, kInvalidDisparity);
  m.q4[2 * 10 + 5] = 160;  // column 5: rows 3..6 empty, ends agree
  m.q4[7 * 10 + 5] = 168;
  m.q4[2 * 10 + 8] = 160;  // column 8: ends disagree
  m.q4[7 * 10 + 8] = 200;
  SgmParams p;
  p.minContrast = 0;
  p.refineLk = false;
  p.median = false;
  p.speckleSize = 0;
  stereoPostProcess(im, im, 0, p, m);
  for (int y = 3; y <= 6; ++y) {
    const int v = m.q4[y * 10 + 5];
    CHECK(v >= 160 && v <= 168 && m.filled[y * 10 + 5]);
    CHECK(m.q4[y * 10 + 8] < 0);
  }
  CHECK(!m.filled[2 * 10 + 5] && !m.filled[7 * 10 + 5]);
}

// Flicker of isolated wrong matches over frames with independent sensor noise.
void testTemporalFilter() {
  Texture tex(21);
  StereoImage l0, r0;
  auto d = [](int x, int) { return x < 170 ? 12.0 : 26.0; };
  renderPair(tex, 320, 180, d, l0, r0);
  std::mt19937 rng(3);
  std::normal_distribution<double> noise(0, 3);
  TemporalDisparityFilter tf;
  SgmParams p;
  p.census = 9;
  p.paths = 2;
  int rawWrong = 0, filtWrong = 0, rawValid = 0, filtValid = 0;
  for (int f = 0; f < 8; ++f) {
    StereoImage l = l0, r = r0;
    for (auto* im : {&l, &r})
      for (auto& v : im->px) v = static_cast<uint8_t>(std::max(0.0, std::min(255.0, v + noise(rng))));
    const DisparityMap raw = matchSgm(l, r, p);
    const DisparityMap flt = tf.apply(raw);
    if (f < 2) continue;
    for (int y = 12; y < 168; ++y)
      for (int x = 76; x < 308; ++x) {
        const size_t i = static_cast<size_t>(y) * 320 + x;
        if (raw.measured(i)) {
          ++rawValid;
          rawWrong += std::fabs(raw.q4[i] / 16.0 - d(x, y)) > 1;
        }
        if (flt.measured(i)) {
          ++filtValid;
          filtWrong += std::fabs(flt.q4[i] / 16.0 - d(x, y)) > 1;
        }
      }
  }
  const double rb = static_cast<double>(rawWrong) / rawValid, fb = static_cast<double>(filtWrong) / filtValid;
  std::printf("  temporal: wrong %.4f -> %.4f, measured %d -> %d px over 6 frames\n", rb, fb, rawValid, filtValid);
  CHECK(fb <= rb);
  CHECK(filtValid > 0.9 * rawValid);
}

}  // namespace

// Opposite vertical scans must agree before becoming a measured depth.
// A hole may retain a real one-sided estimate with independent neighbours;
// it must never acquire an invented depth from interpolation.
static void testVerticalPasses() {
  StereoImage image;image.w=3;image.h=2;image.px={1,2,3,4,5,6};
  CHECK(flipStereoRows(image).px == std::vector<uint8_t>({4,5,6,1,2,3}));
  DisparityMap a,b;a.w=b.w=5;a.h=b.h=5;a.q4.assign(25,128);b.q4.assign(25,136);
  auto out=fuseVerticalPasses(a,b);CHECK(out.q4[12]==132);
  b.q4[12]=256;out=fuseVerticalPasses(a,b);CHECK(out.q4[12]==kInvalidDisparity);
  b.q4[12]=-1;out=fuseVerticalPasses(a,b);CHECK(out.q4[12]==128);
  b.q4.assign(25,-1);out=fuseVerticalPasses(a,b);CHECK(out.valid()==0);
  b.q4.assign(25,128);b.filled.assign(25,1);out=fuseVerticalPasses(a,b);CHECK(out.valid()==0);
  b.filled.clear();b.q4.assign(25,128);a.q4.assign(25,-1);a.q4[6]=128;a.q4[7]=128;a.q4[8]=128;
  b.q4[12]=-1;out=fuseVerticalPasses(a,b);CHECK(out.q4[12]==-1);
  // Independent synthetic truth: retain the textured planes and their
  // depth step despite an exposure difference between the cameras.
  Texture texture(501);
  StereoImage l,r;
  auto truth=[](int x,int y){return x>100&&y>40 ? 14.35 : 8.35;};
  renderPair(texture,192,96,truth,l,r,1.12,7);
  auto old=plCoreModel(l,r);
  auto fused=fuseVerticalPasses(old,plCoreModel(flipStereoRows(l),flipStereoRows(r)));
  int oldCount=0,newCount=0,oldBad=0,newBad=0;
  for(int y=12;y<84;++y) for(int x=40;x<180;++x){
    size_t i=static_cast<size_t>(y)*192+x;
    if(old.measured(i)){++oldCount;oldBad+=std::abs(old.q4[i]/16.0-truth(x,y))>1;}
    if(fused.measured(i)){++newCount;newBad+=std::abs(fused.q4[i]/16.0-truth(x,y))>1;}
  }
  CHECK(newCount>0.75*oldCount);
  CHECK(newBad*1.0/newCount<=oldBad*1.0/oldCount+0.005);
  std::printf("  vertical check: measured %d -> %d, bad>1px %.4f -> %.4f\n",oldCount,newCount,
              oldBad*1.0/oldCount,newBad*1.0/newCount);
}

int main() {
  testColourRectification();
  testSearchRangeSettingsOverlay();
  testExtendedPlRange();
  testTwentyCentimetreProfile();
  testVerticalPasses();
  testPlane();
  testSubpixelBias();
  testStepAndExposure();
  testFlatRejected();
  testTemporalFilter();
  testTemporalHold();
  testFillColumns();
  testCalibrationAndService();
  return testSummary("stereo");
}
