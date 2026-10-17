#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FaceDetection
{
struct Point { double x = 0, y = 0; };
struct Box { double x = 0, y = 0, width = 0, height = 0; };
struct Landmark
{
    std::string region;
    size_t index = 0;
    Point position; // Whole-image normalized coordinates, origin bottom-left.
};
struct Face
{
    Box box;
    double confidence = 0;
    std::vector<Landmark> landmarks;
    std::vector<Point> allPoints;
};
enum class MaskShape { Oval, LandmarkHull, BoundingBox };
Point imagePoint(const Box& box, Point local);
size_t imageBytes(uint32_t width, uint32_t height, size_t bytesPerPixel);
std::vector<Face> selectFaces(std::vector<Face> faces, double minimumConfidence, bool largestOnly);
// Bottom-left row order. All selected faces are combined with a maximum union.
std::vector<float> renderMask(const std::vector<Face>& faces, uint32_t width, uint32_t height, MaskShape shape);

class InfoTable
{
public:
    static constexpr int32_t columns = 10;
    void assign(const std::vector<Face>& faces);
    void clear() noexcept { rows_.clear(); }
    int32_t rowCount() const { return static_cast<int32_t>(rows_.size()) + 1; }
    const char* cell(int32_t row, int32_t column) const noexcept;
private:
    std::vector<std::array<std::string, columns>> rows_;
};
}
