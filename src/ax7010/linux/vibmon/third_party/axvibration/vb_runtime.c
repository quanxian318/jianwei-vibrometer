#include "vb_runtime.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAGIC 0x56424231u
static float median(float *a,int n){int i,j;for(i=1;i<n;i++){float x=a[i];j=i-1;while(j>=0&&a[j]>x){a[j+1]=a[j];j--;}a[j+1]=x;}return n%2?a[n/2]:(a[n/2-1]+a[n/2])*.5f;}
int vr_config_load(const char *path,VRConfig *c){
 FILE *f=fopen(path,"r");int i;if(!f)return 0;memset(c,0,sizeof(*c));
 if(fscanf(f,"%d%d%d%d",&c->width,&c->height,&c->nroi,&c->nref)!=4)goto bad;
 if(c->width<32||c->height<32||c->width>1920||c->height>1080||c->nroi<1||c->nroi>VR_MAX_ROI||c->nref<3||c->nref>VR_MAX_REF)goto bad;
 for(i=0;i<c->nroi+c->nref;i++){
  if(fscanf(f,"%f%f",&c->xy[i][0],&c->xy[i][1])!=2)goto bad;
  if(!isfinite(c->xy[i][0])||!isfinite(c->xy[i][1])||c->xy[i][0]<15||c->xy[i][1]<15||c->xy[i][0]>c->width-16||c->xy[i][1]>c->height-16)goto bad;
 }
 fclose(f);return 1;
 bad:fclose(f);return 0;
}
uint32_t vr_config_hash(const VRConfig *c){
 uint32_t h=2166136261u;const unsigned char *p=(const unsigned char *)c;size_t i;
 for(i=0;i<sizeof(*c);i++){h^=p[i];h*=16777619u;}return h;
}
int vr_init(VRSession *s,const VRConfig *c){memset(s,0,sizeof(*s));s->cfg=*c;return 1;}
void vr_frame(VRSession *s,const float *image,uint32_t sequence,double seconds){
 float ref[2*VR_MAX_REF],spread=0,translation[2]={0};unsigned char good[VR_MAX_REF];
 int i,k=(int)(s->frames%VR_N),n=s->cfg.nroi;
 if(!s->initialized){for(i=0;i<n+s->cfg.nref;i++)vb_patch_init(&s->patch[i],image,s->cfg.width,s->cfg.height,s->cfg.xy[i][0],s->cfg.xy[i][1]);s->initialized=1;}
 for(i=0;i<s->cfg.nref;i++)good[i]=(unsigned char)vb_patch_step(&s->patch[n+i],image,s->cfg.width,s->cfg.height,&ref[2*i]);
 s->reference_valid=vb_reference_translation(ref,good,s->cfg.nref,translation,&spread);
 s->reference_spread=spread;s->reference[0]=translation[0];s->reference[1]=translation[1];s->tracking_valid=0;
 for(i=0;i<n;i++){
  float out[2];int ok=vb_patch_step(&s->patch[i],image,s->cfg.width,s->cfg.height,out);
  int safe=ok&&s->reference_valid&&s->cfg.xy[i][0]+out[0]>=14&&s->cfg.xy[i][1]+out[1]>=14&&s->cfg.xy[i][0]+out[0]<=s->cfg.width-15&&s->cfg.xy[i][1]+out[1]<=s->cfg.height-15;
  s->valid[i][k]=s->last_valid[i]=(unsigned char)safe;s->tracking_valid+=safe;
  s->uv[i][k][0]=s->last[i][0]=out[0]-translation[0];s->uv[i][k][1]=s->last[i][1]=out[1]-translation[1];
 }
 s->seq[k]=sequence;s->time[k]=seconds;s->frames++;
}
int vr_analyze(VRSession *s,VRBaseline *base,int collect,VRResult *r){
 float amplitudes[VR_MAX_ROI][VR_BINS],aggregate[VR_BINS],tmp[VR_MAX_ROI];
 float signal[VR_N],ax[VR_BINS],ay[VR_BINS],rms,fs=0,fx[5],fy[5];
 double t[VR_N];uint32_t seq[VR_N];unsigned char valid[VR_N];
 int i,j,k,start,best=-1;float score=-1;
 memset(r,0,sizeof(*r));r->status=-1;
 if(s->frames<VR_N)return 0;
 start=(int)(s->frames%VR_N);
 for(j=0;j<VR_N;j++){k=(start+j)%VR_N;t[j]=s->time[k];seq[j]=s->seq[k];if(!isfinite(t[j]))return 1;}
 if(collect)for(j=1;j<VR_N;j++)if(seq[j]!=seq[j-1]+1)return 1;
 for(i=0;i<s->cfg.nroi;i++){
  for(j=0;j<VR_N;j++){k=(start+j)%VR_N;signal[j]=s->uv[i][k][0];valid[j]=s->valid[i][k];}
  if(vb_window_spectrum(signal,valid,seq,t,VR_N,&fs,ax,&rms)!=1)continue;
  for(j=0;j<VR_N;j++)signal[j]=s->uv[i][(start+j)%VR_N][1];
  if(vb_window_spectrum(signal,valid,seq,t,VR_N,&fs,ay,&rms)!=1)continue;
  r->valid[i]=1;r->valid_spectra++;
  for(j=0;j<VR_BINS;j++)amplitudes[i][j]=sqrtf(ax[j]*ax[j]+ay[j]*ay[j]);
 }
 if(!r->valid_spectra)return 1;
 r->fs=fs;
 if(base->windows&&fabsf(fs/base->fs-1)>.01f){r->status=-2;return 1;}
 if(collect){
  /* A baseline window must cover every configured ROI; no missing noise
   * values are silently treated as zero. */
  if(r->valid_spectra!=s->cfg.nroi)return 1;
  if(!base->windows){base->magic=MAGIC;base->version=1;base->config_hash=vr_config_hash(&s->cfg);base->nroi=(uint32_t)s->cfg.nroi;base->fs=fs;}
  for(i=0;i<s->cfg.nroi;i++)for(j=0;j<VR_BINS;j++)if(amplitudes[i][j]>base->envelope[i][j])base->envelope[i][j]=amplitudes[i][j];
  base->windows++;r->status=2;return 1;
 }
 if(base->windows<5){r->status=-2;return 1;}
 for(j=0;j<VR_BINS;j++){
  int n=0;float bkg[VR_MAX_ROI];
  for(i=0;i<s->cfg.nroi;i++)if(r->valid[i]){tmp[n]=amplitudes[i][j];bkg[n]=base->envelope[i][j];n++;}
  aggregate[j]=median(tmp,n);
  /* Search up to 30 Hz, within the available sub-Nyquist bins. The former 0.45*fs cutoff
   * discarded the 21.6--22 Hz motor signal at the measured 45 fps.
   * Keep the Nyquist bin as an interpolation neighbour, never a candidate. */
  if(j>0&&j<VR_BINS-1&&j*fs/VR_N>=.5f&&j*fs/VR_N<=30.0f){float b=median(bkg,n),v=aggregate[j]*aggregate[j]-b*b;if(v>score){score=v;best=j;}}
 }
 if(best<1||score<=0){r->status=0;return 1;}
 /* A spectrum still rising into Nyquist has no resolved upper-side peak.
  * vb_spectrum halves that endpoint in its single-sided amplitude output;
  * undo this scaling for the comparison with an ordinary interior bin. */
 if(best==VR_BINS-2&&2*aggregate[best+1]>=aggregate[best]){r->status=0;return 1;}
 r->frequency_hz=(best+vb_peak_offset(aggregate[best-1],aggregate[best],aggregate[best+1]))*fs/VR_N;
 for(i=0;i<s->cfg.nroi;i++)if(r->valid[i]){
  float floor=base->envelope[i][best];
  for(j=best-1;j<=best+1;j++)if(base->envelope[i][j]>floor)floor=base->envelope[i][j];
  r->gate_ratio[i]=amplitudes[i][best]/fmaxf(floor,1e-6f);
  for(j=0;j<VR_N;j++)signal[j]=s->uv[i][(start+j)%VR_N][0];
  if(!vb_sine_fit(signal,t,VR_N,r->frequency_hz,fx)){r->valid[i]=0;continue;}
  for(j=0;j<VR_N;j++)signal[j]=s->uv[i][(start+j)%VR_N][1];
  if(!vb_sine_fit(signal,t,VR_N,r->frequency_hz,fy)){r->valid[i]=0;continue;}
  r->rms_px[i]=sqrtf((fx[2]*fx[2]+fx[3]*fx[3]+fy[2]*fy[2]+fy[3]*fy[3])*.5f);
  r->gate[i]=(unsigned char)(r->gate_ratio[i]>=3);r->passed+=r->gate[i];
 }
 r->status=r->passed?1:0;return 1;
}
int vr_baseline_load(const char *path,const VRConfig *c,VRBaseline *b){
 FILE *f=fopen(path,"rb");int ok,i,j;if(!f)return 0;
 ok=fread(b,sizeof(*b),1,f)==1&&fgetc(f)==EOF;fclose(f);
 if(!ok||b->magic!=MAGIC||b->version!=1||b->config_hash!=vr_config_hash(c)||b->nroi!=(uint32_t)c->nroi||b->windows<5||!isfinite(b->fs)||b->fs<2)return 0;
 for(i=0;i<c->nroi;i++)for(j=0;j<VR_BINS;j++)if(!isfinite(b->envelope[i][j])||b->envelope[i][j]<0)return 0;
 return 1;
}
int vr_baseline_save(const char *path,const VRBaseline *b){
 FILE *f;int ok;if(b->windows<5)return 0;
 f=fopen(path,"wbx");if(!f)return 0;ok=fwrite(b,sizeof(*b),1,f)==1;if(fclose(f))ok=0;return ok;
}
