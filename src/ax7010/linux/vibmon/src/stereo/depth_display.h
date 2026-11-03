#pragma once
#include "measure/depth.h"

namespace vm {
// Independent, current-frame visualisation only. Does not change mm, dispQ4,
// filled, validFraction or any input used by metrology/calibration.
void completeDepthForDisplay(DepthMap& map, int radius = 32);
// Current-pair matches awaiting temporal confirmation may seed the preview.
// They and every inferred value remain displayEstimated, never mm.
void completeDepthForDisplay(DepthMap& map, const std::vector<uint16_t>& candidates, int radius = 32);
}
