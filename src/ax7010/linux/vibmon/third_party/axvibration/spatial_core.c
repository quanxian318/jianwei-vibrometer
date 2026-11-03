#include "portable_vibration.h"
#include <math.h>
static int solve(float a[4][5],int n,float *x){
 int i,j,k;for(i=0;i<n;i++){
  int p=i;for(j=i+1;j<n;j++)if(fabsf(a[j][i])>fabsf(a[p][i]))p=j;
  if(fabsf(a[p][i])<1e-7f)return 0;
  if(p!=i)for(k=i;k<=n;k++){float tmp=a[i][k];a[i][k]=a[p][k];a[p][k]=tmp;}
  {float div=a[i][i];for(k=i;k<=n;k++)a[i][k]/=div;}
  for(j=0;j<n;j++)if(j!=i){float v=a[j][i];for(k=i;k<=n;k++)a[j][k]-=v*a[i][k];}
 }
 for(i=0;i<n;i++){x[i]=a[i][n];}
 return 1;
}
static float med(float *x,int n){int i,j;for(i=1;i<n;i++){float v=x[i];j=i-1;while(j>=0&&x[j]>v){x[j+1]=x[j];j--;}x[j+1]=v;}return n%2?x[n/2]:(x[n/2-1]+x[n/2])*.5f;}
/* Small fixed-object patch ensemble. Patches on one object are correlated,
 * not independent calibration references. No rotation extrapolation. */
