#include "HullEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Hull
{
namespace
{
int64_t cross(Point a, Point b, Point c)
{
    return int64_t(b.x - a.x) * (c.y - a.y) - int64_t(b.y - a.y) * (c.x - a.x);
}
double unit(double value, double fallback)
{
    return std::isfinite(value) ? std::clamp(value, 0., 1.) : fallback;
}
}

void convexHull(std::vector<Point>& points, std::vector<Point>& hull)
{
    std::sort(points.begin(), points.end(), [](Point a, Point b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    hull.clear();
    if (points.size() < 2) { hull = points; return; }
    for (Point p : points)
    {
        while (hull.size() > 1 && cross(hull[hull.size()-2], hull.back(), p) <= 0) hull.pop_back();
        hull.push_back(p);
    }
    const size_t lower = hull.size();
    for (size_t i = points.size()-1; i-- > 0;)
    {
        Point p = points[i];
        while (hull.size() > lower && cross(hull[hull.size()-2], hull.back(), p) <= 0) hull.pop_back();
        hull.push_back(p);
    }
    hull.pop_back();
}


Engine::~Engine()
{
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    start_.notify_all();
    for (auto& t : workers_) t.join();
}

void Engine::startWorkers()
{
    if (!workers_.empty()) return;
    const unsigned count = std::min(maxWorkers_, std::max(1u, std::thread::hardware_concurrency())-1);
    // No allocation may occur between constructing a joinable thread and storing it.
    workers_.reserve(count);
    for (unsigned i = 0; i < count; ++i) workers_.emplace_back(&Engine::worker, this);
}

void Engine::worker()
{
    std::unique_lock<std::mutex> lock(mutex_);
    uint64_t seen = 0;
    for (;;)
    {
        start_.wait(lock, [&] { return stopping_ || generation_ != seen; });
        if (stopping_) return;
        seen = generation_;
        lock.unlock();
        jobs();
        lock.lock();
        if (--pending_ == 0) done_.notify_one();
    }
}

void Engine::jobs() noexcept
{
    for (unsigned job = nextJob_.fetch_add(1); job < jobCount_; job = nextJob_.fetch_add(1))
    {
        try { processPlane(job); }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!failure_) failure_ = std::current_exception();
        }
    }
}

Image Engine::process(Input input, Settings settings)
{
    measurements_.clear();
    try { return processImpl(input, settings); }
    catch (...) { measurements_.clear(); stats_ = {}; throw; }
}

Image Engine::processImpl(Input input, Settings settings)
{
    stats_ = {};
    if (!input.data || !input.width || !input.height ||
        input.width > INT32_MAX || input.height > INT32_MAX ||
        input.width > size_t(INT32_MAX) / input.height ||
        input.stride < input.width * 4 || input.stride > SIZE_MAX / input.height / sizeof(float))
        throw std::invalid_argument("Invalid or excessively large RGBA input image");
    if (unsigned(settings.source) > 6 || unsigned(settings.grouping) > 2 ||
        unsigned(settings.view) > 2 || unsigned(settings.alpha) > 2)
        throw std::invalid_argument("Invalid convex hull settings");
    settings.threshold = unit(settings.threshold, .5);
    for (auto& t : settings.thresholds) t = unit(t, .5);
    settings.alphaCutoff = unit(settings.alphaCutoff, 0);
    settings.minimumArea = std::max(uint64_t(1), settings.minimumArea);
    settings.outlineWidth = std::clamp(settings.outlineWidth, 1, INT32_MAX);
    input_ = input;
    settings_ = settings;
    jobCount_ = settings.source == Source::RGBA ? 4 : settings.source == Source::RGB ? 3 : 1;
    for (unsigned i = 0; i < jobCount_; ++i)
        sources_[i] = jobCount_ == 1 ? unsigned(settings.source) : i+1;
    failure_ = nullptr;
    nextJob_ = 0;
    if (jobCount_ > 1 && input.width * input.height >= 4096 && maxWorkers_)
    {
        startWorkers();
        { std::lock_guard<std::mutex> lock(mutex_); pending_ = unsigned(workers_.size()); ++generation_; }
        start_.notify_all();
        jobs();
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
    }
    else jobs();
    if (failure_) { stats_ = {}; std::rethrow_exception(failure_); }
    // Publish only after all jobs complete, in source then component scan order.
    for (unsigned job = 0; job < jobCount_; ++job)
        measurements_.insert(measurements_.end(), planes_[job].measurements.begin(),
                             planes_[job].measurements.end());
    Image image{input.width, input.height, {}, input, settings.alpha};
    for (unsigned c = 0; c < 3; ++c) image.channels[c] = planes_[jobCount_ == 1 ? 0 : c].mask.data();
    if (jobCount_ == 4) image.channels[3] = planes_[3].mask.data();
    return image;
}

uint32_t Engine::root(Plane& plane, uint32_t i)
{
    while (plane.runs[i].parent != i)
    {
        plane.runs[i].parent = plane.runs[plane.runs[i].parent].parent;
        i = plane.runs[i].parent;
    }
    return i;
}

void Engine::processPlane(unsigned job)
{
    Plane& p = planes_[job];
    const unsigned source = sources_[job];
    auto& stats = stats_[source];
    const int w = int(input_.width), h = int(input_.height);
    p.mask.assign(size_t(w)*h, 0);
    p.runs.clear();
    p.measurements.clear();
    const double threshold = settings_.independent && jobCount_ > 1 ? settings_.thresholds[source-1] : settings_.threshold;
    size_t previousBegin = 0, previousEnd = 0;
    for (int y = 0; y < h; ++y)
    {
        const size_t rowBegin = p.runs.size();
        size_t previous = previousBegin;
        const float* row = input_.data + size_t(y)*input_.stride;
        auto selected = [&](int x) {
            const float* v = row + size_t(x)*4;
            if (settings_.ignoreTransparent && (!std::isfinite(v[3]) || !(v[3] > settings_.alphaCutoff))) return false;
            // Use the same separate float32 multiply/add order as Dither's luminance.
            float value = source ? v[source-1] : .2126f*v[0] + .7152f*v[1] + .0722f*v[2];
            return std::isfinite(value) && (settings_.below ? value <= threshold : value > threshold);
        };
        for (int x = 0; x < w;)
        {
            if (!selected(x)) { ++x; continue; }
            const int left = x++;
            while (x < w && selected(x)) ++x;
            const int right = x-1;
            const uint32_t id = uint32_t(p.runs.size());
            p.runs.push_back({left, right, y, id});
            stats.selectedPixels += uint64_t(right-left+1);
            while (previous < previousEnd && p.runs[previous].right < left-1) ++previous;
            for (size_t j = previous; j < previousEnd && p.runs[j].left <= right+1; ++j)
            {
                const uint32_t a = root(p, id), b = root(p, uint32_t(j));
                p.runs[std::max(a,b)].parent = std::min(a,b);
            }
        }
        previousBegin = rowBegin;
        previousEnd = p.runs.size();
    }
    for (uint32_t i = 0; i < p.runs.size(); ++i)
    {
        const uint32_t r = root(p, i);
        p.runs[i].parent = r;
        p.runs[r].area += uint64_t(p.runs[i].right-p.runs[i].left+1);
        p.runs[i].next = p.runs[r].head;
        p.runs[r].head = i;
    }
    uint32_t largest = none;
    for (uint32_t i = 0; i < p.runs.size(); ++i)
        if (p.runs[i].parent == i && p.runs[i].area >= settings_.minimumArea &&
            (largest == none || p.runs[i].area > p.runs[largest].area)) largest = i;
    auto retained = [&](uint32_t r) {
        return p.runs[r].area >= settings_.minimumArea &&
            (settings_.grouping != Grouping::LargestBlob || r == largest);
    };
    for (uint32_t i = 0; i < p.runs.size(); ++i)
        if (p.runs[i].parent == i && retained(i))
        {
            ++stats.blobs;
            stats.retainedPixels += p.runs[i].area;
        }
    stats.hulls = settings_.grouping == Grouping::Global ? uint64_t(stats.blobs != 0) : stats.blobs;
    if (settings_.view == View::Selection)
    {
        for (const Run& run : p.runs) if (retained(run.parent)) span(p, run.y, run.left, run.right);
    }
    if (settings_.grouping == Grouping::PerBlob)
    {
        for (uint32_t i = 0; i < p.runs.size(); ++i)
        {
            if (p.runs[i].parent != i || !retained(i)) continue;
            p.points.clear();
            int y = -1, left = w, right = -1;
            auto flushRow = [&] {
                if (y < 0) return;
                p.points.push_back({left,y});
                if (left != right) p.points.push_back({right,y});
            };
            // Each component's linked runs are in reverse scan order. Multiple
            // runs on the same row contribute only their two outermost pixels.
            for (uint32_t j = p.runs[i].head; j != none; j = p.runs[j].next)
            {
                const Run& r = p.runs[j];
                if (r.y != y) { flushRow(); y = r.y; left = w; right = -1; }
                left = std::min(left,r.left);
                right = std::max(right,r.right);
            }
            flushRow();
            convexHull(p.points, p.hull);
            p.measurements.push_back(measureHull(p.hull, source, p.runs[i].area, 1));
            if (settings_.view != View::Selection) render(p);
        }
    }
    else if (stats.hulls)
    {
        // All interior samples on a retained row are redundant for a global hull.
        p.rowLeft.assign(h,w);
        p.rowRight.assign(h,-1);
        for (const Run& r : p.runs) if (retained(r.parent))
        {
            p.rowLeft[r.y] = std::min(p.rowLeft[r.y], r.left);
            p.rowRight[r.y] = std::max(p.rowRight[r.y], r.right);
        }
        p.points.clear();
        for (int y = 0; y < h; ++y) if (p.rowRight[y] >= 0)
        {
            p.points.push_back({p.rowLeft[y], y});
            if (p.rowRight[y] != p.rowLeft[y]) p.points.push_back({p.rowRight[y], y});
        }
        convexHull(p.points,p.hull);
        p.measurements.push_back(measureHull(p.hull, source, stats.retainedPixels, stats.blobs));
        if (settings_.view != View::Selection) render(p);
    }
    for (uint8_t v : p.mask) stats.outputPixels += v;
}

void Engine::span(Plane& p, int y, int left, int right)
{
    if (y < 0 || y >= int(input_.height)) return;
    left = std::max(left,0);
    right = std::min(right,int(input_.width)-1);
    if (left <= right) std::fill(p.mask.begin()+size_t(y)*input_.width+left,
                                p.mask.begin()+size_t(y)*input_.width+right+1, uint8_t(1));
}

void Engine::line(Plane& p, Point a, Point b, int width)
{
    if (width == 1)
    {
        // Major-axis integer interpolation. Halfway samples round toward +x/+y,
        // independent of endpoint ordering, including negative-slope edges.
        const bool swap = std::abs(b.y-a.y) > std::abs(b.x-a.x);
        if (swap) { std::swap(a.x,a.y); std::swap(b.x,b.y); }
        if (a.x > b.x) std::swap(a,b);
        const int64_t d = b.x-a.x;
        for (int x = a.x; x <= b.x; ++x)
        {
            const int y = d ? int((int64_t(a.y)*(b.x-x)+int64_t(b.y)*(x-a.x)+d/2)/d) : a.y;
            if (swap) span(p,x,y,y); else span(p,y,x,x);
        }
        return;
    }
    // Scanline rasterization of a round-capped stroke (no supersampling).
    // Even widths are shifted half a pixel toward +x/+y to occupy exactly the
    // requested number of pixels on horizontal/vertical segments.
    const double shift = width % 2 ? 0 : .5;
    const double ax = a.x+shift, ay = a.y+shift, bx = b.x+shift, by = b.y+shift;
    const double radius = width*.5, dx = bx-ax, dy = by-ay, length = std::hypot(dx,dy);
    const double nx = length ? -dy/length*radius : 0, ny = length ? dx/length*radius : 0;
    const std::array<std::array<double,2>,4> corners{{{{ax+nx,ay+ny}},{{bx+nx,by+ny}},{{bx-nx,by-ny}},{{ax-nx,ay-ny}}}};
    const int low = int(std::max(0.,std::ceil(std::min(ay,by)-radius)));
    const int high = int(std::min(double(input_.height-1),std::floor(std::max(ay,by)+radius)));
    for (int y = low; y <= high; ++y)
    {
        double left = double(input_.width), right = -1;
        auto include = [&](double x) { left = std::min(left,x); right = std::max(right,x); };
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto& c = corners[i]; const auto& d = corners[(i+1)%4];
            if (y < std::min(c[1],d[1]) || y > std::max(c[1],d[1])) continue;
            if (c[1] == d[1]) { include(c[0]); include(d[0]); }
            else include(c[0]+(y-c[1])*(d[0]-c[0])/(d[1]-c[1]));
        }
        for (const auto& center : {std::array<double,2>{{ax,ay}}, std::array<double,2>{{bx,by}}})
        {
            const double offset = y-center[1];
            if (std::abs(offset) > radius) continue;
            const double extent = std::sqrt(std::max(0.,radius*radius-offset*offset));
            include(center[0]-extent); include(center[0]+extent);
        }
        if (left <= right)
            span(p,y,int(std::max(0.,std::ceil(left))),int(std::min(double(input_.width-1),std::floor(right))));
    }
}

