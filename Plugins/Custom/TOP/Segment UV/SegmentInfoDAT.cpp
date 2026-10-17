#include "SegmentInfoDAT.h"
#include <charconv>
#include <cstdio>
#include <xlocale.h>
#include <limits>
#include <stdexcept>

namespace Segment
{
namespace
{
constexpr const char* headers[] = {
    "id", "u", "v", "width", "height", "left", "top", "right", "bottom",
    "selected_pixels", "centroid_u", "centroid_v", "output_r", "output_g", "output_b"
};
std::string number(double value)
{
    // Locale independent, round-trippable, without a stream per cell.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 130300
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) throw std::runtime_error("Could not format segment measurement");
    return {buffer, result.ptr};
#else
    // libc++ floating-point to_chars requires a macOS 13.3+ deployment target.
    // Keep a locale-independent fallback for explicitly configured older builds.
    struct Locale
    {
        locale_t value = newlocale(LC_NUMERIC_MASK, "C", nullptr);
        Locale() { if (!value) throw std::runtime_error("Could not create numeric locale"); }
        ~Locale() { freelocale(value); }
    };
    static const Locale locale;
    char buffer[64];
    const int count = snprintf_l(buffer, sizeof(buffer), locale.value, "%.*g",
        std::numeric_limits<double>::max_digits10, value);
    if (count < 0 || size_t(count) >= sizeof(buffer))
        throw std::runtime_error("Could not format segment measurement");
    return {buffer, size_t(count)};
#endif
}
}

void InfoTable::setDetail(DATDetail detail)
{
    int32_t columns;
    switch (detail)
    {
        case DATDetail::Basic: columns = 5; break;
        case DATDetail::Medium: columns = 9; break;
        case DATDetail::Maximum: columns = 15; break;
        default: throw std::invalid_argument("Invalid segment DAT detail");
    }
    if (columns != columns_) { clear(); columns_ = columns; }
}

void InfoTable::reserve(int segments)
{
    if (segments < 0 || segments >= INT32_MAX || size_t(segments) > cells_.max_size() / columns_)
        throw std::length_error("Too many segments for the Info DAT");
    cells_.reserve(size_t(segments) * columns_);
}

void InfoTable::append(const Measurement& m, int w, int h)
{
    try
    {
        if (w <= 0 || h <= 0 || m.left < 0 || m.top < 0 || m.width <= 0 || m.height <= 0 ||
            m.width > w || m.height > h || m.left > w - m.width || m.top > h - m.height ||
            m.selectedPixels <= 0 || rowCount() == INT32_MAX)
            throw std::invalid_argument("Invalid segment measurements");
        const int id = rowCount() - 1;
        const double left = double(m.left) / w, right = double(m.left + m.width) / w;
        const double top = double(h - m.top) / h, bottom = double(h - m.top - m.height) / h;
        cells_.push_back(std::to_string(id));
        cells_.push_back(number((double(m.left) + m.width * .5) / w));
        cells_.push_back(number((double(h - m.top) - m.height * .5) / h));
        cells_.push_back(number(double(m.width) / w));
        cells_.push_back(number(double(m.height) / h));
        if (columns_ >= 9)
        {
            cells_.push_back(number(left)); cells_.push_back(number(top));
            cells_.push_back(number(right)); cells_.push_back(number(bottom));
        }
        if (columns_ == 15)
        {
            cells_.push_back(std::to_string(m.selectedPixels));
            cells_.push_back(number((m.centroidX + .5) / w));
            cells_.push_back(number((h - m.centroidY - .5) / h));
            for (float value : m.output) cells_.push_back(number(value));
        }
    }
    catch (...) { clear(); throw; }
}

const char* InfoTable::cell(int32_t row, int32_t column) const noexcept
{
    if (row < 0 || column < 0 || row >= rowCount() || column >= columns_) return "";
    return row == 0 ? headers[column] : cells_[size_t(row - 1) * columns_ + column].c_str();
}

size_t InfoTable::retainedBytes() const noexcept
{
    size_t bytes = cells_.capacity() * sizeof(std::string);
    // Conservatively includes inline string capacity already counted above.
    for (const auto& cell : cells_) bytes += cell.capacity() + 1;
    return bytes;
}
}
