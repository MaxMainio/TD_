#include "Metadata.h"
#include <algorithm>
#include <set>

namespace Metadata {
std::string cleanUTF8(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i];
        size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = n && i + n <= text.size();
        for (size_t j = 1; valid && j < n; ++j) valid = (static_cast<unsigned char>(text[i+j]) & 0xc0) == 0x80;
        if (valid && n >= 3) {
            unsigned char b = text[i+1];
            valid = !(c == 0xe0 && b < 0xa0) && !(c == 0xed && b >= 0xa0)
                 && !(c == 0xf0 && b < 0x90) && !(c == 0xf4 && b >= 0x90);
        }
        if (!valid) { out += "\xef\xbf\xbd"; ++i; }
        else if (c == 0) { out += "\\u0000"; ++i; }
        else { out.append(text, i, n); i += n; }
    }
    return out;
}
void Table::warning(const std::string& message) {
    auto safe = cleanUTF8(message.substr(0, 2048));
    if (result_.warnings.size() < 64 && std::find(result_.warnings.begin(), result_.warnings.end(), safe) == result_.warnings.end())
        result_.warnings.push_back(safe);
}
void Table::add(std::string key, std::string value, std::string group, std::string source) {
    if (result_.rows.size() >= 20000 || bytes_ >= 16u * 1024 * 1024) {
        if (!capped_) { warning("Metadata table truncated at 20000 rows or 16 MiB"); capped_ = true; }
        return;
    }
    key = cleanUTF8(key); value = cleanUTF8(value); group = cleanUTF8(group); source = cleanUTF8(source);
    for (auto* cell : {&key, &value, &group, &source}) {
        if (cell->size() <= 65536) continue;
        size_t length = 65536;
        while (length && (static_cast<unsigned char>((*cell)[length]) & 0xc0) == 0x80) --length;
        cell->resize(length);
        warning("A metadata cell was truncated at 64 KiB");
    }
    const size_t bytes = key.size() + value.size() + group.size() + source.size();
    if (bytes_ + bytes > 16u * 1024 * 1024) {
        warning("Metadata table truncated at 20000 rows or 16 MiB"); capped_ = true; return;
    }
    bytes_ += bytes;
    result_.rows.push_back({std::move(key), std::move(value), std::move(group), std::move(source)});
}
void Table::flatten(const JSON& value, const std::string& key, const std::string& group, const std::string& source, int depth) {
    if (depth > 32) { warning("Nested metadata truncated at depth 32"); return; }
    if (capped_) return;
    if (value.is_primitive() || value.empty()) {
        add(key, value.is_string() ? value.get<std::string>() : value.dump(-1, ' ', false, JSON::error_handler_t::replace), group, source);
        return;
    }
    size_t index = 0;
    for (auto it = value.begin(); it != value.end(); ++it) {
        std::string suffix;
        if (value.is_array()) suffix = "[" + std::to_string(index++) + "]";
        else {
            suffix = ".";
            for (char c : it.key()) { if (c == '.' || c == '[' || c == ']' || c == '\\') suffix += '\\'; suffix += c; }
        }
        flatten(it.value(), key + suffix, group, source, depth + 1);
    }
}
void Table::finish() {
    std::stable_sort(result_.rows.begin(), result_.rows.end(), [](const Row& a, const Row& b) { return a.key < b.key; });
    std::set<std::string> seen;
    for (auto& row : result_.rows) {
        auto original = row.key;
        size_t occurrence = 1;
        while (!seen.insert(row.key).second) row.key = original + "#" + std::to_string(++occurrence);
    }
}
}
