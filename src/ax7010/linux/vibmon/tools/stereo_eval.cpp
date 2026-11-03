// stereo_eval - runs a matcher on a rectified pair, for comparison against
// ground truth (pc_sim/stereo_benchmark.py) and as the reference for a PL port.
//
//   stereo_eval <left.pgm> <right.pgm> <out.raw> <sgm|bm> [key=value ...]
//   stereo_eval --map <calibration.json> <srcW> <srcH> <w> <h> <left|right> <out.raw>
//       dumps the rectification map (float32 sx then sy) for cross-checking
//       against cv2.initUndistortRectifyMap (pc_sim/stereo_calibrate.py --self-test)
//
// sgm keys: disp census paths p1 p2 adaptive uniq lr lrtol median speckle
// bm keys:  disp window uniq support
// out.raw: int16 little endian disparity in 1/16 px, -1 invalid, row major.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "stereo/stereo_match.h"
#include "stereo/stereo_rectify.h"

using namespace vm;

static bool readPgm(const char* path, StereoImage& im) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  int maxv = 0;
  char magic[3] = {0};
  bool ok = fscanf(f, "%2s %d %d %d", magic, &im.w, &im.h, &maxv) == 4 && std::strcmp(magic, "P5") == 0 &&
            maxv == 255 && fgetc(f) != EOF;
  if (ok) {
    im.px.resize(static_cast<size_t>(im.w) * im.h);
    ok = fread(im.px.data(), 1, im.px.size(), f) == im.px.size();
  }
  fclose(f);
  return ok;
}

int main(int argc, char** argv) {
  if (argc == 9 && std::string(argv[1]) == "--map") {
    StereoCalibration c;
    std::string err;
    if (!loadStereoCalibration(argv[2], c, err)) {
      fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    const RectifyMap m = buildRectifyMap(c, std::string(argv[7]) == "right", std::atoi(argv[3]), std::atoi(argv[4]),
                                         std::atoi(argv[5]), std::atoi(argv[6]));
    FILE* o = fopen(argv[8], "wb");
    if (!o) return 1;
    fwrite(m.sx.data(), sizeof(float), m.sx.size(), o);
    fwrite(m.sy.data(), sizeof(float), m.sy.size(), o);
    fclose(o);
    printf("{\"baseline_mm\": %.4f, \"focal_px\": %.4f}\n", c.baselineMm, rectifiedFocalPx(c, std::atoi(argv[5])));
    return 0;
  }
  if (argc < 5) {
    fprintf(stderr, "usage: %s <left.pgm> <right.pgm> <out.raw> <sgm|bm> [key=value ...]\n", argv[0]);
    return 2;
  }
  StereoImage l, r;
  if (!readPgm(argv[1], l) || !readPgm(argv[2], r)) {
    fprintf(stderr, "cannot read input PGM (binary P5, maxval 255)\n");
    return 1;
  }
  const std::string kind = argv[4];
  SgmParams sp;
  BmParams bp;
  for (int i = 5; i < argc; ++i) {
    std::string kv = argv[i];
    const size_t eq = kv.find('=');
    if (eq == std::string::npos) return 2;
    const std::string k = kv.substr(0, eq);
    const int v = std::atoi(kv.c_str() + eq + 1);
    if (k == "disp") sp.maxDisp = bp.maxDisp = v;
    else if (k == "census") sp.census = v;
    else if (k == "paths") sp.paths = v;
    else if (k == "p1") sp.p1 = v;
    else if (k == "p2") sp.p2 = v;
    else if (k == "adaptive") sp.adaptiveP2 = v != 0;
    else if (k == "uniq") sp.uniquenessPct = bp.uniqQ10 = v;
    else if (k == "lr") sp.lrCheck = v != 0;
    else if (k == "lrtol") sp.lrTolerance = v;
    else if (k == "median") sp.median = v != 0;
    else if (k == "speckle") sp.speckleSize = v;
    else if (k == "subpix") sp.subpixel = v;
    else if (k == "contrast") sp.minContrast = v;
    else if (k == "refine") sp.refineLk = v != 0;
    else if (k == "fill") sp.fillHoles = v != 0;
    else if (k == "reject") sp.refineReject = v;
    else if (k == "fillrun") sp.fillMaxRun = v;
    else if (k == "speckle_diff") sp.speckleDiffQ4 = v;
    else if (k == "window") bp.window = v;
    else if (k == "support") bp.spatialSupport = v != 0;
    else {
      fprintf(stderr, "unknown key %s\n", k.c_str());
      return 2;
    }
  }
  const auto t0 = std::chrono::steady_clock::now();
  DisparityMap d = kind == "bm" ? matchBmBaseline(l, r, bp) : matchSgm(l, r, sp);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  FILE* o = fopen(argv[3], "wb");
  if (!o) return 1;
  fwrite(d.q4.data(), sizeof(int16_t), d.q4.size(), o);
  fclose(o);
  // Filled-pixel mask next to the output (absent when no filling ran).
  const std::string maskPath = std::string(argv[3]) + ".filled";
  std::remove(maskPath.c_str());
  if (!d.filled.empty()) {
    FILE* m = fopen(maskPath.c_str(), "wb");
    if (m) {
      fwrite(d.filled.data(), 1, d.filled.size(), m);
      fclose(m);
    }
  }
  printf("{\"w\": %d, \"h\": %d, \"valid\": %d, \"ms\": %.1f}\n", d.w, d.h, d.valid(), ms);
  return 0;
}
