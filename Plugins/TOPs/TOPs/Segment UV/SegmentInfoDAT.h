#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Segment
{
enum class DATDetail { Basic, Medium, Maximum };

// OpenCV coordinates: top-left origin, integer pixel indices. No image storage.
struct Measurement
{
    int left, top, width, height, selectedPixels;
    double centroidX, centroidY;
    std::array<float, 3> output;
};

class InfoTable
{
public:
    void setDetail(DATDetail detail);
    void clear() noexcept { cells_.clear(); }
    void reserve(int segments);
    void append(const Measurement&, int inputWidth, int inputHeight);
    int32_t rowCount() const noexcept { return int32_t(cells_.size() / columns_) + 1; }
    int32_t columnCount() const noexcept { return columns_; }
    const char* cell(int32_t row, int32_t column) const noexcept;
    size_t retainedBytes() const noexcept;
private:
    int32_t columns_ = 5;
    // Allocate only the selected columns; Basic does not pay for Maximum's cells.
    std::vector<std::string> cells_;
};
}
