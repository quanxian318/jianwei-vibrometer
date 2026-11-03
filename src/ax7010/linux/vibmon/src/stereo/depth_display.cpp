#include "stereo/depth_display.h"
#include <array>
#include <algorithm>
#include <cstdlib>

namespace vm {
void completeDepthForDisplay(DepthMap& m, const std::vector<uint16_t>& candidates, int radius) {
  if (candidates.size() != m.mm.size()) { completeDepthForDisplay(m, radius); return; }
  DepthMap preview;
  preview.w = m.w; preview.h = m.h;
  preview.left = m.left; preview.mm = m.mm; preview.filled = m.filled;
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (!candidates[i] || (m.mm[i] && (m.filled.empty() || !m.filled[i]))) continue;
    preview.mm[i] = candidates[i];
    if (!preview.filled.empty()) preview.filled[i] = 0;
  }
  completeDepthForDisplay(preview, radius);
  m.displayMm = std::move(preview.displayMm);
  m.displayEstimated = std::move(preview.displayEstimated);
  for (size_t i = 0; i < m.displayMm.size(); ++i)
    if (m.displayMm[i] && (!m.mm[i] || (!m.filled.empty() && m.filled[i]))) m.displayEstimated[i] = 1;
}

void completeDepthForDisplay(DepthMap& m, int radius) {
  m.displayMm.clear();
  m.displayEstimated.clear();
  const size_t n = m.mm.size();
  if (m.w <= 0 || m.h <= 0 || n != static_cast<size_t>(m.w)*m.h || m.left.size() != n ||
      (!m.filled.empty() && m.filled.size() != n)) return;
  m.displayMm = m.mm;
  m.displayEstimated = m.filled.empty() ? std::vector<uint8_t>(n,0) : m.filled;
  radius = std::max(0, std::min(radius, 64));
  std::array<std::vector<int>,4> origin;
  for (auto& a : origin) a.assign(n,-1);
  auto measured = [&](size_t i) { return m.mm[i] && (m.filled.empty() || !m.filled[i]); };
  // Four linear sweeps, O(width*height), not an unbounded flood-fill. A seed
  // cannot cross an intensity edge or drift onto another intensity surface.
  for (int dir=0; dir<4; ++dir) {
    const bool horizontal = dir < 2, reverse = (dir & 1) != 0;
    const int lines = horizontal ? m.h : m.w, length = horizontal ? m.w : m.h;
    for (int line=0; line<lines; ++line) {
      int seed=-1, distance=0, previous=-1;
      for (int p=0; p<length; ++p) {
        const int q = reverse ? length-1-p : p;
        const int i = horizontal ? line*m.w+q : q*m.w+line;
        if (measured(i)) { seed=i; distance=0; }
        else if (seed >= 0) {
          ++distance;
          if (distance > radius || previous < 0 ||
              std::abs(int(m.left[i])-int(m.left[previous])) > 20 ||
              std::abs(int(m.left[i])-int(m.left[seed])) > 18) seed=-1;
        }
        origin[dir][i]=seed;
        previous=i;
      }
    }
  }
  for (size_t i=0; i<n; ++i) {
    if (m.mm[i]) continue; // preserve all original measured and inferred values
    int64_t estimate=0; int pairCount=0;
    for (int axis=0; axis<2; ++axis) {
      const int a=origin[2*axis][i], b=origin[2*axis+1][i];
      if (a<0 || b<0 || a==b) continue;
      const int za=m.mm[a], zb=m.mm[b];
      if (std::abs(za-zb) > std::max(80,std::min(za,zb)/10)) continue;
      const int da = axis==0 ? std::abs(int(i)-a) : std::abs(int(i)-a)/m.w;
      const int db = axis==0 ? std::abs(int(i)-b) : std::abs(int(i)-b)/m.w;
      // Interpolate disparity (inverse Z), not a fictitious measured depth.
      const int64_t z = int64_t(za)*zb*(da+db)/(int64_t(zb)*db+int64_t(za)*da);
      estimate += z; ++pairCount;
    }
    if (!pairCount) continue;
    const int z = static_cast<int>(estimate/pairCount);
    // Any other visible seed disagreeing with the proposed surface vetoes
    // completion; do not smear foreground depth across an occlusion boundary.
    bool consistent=true;
    for (int dir=0; dir<4; ++dir) {
      const int seed=origin[dir][i];
      if (seed>=0 && std::abs(int(m.mm[seed])-z) > std::max(80,z/10)) consistent=false;
    }
    if (!consistent) continue;
    m.displayMm[i]=static_cast<uint16_t>(z);
    m.displayEstimated[i]=1;
  }
  // A curved/irregular uniform surface need not have seeds on opposite rays.
  // Grow bounded intensity regions and permit a local plane hypothesis ONLY
  // if >=6 measured seeds form a >=75% dominant coherent cluster. Conflicting
  // seeds protect their nearest neighbourhood. Distance to a measured seed
  // is still bounded; estimates never become new seeds.
  std::vector<int> region(n,-1), component, queue;
  std::vector<uint16_t> depths;
  std::vector<uint8_t> distance(n,255);
  std::vector<uint8_t> supported(n,0);
  int id=0;
  for (size_t start=0; start<n; ++start) {
    if (region[start]>=0) continue;
    component.clear(); component.push_back(static_cast<int>(start)); region[start]=id;
    const int anchor=m.left[start];
    for (size_t head=0; head<component.size(); ++head) {
      const int i=component[head], x=i%m.w, y=i/m.w;
      const int next[4]={x>0?i-1:-1,x+1<m.w?i+1:-1,y>0?i-m.w:-1,y+1<m.h?i+m.w:-1};
      for (int j: next) {
        if (j<0 || region[j]>=0 || std::abs(int(m.left[j])-anchor)>18 ||
            std::abs(int(m.left[j])-int(m.left[i]))>12) continue;
        region[j]=id; component.push_back(j);
      }
    }
    if (component.size()>=12 && component.size()<=4096) {
      depths.clear(); queue.clear();
      for (int i: component) if (measured(i)) { depths.push_back(m.mm[i]); queue.push_back(i); distance[i]=0; }
      if (depths.size()>=6) {
        std::sort(depths.begin(),depths.end());
        size_t best=0,bestEnd=0,end=0;
        for(size_t first=0;first<depths.size();++first) {
          while(end<depths.size() && depths[end]-depths[first]<=std::max(120,int(depths[first])*15/100)) ++end;
          if(end-first>bestEnd-best) { best=first; bestEnd=end; }
        }
        const int z=depths[(best+bestEnd)/2];
        if (bestEnd-best>=6 && (bestEnd-best)*4>=depths.size()*3) {
          // Robust dominant surface, not an average across two depth layers.
          // Conflict seeds get first priority in the nearest-seed partition:
          // holes closer to a contradictory measured surface stay unknown.
          queue.clear();
          for(int i:component) if(measured(i)) {
            supported[i]=std::abs(int(m.mm[i])-z)<=std::max(120,z*15/100);
            if(!supported[i]) queue.push_back(i);
          }
          for(int i:component) if(measured(i) && supported[i]) queue.push_back(i);
          for (size_t head=0; head<queue.size(); ++head) {
            const int i=queue[head], x=i%m.w, y=i/m.w;
            if (supported[i] && !m.mm[i] && !m.displayMm[i]) { m.displayMm[i]=static_cast<uint16_t>(z); m.displayEstimated[i]=1; }
            if (distance[i]>=radius) continue;
            const int next[4]={x>0?i-1:-1,x+1<m.w?i+1:-1,y>0?i-m.w:-1,y+1<m.h?i+m.w:-1};
            for (int j: next) if (j>=0 && region[j]==id && distance[j]==255) {
              distance[j]=static_cast<uint8_t>(distance[i]+1); supported[j]=supported[i]; queue.push_back(j);
            }
          }
        }
      }
    }
    ++id;
  }
}
}
