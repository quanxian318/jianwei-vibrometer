// clip_track - offline multi-point displacement tracking of a recorded clip
// with the production trackers (no algorithm copy), for algorithm selection
// and as the golden-data generator for the PL displacement core.
//
//   clip_track <frames.gray> <width> <height> <pts.txt> <out_dir> <ncc|lk> <radius> name:x:y:size[:ref] ...
//
// A point tagged ":ref" is the background reference: every other point is also
// analysed relative to it with the production analyzeWithReference().
// frames.gray: raw 8-bit frames back to back; pts.txt: one time in seconds per frame.
// Writes <out_dir>/<name>_<tracker>.csv: t_s,dx_px,dy_px,score,ok,edge,saturated,converged,iterations,residual
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include "measure/field_spectrum.h"
#include "measure/lk_tracker.h"
#include "measure/session.h"
#include "measure/tracker.h"
#include "util/json_util.h"

using namespace vm;

// Experiment knobs: VM_LK_ITERS, VM_LK_EPS override the LkTracker options.
static LkTracker::Options lkOptions() {
  LkTracker::Options o;
  if (const char* v = getenv("VM_LK_ITERS")) o.maxIterations = std::atoi(v);
  if (const char* v = getenv("VM_LK_EPS")) o.convergePx = std::atof(v);
  return o;
}

