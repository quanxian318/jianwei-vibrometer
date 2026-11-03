#include "portable_vibration.h"
#include <math.h>
#define PI 3.14159265358979323846f
static float clip(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static float sample(const float *im,int w,int h,float x,float y){
 int ix,iy;float fx,fy;
 x=clip(x,0,w-1.0001f);y=clip(y,0,h-1.0001f);ix=(int)x;iy=(int)y;fx=x-ix;fy=y-iy;
 return (im[iy*w+ix]*(1-fx)+im[iy*w+ix+1]*fx)*(1-fy)+(im[(iy+1)*w+ix]*(1-fx)+im[(iy+1)*w+ix+1]*fx)*fy;
}
static void profile(VBEdge *s,const float *im,int w,int h,float *g){
 float p[129];int r,k;
 for(r=-64;r<=64;r++){
  float sum=0;for(k=-12;k<=12;k+=2)sum+=sample(im,w,h,s->x+r*s->normal_x+k*s->normal_y,s->y+r*s->normal_y-k*s->normal_x);
  p[r+64]=sum/13;
 }
 for(r=0;r<125;r++)g[r]=(p[r+4]-p[r])*.25f;
}
int vb_edge_bytes(void){return sizeof(VBEdge);}
float vb_peak_offset(float left,float middle,float right){
 float den=left+right-2*middle;
 if(den>=-1e-8f)return 0;
 return clip(.5f*(left-right)/den,-.75f,.75f);
}
void vb_edge_init(VBEdge *s,const float *im,int w,int h,float x,float y,float nx,float ny){
 float g[125],mean=0;int i;s->x=x;s->y=y;s->normal_x=nx;s->normal_y=ny;s->shift=s->score=0;s->valid=0;
 profile(s,im,w,h,g);for(i=0;i<21;i++)mean+=g[i+52];mean/=21;s->energy=0;
 for(i=0;i<21;i++){s->reference[i]=g[i+52]-mean;s->energy+=s->reference[i]*s->reference[i];}
}
int vb_edge_step(VBEdge *s,const float *im,int w,int h,float *out){
 float g[125],corr[17],mean,energy,dot,best=-2,sub=0;int center=(int)(s->shift>=0?s->shift+.5f:s->shift-.5f);
 int lo=center-8,hi=center+8,i,k,at=0,n;
 if(lo< -45)lo=-45;
 if(hi>45)hi=45;
 n=hi-lo+1;profile(s,im,w,h,g);
 for(k=lo;k<=hi;k++){
  mean=0;for(i=0;i<21;i++)mean+=g[52+k+i];mean/=21;dot=energy=0;
  for(i=0;i<21;i++){float v=g[52+k+i]-mean;dot+=v*s->reference[i];energy+=v*v;}
  corr[k-lo]=dot/sqrtf(energy*s->energy+1e-10f);
  if(corr[k-lo]>best){best=corr[k-lo];at=k-lo;}
 }
 if(at>0&&at<n-1)sub=vb_peak_offset(corr[at-1],corr[at],corr[at+1]);
 *out=lo+at+sub;s->score=best;s->valid=best>.9f&&s->energy>20&&at>0&&at<n-1;
 if(s->valid)s->shift=*out;
 return s->valid;
}
int vb_patch_bytes(void){return sizeof(VBPatch);}
void vb_patch_init(VBPatch *s,const float *im,int w,int h,float x,float y){
 int xx,yy,i=0;float mean=0,mx=0,my=0;s->x=x;s->y=y;s->u=s->v=s->score=0;s->valid=0;
 s->a=s->b=s->c=s->variance=0;
 for(yy=-12;yy<=12;yy++)for(xx=-12;xx<=12;xx++,i++){
  float px=x+xx,py=y+yy;s->reference[i]=sample(im,w,h,px,py);
  s->gx[i]=(sample(im,w,h,px+1,py)-sample(im,w,h,px-1,py))*.5f;
  s->gy[i]=(sample(im,w,h,px,py+1)-sample(im,w,h,px,py-1))*.5f;
  mean+=s->reference[i];mx+=s->gx[i];my+=s->gy[i];
 }
 mean/=VB_PIXELS;mx/=VB_PIXELS;my/=VB_PIXELS;
 for(i=0;i<VB_PIXELS;i++){
  s->reference[i]-=mean;s->gx[i]-=mx;s->gy[i]-=my;
  s->a+=s->gx[i]*s->gx[i];s->b+=s->gx[i]*s->gy[i];s->c+=s->gy[i]*s->gy[i];s->variance+=s->reference[i]*s->reference[i];
 }
 s->variance/=VB_PIXELS;s->det=s->a*s->c-s->b*s->b;
 s->strength=s->det/(s->a+s->c+1e-9f);s->condition=s->det/((s->a+s->c)*(s->a+s->c)+1e-9f);
}
/* All 625 samples in a translated patch share 25 x and 25 y coordinates.
 * Cache clipping, indices and fractions once per row/column. Keep the scalar
 * interpolation and reduction order identical to sample(): no new filter,
 * changed LK convergence threshold or relaxed tracking quality checks. */
static float sample_patch(const VBPatch *s,const float *im,int w,int h,float u,float v,float *cur){
 int xi[VB_PATCH],yi[VB_PATCH],x,y,i=0;float fx[VB_PATCH],fy[VB_PATCH],mean=0;
 for(x=0;x<VB_PATCH;x++){
  float px=clip(s->x+(x-12)+u,0,w-1.0001f);xi[x]=(int)px;fx[x]=px-xi[x];
 }
 for(y=0;y<VB_PATCH;y++){
  float py=clip(s->y+(y-12)+v,0,h-1.0001f);yi[y]=(int)py;fy[y]=py-yi[y];
 }
 for(y=0;y<VB_PATCH;y++){
  const float *top=im+yi[y]*w,*bottom=top+w;
  for(x=0;x<VB_PATCH;x++,i++){
   const int ix=xi[x];const float ax=fx[x],ay=fy[y];
   cur[i]=(top[ix]*(1-ax)+top[ix+1]*ax)*(1-ay)+(bottom[ix]*(1-ax)+bottom[ix+1]*ax)*ay;
   mean+=cur[i];
  }
 }
 return mean/VB_PIXELS;
}
int vb_patch_step(VBPatch *s,const float *im,int w,int h,float *out){
 float u=s->u,v=s->v,cur[VB_PIXELS],nref=0;int i,it,bad=0;
 for(i=0;i<VB_PIXELS;i++)nref+=s->reference[i]*s->reference[i];
 for(it=0;it<8;it++){
  float mean=sample_patch(s,im,w,h,u,v,cur),dot=0,gain,bx=0,by=0,du,dv;
  for(i=0;i<VB_PIXELS;i++){cur[i]-=mean;dot+=cur[i]*s->reference[i];}
  gain=dot/(nref+1e-9f);if(gain<.25f||gain>4)bad=1;gain=clip(gain,.25f,4);
  for(i=0;i<VB_PIXELS;i++){float residual=cur[i]/gain-s->reference[i];bx+=residual*s->gx[i];by+=residual*s->gy[i];}
  du=(s->c*bx-s->b*by)/(s->det+1e-9f);dv=(s->a*by-s->b*bx)/(s->det+1e-9f);
  u-=clip(du,-3,3);v-=clip(dv,-3,3);if(fabsf(du)<.005f&&fabsf(dv)<.005f)break;
 }
 {float mean=sample_patch(s,im,w,h,u,v,cur),dot=0,energy=0;
  for(i=0;i<VB_PIXELS;i++){cur[i]-=mean;dot+=cur[i]*s->reference[i];energy+=cur[i]*cur[i];}
  s->score=dot/sqrtf(nref*energy+1e-9f);
 }
 s->valid=s->score>.85f&&s->condition>.005f&&s->variance>1&&s->strength>5&&!bad&&fabsf(u)<100&&fabsf(v)<100;
 out[0]=u;out[1]=v;if(s->valid){s->u=u;s->v=v;}return s->valid;
}
int vb_spectrum(const float *signal,int n,float fs,float *amp,float *rms){
 float re[256],im[256],mean=0,slope=0,den=0,wsum=0,energy=0;int i,j,k,len;
 if((n!=128&&n!=256)||fs<=0)return 0;
 for(i=0;i<n;i++)mean+=signal[i];
 mean/=n;
 for(i=0;i<n;i++){float x=i-(n-1)*.5f;slope+=x*(signal[i]-mean);den+=x*x;}slope/=den;
 for(i=0;i<n;i++){float v=signal[i]-mean-slope*(i-(n-1)*.5f),w=.5f-.5f*cosf(2*PI*i/(n-1));re[i]=v*w;im[i]=0;energy+=v*v;wsum+=w;}
 for(i=1,j=0;i<n;i++){int bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){float tmp=re[i];re[i]=re[j];re[j]=tmp;}}
 for(len=2;len<=n;len<<=1)for(i=0;i<n;i+=len)for(j=0;j<len/2;j++){
  int a=i+j,b=a+len/2;float c=cosf(-2*PI*j/len),s=sinf(-2*PI*j/len),tr=c*re[b]-s*im[b],ti=c*im[b]+s*re[b];
  re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;
 }
 for(k=0;k<=n/2;k++){amp[k]=2*sqrtf(re[k]*re[k]+im[k]*im[k])/wsum;if(!k||k==n/2)amp[k]*=.5f;}
 *rms=sqrtf(energy/n);return 1;
}
void vb_gray_bgr888(const unsigned char *src,int w,int h,int stride,float *out){
 int x,y;for(y=0;y<h;y++)for(x=0;x<w;x++){const unsigned char *p=src+y*stride+x*3;out[y*w+x]=(29*p[0]+150*p[1]+77*p[2])*(1.0f/256);}
}
int vb_window_spectrum(const float *signal,const unsigned char *valid,const uint32_t *sequence,const double *seconds,int n,float *fs,float *amp,float *rms){
 int i,j=0;uint32_t span,missing=0;double average;float uniform[256];
 if(n!=128&&n!=256)return -1;
 span=sequence[n-1]-sequence[0];
 if(span<(uint32_t)(n-1)||span>100000)return -3;
 average=(seconds[n-1]-seconds[0])/span;
 if(!isfinite(average)||average<=0)return -4;
 for(i=0;i<n;i++){
  if(!valid[i]||!isfinite(signal[i]))return -2;
  if(!isfinite(seconds[i]))return -4;
  if(i){
   uint32_t step=sequence[i]-sequence[i-1];double dt=seconds[i]-seconds[i-1];
   if(step<1||step>3)return -3;
   missing+=step-1;
   if(dt<=0||fabs(dt/step-average)>.1*average)return -4;
  }
 }
 if(missing>.05*(n+missing))return -3;
 /* Keep the camera sampling rate, not the slower rate of received samples.
  * Resample only the last n frame periods, never extrapolate across an edge. */
 for(i=0;i<n;i++){
  double target=seconds[n-1]-(n-1-i)*average,alpha;
  while(j<n-2&&seconds[j+1]<target)j++;
  alpha=(target-seconds[j])/(seconds[j+1]-seconds[j]);
  if(alpha<0)alpha=0;if(alpha>1)alpha=1;
  uniform[i]=(float)(signal[j]+alpha*(signal[j+1]-signal[j]));
 }
 *fs=(float)(1/average);return vb_spectrum(uniform,n,*fs,amp,rms);
}
void vb_blur3(const float *src,int w,int h,float *out){
 const float weights[3]={.20945361f,.58109278f,.20945361f};int x,y,dx,dy;
 for(y=0;y<h;y++)for(x=0;x<w;x++){
  float v=0;for(dy=-1;dy<=1;dy++)for(dx=-1;dx<=1;dx++){int xx=x+dx,yy=y+dy;if(xx<0)xx=-xx;if(yy<0)yy=-yy;if(xx>=w)xx=2*w-xx-2;if(yy>=h)yy=2*h-yy-2;v+=weights[dx+1]*weights[dy+1]*src[yy*w+xx];}out[y*w+x]=v;
 }
}
