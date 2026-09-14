#pragma once
#include "Parameters.h"
#include "SegmentInfoDAT.h"
#include <opencv2/core.hpp>

namespace Segment
{
// Image rows are top-down RGBA32Float; encoded UV coordinates are bottom-left.
// A null table is for image-only timing.
// On failure, any supplied table is cleared before the exception propagates.
cv::Mat process(const cv::Mat&, double alphaThreshold, MethodMenuItems, int seed,
                InfoTable* table = nullptr);
}
