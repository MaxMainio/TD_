#pragma once

#include "TOP_CPlusPlusBase.h"
#include "DitherEngine.h"
#include "TOPOutputHelper.h"

#include <stdexcept>

namespace Dither
{
inline unsigned outputChannels(TD::OP_PixelFormat format)
{
    using F = TD::OP_PixelFormat;
    switch (format)
    {
        case F::Mono8Fixed: case F::Mono16Fixed: case F::Mono16Float: case F::Mono32Float: return 1;
        case F::MonoA8Fixed: case F::MonoA16Fixed: case F::MonoA16Float: case F::MonoA32Float: return 9;
        case F::RG8Fixed: case F::RG16Fixed: case F::RG16Float: case F::RG32Float: return 3;
        case F::A8Fixed: case F::A16Fixed: case F::A16Float: case F::A32Float: return 8;
        case F::RGB11Float: return 7;
        default: return 15;
    }
}

inline void packImage(const Image& image, TD::OP_PixelFormat format, void* destination, size_t bytes)
{
    namespace Pack = TDPlugin::TOPOutput;
    using F = TD::OP_PixelFormat;
    const size_t bpp = Pack::bytesPerPixel(format);
    if (!bpp || !destination || !image.width || !image.height || image.width > SIZE_MAX / image.height / bpp ||
        bytes < image.width * image.height * bpp)
        throw std::invalid_argument("Invalid output buffer or pixel format");
    uint8_t* dst = static_cast<uint8_t*>(destination);
    if (format == F::RGBA8Fixed || format == F::BGRA8Fixed)
    {
        const size_t first = format == F::BGRA8Fixed ? 2 : 0;
        const size_t third = format == F::BGRA8Fixed ? 0 : 2;
        // Float round then narrow is equivalent to lround for these bounded
        // values, and allows the compiler to vectorize the byte packing.
        auto byte = [](float value) { return static_cast<uint8_t>(std::round(std::clamp(value, 0.f, 1.f) * 255.f)); };
        for (size_t i = 0; i < image.width * image.height; ++i)
        {
            *dst++ = byte(image.value(first, i));
            *dst++ = byte(image.value(1, i));
            *dst++ = byte(image.value(third, i));
            *dst++ = byte(image.value(3, i));
        }
        return;
    }
    for (size_t i = 0; i < image.width * image.height; ++i)
    {
        const float r = image.value(0, i), g = image.value(1, i);
        const float b = image.value(2, i), a = image.value(3, i);
        // Mono is storage of processed R, not a second luminance conversion.
        // Mixing already-quantized RGB here would create extra tonal levels.
        switch (format)
        {
            case F::Mono8Fixed: *dst++ = Pack::to8(r); break;
            case F::Mono16Fixed: Pack::writeValue<uint16_t>(dst, Pack::to16(r)); break;
            case F::Mono16Float: Pack::writeFloat16(dst, r); break;
            case F::Mono32Float: Pack::writeFloat32(dst, r); break;
            case F::MonoA8Fixed: *dst++ = Pack::to8(r); *dst++ = Pack::to8(a); break;
            case F::MonoA16Fixed:
                Pack::writeValue<uint16_t>(dst, Pack::to16(r));
                Pack::writeValue<uint16_t>(dst, Pack::to16(a)); break;
            case F::MonoA16Float: Pack::writeFloat16(dst, r); Pack::writeFloat16(dst, a); break;
            case F::MonoA32Float: Pack::writeFloat32(dst, r); Pack::writeFloat32(dst, a); break;
            case F::RGBA32Float:
                Pack::writeFloat32(dst, r); Pack::writeFloat32(dst, g);
                Pack::writeFloat32(dst, b); Pack::writeFloat32(dst, a); break;
            default: Pack::packPixel(dst, format, r, g, b, a); break;
        }
    }
}
}
