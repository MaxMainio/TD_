#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Dither
{
enum class Kernel { FloydSteinberg, JarvisJudiceNinke, Stucki, Atkinson,
                    Burkes, SierraFull, SierraTwoRow, SierraLite };
enum class Scan { Raster, Serpentine };
enum class Color { RGB, Monochrome };
enum class Source { Luminance, Red, Green, Blue, Alpha };
enum class Alpha { Preserve, Dither, Opaque };

struct Settings
{
    Kernel kernel = Kernel::FloydSteinberg;
    int bits = 1;
    float strength = 1.0f;
    Scan scan = Scan::Raster;
    Color color = Color::RGB;
    Source source = Source::Luminance;
    Alpha alpha = Alpha::Preserve;
};

// RGBA float input, bottom row first. Stride is measured in floats.
struct Input
{
    const float* data = nullptr;
    size_t width = 0, height = 0, stride = 0;
};

struct Image
{
    size_t width = 0, height = 0;
    std::array<const float*, 4> channels{};
    float value(size_t channel, size_t pixel) const
    {
        return channels[channel] ? channels[channel][pixel] : (channel == 3 ? 1.0f : 0.0f);
    }
};

// One process() caller at a time. Returned planes remain valid until the next
// process() call. Workers only see owned scratch/output and the live input view.
class Engine
{
public:
    explicit Engine(unsigned maxWorkers = 3);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Image process(Input input, size_t width, size_t height, Settings settings,
                  unsigned outputChannels = 15);
    size_t scratchBytes() const;
    size_t retainedBytes() const;
    size_t workerCount() const { return workers_.size(); }

private:
    struct Job { int output = 0, source = 0; bool luminance = false, diffuse = true; };
    void ensureWorkers(unsigned count);
    void stopWorkers();
    void worker();
    void runJobs() noexcept;
    void runChannel(const Job& job) noexcept;
    template <size_t KernelIndex> void diffuseChannel(const Job& job) noexcept;
    float sample(const Job& job, size_t x, size_t y) const noexcept;

    unsigned maxWorkers_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable start_, done_;
    bool stopping_ = false;
    uint64_t generation_ = 0;
    size_t pending_ = 0;
    std::atomic<size_t> nextJob_{0};
    std::array<Job, 4> jobs_{};
    size_t jobCount_ = 0;
    Input input_;
    size_t width_ = 0, height_ = 0;
    Settings settings_;
    std::array<float, 256> normalized_{};
    std::vector<size_t> xMap_, yMap_;
    std::array<std::vector<float>, 4> planes_, rows_;
};
}
