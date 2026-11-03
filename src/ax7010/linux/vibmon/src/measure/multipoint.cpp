#include "measure/multipoint.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "vb_runtime.h"

namespace vm {

namespace {

// The core blurs every whole frame (vb_blur3) before tracking; only the
// pixels its templates read are needed. A patch reads within 12 (half
// template) + 8 x 3 (Gauss-Newton steps, each clipped to 3 px) + 2 (gradient
// and bilinear taps) pixels of where it was last found.
constexpr int kReach = 12 + 8 * 3 + 2;

VRConfig toConfig(const MultiPointLayout& l) {
  VRConfig c;
  memset(&c, 0, sizeof(c));  // the configuration hash covers every byte
  c.width = l.width;
  c.height = l.height;
  c.nroi = static_cast<int>(l.points.size());
  c.nref = static_cast<int>(l.refs.size());
  int k = 0;
  for (const auto& p : l.points) {
    c.xy[k][0] = p[0];
    c.xy[k++][1] = p[1];
  }
  for (const auto& p : l.refs) {
    c.xy[k][0] = p[0];
    c.xy[k++][1] = p[1];
  }
  return c;
}

}  // namespace

MultiPointLayout multiPointLayout(int width, int height, const Roi& box, double refCx, double refCy,
                                  const MultiPointConfig& cfg) {
  MultiPointLayout l;
  l.width = width;
  l.height = height;
  const int pitch = std::max(25, cfg.pointPitchPx);
  int nx = std::max(1, box.w / pitch), ny = std::max(1, box.h / pitch);
  if (cfg.gridCols > 0 && cfg.gridRows > 0) {
    nx = std::min(nx, cfg.gridCols);
    ny = std::min(ny, cfg.gridRows);
  }
  const int maxPoints = std::max(1, std::min(cfg.maxPoints, VR_MAX_ROI));
  while (nx * ny > maxPoints) {
    if (nx >= ny) --nx;
    else --ny;
  }
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i)
      l.points.push_back({static_cast<float>(box.x + (i + 0.5) * box.w / nx),
                          static_cast<float>(box.y + (j + 0.5) * box.h / ny)});
  const double rx = refCx * width, ry = refCy * height, h = 0.5 * std::max(25, cfg.refPitchPx);
  for (double dy : {-h, h})
    for (double dx : {-h, h}) l.refs.push_back({static_cast<float>(rx + dx), static_cast<float>(ry + dy)});
  // The core takes centres 15 .. size - 16 pixels from the borders.
  const auto inside = [&](const std::array<float, 2>& p) {
    return p[0] >= 15 && p[1] >= 15 && p[0] <= width - 16 && p[1] <= height - 16;
  };
  for (const auto& p : l.points)
    if (!inside(p)) l.error = "roi_outside_frame";
  const double bx = box.x + 0.5 * box.w, by = box.y + 0.5 * box.h;
  for (const auto& p : l.refs) {
    if (l.error.empty() && !inside(p)) l.error = "reference_outside_frame";
    // A reference template reaching into the measuring box moves with the plate.
    if (l.error.empty() && std::fabs(p[0] - bx) < 0.5 * box.w + 12 && std::fabs(p[1] - by) < 0.5 * box.h + 12)
      l.error = "reference_in_box";
  }
  return l;
}

uint32_t multiPointHash(const MultiPointLayout& l) {
  const VRConfig c = toConfig(l);
  return vr_config_hash(&c);
}

const char* multiPointStatusName(int s) {
  return s == 1 ? "SIGNAL" : s == 0 ? "NO_SIGNAL" : s == 2 ? "BASELINE" : s == -2 ? "BASELINE_MISMATCH"
                                                                       : s == -3 ? "WARMUP" : "INVALID_WINDOW";
}

std::string multiPointBaselinePath(const std::string& dir, uint32_t hash, const std::string& tag) {
  char h[16];
  snprintf(h, sizeof(h), "%08x", hash);
  return dir + "/" + h + "-" + tag + ".vbb";
}

