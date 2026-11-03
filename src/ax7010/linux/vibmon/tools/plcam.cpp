// plcam - brings up the AN5642 cameras on the team's PL design (see
// drv/pl_camera.h) and saves one frame per camera as PPM: a bring-up check
// without vibmond.
//
//   plcam [--buffer 0x1E000000] [--stride 0x300000] [--cams 01] [--out /tmp/plcam]
//
// --stride 0 lets all frame stores share one buffer; the cameras then run one
// after the other (e.g. into a borrowed framebuffer before DDR is reserved).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "drv/pl_camera.h"
#include "hw/phys_window.h"
#include "util/clock.h"

using namespace vm;

namespace {

struct CamResult {
  double fps = 0, copyMs = 0;
  uint32_t errors = 0;
  bool saved = false;
};

// Counts store changes for windowMs (clearing start-up errors meanwhile) and
// copies the newest complete frame; the copy is dropped if the writer reached
// that store before it finished.
CamResult grab(PlCameraRig& rig, int cam, const PhysWindow& frames, const std::string& path) {
  CamResult r;
  int last = rig.writingStore(cam), changes = 0;
  const int64_t t0 = monoMs();
  int64_t lastClear = t0;
  while (monoMs() - t0 < 2000) {
    const int s = rig.writingStore(cam);
    if (s != last) {
      ++changes;
      last = s;
    }
    if (monoMs() - lastClear >= 100) {
      rig.clearErrors(cam);
      lastClear = monoMs();
    }
    sleepMs(1);
  }
  r.fps = changes / 2.0;
  std::vector<uint8_t> rgb(PlCameraRig::kFrameBytes);
  for (int attempt = 0; attempt < 10 && !r.saved; ++attempt) {
    rig.clearErrors(cam);
    const int done = (rig.writingStore(cam) + 2) % PlCameraRig::kStores;
    const uint32_t offset = rig.storeAddress(cam, done) - rig.config().bufferBase;
    const int64_t c0 = monoNs();
    memcpy(rgb.data(), frames.bytes() + offset, rgb.size());
    r.copyMs = (monoNs() - c0) * 1e-6;
    r.errors = rig.errors(cam);
    if (rig.config().frameStride != 0 && rig.writingStore(cam) == done) continue;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) break;
    fprintf(f, "P6\n%d %d\n255\n", PlCameraRig::kWidth, PlCameraRig::kHeight);
    fwrite(rgb.data(), 1, rgb.size(), f);
    fclose(f);
    r.saved = true;
  }
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  PlCameraConfig cfg;
  std::string cams = "01", out = "/tmp/plcam";
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--buffer") cfg.bufferBase = static_cast<uint32_t>(strtoul(v.c_str(), nullptr, 0));
    else if (k == "--stride") cfg.frameStride = static_cast<uint32_t>(strtoul(v.c_str(), nullptr, 0));
    else if (k == "--cams") cams = v;
    else if (k == "--out") out = v;
    else {
      fprintf(stderr, "usage: %s [--buffer addr] [--stride bytes] [--cams 01] [--out prefix]\n", argv[0]);
      return 2;
    }
  }
  std::string err;
  std::unique_ptr<PlCameraRig> rig = PlCameraRig::open(cfg, err);
  if (!rig) {
    fprintf(stderr, "plcam: %s\n", err.c_str());
    return 1;
  }
  rig->resetSensors();
  std::vector<int> list;
  for (char c : cams) {
    const int cam = c - '0';
    if (cam != 0 && cam != 1) continue;
    if (!rig->initSensor(cam, err)) {
      fprintf(stderr, "camera %d: %s\n", cam, err.c_str());
      return 1;
    }
    printf("camera %d: OV5640 configured\n", cam);
    list.push_back(cam);
  }
  const uint32_t span = cfg.frameStride ? 2 * PlCameraRig::kStores * cfg.frameStride : PlCameraRig::kFrameBytes;
  PhysWindow frames;
  if (list.empty() || !frames.open(cfg.bufferBase, span)) return 1;
  const bool together = cfg.frameStride != 0;
  if (together) {
    for (int cam : list) {
      if (!rig->startCapture(cam, err)) fprintf(stderr, "camera %d: %s\n", cam, err.c_str());
    }
  }
  int failures = 0;
  for (int cam : list) {
    if (!together && !rig->startCapture(cam, err)) {
      fprintf(stderr, "camera %d: %s\n", cam, err.c_str());
      ++failures;
      continue;
    }
    const std::string path = out + std::to_string(cam) + ".ppm";
    const CamResult r = grab(*rig, cam, frames, path);
    printf("camera %d: %.1f fps, errors 0x%03X, frame copy %.1f ms, %s\n", cam, r.fps, r.errors, r.copyMs,
           r.saved ? path.c_str() : "no frame saved");
    if (!r.saved || r.fps <= 0) ++failures;
    if (!together) rig->stopCapture(cam);
  }
  if (together) {
    for (int cam : list) rig->stopCapture(cam);
  }
  return failures ? 1 : 0;
}
