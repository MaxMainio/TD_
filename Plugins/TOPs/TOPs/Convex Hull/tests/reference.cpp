#include "HullEngine.h"
#include "OutputPacking.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

using namespace Hull;
namespace
{
size_t comparisons = 0;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
int64_t orientation(Point a, Point b, Point c)
{
    return int64_t(b.x-a.x)*(c.y-a.y)-int64_t(b.y-a.y)*(c.x-a.x);
}
int64_t distance(Point a, Point b) { return int64_t(a.x-b.x)*(a.x-b.x)+int64_t(a.y-b.y)*(a.y-b.y); }

// Independent gift wrapping, intentionally using every selected pixel.
std::vector<Point> referenceHull(const std::vector<Point>& points)
{
    if (points.empty()) return {};
    Point start = points[0];
    for (Point p : points) if (p.x < start.x || (p.x == start.x && p.y < start.y)) start = p;
    std::vector<Point> result;
    Point current = start;
    do
    {
        result.push_back(current);
        Point next = current;
        for (Point candidate : points)
        {
            const auto turn = orientation(current,next,candidate);
            if (next == current || turn < 0 || (turn == 0 && distance(current,candidate) > distance(current,next))) next = candidate;
        }
        current = next;
    } while (!(current == start));
    return result;
}

std::vector<std::vector<Point>> components(const std::vector<uint8_t>& mask, int w, int h)
{
    std::vector<uint8_t> visited(mask.size());
    std::vector<std::vector<Point>> result;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    {
        if (!mask[y*w+x] || visited[y*w+x]) continue;
        std::vector<Point> points{{x,y}};
        visited[y*w+x] = 1;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const Point p = points[i];
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            {
                const int nx = p.x+dx, ny = p.y+dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                if (mask[ny*w+nx] && !visited[ny*w+nx])
                { visited[ny*w+nx] = 1; points.push_back({nx,ny}); }
            }
        }
        result.push_back(std::move(points));
    }
    return result;
}

void referenceRaster(const std::vector<Point>& hull, int w, int h, View view, int width, std::vector<uint8_t>& out)
{
    if (hull.empty()) return;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    {
        bool inside = hull.size() >= 3 && view == View::Filled;
        for (size_t i = 0; inside && i < hull.size(); ++i)
            inside = orientation(hull[i],hull[(i+1)%hull.size()],{x,y}) >= 0;
        bool boundary = false;
        for (size_t i = 0; i < (hull.size() <= 2 ? 1 : hull.size()); ++i)
        {
            const Point a = hull[i], b = hull[(i+1)%hull.size()];
            const int thickness = view == View::Outline ? width : 1;
            if (thickness == 1)
            {
                const bool majorX = std::abs(b.x-a.x) >= std::abs(b.y-a.y);
                const int v = majorX ? x : y, amin = majorX ? a.x : a.y, bmin = majorX ? b.x : b.y;
                if (v < std::min(amin,bmin) || v > std::max(amin,bmin)) continue;
                if (a == b) boundary |= x == a.x && y == a.y;
                else
                {
                    const long double t = (static_cast<long double>(v)-amin)/(bmin-amin);
                    const long double interpolated = majorX ? a.y+t*(b.y-a.y) : a.x+t*(b.x-a.x);
                    boundary |= (majorX ? y : x) == int(std::floor(interpolated+.500000000001L));
                }
            }
            else
            {
                const double offset = thickness%2 ? 0 : .5;
                const double dx = b.x-a.x, dy = b.y-a.y;
                const double t = dx*dx+dy*dy ? std::clamp(((x-a.x-offset)*dx+(y-a.y-offset)*dy)/(dx*dx+dy*dy),0.,1.) : 0;
                boundary |= std::hypot(x-a.x-offset-t*dx,y-a.y-offset-t*dy) <= thickness*.5+1e-12;
            }
        }
        if (inside || boundary) out[y*w+x] = 1;
    }
}

