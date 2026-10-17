#include "DitherEngine.h"
#include "OutputPacking.h"

#include <algorithm>
#include <array>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// Independent full-image reference: integer matrices transcribed from the
// supplied COMP, no production taps, rolling buffers, or worker implementation.
struct Matrix { int divisor; std::array<int, 15> weights; };
const Matrix matrices[] = {
    {16, {0,0,0,7,0, 0,3,5,1,0, 0,0,0,0,0}},
    {48, {0,0,0,7,5, 3,5,7,5,3, 1,3,5,3,1}},
    {42, {0,0,0,8,4, 2,4,8,4,2, 1,2,4,2,1}},
    {8,  {0,0,0,1,1, 0,1,1,1,0, 0,0,1,0,0}},
    {32, {0,0,0,8,4, 2,4,8,4,2, 0,0,0,0,0}},
    {32, {0,0,0,5,3, 2,4,5,4,2, 0,2,3,2,0}},
    {16, {0,0,0,4,3, 1,2,3,2,1, 0,0,0,0,0}},
    {4,  {0,0,0,2,0, 0,1,1,0,0, 0,0,0,0,0}}
};

float finite(float v)
{
    return std::isnan(v) ? 0.f : std::isinf(v) ? (v > 0 ? 1.f : 0.f) : v;
}

std::vector<float> reference(const std::vector<float>& input, int iw, int ih, int stride,
                             int w, int h, Dither::Settings s)
{
    const float levels = static_cast<float>((1 << std::clamp(s.bits, 1, 8)) - 1);
    const float strength = std::isfinite(s.strength) ? std::clamp(s.strength, 0.f, 1.f) : 1.f;
    std::vector<float> result(w * h * 4);
    for (int c = 0; c < 4; ++c)
    {
        std::vector<float> channel(w * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int sx = std::min(iw - 1, static_cast<int>(std::floor((x + .5) * iw / w)));
                const int sy = std::min(ih - 1, static_cast<int>(std::floor((y + .5) * ih / h)));
                const float* p = &input[sy * stride + sx * 4];
                float value = finite(p[c]);
                if (c < 3 && s.color == Dither::Color::Monochrome)
                    value = s.source == Dither::Source::Luminance ?
                        0.2126f * finite(p[0]) + 0.7152f * finite(p[1]) + 0.0722f * finite(p[2]) :
                        finite(p[static_cast<int>(s.source) - 1]);
                channel[y * w + x] = std::clamp(value, -1.0e30f, 1.0e30f) * levels;
                result[(y * w + x) * 4 + c] = c == 3 && s.alpha == Dither::Alpha::Opaque ? 1.f : value;
            }
        if (c == 3 && s.alpha != Dither::Alpha::Dither) continue;
        const Matrix& matrix = matrices[static_cast<int>(s.kernel)];
        for (int y = 0; y < h; ++y)
        {
            const bool reverse = s.scan == Dither::Scan::Serpentine && y % 2;
            for (int i = 0; i < w; ++i)
            {
                const int x = reverse ? w - 1 - i : i;
                const float old = channel[y * w + x];
                const float quantized = std::nearbyint(old);
                result[(y * w + x) * 4 + c] = std::clamp(quantized / levels, 0.f, 1.f);
                const float error = (old - quantized) * strength;
                for (int dy = 0; dy <= 2; ++dy)
                    for (int k = 0; k < 5; ++k)
                    {
                        const int nx = x + (reverse ? 2 - k : k - 2);
                        const int ny = y + dy;
                        const int numerator = matrix.weights[dy * 5 + k];
                        if (!numerator || nx < 0 || nx >= w || ny >= h) continue;
                        const float weight = static_cast<float>(numerator) / matrix.divisor;
                        channel[ny * w + nx] += error * weight;
                    }
            }
        }
    }
    return result;
}

std::vector<float> fixture(int w, int h, int stride)
{
    std::vector<float> input(h * stride, -999.f);
    uint32_t rng = 132713;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c)
            {
                rng = rng * 1664525u + 1013904223u;
                input[y * stride + x * 4 + c] = ((rng >> 8) % 2048) / 1700.f - .1f;
            }
    return input;
}