void Engine::render(Plane& p)
{
    if (p.hull.empty()) return;
    if (settings_.view == View::Filled && p.hull.size() >= 3)
    {
        int low = p.hull[0].y, high = low;
        for (Point v : p.hull) { low = std::min(low,v.y); high = std::max(high,v.y); }
        for (int y = low; y <= high; ++y)
        {
            int left = int(input_.width), right = -1;
            for (size_t i = 0; i < p.hull.size(); ++i)
            {
                Point a = p.hull[i], b = p.hull[(i+1)%p.hull.size()];
                if (a.y > b.y) std::swap(a,b);
                if (y < a.y || y > b.y) continue;
                if (a.y == b.y) { left = std::min({left,a.x,b.x}); right = std::max({right,a.x,b.x}); }
                else
                {
                    const int64_t denominator = b.y-a.y;
                    const int64_t numerator = int64_t(a.x)*(b.y-y)+int64_t(b.x)*(y-a.y);
                    left = std::min(left,int((numerator+denominator-1)/denominator));
                    right = std::max(right,int(numerator/denominator));
                }
            }
            span(p,y,left,right);
        }
    }
    const int width = settings_.view == View::Outline ? settings_.outlineWidth : 1;
    const size_t edges = p.hull.size() <= 2 ? 1 : p.hull.size();
    for (size_t i = 0; i < edges; ++i) line(p,p.hull[i],p.hull[(i+1)%p.hull.size()],width);
}

size_t Engine::retainedBytes() const
{
    size_t bytes = workers_.capacity()*sizeof(std::thread) + measurements_.capacity()*sizeof(Measurement);
    for (const auto& p : planes_)
        bytes += p.mask.capacity() + p.runs.capacity()*sizeof(Run) +
                 (p.points.capacity()+p.hull.capacity())*sizeof(Point) +
                 (p.rowLeft.capacity()+p.rowRight.capacity())*sizeof(int) +
                 p.measurements.capacity()*sizeof(Measurement);
    return bytes;
}
}