struct Reference { std::array<std::vector<uint8_t>,4> planes; std::array<SourceStats,5> stats{}; };
Reference reference(Input in, Settings s)
{
    Reference result;
    const int jobs = s.source == Source::RGBA ? 4 : s.source == Source::RGB ? 3 : 1;
    for (int job = 0; job < jobs; ++job)
    {
        const unsigned source = jobs == 1 ? unsigned(s.source) : job+1;
        auto& stats = result.stats[source];
        auto& out = result.planes[job];
        out.assign(in.width*in.height,0);
        std::vector<uint8_t> mask(out.size());
        for (size_t y = 0; y < in.height; ++y) for (size_t x = 0; x < in.width; ++x)
        {
            const float* p = in.data+y*in.stride+x*4;
            volatile float r = .2126f*p[0], g = .7152f*p[1], b = .0722f*p[2];
            const float v = source ? p[source-1] : (r+g)+b;
            const double t = s.independent && jobs > 1 ? s.thresholds[source-1] : s.threshold;
            mask[y*in.width+x] = std::isfinite(v) && (s.below ? v <= t : v > t) &&
                (!s.ignoreTransparent || (std::isfinite(p[3]) && p[3] > s.alphaCutoff));
            stats.selectedPixels += mask[y*in.width+x];
        }
        auto blobs = components(mask,int(in.width),int(in.height));
        blobs.erase(std::remove_if(blobs.begin(),blobs.end(),[&](const auto& b) { return b.size() < s.minimumArea; }),blobs.end());
        if (s.grouping == Grouping::LargestBlob && !blobs.empty())
        {
            auto biggest = std::max_element(blobs.begin(),blobs.end(),[](const auto& a, const auto& b) { return a.size() < b.size(); });
            std::vector<Point> chosen = *biggest;
            blobs = {chosen};
        }
        stats.blobs = blobs.size();
        for (const auto& b : blobs) stats.retainedPixels += b.size();
        if (s.grouping == Grouping::Global && !blobs.empty())
        {
            std::vector<Point> all;
            for (const auto& b : blobs) all.insert(all.end(),b.begin(),b.end());
            blobs = {all};
        }
        stats.hulls = blobs.size();
        for (const auto& blob : blobs)
        {
            if (s.view == View::Selection) for (Point p : blob) out[p.y*in.width+p.x] = 1;
            else referenceRaster(referenceHull(blob),int(in.width),int(in.height),s.view,s.outlineWidth,out);
        }
        for (auto v : out) stats.outputPixels += v;
    }
    if (jobs == 1) result.planes[1] = result.planes[2] = result.planes[0];
    return result;
}

void compare(Engine& engine, const std::vector<float>& values, int w, int h, Settings s)
{
    const Input input{values.data(),size_t(w),size_t(h),size_t(w)*4};
    const auto expected = reference(input,s);
    const auto image = engine.process(input,s);
    for (int c = 0; c < 4; ++c) for (int i = 0; i < w*h; ++i)
    {
        float want;
        if (c < 3 || s.source == Source::RGBA) want = expected.planes[c][i];
        else if (s.alpha == Alpha::Opaque) want = 1;
        else if (s.alpha == Alpha::Result) want = std::max({expected.planes[0][i],expected.planes[1][i],expected.planes[2][i]});
        else want = std::isfinite(values[i*4+3]) ? std::clamp(values[i*4+3],0.f,1.f) : 0;
        require(image.value(c,i) == want,"Pixel mismatch case="+std::to_string(comparisons)+" channel="+std::to_string(c)+" pixel="+std::to_string(i));
    }
    for (int c = 0; c < 5; ++c)
    {
        const auto& a = engine.stats()[c]; const auto& b = expected.stats[c];
        require(a.selectedPixels == b.selectedPixels && a.retainedPixels == b.retainedPixels &&
            a.blobs == b.blobs && a.hulls == b.hulls && a.outputPixels == b.outputPixels,"Source statistics mismatch");
    }
    ++comparisons;
}

