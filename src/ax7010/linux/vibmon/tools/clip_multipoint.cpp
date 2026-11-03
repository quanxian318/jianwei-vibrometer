// clip_multipoint - offline replay of the multi-point measurement
// (measure/multipoint, the team's core) on recorded clips: a motor-off clip
// gives the baseline, a motor-on clip is measured, with the production code.
//
//   clip_multipoint <w> <h> <box_x> <box_y> <box_w> <box_h> <ref_x> <ref_y> <max_points>
//                   <baseline.gray> <baseline_pts.txt> <measure.gray> <measure_pts.txt>
//
// Box and reference group centre in image pixels. *.gray: raw 8-bit frames
// back to back; pts.txt: one time in seconds per frame (ffprobe csv works).
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "measure/multipoint.h"
#include "measure/session.h"

using namespace vm;

namespace {

struct Clip {
  FILE* f = nullptr;
  std::vector<double> t;
  int w = 0, h = 0;
  size_t next = 0;
  bool open(const char* frames, const char* pts, int width, int height) {
    w = width;
    h = height;
    std::ifstream in(pts);
    std::string line;
    while (std::getline(in, line))
      if (!line.empty()) t.push_back(std::atof(line.c_str()));
    f = fopen(frames, "rb");
    return f && !t.empty();
  }
  bool read(GrayFrame& g) {
    if (next >= t.size()) return false;
    g.width = w;
    g.height = h;
    g.pixels.resize(static_cast<size_t>(w) * h);
    if (fread(g.pixels.data(), 1, g.pixels.size(), f) != g.pixels.size()) return false;
    g.timestampNs = static_cast<int64_t>((1.0 + t[next]) * 1e9);  // > 0
    g.timestampValid = true;
    g.sequence = static_cast<uint32_t>(next++);
    return true;
  }
  double fps() const { return t.size() > 1 ? (t.size() - 1) / (t.back() - t.front()) : 0; }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 14) {
    fprintf(stderr,
            "usage: %s <w> <h> <box_x> <box_y> <box_w> <box_h> <ref_x> <ref_y> <max_points> "
            "<baseline.gray> <baseline_pts> <measure.gray> <measure_pts>\n",
            argv[0]);
    return 2;
  }
  const int w = std::atoi(argv[1]), h = std::atoi(argv[2]);
  const Roi box{std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6])};
  MeasureConfig cfg;
  cfg.method = "multipoint";
  cfg.multipoint.maxPoints = std::atoi(argv[9]);
  cfg.refCx = std::atof(argv[7]) / w;
  cfg.refCy = std::atof(argv[8]) / h;
  const MultiPointLayout layout = multiPointLayout(w, h, box, cfg.refCx, cfg.refCy, cfg.multipoint);
  if (!layout.error.empty()) {
    fprintf(stderr, "layout: %s\n", layout.error.c_str());
    return 1;
  }
  printf("layout: %zu points, %zu references, hash %08x\n", layout.points.size(), layout.refs.size(),
         multiPointHash(layout));

  // Motor-off baseline.
  Clip base;
  if (!base.open(argv[10], argv[11], w, h)) return 1;
  MultiPointTracker t;
  std::string err;
  t.start(layout, true, "", err);
  GrayFrame g;
  int baseWindows = 0;
  while (base.read(g))
    if (t.push(g) && t.lastWindow().status == 2) ++baseWindows;
  const std::string path = "/tmp/clip_multipoint_" + std::to_string(getpid()) + ".vbb";
  unlink(path.c_str());
  if (!t.saveBaseline(path)) {
    fprintf(stderr, "baseline not saved (%d windows over %zu frames)\n", baseWindows, base.t.size());
    return 1;
  }
  printf("baseline: %zu frames at %.2f fps, %d windows\n", base.t.size(), base.fps(), t.baselineWindows());

  // Measurement with the production capture and analysis.
  Clip meas;
  if (!meas.open(argv[12], argv[13], w, h)) return 1;
  MultiPointTracker m;
  if (!m.start(layout, false, path, err)) {
    fprintf(stderr, "measure: %s\n", err.c_str());
    return 1;
  }
  MultiPointCapture cap;
  cap.reset(layout);
  while (meas.read(g)) {
    const uint32_t before = m.frames();
    const bool analysed = m.push(g);
    cap.add(g, m, m.frames() != before);
    if (!analysed) continue;
    const MultiPointWindow& win = m.lastWindow();
    cap.windows.push_back(win);
    printf("  window at %6.2f s: %-17s %s passed %d/%zu\n", meas.t[g.sequence], multiPointStatusName(win.status),
           win.status == 1 ? (std::to_string(win.frequencyHz).substr(0, 6) + " Hz").c_str() : "         ",
           win.passed, layout.points.size());
  }
  unlink(path.c_str());
  cfg.baselineFile = path;
  const MeasureResult r = analyzeMultiPoint(cap, cfg, meas.fps());
  const MeasureResult::MultiPoint& mp = r.multipoint;
  std::vector<float> rms;
  for (size_t i = 0; i < mp.gate.size(); ++i)
    if (mp.gate[i]) rms.push_back(mp.rmsPx[i]);
  std::sort(rms.begin(), rms.end());
  printf("result: %s%s, %.3f Hz (signal windows %d of %d, %.3f..%.3f Hz), %d/%zu points above the baseline, "
         "median RMS %.3f px\n",
         r.valid ? "valid" : "invalid ", r.valid ? "" : r.reason.c_str(), r.dominantHz, mp.signalWindows, mp.windows,
         mp.frequencyMinHz, mp.frequencyMaxHz, mp.passed, mp.points.size(),
         rms.empty() ? 0.0 : rms[rms.size() / 2]);
  return r.valid ? 0 : 3;
}
