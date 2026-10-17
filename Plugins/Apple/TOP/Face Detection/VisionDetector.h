#pragma once
#include "FaceData.h"
#include <memory>

namespace FaceDetection
{
class VisionDetector
{
public:
    VisionDetector();
    ~VisionDetector();
    VisionDetector(const VisionDetector&) = delete;
    VisionDetector& operator=(const VisionDetector&) = delete;
    // Packed BGRA8, top-left first, with rows of width*4 bytes.
    std::vector<Face> detect(const uint8_t* pixels, size_t bytes, uint32_t width, uint32_t height);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
