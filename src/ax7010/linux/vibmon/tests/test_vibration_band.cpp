// Exercise production spectral selection with known displacements, not camera
// rendering: the former cutoff missed a strong 1320 rpm tone at 45 fps.
#include <cmath>
#include <initializer_list>
#include <memory>

#include "test_util.h"
#include "vb_runtime.h"
#include "portable_vibration.h"

VRResult tone(double hz, double phase, bool gap = false, bool mismatch = false, int missing = 0) {
  std::unique_ptr<VRSession> s(new VRSession());
  std::unique_ptr<VRBaseline> b(new VRBaseline());
  s->cfg.nroi = 3;
  s->frames = VR_N;
  b->windows = 5;
  b->fs = mismatch ? 60 : 45;
  for (int k = 0; k < VR_N; ++k) {
    const int frame = k + (k >= 64 ? missing : 0);
    s->time[k] = 1.0 + frame / 45.0;
    s->seq[k] = frame + (gap && k >= 64 ? 1 : 0);
    for (int i = 0; i < s->cfg.nroi; ++i) {
      s->valid[i][k] = 1;
      s->uv[i][k][1] = std::sin(2 * 3.141592653589793 * hz * frame / 45.0 + phase);
    }
  }
  for (int i = 0; i < s->cfg.nroi; ++i)
    for (int k = 0; k < VR_BINS; ++k) b->envelope[i][k] = .005f;
  VRResult r;
  CHECK(vr_analyze(s.get(), b.get(), 0, &r) == 1);
  return r;
}

void gapQuality() {
  float signal[128],amp[65],fs,rms; unsigned char valid[128];
  uint32_t seq[128];double t[128];
  for (int mode=0;mode<4;++mode) {
    int frame=0;
    for(int k=0;k<128;++k,++frame) {
      if(k && k%16==0)++frame; // seven isolated losses: over 5%
      seq[k]=frame;t[k]=frame/45.0;signal[k]=std::sin(frame*.4);valid[k]=1;
    }
    if(mode==1)valid[20]=0;
    if(mode==2)t[20]=t[19];
    if(mode==3)seq[20]=seq[19];
    CHECK(vb_window_spectrum(signal,valid,seq,t,128,&fs,amp,&rms)!=1);
  }
}
int main() {
  gapQuality();
  for (double hz : {7.0, 21.6, 22.0}) {
    for (double phase : {.3, 1.1}) {
      const auto r = tone(hz, phase);
      CHECK(r.status == 1 && r.passed == 3);
      CHECK_NEAR(r.frequency_hz, hz, .1);
      CHECK(r.frequency_hz < 22.5);
      CHECK_NEAR(r.rms_px[0], 1 / std::sqrt(2.0), .08);
    }
  }
  for (int missing : {1, 2}) {
    auto r = tone(22, .7, false, false, missing);
    CHECK(r.status == 1 && r.passed == 3);
    CHECK_NEAR(r.fs, 45, .001);
    CHECK_NEAR(r.frequency_hz, 22, .1);
    CHECK_NEAR(r.rms_px[0], 1 / std::sqrt(2.0), .08);
  }
  CHECK(tone(7, .7, false, false, 3).status == -1);
  CHECK(tone(22.5, .7).status == 0);  // unresolved Nyquist boundary
  CHECK(tone(22, .7, true).status == -1);  // dropped frame still rejected
  CHECK(tone(22, .7, false, true).status == -2);  // baseline rate still checked
  return testSummary("vibration_band");
}
