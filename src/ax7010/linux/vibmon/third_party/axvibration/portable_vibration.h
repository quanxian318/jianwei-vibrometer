#ifndef PORTABLE_VIBRATION_H
#define PORTABLE_VIBRATION_H
#ifdef __cplusplus
extern "C" {
#endif
#ifdef _MSC_VER
#define VB_API __declspec(dllexport)
#elif defined(_WIN32)
#define VB_API __attribute__((dllexport))
#else
#define VB_API
#endif
#define VB_PATCH 25
#define VB_PIXELS 625
#include <stdint.h>
typedef struct {
 float x,y,normal_x,normal_y,reference[21],energy,shift,score;
 int valid;
} VBEdge;
typedef struct {
 float x,y,reference[VB_PIXELS],gx[VB_PIXELS],gy[VB_PIXELS];
 float a,b,c,det,variance,strength,condition,u,v,score;
 int valid;
} VBPatch;
VB_API int vb_edge_bytes(void);
VB_API float vb_peak_offset(float,float,float);
VB_API void vb_edge_init(VBEdge *,const float *,int,int,float,float,float,float);
VB_API int vb_edge_step(VBEdge *,const float *,int,int,float *);
VB_API int vb_patch_bytes(void);
VB_API void vb_patch_init(VBPatch *,const float *,int,int,float,float);
VB_API int vb_patch_step(VBPatch *,const float *,int,int,float *);
/* n=128 or256; input must be a fully valid uniformly sampled contiguous window.
 * amplitudes have n/2+1 elements; delta-Hz=fs/n, not accuracy guarantee. */
VB_API int vb_spectrum(const float *,int,float,float *,float *);
/* 1 success; -1 bad length; -2 LOST; -3 sequence gap; -4 bad timebase.
 * seconds must be observation/exposure timestamps, NOT webpage timestamps. */
VB_API int vb_window_spectrum(const float *,const unsigned char *,const uint32_t *,const double *,int,float *,float *,float *);
VB_API void vb_gray_bgr888(const unsigned char *,int,int,int,float *);
VB_API void vb_blur3(const float *,int,int,float *);
/* Extended diagnostics; background affine fit can fail and must then be rejected. */
VB_API int vb_background(const float *,const float *,const unsigned char *,int,float *,float *);
VB_API int vb_sine_fit(const float *,const double *,int,float,float *);
VB_API int vb_reference_translation(const float *,const unsigned char *,int,float *,float *);
#ifdef __cplusplus
}
#endif
#endif
