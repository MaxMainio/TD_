#include "HullEngine.h"

namespace Hull
{
Measurement measureHull(const std::vector<Point>& hull, unsigned source,
                        uint64_t selectedPixels, uint64_t blobCount)
{
    Measurement m;
    m.source = source;
    m.selectedPixels = selectedPixels;
    m.blobCount = blobCount;
    m.vertexCount = hull.size();
    if (hull.empty()) return m;
    m.left = m.right = hull.front().x;
    m.bottom = m.top = hull.front().y;
    long double twiceArea = 0, weightedX = 0, weightedY = 0, perimeter = 0;
    // Translate before taking cross products to avoid cancellation far from origin.
    const Point origin = hull.front();
    for (size_t i = 0; i < hull.size(); ++i)
    {
        const Point a = hull[i], b = hull[(i + 1) % hull.size()];
        m.left = std::min(m.left, a.x); m.right = std::max(m.right, a.x);
        m.bottom = std::min(m.bottom, a.y); m.top = std::max(m.top, a.y);
        const long double ax = static_cast<long double>(a.x) - origin.x;
        const long double ay = static_cast<long double>(a.y) - origin.y;
        const long double bx = static_cast<long double>(b.x) - origin.x;
        const long double by = static_cast<long double>(b.y) - origin.y;
        const long double cross = ax * by - bx * ay;
        twiceArea += cross;
        weightedX += (ax + bx) * cross;
        weightedY += (ay + by) * cross;
        perimeter += std::hypot(bx - ax, by - ay);
    }
    ++m.right; ++m.top;
    m.centroidX = (double(m.left) + m.right) * .5;
    m.centroidY = (double(m.bottom) + m.top) * .5;
    if (twiceArea != 0)
    {
        m.centroidX = double(origin.x + weightedX / (3 * twiceArea) + .5L);
        m.centroidY = double(origin.y + weightedY / (3 * twiceArea) + .5L);
    }
    m.area = double(std::abs(twiceArea) * .5L);
    m.perimeter = double(perimeter);
    return m;
}
}
