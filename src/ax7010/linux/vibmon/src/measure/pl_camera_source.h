#pragma once

#include <array>
#include "drv/pl_camera.h"
#include "measure/frame_source.h"

namespace vm {

// {left, right} (PlCameraConfig::leftCamera). Both sources share sensor reset
// and register ownership. The matching camera bitstream and excluded DDR must
// be prepared before either source is started.
std::array<std::unique_ptr<FrameSource>, 2> makePlCameraSources(const PlCameraConfig& cfg);

}  // namespace vm