void geometryTests()
{
    for (unsigned bits = 0; bits < 512; ++bits)
    {
        std::vector<Point> points;
        for (int i = 0; i < 9; ++i) if (bits & (1u<<i)) points.push_back({i%3,i/3});
        auto expected = referenceHull(points);
        std::vector<Point> actual;
        convexHull(points,actual);
        require(actual == expected,"Gift wrapping geometry mismatch");
    }
    Engine engine(0);
    for (unsigned bits = 0; bits < 512; ++bits)
    {
        std::vector<float> values(36);
        for (int i = 0; i < 9; ++i) values[i*4] = (bits>>i)&1;
        for (int group = 0; group < 3; ++group) for (int view = 0; view < 3; ++view)
            for (int area : {1,2,4})
            {
                Settings s; s.source = Source::Red; s.grouping = Grouping(group); s.view = View(view); s.minimumArea = area;
                compare(engine,values,3,3,s);
            }
    }
}

void settingsTests()
{
    std::mt19937 rng(24141);
    Engine engine;
    for (int fixture = 0; fixture < 10; ++fixture)
    {
        const int w = fixture == 0 ? 1 : 13, h = fixture == 1 ? 1 : 11;
        std::vector<float> pixels(size_t(w)*h*4);
        for (auto& p : pixels) p = (int(rng()%9)-2)*.25f;
        for (int source = 0; source < 7; ++source) for (int group = 0; group < 3; ++group)
            for (int view = 0; view < 3; ++view) for (int alpha = 0; alpha < 3; ++alpha) for (int flags = 0; flags < 8; ++flags)
            {
                Settings s; s.source = Source(source); s.grouping = Grouping(group); s.view = View(view); s.alpha = Alpha(alpha);
                s.independent = flags&1; s.below = flags&2; s.ignoreTransparent = flags&4;
                s.thresholds = {{0,.25,.75,1}}; s.alphaCutoff = .5; s.minimumArea = fixture%3+1;
                s.outlineWidth = fixture%6+1;
                compare(engine,pixels,w,h,s);
            }
    }
    std::vector<float> invalid = {NAN,INFINITY,-INFINITY,NAN, 1000,-1000,.5f,INFINITY, .5f,.5f,.5f,-1, 1,1,1,2};
    for (int source = 0; source < 7; ++source) for (int below = 0; below < 2; ++below) for (int alpha = 0; alpha < 3; ++alpha)
    {
        Settings s; s.source = Source(source); s.below = below; s.alpha = Alpha(alpha);
        compare(engine,invalid,4,1,s);
    }
    bool threw = false;
    try { engine.process({},{}); } catch (const std::invalid_argument&) { threw = true; }
    require(threw,"Empty input accepted");
    threw = false;
    try { engine.process({invalid.data(),4,1,3},{}); } catch (const std::invalid_argument&) { threw = true; }
    require(threw,"Short stride accepted");
    threw = false;
    try { engine.process({invalid.data(),size_t(INT32_MAX),2,size_t(INT32_MAX)*4},{}); } catch (const std::invalid_argument&) { threw = true; }
    require(threw,"Excessive dimensions accepted");
}

void parallelTests()
{
    std::mt19937 rng(879);
    std::vector<float> pixels(67*65*4);
    for (auto& v : pixels) v = float(rng()%256)/255;
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        Engine serial(0), parallel;
        Settings s; s.source = iteration%2 ? Source::RGB : Source::RGBA;
        s.grouping = Grouping(iteration%3); s.view = View((iteration/3)%3); s.alpha = Alpha::Preserve;
        const auto a = serial.process({pixels.data(),67,65,67*4},s);
        for (int repeat = 0; repeat < 4; ++repeat)
        {
            const auto b = parallel.process({pixels.data(),67,65,67*4},s);
            for (size_t i = 0; i < 67*65; ++i) for (size_t c = 0; c < 4; ++c)
                require(a.value(c,i) == b.value(c,i),"Parallel result differs");
        }
        const size_t retained = parallel.retainedBytes();
        parallel.process({pixels.data(),67,65,67*4},s);
        require(parallel.retainedBytes() == retained,"Identical cooks grow retained buffers");
        parallel.process({pixels.data(),1,1,4},{});
        parallel.process({pixels.data(),67,65,67*4},s);
    }
}