int main(int argc, char** argv) {
  if (argc < 9) {
    fprintf(stderr, "usage: %s <frames.gray> <w> <h> <pts.txt> <out_dir> <ncc|lk> <radius> name:x:y:size ...\n", argv[0]);
    return 2;
  }
  FILE* in = fopen(argv[1], "rb");
  if (!in) {
    perror(argv[1]);
    return 1;
  }
  const int w = std::atoi(argv[2]), h = std::atoi(argv[3]);
  std::ifstream pts(argv[4]);
  const std::string out = argv[5], kind = argv[6];
  const int radius = std::atoi(argv[7]);
  if (kind != "ncc" && kind != "lk") return 2;
  struct Point {
    std::string name;
    Roi roi;
    NccTracker ncc;
    LkTracker lk{lkOptions()};
    bool ok = false, ref = false;
    std::ofstream csv;
    std::vector<CaptureSample> samples;
  };
  std::vector<Point> pts_;
  for (int i = 8; i < argc; ++i) {
    Point p;
    char name[64];
    char tag[8] = "";
    if (sscanf(argv[i], "%63[^:]:%d:%d:%d:%7s", name, &p.roi.x, &p.roi.y, &p.roi.w, tag) < 4) return 2;
    p.ref = std::string(tag) == "ref";
    p.name = name;
    p.roi.h = p.roi.w;
    pts_.push_back(std::move(p));
  }
  GrayFrame f;
  f.width = w;
  f.height = h;
  f.pixels.resize(static_cast<size_t>(w) * h);
  double t, t0 = 0, tLast = 0;
  uint32_t frames = 0;
  FieldSpectrum field;
  field.reset(w, h);
  for (uint32_t k = 0; pts >> t; ++k) {
    if (fread(f.pixels.data(), 1, f.pixels.size(), in) != f.pixels.size()) break;
    field.add(f.pixels.data(), w, h);
    tLast = t;
    frames = k + 1;
    f.sequence = k;
    f.timestampNs = static_cast<int64_t>(t * 1e9 + 0.5);
    if (k == 0) t0 = t;
    for (auto& p : pts_) {
      if (k == 0) {
        p.ok = kind == "lk" ? p.lk.init(f, p.roi, radius) : p.ncc.init(f, p.roi, radius);
        p.csv.open(out + "/" + p.name + "_" + kind + ".csv");
        p.csv << "t_s,dx_px,dy_px,score,ok,edge,saturated,converged,iterations,residual,last_step,walked\n" << std::setprecision(10);
        if (kind == "lk")
          fprintf(stderr, "%s: init %d status %d contrast %.1f isotropy %.3f sigma/noise x %.4f y %.4f px\n",
                  p.name.c_str(), p.ok, static_cast<int>(p.lk.initStatus()), p.lk.templateContrast(),
                  p.lk.isotropy(), p.lk.sigmaPerNoiseX(), p.lk.sigmaPerNoiseY());
        else
          fprintf(stderr, "%s: init %d contrast %.1f\n", p.name.c_str(), p.ok, p.ncc.templateContrast());
        p.csv << 0.0 << ",0,0," << (p.ok ? 1 : 0) << ',' << p.ok << ",0,0,1,0,0,0,0\n";
        CaptureSample s0;
        s0.tNs = f.timestampNs;
        s0.seq = k;
        s0.ok = p.ok;
        s0.score = p.ok ? 1 : 0;
        p.samples.push_back(s0);
        continue;
      }
      CaptureSample s;
      s.tNs = f.timestampNs;
      s.seq = k;
      if (!p.ok) {
        p.csv << t - t0 << ",0,0,0,0,0,0,0,0,0,0,0\n";
        p.samples.push_back(s);
        continue;
      }
      if (kind == "lk") {
        const auto r = p.lk.track(f);
        p.csv << t - t0 << ',' << r.dx << ',' << r.dy << ',' << r.score << ',' << r.ok << ',' << r.atEdge << ','
              << r.saturated << ',' << r.converged << ',' << r.iterations << ',' << r.residual << ',' << r.lastStep << ',' << r.walked << '\n';
        s.converged = r.lastStep <= MeasureConfig().maxFinalStepPx;
        s.ok = r.ok; s.dx = r.dx; s.dy = r.dy; s.score = r.score; s.edge = r.atEdge; s.saturated = r.saturated;
      } else {
        const auto r = p.ncc.track(f);
        p.csv << t - t0 << ',' << r.dx << ',' << r.dy << ',' << r.score << ',' << r.ok << ',' << r.atEdge << ','
              << r.saturated << ",1,0,0,0,0\n";
        s.ok = r.ok; s.dx = r.dx; s.dy = r.dy; s.score = r.score; s.edge = r.atEdge; s.saturated = r.saturated;
      }
      p.samples.push_back(s);
    }
  }
  fclose(in);
  // Whole-field brightness lines over the default measurement band (0.5 Hz .. 0.8 Nyquist).
  const double fs = frames > 1 ? (frames - 1) / (tLast - t0) : 0;
  const FieldSpectrumResult fr = field.analyze(fs, 0.5, 0);
  std::ofstream fj(out + "/field_spectrum.json");
  fj << std::setprecision(6) << "{\"valid\": " << (fr.valid ? "true" : "false") << ", \"reason\": \"" << fr.reason
     << "\", \"fs\": " << fs << ", \"grid\": [" << fr.gridW << ", " << fr.gridH << "], \"resolution_hz\": "
     << fr.resolutionHz << ", \"median_fraction\": " << fr.medianFraction << ", \"lines\": [";
  for (size_t i = 0; i < fr.lines.size(); ++i)
    fj << (i ? ", " : "") << "{\"hz\": " << fr.lines[i].hz << ", \"fraction\": " << fr.lines[i].fraction
       << ", \"harmonic_of_strongest\": " << fr.lines[i].harmonicOf << "}";
  fj << "]}\n";
  fprintf(stderr, "field: valid %d %s peak %.3f Hz fraction %.4f median %.4f\n", fr.valid, fr.reason.c_str(), fr.peakHz,
          fr.fraction, fr.medianFraction);
  for (const auto& l : fr.lines) fprintf(stderr, "  line %.3f Hz  %.4f  harmonic %d\n", l.hz, l.fraction, l.harmonicOf);

  // Production analysis per point and axis, raw and relative to the reference.
  const Point* ref = nullptr;
  for (const auto& p : pts_)
    if (p.ref) ref = &p;
  auto summary = [](const MeasureResult& r) {
    JsonValue j = JsonValue::makeObject();
    j.set("valid", r.valid);
    j.set("reason", r.reason);
    j.set("frames", static_cast<int>(r.frames));
    j.set("bad_frames", static_cast<int>(r.badFrames));
    j.set("unconverged_frames", static_cast<int>(r.unconvergedFrames));
    j.set("edge_hits", static_cast<int>(r.edgeHits));
    j.set("fps", r.fpsMeasured);
    j.set("dominant_hz", r.valid ? JsonValue(r.dominantHz) : JsonValue());
    j.set("peak_amp_px", r.valid ? JsonValue(r.peakAmpPx) : JsonValue());
    j.set("peak_snr", r.valid ? JsonValue(r.peakSnr) : JsonValue());
    j.set("peak_clear", r.valid && r.peakClear);
    j.set("peak_to_peak_px", r.valid ? JsonValue(r.ppPx) : JsonValue());
    return j;
  };
  JsonValue all = JsonValue::makeArray();
  for (const auto& p : pts_) {
    for (char axis : {'x', 'y'}) {
      MeasureConfig cfg;
      cfg.axis = axis;
      cfg.tracker = kind;
      JsonValue j = JsonValue::makeObject();
      j.set("name", p.name);
      j.set("axis", std::string(1, axis));
      j.set("tracker", kind);
      j.set("initialized", p.ok);
      j.set("reference", p.ref);
      j.set("raw", summary(analyzeWithReference(p.samples, nullptr, cfg, 30)));
      if (ref && !p.ref) {
        const MeasureResult r = analyzeWithReference(p.samples, &ref->samples, cfg, 30);
        JsonValue rel = summary(r);
        rel.set("reference_valid", r.reference.valid);
        rel.set("reference_reason", r.reference.reason);
        j.set("relative_to_" + ref->name, rel);
      }
      all.push(j);
    }
  }
  std::ofstream(out + "/analysis_" + kind + ".json") << jsonPretty(all);
  return 0;
}
