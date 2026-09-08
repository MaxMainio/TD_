#include "DitherEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace Dither
{
namespace
{
struct Tap { int x, y; float weight; };
struct KernelData { const Tap* taps; size_t count; int depth; };
constexpr Tap fs[] = {{1,0,7.f/16}, {-1,1,3.f/16}, {0,1,5.f/16}, {1,1,1.f/16}};
constexpr Tap jjn[] = {{1,0,7.f/48}, {2,0,5.f/48}, {-2,1,3.f/48}, {-1,1,5.f/48},
    {0,1,7.f/48}, {1,1,5.f/48}, {2,1,3.f/48}, {-2,2,1.f/48}, {-1,2,3.f/48},
    {0,2,5.f/48}, {1,2,3.f/48}, {2,2,1.f/48}};
constexpr Tap stucki[] = {{1,0,8.f/42}, {2,0,4.f/42}, {-2,1,2.f/42}, {-1,1,4.f/42},
    {0,1,8.f/42}, {1,1,4.f/42}, {2,1,2.f/42}, {-2,2,1.f/42}, {-1,2,2.f/42},
    {0,2,4.f/42}, {1,2,2.f/42}, {2,2,1.f/42}};
constexpr Tap atkinson[] = {{1,0,1.f/8}, {2,0,1.f/8}, {-1,1,1.f/8}, {0,1,1.f/8},
    {1,1,1.f/8}, {0,2,1.f/8}};
constexpr Tap burkes[] = {{1,0,8.f/32}, {2,0,4.f/32}, {-2,1,2.f/32}, {-1,1,4.f/32},
    {0,1,8.f/32}, {1,1,4.f/32}, {2,1,2.f/32}};
constexpr Tap sierra[] = {{1,0,5.f/32}, {2,0,3.f/32}, {-2,1,2.f/32}, {-1,1,4.f/32},
    {0,1,5.f/32}, {1,1,4.f/32}, {2,1,2.f/32}, {-1,2,2.f/32}, {0,2,3.f/32}, {1,2,2.f/32}};
constexpr Tap twoRow[] = {{1,0,4.f/16}, {2,0,3.f/16}, {-2,1,1.f/16}, {-1,1,2.f/16},
    {0,1,3.f/16}, {1,1,2.f/16}, {2,1,1.f/16}};
constexpr Tap lite[] = {{1,0,2.f/4}, {-1,1,1.f/4}, {0,1,1.f/4}};
constexpr KernelData kernels[] = {{fs,4,1}, {jjn,12,2}, {stucki,12,2}, {atkinson,6,2},
    {burkes,7,1}, {sierra,10,2}, {twoRow,7,1}, {lite,3,1}};

float finiteSample(float value) noexcept
{
    if (std::isnan(value)) return 0.0f;
    if (std::isinf(value)) return value > 0 ? 1.0f : 0.0f;
    return value;
}

// Independent of the host's floating-point rounding mode. Float values at
// magnitude >= 2^23 already have integral spacing. Do not use std::round:
// it rounds ties away from zero, unlike the component's np.round.
float roundEven(float value) noexcept
{
#if defined(__has_builtin)
#if __has_builtin(__builtin_roundevenf)
    return __builtin_roundevenf(value);
#endif
#endif
    if (std::abs(value) >= 8388608.0f) return value;
    const float lower = std::floor(value);
    const float fraction = value - lower;
    if (fraction < 0.5f) return lower;
    if (fraction > 0.5f) return lower + 1.0f;
    return (static_cast<int32_t>(lower) & 1) ? lower + 1.0f : lower;
}

size_t checkedPixels(size_t width, size_t height)
{
    if (!width || !height || width > static_cast<size_t>(INT32_MAX) ||
        height > static_cast<size_t>(INT32_MAX) ||
        width > std::numeric_limits<size_t>::max() / height / (4 * sizeof(float)))
        throw std::invalid_argument("Invalid or excessively large image dimensions");
    return width * height;
}
}

Engine::Engine(unsigned maxWorkers) : maxWorkers_(std::min(maxWorkers, 3u)) {}
Engine::~Engine() { stopWorkers(); }

void Engine::stopWorkers()
{
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    start_.notify_all();
    for (auto& thread : workers_) thread.join();
}

void Engine::ensureWorkers(unsigned count)
{
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    count = std::min({count, maxWorkers_, hardware - 1});
    while (workers_.size() < count)
        workers_.emplace_back(&Engine::worker, this);
}

void Engine::worker()
{
    std::unique_lock<std::mutex> lock(mutex_);
    // Newly created threads must participate in the first pending generation,
    // even if the caller starts the batch before they reach this wait.
    uint64_t seen = 0;
    for (;;)
    {
        start_.wait(lock, [&] { return stopping_ || generation_ != seen; });
        if (stopping_) return;
        seen = generation_;
        lock.unlock();
        runJobs();
        lock.lock();
        if (--pending_ == 0) done_.notify_one();
    }
}

Image Engine::process(Input input, size_t width, size_t height, Settings settings,
                      unsigned outputChannels)
{
    checkedPixels(input.width, input.height);
    const size_t pixels = checkedPixels(width, height);
    if (!input.data || input.stride < input.width * 4 ||
        input.stride > std::numeric_limits<size_t>::max() / input.height / sizeof(float))
        throw std::invalid_argument("Invalid RGBA input buffer or stride");
    if (static_cast<unsigned>(settings.kernel) >= 8 ||
        static_cast<unsigned>(settings.scan) >= 2 || static_cast<unsigned>(settings.color) >= 2 ||
        static_cast<unsigned>(settings.source) >= 5 || static_cast<unsigned>(settings.alpha) >= 3)
        throw std::invalid_argument("Invalid dithering settings");
    settings.bits = std::clamp(settings.bits, 1, 8);
    settings.strength = std::isfinite(settings.strength) ? std::clamp(settings.strength, 0.f, 1.f) : 1.f;
    settings_ = settings;
    const int levels = (1 << settings.bits) - 1;
    for (int q = 0; q <= levels; ++q) normalized_[q] = static_cast<float>(q) / levels;
    input_ = input;
    width_ = width;
    height_ = height;
    xMap_.resize(width);
    yMap_.resize(height);
    for (size_t x = 0; x < width; ++x)
        xMap_[x] = std::min(input.width - 1, static_cast<size_t>((x + 0.5) * input.width / width));
    for (size_t y = 0; y < height; ++y)
        yMap_[y] = std::min(input.height - 1, static_cast<size_t>((y + 0.5) * input.height / height));

    jobCount_ = 0;
    auto add = [&](int output, int source, bool luminance, bool diffuse)
    {
        jobs_[jobCount_++] = {output, source, luminance, diffuse};
        planes_[output].resize(pixels);
        if (diffuse) rows_[output].resize(width * 3);
    };
    Image result{width, height, {}};
    if (settings.color == Color::Monochrome && (outputChannels & 7))
    {
        add(0, std::max(0, static_cast<int>(settings.source) - 1), settings.source == Source::Luminance, true);
        for (int c = 0; c < 3; ++c) result.channels[c] = planes_[0].data();
    }
    else
        for (int c = 0; c < 3; ++c)
            if (outputChannels & (1u << c))
            {
                add(c, c, false, true);
                result.channels[c] = planes_[c].data();
            }
    if (outputChannels & 8)
    {
        add(3, 3, false, settings.alpha == Alpha::Dither);
        result.channels[3] = planes_[3].data();
    }

    // Small images run inline, avoiding worker wake-up overhead. Grow the pool
    // only between completed batches, with new workers starting at this epoch.
    if (pixels >= 4096 && jobCount_ > 1 && maxWorkers_)
    {
        // Existing workers cannot be joined by late starters in an old epoch.
        // Allocate the bounded pool on its first use, never grow it mid-life.
        if (workers_.empty()) ensureWorkers(maxWorkers_);
        { std::lock_guard<std::mutex> lock(mutex_);
          nextJob_ = 0; pending_ = workers_.size(); ++generation_; }
        start_.notify_all();
        runJobs();
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
    }
    else
    {
        nextJob_ = 0;
        runJobs();
    }
    input_.data = nullptr;
    return result;
}

void Engine::runJobs() noexcept
{
    for (size_t i = nextJob_.fetch_add(1); i < jobCount_; i = nextJob_.fetch_add(1))
        runChannel(jobs_[i]);
}

float Engine::sample(const Job& job, size_t x, size_t y) const noexcept
{
    const float* p = input_.data + yMap_[y] * input_.stride + xMap_[x] * 4;
    if (job.luminance)
        return 0.2126f * finiteSample(p[0]) + 0.7152f * finiteSample(p[1]) + 0.0722f * finiteSample(p[2]);
    return finiteSample(p[job.source]);
}

void Engine::runChannel(const Job& job) noexcept
{
    float* output = planes_[job.output].data();
    if (!job.diffuse)
    {
        for (size_t y = 0; y < height_; ++y)
            for (size_t x = 0; x < width_; ++x)
                output[y * width_ + x] = settings_.alpha == Alpha::Opaque ? 1.f : sample(job, x, y);
        return;
    }
    // Compile each fixed kernel independently so the inner loop has neither
    // runtime tap counts nor division to address the rolling rows.
    switch (settings_.kernel)
    {
        case Kernel::FloydSteinberg: diffuseChannel<0>(job); break;
        case Kernel::JarvisJudiceNinke: diffuseChannel<1>(job); break;
        case Kernel::Stucki: diffuseChannel<2>(job); break;
        case Kernel::Atkinson: diffuseChannel<3>(job); break;
        case Kernel::Burkes: diffuseChannel<4>(job); break;
        case Kernel::SierraFull: diffuseChannel<5>(job); break;
        case Kernel::SierraTwoRow: diffuseChannel<6>(job); break;
        case Kernel::SierraLite: diffuseChannel<7>(job); break;
    }
}

template <size_t KernelIndex>
void Engine::diffuseChannel(const Job& job) noexcept
{
    float* output = planes_[job.output].data();
    const float levels = static_cast<float>((1 << settings_.bits) - 1);
    constexpr KernelData kernel = kernels[KernelIndex];
    constexpr size_t rowCount = static_cast<size_t>(kernel.depth + 1);
    float* ring = rows_[job.output].data();
    auto loadRow = [&](size_t y)
    {
        float* row = ring + (y % rowCount) * width_;
        for (size_t x = 0; x < width_; ++x)
        {
            // Very large finite floats are already beyond the output range
            // and have no fractional quantization error; bound scaling safely.
            const float v = std::clamp(sample(job, x, y), -1.0e30f, 1.0e30f);
            row[x] = v * levels;
        }
    };
    for (size_t y = 0; y < std::min(rowCount, height_); ++y) loadRow(y);
    for (size_t y = 0; y < height_; ++y)
    {
        float* targetRows[3] = {ring + (y % rowCount) * width_,
                               ring + ((y + 1) % rowCount) * width_,
                               ring + ((y + 2) % rowCount) * width_};
        float* current = targetRows[0];
        float* outputRow = output + y * width_;
        const bool reverse = settings_.scan == Scan::Serpentine && (y & 1);
        auto processRow = [&](auto directionTag)
        {
            constexpr int direction = decltype(directionTag)::value;
            for (size_t step = 0; step < width_; ++step)
            {
                const size_t x = direction < 0 ? width_ - 1 - step : step;
                const float old = current[x];
                const float quantized = roundEven(old);
                // The quantizer has at most 256 outputs. Precompute the exact
                // float32 divisions once per cook, avoiding a pixel-wise divide.
                outputRow[x] = normalized_[static_cast<unsigned>(std::clamp(quantized, 0.f, levels))];
                const float error = (old - quantized) * settings_.strength;
                if (error == 0.f) continue;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
                for (size_t t = 0; t < kernel.count; ++t)
                {
                    const Tap& tap = kernel.taps[t];
                    const ptrdiff_t nx = static_cast<ptrdiff_t>(x) + direction * tap.x;
                    if (nx < 0 || static_cast<size_t>(nx) >= width_ || y + tap.y >= height_) continue;
                    targetRows[tap.y][nx] += error * tap.weight;
                }
            }
        };
        if (reverse) processRow(std::integral_constant<int,-1>{});
        else processRow(std::integral_constant<int,1>{});
        if (y + rowCount < height_) loadRow(y + rowCount);
    }
}

size_t Engine::scratchBytes() const
{
    size_t bytes = 0;
    for (const auto& row : rows_) bytes += row.size() * sizeof(float);
    return bytes;
}

size_t Engine::retainedBytes() const
{
    size_t bytes = (xMap_.capacity() + yMap_.capacity()) * sizeof(size_t);
    for (const auto& row : rows_) bytes += row.capacity() * sizeof(float);
    for (const auto& plane : planes_) bytes += plane.capacity() * sizeof(float);
    return bytes;
}
}