void fixtureTests()
{
    Engine engine;
    // A hollow border enclosing a disconnected central blob exercises overlapping
    // per-blob hulls, hole filling, and area filtering before hull construction.
    std::vector<float> ring(11*11*4);
    for (int y = 0; y < 11; ++y) for (int x = 0; x < 11; ++x)
        ring[(y*11+x)*4] = (x == 0 || x == 10 || y == 0 || y == 10 || (x == 5 && y == 5));
    for (int group = 0; group < 3; ++group) for (int view = 0; view < 3; ++view) for (int width : {1,2,3,4,19,100})
    {
        Settings s; s.source = Source::Red; s.grouping = Grouping(group); s.view = View(view); s.outlineWidth = width;
        compare(engine,ring,11,11,s);
        s.minimumArea = 2; compare(engine,ring,11,11,s);
    }
    // Rows may include padding and samples must not accidentally consume it.
    std::vector<float> padded(11*48,-999);
    for (int y = 0; y < 11; ++y) std::copy_n(ring.data()+y*44,44,padded.data()+y*48);
    Settings s; s.source = Source::Red;
    const auto expected = reference({ring.data(),11,11,44},s);
    const auto actual = engine.process({padded.data(),11,11,48},s);
    for (int i = 0; i < 121; ++i) require(actual.value(0,i) == expected.planes[0][i],"Padded input stride mismatch");
}

