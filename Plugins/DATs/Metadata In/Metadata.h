#pragma once
#include "vendor/json.hpp"
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

namespace Metadata {
using JSON = nlohmann::ordered_json;
using Clock = std::chrono::steady_clock;
struct Row { std::string key, value, group, source; };
struct Result {
    std::vector<Row> rows;
    std::vector<std::string> warnings;
    std::string status = "idle", path, error;
    std::string exifVersion, ffprobeVersion;
    uint64_t revision = 0, reads = 0;
    double elapsedMs = 0;
};
struct Request {
    std::string path, resources;
    bool deep = false, automatic = true;
    double interval = 1, timeout = 30;
    bool sameExtraction(const Request& b) const {
        return path == b.path && resources == b.resources && deep == b.deep && timeout == b.timeout;
    }
};
struct Cancelled {};
struct Context {
    std::function<bool()> cancelled;
    Clock::time_point deadline;
    void check() const;
};
struct ProcessResult { int exitCode = -1; std::string out, err; };
ProcessResult runProcess(const std::vector<std::string>& args, const Context& context);
std::string cleanUTF8(const std::string& text);
class Table {
public:
    explicit Table(Result& result) : result_(result) {}
    void add(std::string key, std::string value, std::string group, std::string source);
    void flatten(const JSON& value, const std::string& key, const std::string& group,
                 const std::string& source, int depth = 0);
    void warning(const std::string& message);
    void finish();
private:
    Result& result_;
    size_t bytes_ = 0;
    bool capped_ = false;
};
bool readGeometry(const Request&, Table&, const Context&);
Result extract(const Request&, const Context&);
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void request(const Request&, bool force = false);
    std::shared_ptr<const Result> result() const;
private:
    void work();
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> stop_{false};
    Request request_;
    bool pending_ = false;
    std::shared_ptr<const Result> result_;
    uint64_t revision_ = 0, reads_ = 0;
    std::thread worker_;
};
}
