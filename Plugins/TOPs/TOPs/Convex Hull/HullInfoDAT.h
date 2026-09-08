#pragma once

#include "HullEngine.h"
#include <string>

namespace Hull
{
enum class DATDetail { Basic, Medium, Maximum };

// Owns the strings handed to TouchDesigner's Info DAT callbacks. No borrowed
// engine/image buffers and no image processing occur while reading the table.
class InfoTable
{
public:
    static constexpr int32_t maxColumns = 15;
    void setDetail(DATDetail detail);
    void assign(const std::vector<Measurement>& measurements, size_t width, size_t height,
                DATDetail detail = DATDetail::Basic);
    void clear() noexcept { rows_.clear(); }
    int32_t rowCount() const { return static_cast<int32_t>(rows_.size()) + 1; }
    int32_t columnCount() const { return columns_; }
    const char* cell(int32_t row, int32_t column) const noexcept;
private:
    int32_t columns_ = 6;
    std::vector<std::array<std::string, maxColumns>> rows_;
};
}
