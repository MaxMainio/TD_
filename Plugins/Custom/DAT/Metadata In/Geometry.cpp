#include "Metadata.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Metadata {
namespace {
constexpr size_t headerLimit = 1024 * 1024;
std::string lower(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string number(double value) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return s.str();
}
bool numeric(const std::string& s, double& value) {
    std::istringstream stream(s); stream.imbue(std::locale::classic());
    stream >> value;
    return stream && (stream >> std::ws).eof() && std::isfinite(value);
}
uint64_t count(const std::string& s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid geometry count");
    size_t used = 0;
    auto n = std::stoull(s, &used);
    if (used != s.size()) throw std::runtime_error("Invalid geometry count");
    return n;
}
bool line(std::istream& in, std::string& out, const Context& ctx) {
    ctx.check();
    std::array<char, 65537> buf{};
    in.getline(buf.data(), std::streamsize(buf.size()));
    if (in.bad() || (in.fail() && !in.eof())) throw std::runtime_error("Geometry line exceeds 64 KiB or cannot be read");
    if (in.gcount() == 0) return false;
    const size_t length = size_t(in.gcount()) - (in.eof() ? 0 : 1);
    out.assign(buf.data(), length);
    if (out.find('\0') != std::string::npos) throw std::runtime_error("Null byte in geometry text");
    if (!out.empty() && out.back() == '\r') out.pop_back();
    return true;
}
std::vector<std::string> tokens(const std::string& value, char delimiter = ' ') {
    std::vector<std::string> out;
    std::string cell;
    char quote = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (quote) {
            if (c == quote) {
                if (i + 1 < value.size() && value[i+1] == quote) { cell += c; ++i; }
                else quote = 0;
            } else cell += c;
        } else if ((c == '"' || c == '\'') && cell.empty()) quote = c;
        else if ((delimiter == ' ' && (c == ' ' || c == '\t')) || c == delimiter) {
            if (delimiter != ' ' || !cell.empty()) out.push_back(cell);
            cell.clear();
        } else cell += c;
    }
    if (quote) throw std::runtime_error("Unterminated quoted geometry field");
    if (delimiter != ' ' || !cell.empty()) out.push_back(cell);
    return out;
}
char delimiter(const std::string& s) {
    char quote = 0;
    bool tab = false;
    for (char c : s) {
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == ',') return ',';
        else if (c == '\t') tab = true;
    }
    return tab ? '\t' : ' ';
}
void add(Table& table, const std::string& key, const std::string& value, bool computed = false) {
    table.add("geometry." + key, value, computed ? "Calculated" : "Stored", "geometry");
}
struct Bounds {
    std::array<double, 3> min{{INFINITY, INFINITY, INFINITY}}, max{{-INFINITY, -INFINITY, -INFINITY}};
    uint64_t valid = 0;
    void accept(const std::array<double, 3>& p) {
        if (!std::all_of(p.begin(), p.end(), [](double v) { return std::isfinite(v); })) return;
        ++valid;
        for (int i = 0; i < 3; ++i) { min[i] = std::min(min[i], p[i]); max[i] = std::max(max[i], p[i]); }
    }
    void write(Table& table) const {
        add(table, "calculated.finite_points", std::to_string(valid), true);
        if (!valid) return;
        for (int i = 0; i < 3; ++i) {
            std::string axis(1, "xyz"[i]);
            add(table, "calculated.bounds.min_" + axis, number(min[i]), true);
            add(table, "calculated.bounds.max_" + axis, number(max[i]), true);
        }
        add(table, "calculated.bounds.units", "file_coordinates", true);
    }
};
struct Prop { std::string name, type, listType; };
struct Element { std::string name; uint64_t count; std::vector<Prop> props; };
size_t typeSize(const std::string& type) {
    if (type == "char" || type == "uchar" || type == "int8" || type == "uint8") return 1;
    if (type == "short" || type == "ushort" || type == "int16" || type == "uint16") return 2;
    if (type == "int" || type == "uint" || type == "float" || type == "int32" || type == "uint32" || type == "float32") return 4;
    if (type == "double" || type == "float64") return 8;
    throw std::runtime_error("Unsupported PLY scalar type: " + type);
}
double scalar(std::istream& in, const std::string& type, bool ascii, bool big) {
    if (ascii) {
        std::string s;
        // Bounded numeric tokens; reading a hostile token must not allocate without limit.
        in >> std::ws;
        while (in && in.peek() != EOF && !std::isspace(static_cast<unsigned char>(in.peek()))) {
            if (s.size() == 128) throw std::runtime_error("PLY numeric token too long");
            s += char(in.get());
        }
        double value;
        if (!numeric(s, value)) throw std::runtime_error("Invalid or nonfinite PLY value");
        return value;
    }
    const size_t size = typeSize(type);
    uint64_t bits = 0;
    std::array<unsigned char, 8> data{};
    in.read(reinterpret_cast<char*>(data.data()), std::streamsize(size));
    if (!in) throw std::runtime_error("Truncated binary PLY");
    for (size_t i = 0; i < size; ++i) bits |= uint64_t(data[i]) << (8 * (big ? size - 1 - i : i));
    if (type == "float" || type == "float32") { uint32_t v = uint32_t(bits); float f; std::memcpy(&f, &v, 4); return f; }
    if (type == "double" || type == "float64") { double d; std::memcpy(&d, &bits, 8); return d; }
    bool signedType = type == "char" || type == "short" || type == "int" || type == "int8" || type == "int16" || type == "int32";
    if (signedType && (bits & (uint64_t(1) << (size*8 - 1)))) return double(int64_t(bits) - (int64_t(1) << (size*8)));
    return double(bits);
}
void ply(const Request& request, Table& table, const Context& ctx, std::istream& in) {
    std::string s, format;
    if (!line(in, s, ctx) || s != "ply") throw std::runtime_error("Invalid PLY signature");
    size_t bytes = 4, comments = 0;
    std::vector<Element> elements;
    bool ended = false;
    while (line(in, s, ctx)) {
        bytes += s.size() + 1;
        if (bytes > headerLimit) throw std::runtime_error("PLY header exceeds 1 MiB");
        auto t = tokens(s);
        if (t.empty()) continue;
        if (t[0] == "end_header") { ended = true; break; }
        if (t[0] == "format" && t.size() == 3) {
            format = t[1]; add(table, "format", format); add(table, "version", t[2]);
            if (t[2] != "1.0") throw std::runtime_error("Unsupported PLY version");
        } else if (t[0] == "element" && t.size() == 3) {
            elements.push_back({t[1], count(t[2]), {}});
            add(table, "elements[" + std::to_string(elements.size()-1) + "].name", t[1]);
            add(table, "elements[" + std::to_string(elements.size()-1) + "].count", t[2]);
        } else if (t[0] == "property" && !elements.empty()) {
            Prop p;
            if (t.size() == 3) p = {t[2], t[1], ""};
            else if (t.size() == 5 && t[1] == "list") p = {t[4], t[3], t[2]};
            else throw std::runtime_error("Invalid PLY property");
            typeSize(p.type); if (!p.listType.empty()) typeSize(p.listType);
            auto& element = elements.back();
            std::string key = "elements[" + std::to_string(elements.size()-1) + "].properties[" + std::to_string(element.props.size()) + "]";
            add(table, key + ".name", p.name); add(table, key + ".type", p.type);
            if (!p.listType.empty()) add(table, key + ".list_count_type", p.listType);
            element.props.push_back(std::move(p));
        } else if (t[0] == "comment" || t[0] == "obj_info") {
            add(table, "comments[" + std::to_string(comments++) + "]", s.substr(t[0].size()));
        } else throw std::runtime_error("Invalid PLY header declaration");
    }
    if (!ended || (format != "ascii" && format != "binary_little_endian" && format != "binary_big_endian"))
        throw std::runtime_error("Incomplete or unsupported PLY header");
    if (!request.deep) return;
    Bounds bounds;
    bool coordinates = false;
    for (const auto& element : elements) {
        if (element.props.empty() && element.count) throw std::runtime_error("PLY element has no properties");
        bool xyz = element.name == "vertex";
        for (const char* axis : {"x", "y", "z"})
            xyz = xyz && std::any_of(element.props.begin(), element.props.end(), [&](const Prop& p) { return p.name == axis && p.listType.empty(); });
        coordinates = coordinates || xyz;
        for (uint64_t i = 0; i < element.count; ++i) {
            ctx.check(); std::array<double,3> point{{NAN,NAN,NAN}};
            for (const auto& prop : element.props) {
                uint64_t n = 1;
                if (!prop.listType.empty()) {
                    double v = scalar(in, prop.listType, format == "ascii", format == "binary_big_endian");
                    if (!std::isfinite(v) || v < 0 || v > 1000000 || std::floor(v) != v) throw std::runtime_error("Invalid PLY list length");
                    n = uint64_t(v);
                }
                for (uint64_t j = 0; j < n; ++j) {
                    if ((j & 1023) == 0) ctx.check();
                    double v = scalar(in, prop.type, format == "ascii", format == "binary_big_endian");
                    if (xyz && prop.listType.empty()) {
                        if (prop.name == "x") point[0] = v;
                        if (prop.name == "y") point[1] = v;
                        if (prop.name == "z") point[2] = v;
                    }
                }
            }
            if (xyz) bounds.accept(point);
        }
    }
    if (coordinates) bounds.write(table);
}
void obj(const Request& request, Table& table, const Context& ctx, std::istream& in) {
    std::string s;
    size_t bytes = 0, comments = 0, declarations = 0;
    uint64_t vertices = 0, faces = 0, normals = 0, uv = 0;
    Bounds bounds;
    while (line(in, s, ctx)) {
        bytes += s.size()+1;
        if (!request.deep && bytes > headerLimit) { table.warning("OBJ prefix inspection limited to 1 MiB; enable Deep Scan for complete declarations/counts"); break; }
        auto start = s.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        s.erase(0, start);
        if (s[0] == '#') { add(table, "comments[" + std::to_string(comments++) + "]", s.substr(1)); continue; }
        auto t = tokens(s);
        if (t.empty()) continue;
        if (t[0] == "v") {
            ++vertices;
            if (request.deep) {
                std::array<double,3> p;
                if (t.size() < 4 || !numeric(t[1],p[0]) || !numeric(t[2],p[1]) || !numeric(t[3],p[2]))
                    throw std::runtime_error("Invalid OBJ vertex coordinates");
                bounds.accept(p);
            }
        } else if (t[0] == "f") ++faces;
        else if (t[0] == "vn") ++normals;
        else if (t[0] == "vt") ++uv;
        else if (t[0] == "o" || t[0] == "g" || t[0] == "mtllib" || t[0] == "usemtl")
            add(table, "declarations[" + std::to_string(declarations++) + "]." + t[0], s.substr(t[0].size()));
    }
    add(table, "format", "obj");
    add(table, "inspection", request.deep ? "full" : "prefix");
    if (request.deep) {
        add(table, "calculated.vertex_count", std::to_string(vertices), true);
        add(table, "calculated.face_count", std::to_string(faces), true);
        add(table, "calculated.normal_count", std::to_string(normals), true);
        add(table, "calculated.texcoord_count", std::to_string(uv), true);
        bounds.write(table);
    }
}
void textPoints(const Request& request, Table& table, const Context& ctx, std::istream& in, const std::string& extension) {
    std::string s;
    size_t bytes = 0, comments = 0;
    bool first = true, fieldsKnown = false;
    char sep = ' ';
    std::vector<std::string> fields;
    std::array<int,3> xyz{{-1,-1,-1}};
    uint64_t points = 0;
    Bounds bounds;
    while (line(in, s, ctx)) {
        bytes += s.size()+1;
        if (!request.deep && bytes > headerLimit) throw std::runtime_error("Point-file header exceeds 1 MiB");
        auto start = s.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        if (s[start] == '#') { add(table, "comments[" + std::to_string(comments++) + "]", s.substr(start+1)); continue; }
        if (!fieldsKnown) sep = delimiter(s);
        auto t = tokens(s, sep);
        if (first && t.size() == 1 && !t[0].empty() && t[0].find_first_not_of("0123456789") == std::string::npos) {
            add(table, "declared_point_count", std::to_string(count(t[0]))); first = false; continue;
        }
        first = false;
        if (!fieldsKnown) {
            double unused;
            bool header = !std::all_of(t.begin(), t.end(), [&](const std::string& v) { return numeric(v, unused); });
            for (size_t i=0; i<t.size(); ++i) {
                std::string name = header ? t[i] : "field" + std::to_string(i);
                fields.push_back(name);
                add(table, "fields[" + std::to_string(i) + "].name", name);
                auto coordinate = lower(name);
                auto begin = coordinate.find_first_not_of(" \t");
                auto end = coordinate.find_last_not_of(" \t");
                coordinate = begin == std::string::npos ? "" : coordinate.substr(begin, end-begin+1);
                for (int a=0; a<3; ++a) if (coordinate == std::string(1,"xyz"[a])) {
                    if (xyz[a] != -1) throw std::runtime_error("Ambiguous duplicate coordinate columns");
                    xyz[a] = int(i);
                }
            }
            if (!header && (extension == ".xyz" || extension == ".pts") && t.size() >= 3) xyz = {{0,1,2}};
            add(table, "delimiter", sep == ',' ? "comma" : sep == '\t' ? "tab" : "whitespace");
            fieldsKnown = true;
            if (!request.deep) break;
            if (header) continue;
        }
        if (t.size() != fields.size()) throw std::runtime_error("Inconsistent point-file column count");
        ++points;
        if (*std::min_element(xyz.begin(), xyz.end()) >= 0) {
            std::array<double,3> p;
            for (int a=0; a<3; ++a)
                if (!numeric(t[size_t(xyz[a])],p[a])) throw std::runtime_error("Invalid point-file coordinate");
            bounds.accept(p);
        }
    }
    add(table, "format", "text_points");
    if (!fieldsKnown) table.warning("No point fields found");
    if (request.deep) {
        add(table, "calculated.point_count", std::to_string(points), true);
        if (*std::min_element(xyz.begin(), xyz.end()) >= 0) bounds.write(table);
    }
}
}
bool readGeometry(const Request& request, Table& table, const Context& ctx) {
    std::string ext = lower(std::filesystem::path(request.path).extension().string());
    if (ext != ".ply" && ext != ".obj" && ext != ".xyz" && ext != ".pts" && ext != ".csv" && ext != ".txt") return false;
    std::ifstream in(request.path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open geometry file");
    if (ext == ".ply") ply(request, table, ctx, in);
    else if (ext == ".obj") obj(request, table, ctx, in);
    else textPoints(request, table, ctx, in, ext);
    return true;
}
}
