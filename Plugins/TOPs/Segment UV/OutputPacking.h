#pragma once
#include "TOPOutputHelper.h"
#include <opencv2/imgproc.hpp>
#include <climits>
#include <stdexcept>

namespace Segment
{
inline TD::OP_SmartRef<TD::TOP_Buffer> packOutput(TD::TOP_Context* context,
    const cv::Mat& source, const TD::OP_TextureDesc& desc)
{
    namespace Pack = TDPlugin::TOPOutput;
    const size_t bpp = Pack::bytesPerPixel(desc.pixelFormat);
    if (!desc.width || !desc.height || desc.width > INT_MAX || desc.height > INT_MAX ||
        !bpp || size_t(desc.width) > SIZE_MAX / desc.height / bpp)
        throw std::runtime_error("Invalid output dimensions or format");
    cv::Mat image = source;
    if (image.cols != int(desc.width) || image.rows != int(desc.height))
        cv::resize(image, image, cv::Size(desc.width, desc.height));
    const size_t bytes = size_t(desc.width) * desc.height * bpp;
    auto buffer = context->createOutputBuffer(bytes, TD::TOP_BufferFlags::None, nullptr);
    if (!buffer || !buffer->data || buffer->size < bytes)
        throw std::runtime_error("Could not allocate the output texture buffer");
    auto* dst = static_cast<uint8_t*>(buffer->data);
    // Equivalent to the original cv::flip(..., 0) then packing. Preserve OpenCV's
    // default linear resize, without mutating the analysis image for upload.
    for (int y = image.rows - 1; y >= 0; --y)
    {
        const auto* row = image.ptr<cv::Vec4f>(y);
        for (int x = 0; x < image.cols; ++x)
            Pack::packPixel(dst, desc.pixelFormat, row[x][0], row[x][1], row[x][2], row[x][3]);
    }
    return buffer;
}
}
