#ifndef THREE_MM_PREVIEW_DEPTH_RANGE_H
#define THREE_MM_PREVIEW_DEPTH_RANGE_H
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace ThreeMM
{
namespace Internal
{
// Fit inspection depth to geometry intersecting the visible frustum. Clipping
// triangles, rather than testing just their vertices or bounds, keeps walls
// crossing the screen and geometry close to the eye in the fitted interval.
class PreviewDepthRange
{
  public:
    using Point = std::array<double, 3>; // Camera X/Y, positive forward distance.
    PreviewDepthRange(double xSlope, double ySlope, double nearClip, double farClip, bool parallel = false)
        : xSlope(xSlope), ySlope(ySlope), nearClip(nearClip), farClip(farClip), parallel(parallel) {}

    void Include(const std::array<Point, 3> &triangle)
    {
        std::array<Point, 12> polygon{}, clipped{};
        std::copy(triangle.begin(), triangle.end(), polygon.begin());
        unsigned count = 3;
        for (unsigned plane = 0; plane < 6 && count; ++plane)
        {
            std::array<double, 12> distances{};
            unsigned inside = 0;
            for (unsigned i = 0; i < count; ++i)
            {
                distances[i] = Distance(polygon[i], plane);
                if (distances[i] >= 0) ++inside;
            }
            if (!inside) return;
            if (inside == count) continue;
            unsigned output = 0;
            auto previous = polygon[count - 1];
            auto previousDistance = distances[count - 1];
            for (unsigned i = 0; i < count; ++i)
            {
                const auto current = polygon[i];
                const auto distance = distances[i];
                if ((distance > 0 && previousDistance < 0) || (distance < 0 && previousDistance > 0))
                {
                    const auto t = previousDistance / (previousDistance - distance);
                    Point intersection{};
                    for (unsigned axis = 0; axis < 3; ++axis)
                        intersection[axis] = previous[axis] + t * (current[axis] - previous[axis]);
                    clipped[output++] = intersection;
                }
                if (distance >= 0) clipped[output++] = current;
                previous = current;
                previousDistance = distance;
            }
            polygon.swap(clipped);
            count = output;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            nearest = std::min(nearest, polygon[i][2]);
            farthest = std::max(farthest, polygon[i][2]);
        }
    }

    std::pair<double, double> Fit() const
    {
        if (!std::isfinite(nearest)) return {nearClip, farClip};
        // Leave generous clearance before the closest visible surface,
        // including during flight, without spending most depth values on empty space.
        const auto near = std::max(nearClip, nearest * .5);
        const auto far = std::min(farClip, std::max(near * 2, farthest * 1.05));
        return {near, far};
    }

  private:
    double Distance(const Point &point, unsigned plane) const
    {
        switch (plane)
        {
        case 0: return (parallel ? 1 : point[2]) * xSlope + point[0];
        case 1: return (parallel ? 1 : point[2]) * xSlope - point[0];
        case 2: return (parallel ? 1 : point[2]) * ySlope + point[1];
        case 3: return (parallel ? 1 : point[2]) * ySlope - point[1];
        case 4: return point[2] - nearClip;
        default: return farClip - point[2];
        }
    }
    double xSlope, ySlope, nearClip, farClip;
    bool parallel;
    double nearest = std::numeric_limits<double>::infinity(), farthest = 0;
};
}
}
#endif