VB_API int vb_reference_translation(const float *d,const unsigned char *valid,int n,float *translation,float *spread){
 float xs[16],ys[16],mx,my;int i,k=0,count=0;
 if(n<3||n>16)return 0;
 for(i=0;i<n;i++)if(valid[i]){xs[k]=d[2*i];ys[k]=d[2*i+1];k++;}
 if(k<3)return 0;
 mx=med(xs,k);my=med(ys,k);translation[0]=translation[1]=0;
 for(i=0;i<n;i++)if(valid[i]){
  float dx=d[2*i]-mx,dy=d[2*i+1]-my;
  if(dx*dx+dy*dy<.0625f){translation[0]+=d[2*i];translation[1]+=d[2*i+1];count++;}
 }
 if(count<3)return 0;
 translation[0]/=count;translation[1]/=count;*spread=0;
 for(i=0;i<n;i++)if(valid[i]){
  float dx=d[2*i]-mx,dy=d[2*i+1]-my;
  if(dx*dx+dy*dy<.0625f){dx=d[2*i]-translation[0];dy=d[2*i+1]-translation[1];*spread+=dx*dx+dy*dy;}
 }
 *spread=sqrtf(*spread/count);return 1;
}
/* First-order image motion approximation from background, not physical 3D pose. */
VB_API int vb_background(const float *xy,const float *d,const unsigned char *valid,int n,float *coef,float *rms){
 float weights[32],res[32],ordered[32];int i,j,k,it,count=0;
 if(n<6||n>32)return 0;
 for(i=0;i<n;i++){weights[i]=valid[i]?1.f:0.f;count+=valid[i]!=0;}
 if(count<6)return 0;
 /* Deterministic bounded consensus initialization; weak edge/flat patches can
  * drift while retaining NCC, and must not define the background motion. */
 {
  unsigned int seed=19;float best[6]={0},besterror=1e30f;int bestcount=0,trial;
  for(trial=0;trial<160;trial++){
   int selected[3],dim,usable=1,hits=0;float candidate[6],error=0;
   for(j=0;j<3;j++){seed=1664525u*seed+1013904223u;selected[j]=(int)((seed>>8)%(unsigned int)n);if(!valid[selected[j]])usable=0;}
   if(!usable||selected[0]==selected[1]||selected[0]==selected[2]||selected[1]==selected[2])continue;
   for(dim=0;dim<2;dim++){
    float mat[4][5]={{0}},answer[4];
    for(j=0;j<3;j++){i=selected[j];mat[j][0]=1;mat[j][1]=(xy[2*i]-640)/640;mat[j][2]=(xy[2*i+1]-360)/360;mat[j][3]=d[2*i+dim];}
    if(!solve(mat,3,answer)){usable=0;break;}
    for(j=0;j<3;j++)candidate[3*dim+j]=answer[j];
   }
   if(!usable)continue;
   for(i=0;i<n;i++)if(valid[i]){
    float xx=(xy[2*i]-640)/640,yy=(xy[2*i+1]-360)/360;
    float dx=d[2*i]-candidate[0]-candidate[1]*xx-candidate[2]*yy;
    float dy=d[2*i+1]-candidate[3]-candidate[4]*xx-candidate[5]*yy;
    float r=dx*dx+dy*dy;if(r<.25f){hits++;error+=r;}
   }
   if(hits>bestcount||(hits==bestcount&&error<besterror)){bestcount=hits;besterror=error;for(j=0;j<6;j++)best[j]=candidate[j];}
  }
  if(bestcount<6||bestcount*2<count)return 0;
  for(i=0;i<n;i++)if(valid[i]){
   float xx=(xy[2*i]-640)/640,yy=(xy[2*i+1]-360)/360;
   float dx=d[2*i]-best[0]-best[1]*xx-best[2]*yy,dy=d[2*i+1]-best[3]-best[4]*xx-best[5]*yy;
   weights[i]=(dx*dx+dy*dy<.25f)?1.f:0.f;
  }
 }
 for(it=0;it<4;it++){
  int dim;for(dim=0;dim<2;dim++){
   float mat[4][5]={{0}},answer[4]={0};
   for(i=0;i<n;i++)if(valid[i]){
    float b[3]={1,(xy[2*i]-640)/640,(xy[2*i+1]-360)/360};
    for(j=0;j<3;j++){for(k=0;k<3;k++)mat[j][k]+=weights[i]*b[j]*b[k];mat[j][3]+=weights[i]*b[j]*d[2*i+dim];}
   }
   if(!solve(mat,3,answer))return 0;
   for(j=0;j<3;j++)coef[dim*3+j]=answer[j];
  }
  k=0;for(i=0;i<n;i++)if(valid[i]){
   float xx=(xy[2*i]-640)/640,yy=(xy[2*i+1]-360)/360;
   float dx=d[2*i]-(coef[0]+coef[1]*xx+coef[2]*yy),dy=d[2*i+1]-(coef[3]+coef[4]*xx+coef[5]*yy);
   res[i]=sqrtf(dx*dx+dy*dy);if(weights[i]>0)ordered[k++]=res[i];
  }
  if(k<6)return 0;
  {float limit=3*med(ordered,k);if(limit<.15f)limit=.15f;if(limit>.5f)limit=.5f;for(i=0;i<n;i++)if(valid[i])weights[i]=res[i]>limit?0.f:1.f;}
 }
 *rms=0;count=0;for(i=0;i<n;i++)if(valid[i]&&weights[i]>0){*rms+=res[i]*res[i];count++;}
 if(count<6)return 0;
 *rms=sqrtf(*rms/count);return 1;
}
/* Fits offset + trend + sin + cos; f must be independently selected from spectrum.
 * caller must first gate contiguous valid samples/timestamps with vb_window_spectrum. */
VB_API int vb_sine_fit(const float *signal,const double *t,int n,float f,float *out){
 float mat[4][5]={{0}},answer[4],error=0;int i,j,k;
 double mid;
 if(n<16||n>256||f<=0)return 0;
 mid=(t[0]+t[n-1])*.5;
 for(i=0;i<n;i++){
  float tau=(float)(t[i]-mid),angle=6.28318530718f*f*tau,b[4]={1,tau,sinf(angle),cosf(angle)};
  for(j=0;j<4;j++){for(k=0;k<4;k++)mat[j][k]+=b[j]*b[k];mat[j][4]+=b[j]*signal[i];}
 }
 if(!solve(mat,4,answer))return 0;
 for(i=0;i<n;i++){
  float tau=(float)(t[i]-mid),angle=6.28318530718f*f*tau;
  float e=signal[i]-answer[0]-answer[1]*tau-answer[2]*sinf(angle)-answer[3]*cosf(angle);error+=e*e;
 }
 for(i=0;i<4;i++){out[i]=answer[i];}
 out[4]=sqrtf(error/n);return 1;
}
