#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ImageFile {
constexpr size_t MaxImageBytes = size_t(1024) * 1024 * 1024;
constexpr size_t BufferCount = 11;
constexpr uint32_t AllAuxiliaries = ((1u << BufferCount) - 1u) & ~1u;
// Public buffer indices are permanent, including when an image lacks a kind.
inline constexpr const char* Kinds[BufferCount] = {"color", "depth", "disparity", "portrait",
    "skin", "hair", "teeth", "glasses", "sky", "hdr_gain", "iso_gain"};
inline constexpr const char* Labels[BufferCount] = {"Color", "Depth", "Disparity", "Portrait Matte",
    "Skin", "Hair", "Teeth", "Glasses", "Sky", "Apple HDR Gain Map", "ISO Gain Map"};
struct Plane {
    uint32_t width=0, height=0, channels=0;
    // Bottom row first, interleaved float. Color is premultiplied linear sRGB.
    // Immutable shared storage lets a worker prepare one auxiliary without
    // copying the cached color image or the other completed planes.
    std::shared_ptr<const std::vector<float>> pixels;
    bool available() const { return width && height && channels; }
    bool present() const { return available() && pixels && !pixels->empty(); }
};
using Row = std::array<std::string,4>;
struct ImageEntry { size_t index=0; uint32_t width=0,height=0; std::string label; };
struct Request {
    std::string path, image="primary", decode="auto";
    bool automatic=true;
    double interval=1;
    uint32_t auxiliaryMask=AllAuxiliaries;
    bool sameContent(const Request& other) const;
};
struct Result {
    std::string path, status="idle", error;
    std::vector<std::string> warnings;
    std::vector<Row> rows;
    std::vector<ImageEntry> images;
    std::array<Plane,BufferCount> planes;
    // Validated native rasters, retained only for this file snapshot. Called on
    // the worker, never in a host cook. Color has no deferred preparation.
    std::array<std::function<Plane(const std::function<bool()>&)>,BufferCount> prepare;
    size_t primary=0, selected=0;
    bool selectionFallback=false;
    double readMs=0, prepareMs=0;
    uint64_t revision=0, sourceRevision=0, reads=0, preparations=0;
};
std::string cleanUTF8(const std::string& text);
std::string escaped(const std::string& key);
size_t imageElements(uint32_t width,uint32_t height,uint32_t channels);
void orientTopLeft(Plane& image,int orientation);
class Table {
public:
    explicit Table(Result& result):result_(result) {for(const auto& row:result.rows)for(const auto& cell:row)bytes_+=cell.size();}
    void add(std::string key,std::string value,std::string group,std::string source);
    void warning(const std::string& message);
    void finish();
    bool full() const { return capped_; }
private:
    Result& result_;
    size_t bytes_=0;
    bool capped_=false;
};
using Cancel = std::function<bool()>;
using Loader = std::function<std::shared_ptr<Result>(const Request&,const Cancel&)>;
std::shared_ptr<Result> decodeFile(const Request&,const Cancel&);
std::string fileIdentity(const std::string& path);
class Engine {
public:
    explicit Engine(Loader loader=decodeFile);
    ~Engine();
    Engine(const Engine&)=delete;
    void request(const Request&,bool refresh=false);
    std::shared_ptr<const Result> result() const;
    bool reading() const;
private:
    void work();
    Loader loader_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Request request_;
    std::shared_ptr<const Result> result_=std::make_shared<Result>();
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> stop_{false};
    bool pending_=false, reading_=false;
    uint64_t revision_=0, reads_=0;
    std::string identity_;
    std::thread worker_;
};
}