std::string findMultiPointBaseline(const std::string& dir, const MultiPointLayout& l, int* windows,
                                  const std::set<std::string>* allowedPaths) {
  if (windows) *windows = 0;
  if (!l.error.empty() || l.points.empty() || l.refs.size() < 3) return std::string();
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%08x-", multiPointHash(l));
  const VRConfig c = toConfig(l);
  DIR* d = opendir(dir.c_str());
  if (!d) return std::string();
  std::string best, bestName;
  time_t bestT = 0;
  std::unique_ptr<VRBaseline> b(new VRBaseline());
  while (dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.compare(0, 9, prefix) != 0 || name.size() < 13 || name.compare(name.size() - 4, 4, ".vbb") != 0) continue;
    const std::string p = dir + "/" + name;
    if (allowedPaths && allowedPaths->count(p) == 0) continue;
    struct stat st;
    if (stat(p.c_str(), &st) != 0) continue;
    if (!best.empty() && (st.st_mtime < bestT || (st.st_mtime == bestT && name < bestName))) continue;
    if (!vr_baseline_load(p.c_str(), &c, b.get())) continue;
    best = p;
    bestName = name;
    bestT = st.st_mtime;
    if (windows) *windows = static_cast<int>(b->windows);
  }
  closedir(d);
  return best;
}

struct MultiPointTracker::Impl {
  VRConfig cfg;
  VRSession s;
  VRBaseline b;
  bool collect = false, haveTime = false;
  double firstTime = 0, lastTime = 0;
  std::vector<float> blurred;
};

MultiPointTracker::MultiPointTracker() = default;
MultiPointTracker::~MultiPointTracker() = default;

bool MultiPointTracker::start(const MultiPointLayout& l, bool collect, const std::string& baselinePath,
                              std::string& err) {
  impl_.reset();
  window_ = MultiPointWindow();
  if (!l.error.empty()) {
    err = l.error;
    return false;
  }
  if (l.points.empty() || l.points.size() > VR_MAX_ROI || l.refs.size() < 3 || l.refs.size() > VR_MAX_REF) {
    err = "roi_outside_frame";
    return false;
  }
  std::unique_ptr<Impl> m(new Impl());
  memset(&m->b, 0, sizeof(m->b));
  m->cfg = toConfig(l);
  if (!collect && !vr_baseline_load(baselinePath.c_str(), &m->cfg, &m->b)) {
    err = "baseline_required";
    return false;
  }
  vr_init(&m->s, &m->cfg);
  m->collect = collect;
  m->blurred.assign(static_cast<size_t>(l.width) * l.height, 0.f);
  impl_ = std::move(m);
  return true;
}

void MultiPointTracker::blurAround(const GrayFrame& f) {
  Impl& m = *impl_;
  const int w = m.cfg.width, h = m.cfg.height;
  // Merge the overlapping template rectangles before filtering. Read the
  // byte image directly, retaining vb_blur3's nine taps and addition order
  // exactly; no separable approximation and no change to the tracking core.
  std::vector<Roi> boxes;
  int top = h, bottom = 0;
  for (int i = 0; i < m.cfg.nroi + m.cfg.nref; ++i) {
    const float cx = m.cfg.xy[i][0] + (m.s.initialized ? m.s.patch[i].u : 0.f);
    const float cy = m.cfg.xy[i][1] + (m.s.initialized ? m.s.patch[i].v : 0.f);
    const int x0 = std::max(0, static_cast<int>(std::floor(cx)) - kReach);
    const int x1 = std::min(w - 1, static_cast<int>(std::ceil(cx)) + kReach);
    const int y0 = std::max(0, static_cast<int>(std::floor(cy)) - kReach);
    const int y1 = std::min(h - 1, static_cast<int>(std::ceil(cy)) + kReach);
    if (x1 < x0 || y1 < y0) continue;
    boxes.push_back(Roi{x0, y0, x1 - x0 + 1, y1 - y0 + 1});
    top = std::min(top, y0);
    bottom = std::max(bottom, y1 + 1);
  }
  constexpr float a = .20945361f, b = .58109278f;
  constexpr float corner = a * a, side = a * b, centre = b * b;
  std::vector<std::pair<int, int>> spans;
  spans.reserve(boxes.size());
  for (int y = top; y < bottom; ++y) {
    spans.clear();
    for (const auto& r : boxes)
      if (y >= r.y && y < r.y + r.h) spans.emplace_back(r.x, r.x + r.w);
    std::sort(spans.begin(), spans.end());
    const uint8_t* up = &f.pixels[static_cast<size_t>(y ? y - 1 : 1) * w];
    const uint8_t* mid = &f.pixels[static_cast<size_t>(y) * w];
    const uint8_t* down = &f.pixels[static_cast<size_t>(y + 1 < h ? y + 1 : h - 2) * w];
    int covered = 0;
    for (const auto& span : spans) {
      for (int x = std::max(covered, span.first); x < span.second; ++x) {
        const int left = x ? x - 1 : 1, right = x + 1 < w ? x + 1 : w - 2;
        float v = 0;
        v += corner * up[left];
        v += side * up[x];
        v += corner * up[right];
        v += side * mid[left];
        v += centre * mid[x];
        v += side * mid[right];
        v += corner * down[left];
        v += side * down[x];
        v += corner * down[right];
        m.blurred[static_cast<size_t>(y) * w + x] = v;
      }
      covered = std::max(covered, span.second);
    }
  }
}