void packingTests()
{
    using F = TD::OP_PixelFormat;
    for (unsigned pattern = 0; pattern < 32; ++pattern)
    {
    const bool preserve = pattern & 16;
    const std::array<float,4> values = {float(pattern&1),float((pattern>>1)&1),float((pattern>>2)&1),preserve ? .4f : float((pattern>>3)&1)};
    const std::array<uint8_t,4> binary = {uint8_t(pattern&1),uint8_t((pattern>>1)&1),uint8_t((pattern>>2)&1),uint8_t((pattern>>3)&1)};
    Hull::Image image{1,1,{&binary[0],&binary[1],&binary[2],preserve ? nullptr : &binary[3]},{values.data(),1,1,4},Hull::Alpha::Preserve};
    const F formats[] = {F::BGRA8Fixed,F::RGBA8Fixed,F::RGBA16Fixed,F::RGBA16Float,F::RGBA32Float,
        F::Mono8Fixed,F::Mono16Fixed,F::Mono16Float,F::Mono32Float,F::RG8Fixed,F::RG16Fixed,F::RG16Float,F::RG32Float,
        F::A8Fixed,F::A16Fixed,F::A16Float,F::A32Float,F::MonoA8Fixed,F::MonoA16Fixed,F::MonoA16Float,F::MonoA32Float,
        F::RGB10A2Fixed,F::RGB11Float};
    for (F format : formats)
    {
        const size_t size = TDPlugin::TOPOutput::bytesPerPixel(format);
        std::array<uint8_t,32> bytes; bytes.fill(0xD7);
        Hull::packImage(image,format,bytes.data()+1,size);
        require(bytes[0] == 0xD7 && bytes[size+1] == 0xD7, "Packing wrote beyond buffer");
        bool threw = false;
        try { Hull::packImage(image,format,bytes.data(),size-1); }
        catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Short output buffer accepted");
        // Decode every format independently and compare the actual stored
        // channel values. This catches wrong channel order and bit layouts,
        // not just allocation bounds.
        auto word = [&](size_t offset, size_t count)
        {
            uint32_t value = 0;
            std::memcpy(&value, bytes.data()+1+offset, count);
            return value;
        };
        auto miniFloat = [](uint32_t bits, int mantissaBits)
        {
            const int exponent = (bits >> mantissaBits) & 31;
            const int mantissa = bits & ((1 << mantissaBits)-1);
            return exponent == 0 ? std::ldexp(static_cast<float>(mantissa), 1-15-mantissaBits) :
                std::ldexp(1.f + static_cast<float>(mantissa)/(1 << mantissaBits), exponent-15);
        };
        if (format == F::RGB10A2Fixed)
        {
            const uint32_t packed = word(0,4);
            for (int c = 0; c < 4; ++c)
            {
                const unsigned max = c == 3 ? 3 : 1023;
                const float actual = static_cast<float>((packed >> (c*10)) & max)/max;
                require(std::abs(actual-values[c]) <= .5001f/max, "RGB10A2 numeric decode mismatch");
            }
        }
        else if (format == F::RGB11Float)
        {
            const uint32_t packed = word(0,4);
            const float decoded[] = {miniFloat(packed & 2047,6), miniFloat((packed >> 11) & 2047,6), miniFloat(packed >> 22,5)};
            for (int c = 0; c < 3; ++c)
                require(std::abs(decoded[c]-values[c]) < .016f, "RGB11Float numeric decode mismatch");
        }
        else
        {
            std::vector<int> channels;
            const unsigned mask = Hull::outputChannels(format);
            for (int c = 0; c < 4; ++c) if (mask & (1u << c)) channels.push_back(c);
            if (format == F::BGRA8Fixed) channels = {2,1,0,3};
            const size_t componentBytes = size/channels.size();
            const bool floating = format == F::RGBA16Float || format == F::RGBA32Float ||
                format == F::Mono16Float || format == F::Mono32Float || format == F::RG16Float ||
                format == F::RG32Float || format == F::A16Float || format == F::A32Float ||
                format == F::MonoA16Float || format == F::MonoA32Float;
            for (size_t c = 0; c < channels.size(); ++c)
            {
                float decoded, tolerance;
                if (floating && componentBytes == 4)
                {
                    const uint32_t bits = word(c*4,4);
                    std::memcpy(&decoded,&bits,4); tolerance = 0;
                }
                else if (floating)
                {
                    decoded = miniFloat(word(c*2,2),10); tolerance = .001f;
                }
                else
                {
                    const float max = componentBytes == 1 ? 255.f : 65535.f;
                    decoded = word(c*componentBytes,componentBytes)/max;
                    tolerance = .5001f/max;
                }
                require(std::abs(decoded-values[channels[c]]) <= tolerance, "Stored channel numeric mismatch");
            }
        }
    }
    }
    const std::array<uint8_t,3> binary = {1,0,1};
    for (int code = 0; code < 255; ++code)
    {
        const float threshold = (code + .5f)/255.f;
        for (float v : {std::nextafter(threshold,0.f), threshold, std::nextafter(threshold,1.f)})
        {
            const float input[] = {0,0,0,v};
            Hull::Image test{1,1,{&binary[0],&binary[1],&binary[2],nullptr},{input,1,1,4},Hull::Alpha::Preserve};
            uint8_t packed[4]; Hull::packImage(test,F::RGBA8Fixed,packed,4);
            require(packed[0] == 255 && packed[1] == 0 && packed[2] == 255 && packed[3] == TDPlugin::TOPOutput::to8(v), "Fast byte packing changed rounding");
        }
    }

    Engine engine;
    Settings s; s.source = Source::RGBA; s.view = View::Selection;
    const std::vector<float> input = {1,0,0,1, 0,1,0,0, 0,0,1,1, 1,1,0,0, 0,1,1,1, 1,0,1,0};
    const auto raster = engine.process({input.data(),3,2,12},s);
    for (const auto size : {std::pair<int,int>{7,5},{1,1},{2,1},{3,2}})
    {
        std::vector<float> actual(size.first*size.second*4);
        packImage(raster,F::RGBA32Float,actual.data(),actual.size()*4,size.first,size.second);
        for (int y = 0; y < size.second; ++y) for (int x = 0; x < size.first; ++x) for (int c = 0; c < 4; ++c)
        {
            const int sx = (2*x+1)*3/(2*size.first), sy = (2*y+1)*2/(2*size.second);
            require(actual[(y*size.first+x)*4+c] == input[(sy*3+sx)*4+c],"Post-hull resize or orientation mismatch");
        }
    }
}

}

int main()
{
    try { geometryTests(); settingsTests(); fixtureTests(); parallelTests(); packingTests(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Passed " << comparisons << " independent image/statistics comparisons, geometry, packing, resizing and lifecycle checks\n";
}
