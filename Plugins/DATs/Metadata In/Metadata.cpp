#include "Metadata.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

namespace Metadata {
namespace fs = std::filesystem;
namespace {
std::string fingerprint(const std::string& path) {
    struct stat s{};
    if (stat(path.c_str(), &s)) return "error:" + std::to_string(errno);
    return std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino) + ":" + std::to_string(s.st_size) + ":" +
           std::to_string(s.st_mtimespec.tv_sec) + ":" + std::to_string(s.st_mtimespec.tv_nsec) + ":" +
           std::to_string(s.st_ctimespec.tv_sec) + ":" + std::to_string(s.st_ctimespec.tv_nsec);
}
std::string timestamp(const struct timespec& t) {
    std::ostringstream s;
    s << t.tv_sec << '.' << std::setw(9) << std::setfill('0') << t.tv_nsec;
    return s.str();
}
JSON parse(const std::string& text) {
    return JSON::parse(text, [](int depth, JSON::parse_event_t, JSON&) {
        if (depth > 64) throw std::runtime_error("Reader JSON exceeds depth limit");
        return true;
    });
}
std::string extension(const std::string& path) {
    auto ext = fs::path(path).extension().string();
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return ext;
}
bool geometryExtension(const std::string& ext) {
    return ext == ".ply" || ext == ".obj" || ext == ".xyz" || ext == ".pts" || ext == ".csv" || ext == ".txt";
}
bool mediaExtension(const std::string& ext) {
    static const std::vector<std::string> media = {".mov", ".mp4", ".m4v", ".mkv", ".webm", ".avi", ".mpg", ".mpeg", ".mxf", ".mts", ".m2ts", ".wav", ".mp3", ".flac", ".aac", ".m4a", ".aiff", ".aif", ".ogg", ".opus", ".wma"};
    return std::find(media.begin(), media.end(), ext) != media.end();
}
}
Result extract(const Request& request, const Context& context) {
    Result result;
    result.path = request.path;
    Table table(result);
    auto start = Clock::now();
    struct stat s{};
    if (request.path.empty()) return result;
    context.check();
    if (request.path.find("://") != std::string::npos || request.path.rfind("vfs:", 0) == 0 || !fs::path(request.path).is_absolute()) {
        result.status = "error"; result.error = "Select one absolute local filesystem file"; return result;
    }
    if (stat(request.path.c_str(), &s)) {
        result.status = "error";
        result.error = "Cannot access file: " + std::string(strerror(errno));
        if (request.path.find_first_of("*?[]") != std::string::npos || request.path.find('%') != std::string::npos)
            result.error += "; sequence patterns are unsupported, select one concrete file";
        return result;
    }
    if (!S_ISREG(s.st_mode)) {
        result.status = "error"; result.error = "Select one regular file; directories and sequence folders are unsupported"; return result;
    }
    std::ifstream accessible(request.path, std::ios::binary);
    if (!accessible) { result.status = "error"; result.error = "File is not readable"; return result; }
    accessible.close();
    table.add("file.path", request.path, "Filesystem", "filesystem");
    table.add("file.name", fs::path(request.path).filename().string(), "Filesystem", "filesystem");
    const auto ext = extension(request.path);
    table.add("file.extension", ext, "Filesystem", "filesystem");
    table.add("file.size_bytes", std::to_string(s.st_size), "Filesystem", "filesystem");
    table.add("file.modified_unix_seconds", timestamp(s.st_mtimespec), "Filesystem", "filesystem");
    table.add("file.created_unix_seconds", timestamp(s.st_birthtimespec), "Filesystem", "filesystem");
    table.add("file.changed_unix_seconds", timestamp(s.st_ctimespec), "Filesystem", "filesystem");
    try {
        std::ifstream versions(fs::path(request.resources) / "versions.json");
        JSON info; versions >> info;
        result.exifVersion = info.value("exiftool", "unknown");
        result.ffprobeVersion = info.value("ffmpeg", "unknown");
    } catch (...) { table.warning("Bundled reader version manifest unavailable"); }
    bool recognized = false, media = mediaExtension(ext);
    if (geometryExtension(ext)) {
        try { recognized = readGeometry(request, table, context); }
        catch (const Cancelled&) { throw; }
        catch (const std::exception& e) { table.warning("Geometry: " + std::string(e.what())); }
    } else {
        try {
            std::vector<std::string> args = {"/usr/bin/perl", (fs::path(request.resources) / "exiftool/exiftool").string(),
                "-config", "", "-json", "-G0:1:3:4", "-struct", "-n", "-a", "-u", "-api", "LargeFileSupport=1"};
            if (ext == ".exr") args.push_back("-ee");
            args.push_back("--"); args.push_back(request.path);
            auto process = runProcess(args, context);
            if (process.out.empty()) throw std::runtime_error(process.err.empty() ? "No output from bundled ExifTool" : process.err);
            JSON info = parse(process.out);
            if (!info.is_array() || info.size() != 1 || !info[0].is_object()) throw std::runtime_error("Invalid ExifTool response");
            for (auto it = info[0].begin(); it != info[0].end(); ++it) {
                auto key = it.key();
                if (key == "SourceFile" || key.find("ExifToolVersion") != std::string::npos) continue;
                if (key.find(":Warning") != std::string::npos || key.find(":Error") != std::string::npos) {
                    table.warning("ExifTool: " + (it.value().is_string() ? it.value().get<std::string>() : it.value().dump())); continue;
                }
                if (key.find(":FileType") != std::string::npos) recognized = true;
                if (key.find(":MIMEType") != std::string::npos && it.value().is_string()) {
                    auto mime = it.value().get<std::string>();
                    media = media || mime.rfind("audio/",0) == 0 || mime.rfind("video/",0) == 0;
                }
                // Tool/System fields duplicate our filesystem and version information.
                if (key.rfind("File:System:", 0) == 0 || key.rfind("ExifTool:", 0) == 0) continue;
                std::string group = key.substr(0, key.rfind(':'));
                table.flatten(it.value(), "exiftool." + key, group, "exiftool");
            }
            if (process.exitCode) table.warning("ExifTool exited with code " + std::to_string(process.exitCode));
            if (!process.err.empty()) table.warning("ExifTool: " + process.err);
        } catch (const Cancelled&) { throw; }
        catch (const std::exception& e) { table.warning("ExifTool: " + std::string(e.what())); }
    }
    if (media) {
        try {
            auto process = runProcess({(fs::path(request.resources) / "ffprobe").string(), "-v", "error",
                "-protocol_whitelist", "file", "-probesize", "5000000", "-analyzeduration", "5000000",
                "-show_format", "-show_streams", "-show_chapters", "-of", "json", "-i", request.path}, context);
            if (process.exitCode) throw std::runtime_error(process.err.empty() ? "Media probe failed" : process.err);
            auto info = parse(process.out);
            if (!info.is_object()) throw std::runtime_error("Invalid ffprobe response");
            for (auto it = info.begin(); it != info.end(); ++it)
                table.flatten(it.value(), "media." + it.key(), it.key(), "ffprobe");
            recognized = true;
            if (!process.err.empty()) table.warning("ffprobe: " + process.err);
        } catch (const Cancelled&) { throw; }
        catch (const std::exception& e) { table.warning("ffprobe: " + std::string(e.what())); }
    }
    if (!recognized) table.warning("Limited coverage: no reader recognized this file; filesystem information is available");
    table.finish();
    result.status = result.warnings.empty() ? "ready" : "partial";
    result.elapsedMs = std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    return result;
}
Engine::Engine() : result_(std::make_shared<Result>()), worker_(&Engine::work, this) {}
Engine::~Engine() {
    stop_ = true; ++generation_; changed_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void Engine::request(const Request& input, bool force) {
    std::lock_guard<std::mutex> lock(mutex_);
    Request r = input;
    r.interval = std::isfinite(r.interval) ? std::clamp(r.interval, .1, 86400.) : 1;
    r.timeout = std::isfinite(r.timeout) ? std::clamp(r.timeout, .1, 86400.) : 30;
    const bool changed = !r.sameExtraction(request_);
    bool scheduleChanged = r.automatic != request_.automatic || r.interval != request_.interval;
    request_ = std::move(r);
    if (scheduleChanged && request_.automatic) pending_ = true;
    if (changed || force) {
        ++generation_;
        pending_ = true;
        auto next = std::make_shared<Result>();
        next->path = request_.path;
        next->status = request_.path.empty() ? "idle" : "reading";
        next->revision = ++revision_; next->reads = reads_;
        result_ = next;
    }
    if (changed || force || scheduleChanged) changed_.notify_all();
}
std::shared_ptr<const Result> Engine::result() const {
    std::lock_guard<std::mutex> lock(mutex_); return result_;
}
void Engine::work() {
    uint64_t previousGeneration = UINT64_MAX;
    std::string previousFingerprint;
    auto nextCheck = Clock::now();
    while (!stop_) {
        Request r; uint64_t generation;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!stop_ && !pending_) {
                if (request_.automatic && !request_.path.empty()) {
                    if (Clock::now() >= nextCheck) break;
                    changed_.wait_until(lock, nextCheck);
                } else changed_.wait(lock);
            }
            if (stop_) return;
            r = request_; generation = generation_.load(); pending_ = false;
        }
        nextCheck = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(r.interval));
        if (r.path.empty()) continue;
        const auto before = fingerprint(r.path);
        if (generation == previousGeneration && before == previousFingerprint) continue;
        Context ctx{[this,generation] { return stop_ || generation_.load() != generation; },
                    Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(r.timeout))};
        Result value;
        try { value = extract(r, ctx); }
        catch (const Cancelled&) { continue; }
        catch (const std::exception& e) { value.path = r.path; value.status = "error"; value.error = e.what(); }
        catch (...) { value.path = r.path; value.status = "error"; value.error = "Unexpected metadata reader failure"; }
        if (ctx.cancelled()) continue;
        const auto after = fingerprint(r.path);
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_.load() != generation || stop_) continue;
        ++reads_;
        if (before != after) {
            // Never publish a result combining two versions of a changing file.
            pending_ = true;
            auto retry = std::make_shared<Result>();
            retry->path = r.path; retry->status = "reading"; retry->reads = reads_; retry->revision = ++revision_;
            retry->warnings.push_back("File changed during extraction; retrying"); result_ = retry;
            continue;
        }
        previousGeneration = generation; previousFingerprint = after;
        value.reads = reads_; value.revision = ++revision_;
        result_ = std::make_shared<Result>(std::move(value));
    }
}
}
