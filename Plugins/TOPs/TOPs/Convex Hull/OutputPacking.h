#pragma once

#include "TOP_CPlusPlusBase.h"
#include "HullEngine.h"
#include "TOPOutputHelper.h"

#include <stdexcept>

namespace Hull
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

inline void packImage(const Image& image, TD::OP_PixelFormat format, void* destination, size_t bytes, size_t width = 0, size_t height = 0)
{
    namespace Pack = TDPlugin::TOPOutput;
    using F = TD::OP_PixelFormat;
    if (!width) width = image.width;
    if (!height) height = image.height;
    const size_t bpp = Pack::bytesPerPixel(format);
    if (!bpp || !destination || !image.width || !image.height || width > SIZE_MAX / height / bpp ||
        bytes < width * height * bpp)
        throw std::invalid_argument("Invalid output buffer or pixel format");
    uint8_t* dst = static_cast<uint8_t*>(destination);
    auto sourcePixel = [&](size_t pixel) {
        if (width == image.width && height == image.height) return pixel;
        const size_t x = std::min(image.width-1, size_t((pixel%width+.5)*image.width/width));
        const size_t y = std::min(image.height-1, size_t((pixel/width+.5)*image.height/height));
        return y*image.width+x;
    };
    if ((format == F::RGBA8Fixed || format == F::BGRA8Fixed) && width == image.width && height == image.height)
    {
        const uint8_t* r = image.channels[format == F::BGRA8Fixed ? 2 : 0];
        const uint8_t* g = image.channels[1];
        const uint8_t* b = image.channels[format == F::BGRA8Fixed ? 0 : 2];
        const uint8_t* a = image.channels[3];
        const size_t count = width*height;
        auto rgb = [&](size_t i) { dst[i*4] = r[i]*255; dst[i*4+1] = g[i]*255; dst[i*4+2] = b[i]*255; };
        // Binary planes can be packed without float conversion or per-pixel dispatch.
        if (a)
            for (size_t i = 0; i < count; ++i) { rgb(i); dst[i*4+3] = a[i]*255; }
        else if (image.alpha == Alpha::Opaque)
            for (size_t i = 0; i < count; ++i) { rgb(i); dst[i*4+3] = 255; }
        else if (image.alpha == Alpha::Result)
            for (size_t i = 0; i < count; ++i) { rgb(i); dst[i*4+3] = std::max({r[i],g[i],b[i]})*255; }
        else
            for (size_t y = 0; y < height; ++y) for (size_t x = 0; x < width; ++x)
            {
                const size_t i = y*width+x;
                rgb(i);
                const float value = image.input.data[y*image.input.stride+x*4+3];
                const float alpha = std::isfinite(value) ? std::clamp(value,0.f,1.f) : 0.f;
                dst[i*4+3] = uint8_t(std::round(alpha*255.f));
            }
        return;
    }
    for (size_t i = 0; i < width * height; ++i)
    {
        const size_t pixel = sourcePixel(i);
        const float r = image.value(0,pixel), g = image.value(1,pixel);
        const float b = image.value(2,pixel), a = image.value(3,pixel);
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