void compare(Dither::Engine& engine, int iw, int ih, int w, int h, Dither::Settings s, unsigned mask = 15)
{
    const int stride = iw * 4 + 8;
    const auto input = fixture(iw, ih, stride);
    const auto expected = reference(input, iw, ih, stride, w, h, s);
    const auto actual = engine.process({input.data(), static_cast<size_t>(iw), static_cast<size_t>(ih), static_cast<size_t>(stride)}, w, h, s, mask);
    for (int c = 0; c < 4; ++c)
        if (mask & (1u << c))
            for (int p = 0; p < w * h; ++p)
                if (actual.value(c, p) != expected[p * 4 + c])
                {
                    std::cerr << "Mismatch kernel=" << static_cast<int>(s.kernel) << " bits=" << s.bits
                              << " scan=" << static_cast<int>(s.scan) << " strength=" << s.strength
                              << " color=" << static_cast<int>(s.color) << " source=" << static_cast<int>(s.source)
                              << " alpha=" << static_cast<int>(s.alpha) << " at=" << p << "," << c << '\n';
                    throw std::runtime_error("Full-image reference mismatch");
                }
}

void referenceTests()
{
    Dither::Engine engine;
    Dither::Settings s;
    for (int k = 0; k < 8; ++k)
        for (int b = 1; b <= 8; ++b)
            for (int scan = 0; scan < 2; ++scan)
                for (float strength : {0.f, .37f, 1.f})
                    for (int color = 0; color < 6; ++color)
                        for (int alpha = 0; alpha < 3; ++alpha)
                        {
                            s.kernel = static_cast<Dither::Kernel>(k);
                            s.bits = b; s.scan = static_cast<Dither::Scan>(scan); s.strength = strength;
                            s.color = color == 0 ? Dither::Color::RGB : Dither::Color::Monochrome;
                            s.source = static_cast<Dither::Source>(std::max(0, color - 1));
                            s.alpha = static_cast<Dither::Alpha>(alpha);
                            compare(engine, 13, 7, 13, 7, s);
                        }
    for (int k = 0; k < 8; ++k)
        for (int scan = 0; scan < 2; ++scan)
        {
            s = {}; s.kernel = static_cast<Dither::Kernel>(k); s.scan = static_cast<Dither::Scan>(scan);
            for (const auto& dims : {std::array<int,2>{1,1}, {1,19}, {21,1}, {2,2}, {3,5}})
                compare(engine, dims[0], dims[1], dims[0], dims[1], s);
            compare(engine, 11, 9, 3, 5, s);
            compare(engine, 3, 5, 11, 9, s);
            for (unsigned mask : {1u, 3u, 7u, 8u, 9u, 15u}) compare(engine, 13, 7, 13, 7, s, mask);
            // Above the dispatch threshold: verify worker results against the
            // same independent reference, including a resize and alpha job.
            s.alpha = Dither::Alpha::Dither;
            compare(engine, 73, 71, 65, 67, s);
        }
    const size_t retained = engine.retainedBytes();
    for (int i = 0; i < 20; ++i) compare(engine, 73, 71, 65, 67, s);
    require(retained == engine.retainedBytes(), "Steady dimensions grow retained memory");
    require(engine.scratchBytes() <= 4 * 3 * 65 * sizeof(float), "More than three scratch rows per channel");
}

