#pragma once

#include <algorithm>
#include <cmath>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Hull
{
enum class Source { Luminance, Red, Green, Blue, Alpha, RGB, RGBA };
enum class Grouping { Global, PerBlob, LargestBlob };
enum class View { Filled, Outline, Selection };
enum class Alpha { Opaque, Preserve, Result };

struct Settings
{
    Source source = Source::Luminance;
    Grouping grouping = Grouping::Global;
    View view = View::Filled;
    Alpha alpha = Alpha::Opaque;
    bool independent = false, below = false, ignoreTransparent = false;
    double threshold = .5;
    std::array<double, 4> thresholds{{.5, .5, .5, .5}};
    double alphaCutoff = 0;
    uint64_t minimumArea = 1;
    int outlineWidth = 1;
};

struct Input { const float* data; size_t width, height, stride; };
struct Point
{
    int x, y;
    bool operator==(Point p) const { return x == p.x && y == p.y; }
};

// Sorts/deduplicates points, returns counterclockwise vertices without a closing duplicate.
void convexHull(std::vector<Point>& points, std::vector<Point>& hull);

struct SourceStats
{
    uint64_t selectedPixels = 0, retainedPixels = 0, blobs = 0, hulls = 0, outputPixels = 0;
};

struct Image
{
    size_t width = 0, height = 0;
    std::array<const uint8_t*, 4> channels{};
    Input input{};
    Alpha alpha = Alpha::Opaque;
    float value(size_t channel, size_t pixel) const;
};

inline float Image::value(size_t channel, size_t pixel) const
{
    if (channels[channel]) return float(channels[channel][pixel]);
    if (channel != 3) return 0;
    if (alpha == Alpha::Opaque) return 1;
    if (alpha == Alpha::Result)
        return float(std::max({channels[0][pixel], channels[1][pixel], channels[2][pixel]}));
    const float a = input.data[(pixel / width) * input.stride + (pixel % width) * 4 + 3];
    return std::isfinite(a) ? std::clamp(a, 0.f, 1.f) : 0.f;
}

class Engine
{
public:
    explicit Engine(unsigned maxWorkers = 3) : maxWorkers_(maxWorkers > 3 ? 3 : maxWorkers) {}
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    // Returned pointers remain valid until the next process() or destruction.
    // Preserve-alpha results also borrow input through output packing.
    Image process(Input input, Settings settings);
    const std::array<SourceStats, 5>& stats() const { return stats_; }
    size_t retainedBytes() const;
    unsigned workerCount() const { return static_cast<unsigned>(workers_.size()); }

private:
    static constexpr uint32_t none = UINT32_MAX;
    struct Run
    {
        int left, right, y;
        uint32_t parent, head = none, next = none;
        uint64_t area = 0;
    };
    struct Plane
    {
        std::vector<uint8_t> mask;
        std::vector<Run> runs;
        std::vector<Point> points, hull;
        std::vector<int> rowLeft, rowRight;
    };
    void processPlane(unsigned job);
    void jobs() noexcept;
    void worker();
    void startWorkers();
    uint32_t root(Plane& plane, uint32_t i);
    void render(Plane& plane);
    void line(Plane& plane, Point a, Point b, int width);
    void span(Plane& plane, int y, int left, int right);

    Input input_{};
    Settings settings_{};
    std::array<Plane, 4> planes_;
    std::array<unsigned, 4> sources_{};
    std::array<SourceStats, 5> stats_{};
    unsigned jobCount_ = 0, maxWorkers_;
    std::vector<std::thread> workers_;
    std::atomic<unsigned> nextJob_{0};
    std::mutex mutex_;
    std::condition_variable start_, done_;
    uint64_t generation_ = 0;
    unsigned pending_ = 0;
    bool stopping_ = false;
    std::exception_ptr failure_;
};
}
