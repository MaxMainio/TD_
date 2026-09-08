#include "HullInfoDAT.h"
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Hull
{
namespace
{
constexpr std::array<const char*, InfoTable::maxColumns> headers{{
    "id", "source", "u", "v", "width", "height", "left", "top", "right", "bottom",
    "selected_pixels", "blob_count", "vertex_count", "hull_area", "hull_perimeter"
}};
std::string number(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}
}

void InfoTable::setDetail(DATDetail detail)
{
    if (detail != DATDetail::Basic && detail != DATDetail::Medium && detail != DATDetail::Maximum)
        throw std::invalid_argument("Invalid hull DAT detail level");
    const int32_t columns = detail == DATDetail::Basic ? 6 : detail == DATDetail::Medium ? 10 : 15;
    if (columns_ != columns) { clear(); columns_ = columns; }
}

void InfoTable::assign(const std::vector<Measurement>& measurements, size_t width, size_t height,
                       DATDetail detail)
{
    clear();
    try
    {
        setDetail(detail);
        if (!width || !height || measurements.size() >= size_t(INT32_MAX))
            throw std::invalid_argument("Invalid hull table dimensions");
        static constexpr const char* sources[] = {"luma", "r", "g", "b", "a"};
        rows_.resize(measurements.size());
        for (size_t i = 0; i < measurements.size(); ++i)
        {
            const auto& m = measurements[i];
            if (m.source >= 5) throw std::invalid_argument("Invalid hull source");
            const double x = (double(m.left) + m.right) * .5;
            const double y = (double(m.bottom) + m.top) * .5;
            const double w = double(m.right) - m.left, h = double(m.top) - m.bottom;
            auto& row = rows_[i];
            row[0] = std::to_string(i); row[1] = sources[m.source];
            row[2] = number(x / width); row[3] = number(y / height);
            row[4] = number(w / width); row[5] = number(h / height);
            if (columns_ >= 10)
            {
                row[6] = number(double(m.left) / width); row[7] = number(double(m.top) / height);
                row[8] = number(double(m.right) / width); row[9] = number(double(m.bottom) / height);
            }
            if (columns_ == 15)
            {
                row[10] = std::to_string(m.selectedPixels); row[11] = std::to_string(m.blobCount);
                row[12] = std::to_string(m.vertexCount); row[13] = number(m.area); row[14] = number(m.perimeter);
            }
        }
    }
    catch (...) { clear(); throw; }
}

const char* InfoTable::cell(int32_t row, int32_t column) const noexcept
{
    if (row < 0 || column < 0 || column >= columns_ || row >= rowCount()) return "";
    return row == 0 ? headers[column] : rows_[size_t(row - 1)][column].c_str();
}
}