bool MultiPointTracker::prime(const GrayFrame& f) {
  if (!impl_ || impl_->s.frames || impl_->s.initialized || f.width != impl_->cfg.width ||
      f.height != impl_->cfg.height || f.pixels.size() != impl_->blurred.size()) return false;
  Impl& m = *impl_;
  blurAround(f);
  for (int i = 0; i < m.cfg.nroi + m.cfg.nref; ++i)
    vb_patch_init(&m.s.patch[i], m.blurred.data(), m.cfg.width, m.cfg.height, m.cfg.xy[i][0], m.cfg.xy[i][1]);
  m.s.initialized = 1;
  return true;
}

bool MultiPointTracker::push(const GrayFrame& f) {
  if (!impl_) return false;
  Impl& m = *impl_;
  const double t = static_cast<double>(f.timestampNs) * 1e-9;
  // As the team's bridge: such a frame is not tracked, and the gap it leaves
  // invalidates the windows that contain it.
  if (f.width != m.cfg.width || f.height != m.cfg.height || f.pixels.size() != m.blurred.size() ||
      !f.timestampValid || f.timestampNs <= 0 || (m.haveTime && t <= m.lastTime)) {
    window_ = MultiPointWindow();
    window_.status = -1;
    return false;
  }
  if (!m.haveTime) m.firstTime = t;
  m.haveTime = true;
  m.lastTime = t;
  blurAround(f);
  vr_frame(&m.s, m.blurred.data(), f.sequence, t);
  if (m.s.frames < VR_N || m.s.frames % (m.collect ? 8u : 32u) != 0) return false;
  VRResult r;
  vr_analyze(&m.s, &m.b, m.collect, &r);
  const int n = m.cfg.nroi;
  window_.status = r.status;
  window_.frequencyHz = r.frequency_hz;
  window_.fs = r.fs;
  window_.validSpectra = r.valid_spectra;
  window_.passed = r.passed;
  window_.rmsPx.assign(r.rms_px, r.rms_px + n);
  window_.gateRatio.assign(r.gate_ratio, r.gate_ratio + n);
  window_.valid.assign(r.valid, r.valid + n);
  window_.gate.assign(r.gate, r.gate + n);
  window_.sequence = f.sequence;
  window_.tNs = f.timestampNs;
  return true;
}

bool MultiPointTracker::pointValid(int i) const { return impl_ && impl_->s.last_valid[i]; }
float MultiPointTracker::pointU(int i) const { return impl_ ? impl_->s.last[i][0] : 0.f; }
float MultiPointTracker::pointV(int i) const { return impl_ ? impl_->s.last[i][1] : 0.f; }
bool MultiPointTracker::referenceValid() const { return impl_ && impl_->s.reference_valid; }

std::vector<Roi> MultiPointTracker::regions() const {
  std::vector<Roi> r;
  if (!impl_) return r;
  const Impl& m = *impl_;
  for (int i = 0; i < m.cfg.nroi + m.cfg.nref; ++i) {  // as blurAround, with its one-pixel margin
    const float cx = m.cfg.xy[i][0] + (m.s.initialized ? m.s.patch[i].u : 0.f);
    const float cy = m.cfg.xy[i][1] + (m.s.initialized ? m.s.patch[i].v : 0.f);
    const int x0 = static_cast<int>(std::floor(cx)) - kReach - 1, y0 = static_cast<int>(std::floor(cy)) - kReach - 1;
    const int x1 = static_cast<int>(std::ceil(cx)) + kReach + 1, y1 = static_cast<int>(std::ceil(cy)) + kReach + 1;
    r.push_back(Roi{x0, y0, x1 - x0 + 1, y1 - y0 + 1});
  }
  return r;
}
uint32_t MultiPointTracker::frames() const { return impl_ ? impl_->s.frames : 0; }
int MultiPointTracker::baselineWindows() const { return impl_ ? static_cast<int>(impl_->b.windows) : 0; }

bool MultiPointTracker::saveBaseline(const std::string& newPath) const {
  // At least 5 s and 5 complete windows (vr_baseline_save checks the windows).
  return impl_ && impl_->collect && impl_->haveTime && impl_->lastTime - impl_->firstTime >= 5 &&
         vr_baseline_save(newPath.c_str(), &impl_->b);
}

}  // namespace vm
