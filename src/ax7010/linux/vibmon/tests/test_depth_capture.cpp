#include "test_util.h"
#include "measure/frame_store_policy.h"
#include "measure/depth.h"
#include "stereo/stereo_service.h"
#include "stereo/depth_display.h"

int main() {
  using namespace vm;
  CHECK(depthRedrawNeeded(true, false, 0));
  CHECK(depthRedrawNeeded(false, true, 10));
  CHECK(!depthRedrawNeeded(false, false, 125));
  CHECK(!depthRedrawNeeded(false, false, 999));
  CHECK(depthRedrawNeeded(false, false, 1000));
  const std::array<int64_t, 3> t{{100, 120, 140}};
  CHECK(nearestCompletedStore(t, 2, 121, 145, 50) == 1);
  CHECK(nearestCompletedStore(t, 2, 101, 145, 50) == 0);
  CHECK(nearestCompletedStore(t, 2, 101, 155, 50) == 1);
  CHECK(nearestCompletedStore(t, 2, 101, 145, 30) == 1); // reserve snapshot time, skip expiring store // old store expired
  CHECK(nearestCompletedStore(t, 2, 120, 200, 50) == -1);
  CHECK(nearestCompletedStore({{0,0,0}}, 0, 100, 120, 50) == -1);
  DepthDisplayRange r;
  r.update(1000, 2000, 1, 0);
  r.update(1020, 1980, 2, 0);
  CHECK(r.lo == 1000 && r.hi == 2000);
  r.update(1500, 3000, 3, 0);
  const double lo = r.lo, hi = r.hi;
  CHECK(lo == 1000 && hi == 2000); // foreground cannot recolour the background
  r.update(1500, 3000, 3, 0); // same map rendered again: no drift
  CHECK(r.lo == lo && r.hi == hi);
  r.update(400, 800, 4, 1); // calibration reset
  CHECK(r.lo == 400 && r.hi == 800);
  r.reset(); r.update(600, 1800, 4, 1);
  CHECK(r.lo == 600 && r.hi == 1800); // deliberate Auto reselect
  DisparityMap d; d.w = 4; d.h = 1; d.q4.assign(4, 160);
  std::vector<uint8_t> image(4, 40);
  TemporalDisparityFilter timed;
  timed.apply(d, 1000000000LL, &image);
  timed.apply(d, 1500000000LL, &image);
  d.q4.assign(4, 168);
  auto fast = timed.apply(d, 2000000000LL, &image);
  CHECK(fast.q4[0] >= 166); // slow depth fps must not add a multi-second IIR lag
  d.q4.assign(4, -1);
  image[0] = 120; // moved object/exposed surface: do not hold its old depth
  auto held = timed.apply(d, 2500000000LL, &image);
  CHECK(held.q4[0] < 0);
  CHECK(held.q4[1] > 0 && !held.measured(1));
  held = timed.apply(d, 3000000000LL, &image);
  CHECK(held.q4[1] < 0); // last measurement is a second old, not four frames old
  TemporalDisparityFilter gradual;
  d.q4.assign(4,160); image.assign(4,40);
  gradual.apply(d,1000000000LL,&image);
  gradual.apply(d,1100000000LL,&image);
  d.q4.assign(4,-1); image.assign(4,55);
  CHECK(gradual.apply(d,1200000000LL,&image).q4[1] > 0);
  image.assign(4,70);
  CHECK(gradual.apply(d,1300000000LL,&image).q4[1] < 0); // 15 per frame, 30 from real surface
  DepthMap view; view.w=17; view.h=9;
  view.mm.assign(153,0); view.left.assign(153,128);
  for (int y=0;y<9;++y) { view.mm[y*17+2]=1000; view.mm[y*17+14]=1050; }
  const auto original=view.mm;
  completeDepthForDisplay(view);
  const int centre=4*17+8;
  CHECK(view.mm == original && view.filled.empty());
  CHECK(view.displayMm[centre]>=1000 && view.displayMm[centre]<=1050);
  CHECK(view.displayEstimated[centre] && !view.displayEstimated[4*17+2]);
  CHECK(view.at(8.5/17,4.5/9)==0); // display estimate is NOT a measured reading
  double scale=0, z=0, offset=0; std::string error;
  CHECK(!stereoScaleAt(view,0.5,0.5,0.03,0.04,1286,scale,z,1));
  view.dispQ4.assign(153,-1);
  view.focalPx=321.5; view.baselineMm=40;
  CHECK(!distanceOffsetPx(view,1280,0.5,0.5,1000,offset,error));
  CHECK(error=="no_depth_at_point");
  completeDepthForDisplay(view,2);
  CHECK(view.displayMm[centre]==0); // finite support radius
  for (int y=0;y<9;++y) for (int x=8;x<17;++x) view.left[y*17+x]=200;
  for (int y=0;y<9;++y) view.mm[y*17+14]=0;
  completeDepthForDisplay(view);
  CHECK(view.displayMm[centre]==0); // no measured seed on this side of the edge
  view.mm=original;
  view.left.assign(153,128);
  for (int y=0;y<9;++y) view.mm[y*17+14]=3000;
  completeDepthForDisplay(view);
  CHECK(view.displayMm[centre]==0); // depth conflict: no invented foreground/background
  view.mm=original;
  view.filled.assign(153,1);
  completeDepthForDisplay(view);
  CHECK(view.displayMm[centre]==0); // estimates cannot recursively seed more estimates
  view.filled.clear(); view.mm[17+8]=3000;
  completeDepthForDisplay(view);
  CHECK(view.displayMm[centre]==0); // a contradicting third direction vetoes completion
  // Current-frame candidates improve preview without entering measurement or calibration.
  view.mm.assign(153,0); view.filled.clear(); view.left.assign(153,128);
  std::vector<uint16_t> candidates(153,0);
  for (int y=0;y<9;++y) { candidates[y*17+2]=1000; candidates[y*17+14]=1050; }
  completeDepthForDisplay(view,candidates);
  CHECK(view.displayMm[centre] > 0 && view.displayEstimated[centre]);
  CHECK(view.displayEstimated[4*17+2] && view.mm[4*17+2]==0 && view.at(.5,.5)==0);
  CHECK(std::all_of(view.mm.begin(),view.mm.end(),[](uint16_t z){return z==0;}));
  candidates.assign(153,0); completeDepthForDisplay(view,candidates);
  CHECK(view.displayMm[centre]==0); // never hold candidates from an old frame
  CHECK(view.nearbyMeasured(.5,.5)==0); // preview candidates cannot enter a regional reading
  for(int y=2;y<=6;++y)for(int x=5;x<=11;++x)view.mm[y*17+x]=1000;
  view.mm[centre]=0;
  CHECK(view.at(.5,.5)==0 && view.nearbyMeasured(.5,.5)==1000);
  for(int y=2;y<=6;++y)for(int x=5;x<=7;++x)view.mm[y*17+x]=2000;
  CHECK(view.nearbyMeasured(.5,.5)==0); // mixed foreground/background is not averaged
  view.left.clear(); completeDepthForDisplay(view);
  CHECK(view.displayMm.empty());
  return testSummary("depth_capture");
}