void tiesAndInvalidTests()
{
    Dither::Engine engine;
    Dither::Settings s; s.bits = 2; s.strength = 0;
    std::vector<float> input = {.5f/3,.5f,2.5f/3, .123f};
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    {
        std::fesetround(mode);
        const auto image = engine.process({input.data(),1,1,4},1,1,s);
        require(image.value(1,0) > .66f && image.value(1,0) < .67f, "Halfway rounding follows host mode");
    }
    std::fesetround(FE_TONEAREST);
    auto image = engine.process({input.data(),1,1,4},1,1,s);
    require(image.value(0,0) == 0 && image.value(1,0) == 2.f/3 && image.value(2,0) == 2.f/3, "Tie-to-even mismatch");
    input = {NAN, INFINITY, -INFINITY, NAN};
    image = engine.process({input.data(),1,1,4},1,1,{});
    require(image.value(0,0) == 0 && image.value(1,0) == 1 && image.value(2,0) == 0 && image.value(3,0) == 0, "Non-finite input policy mismatch");
    input = {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), .5f, .33f};
    image = engine.process({input.data(),1,1,4},1,1,{});
    require(image.value(0,0) == 1 && image.value(1,0) == 0 && image.value(3,0) == .33f, "Extreme finite input corrupted output");
    s.bits = 100; s.strength = NAN;
    compare(engine, 13, 7, 13, 7, s);
    s.bits = -20; s.strength = -1;
    compare(engine, 13, 7, 13, 7, s);
    bool threw = false;
    try { engine.process({},1,1,{}); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Missing input accepted");
    threw = false;
    try { engine.process({input.data(),1,1,3},1,1,{}); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Short row stride accepted");
}

void packingTests()
{
    using F = TD::OP_PixelFormat;
    const std::array<float,4> values = {.2f,.6f,.8f,.4f};
    Dither::Image image{1,1,{&values[0],&values[1],&values[2],&values[3]}};
    const F formats[] = {F::BGRA8Fixed,F::RGBA8Fixed,F::RGBA16Fixed,F::RGBA16Float,F::RGBA32Float,
        F::Mono8Fixed,F::Mono16Fixed,F::Mono16Float,F::Mono32Float,F::RG8Fixed,F::RG16Fixed,F::RG16Float,F::RG32Float,
        F::A8Fixed,F::A16Fixed,F::A16Float,F::A32Float,F::MonoA8Fixed,F::MonoA16Fixed,F::MonoA16Float,F::MonoA32Float,
        F::RGB10A2Fixed,F::RGB11Float};
    for (F format : formats)
    {
        const size_t size = TDPlugin::TOPOutput::bytesPerPixel(format);
        std::array<uint8_t,32> bytes; bytes.fill(0xD7);
        Dither::packImage(image,format,bytes.data()+1,size);
        require(bytes[0] == 0xD7 && bytes[size+1] == 0xD7, "Packing wrote beyond buffer");
        bool threw = false;
        try { Dither::packImage(image,format,bytes.data(),size-1); }
        catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Short output buffer accepted");
        if (format == F::Mono32Float || format == F::MonoA32Float)
        {
            float red; std::memcpy(&red,bytes.data()+1,4);
            require(red == values[0], "Mono packing applied luminance after quantization");
        }
        if (format == F::Mono8Fixed || format == F::MonoA8Fixed || format == F::RGBA8Fixed)
            require(bytes[1] == 51, "8-bit red conversion incorrect");
        if (format == F::BGRA8Fixed)
            require(bytes[1] == 204 && bytes[3] == 51 && bytes[4] == 102, "BGRA swizzle or alpha incorrect");
        if (format == F::RGB10A2Fixed)
        {
            uint32_t packed; std::memcpy(&packed,bytes.data()+1,4);
            require((packed & 1023) == 205 && (packed >> 30) == 1, "RGB10A2 packing incorrect");
        }
        // Decode every format independently and compare the actual stored
        // channel values. This catches wrong channel order and bit layouts,
        // not just allocation bounds.
        auto word = [&](size_t offset, size_t count)
        {
            uint32_t value = 0;
            std::memcpy(&value, bytes.data()+1+offset, count);
            return value;
        };
        auto miniFloat = [](uint32_t bits, int mantissaBits)
        {
            const int exponent = (bits >> mantissaBits) & 31;
            const int mantissa = bits & ((1 << mantissaBits)-1);
            return exponent == 0 ? std::ldexp(static_cast<float>(mantissa), 1-15-mantissaBits) :
                std::ldexp(1.f + static_cast<float>(mantissa)/(1 << mantissaBits), exponent-15);
        };
        if (format == F::RGB10A2Fixed)
        {
            const uint32_t packed = word(0,4);
            for (int c = 0; c < 4; ++c)
            {
                const unsigned max = c == 3 ? 3 : 1023;
                const float actual = static_cast<float>((packed >> (c*10)) & max)/max;
                require(std::abs(actual-values[c]) <= .5001f/max, "RGB10A2 numeric decode mismatch");
            }
        }
        else if (format == F::RGB11Float)
        {
            const uint32_t packed = word(0,4);
            const float decoded[] = {miniFloat(packed & 2047,6), miniFloat((packed >> 11) & 2047,6), miniFloat(packed >> 22,5)};
            for (int c = 0; c < 3; ++c)
                require(std::abs(decoded[c]-values[c]) < .016f, "RGB11Float numeric decode mismatch");
        }
        else
        {
            std::vector<int> channels;
            const unsigned mask = Dither::outputChannels(format);
            for (int c = 0; c < 4; ++c) if (mask & (1u << c)) channels.push_back(c);
            if (format == F::BGRA8Fixed) channels = {2,1,0,3};
            const size_t componentBytes = size/channels.size();
            const bool floating = format == F::RGBA16Float || format == F::RGBA32Float ||
                format == F::Mono16Float || format == F::Mono32Float || format == F::RG16Float ||
                format == F::RG32Float || format == F::A16Float || format == F::A32Float ||
                format == F::MonoA16Float || format == F::MonoA32Float;
            for (size_t c = 0; c < channels.size(); ++c)
            {
                float decoded, tolerance;
                if (floating && componentBytes == 4)
                {
                    const uint32_t bits = word(c*4,4);
                    std::memcpy(&decoded,&bits,4); tolerance = 0;
                }
                else if (floating)
                {
                    decoded = miniFloat(word(c*2,2),10); tolerance = .001f;
                }
                else
                {
                    const float max = componentBytes == 1 ? 255.f : 65535.f;
                    decoded = word(c*componentBytes,componentBytes)/max;
                    tolerance = .5001f/max;
                }
                require(std::abs(decoded-values[channels[c]]) <= tolerance, "Stored channel numeric mismatch");
            }
        }
    }
    for (int code = 0; code < 255; ++code)
    {
        const float threshold = (code + .5f)/255.f;
        for (float v : {std::nextafter(threshold,0.f), threshold, std::nextafter(threshold,1.f)})
        {
            Dither::Image test{1,1,{&v,&v,&v,&v}};
            uint8_t packed[4]; Dither::packImage(test,F::RGBA8Fixed,packed,4);
            for (auto byte : packed) require(byte == TDPlugin::TOPOutput::to8(v), "Fast byte packing changed rounding");
        }
    }
}

#ifdef __APPLE__
size_t threadCount()
{
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    require(task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS, "Cannot inspect worker lifetime");
    for (size_t i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
    return count;
}
#endif

void lifecycleTests()
{
#ifdef __APPLE__
    const auto baseline = threadCount();
#endif
    for (int i = 0; i < 150; ++i)
    {
        Dither::Engine engine;
        if (i % 3 == 0) continue;
        Dither::Settings s; s.kernel = static_cast<Dither::Kernel>(i % 8);
        compare(engine,65,65,65,65,s);
        compare(engine,1,1,1,1,s);
        s.alpha = Dither::Alpha::Dither;
        compare(engine,65,65,65,65,s);
    }
#ifdef __APPLE__
    // pthread_join completes ownership; Darwin may retire its kernel thread
    // entry shortly afterward (also covered this way in Basic Filter's test).
    for (int i = 0; i < 100 && threadCount() != baseline; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(threadCount() == baseline, "Destroying engines leaked worker threads");
#endif
}
}

int main()
{
    try
    {
        std::fesetround(FE_TONEAREST);
        referenceTests(); tiesAndInvalidTests(); packingTests(); lifecycleTests();
        std::cout << "Passed: 6912 settings combinations, boundaries, resizing, masks, parallel parity,\n"
                     "ties, non-finite values, 23 output formats, stable buffers, and worker lifecycle.\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
