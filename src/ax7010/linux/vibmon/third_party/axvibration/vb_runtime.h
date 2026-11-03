#ifndef VB_RUNTIME_H
#define VB_RUNTIME_H
#include "portable_vibration.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VR_MAX_ROI 60
#define VR_MAX_REF 16
#define VR_N 128
#define VR_BINS 65
typedef struct {
 int width,height,nroi,nref;
 float xy[VR_MAX_ROI+VR_MAX_REF][2];
} VRConfig;
typedef struct {
 uint32_t magic,version,config_hash,nroi,windows;
 float fs;
 float envelope[VR_MAX_ROI][VR_BINS];
} VRBaseline;
typedef struct {
 VRConfig cfg;
 VBPatch patch[VR_MAX_ROI+VR_MAX_REF];
 float uv[VR_MAX_ROI][VR_N][2];
 unsigned char valid[VR_MAX_ROI][VR_N];
 uint32_t seq[VR_N];
 double time[VR_N];
 unsigned int frames;
 int initialized,reference_valid,tracking_valid;
 float reference[2],reference_spread,last[VR_MAX_ROI][2];
 unsigned char last_valid[VR_MAX_ROI];
} VRSession;
typedef struct {
 int status,valid_spectra,passed;
 /* status: 1 signal, 0 below baseline, -1 window/time rejected,
  * -2 baseline timebase mismatch, 2 baseline collection. */
 float frequency_hz,fs,gate_ratio[VR_MAX_ROI],rms_px[VR_MAX_ROI];
 unsigned char valid[VR_MAX_ROI],gate[VR_MAX_ROI];
} VRResult;
int vr_config_load(const char *,VRConfig *);
uint32_t vr_config_hash(const VRConfig *);
int vr_init(VRSession *,const VRConfig *);
void vr_frame(VRSession *,const float *,uint32_t,double);
int vr_analyze(VRSession *,VRBaseline *,int,VRResult *);
int vr_baseline_load(const char *,const VRConfig *,VRBaseline *);
int vr_baseline_save(const char *,const VRBaseline *);
#ifdef __cplusplus
}
#endif
#endif
