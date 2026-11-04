#include "app/report.h"
#include <cstdio>
#include <cmath>
int main(int argc,char**argv) {
  vm::ReportData d;
  d.deviceId="jianwei-ax7010";d.measurementId="jianwei-ax7010-20261101-164400";
  d.timeText="2026-11-01 16:43:50 (UTC+8)";
  d.summary.valid=true;d.summary.method="multipoint";d.summary.waveformValid=true;
  d.summary.unit="mm";d.summary.dominantHz=10.51;d.summary.points=8;d.summary.passedPoints=8;
  d.summary.pointRmsPx=.2157;d.fps=45;d.calibrationText="trial-ruler-y-20261031";
  for(int i=0;i<400;++i) { double t=i/400.;d.t.push_back(t);d.disp.push_back(.0703*std::sin(2*3.14159265*10.51*t));d.f.push_back(i*.05);d.amp.push_back(.0698*std::exp(-std::pow((i*.05-10.51)/.3,2))); }
  vm::Canvas c=vm::renderReport(d,384);int wb=0;auto bits=vm::toMonoBits(c,wb);
  FILE*f=fopen(argc>1?argv[1]:"/tmp/report.ppm","wb");fprintf(f,"P6\n%d %d\n255\n",c.width(),c.height());
  for(int y=0;y<c.height();++y)for(int x=0;x<c.width();++x){unsigned char v=(bits[y*wb+x/8]&(128>>(x%8)))?0:255;unsigned char rgb[3]={v,v,v};fwrite(rgb,1,3,f);}fclose(f);
  printf("report %dx%d\n",c.width(),c.height());
}
