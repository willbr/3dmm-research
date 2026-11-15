#ifndef MM_PREVIEW_DEPTH_RANGE_H
#define MM_PREVIEW_DEPTH_RANGE_H
#include "mm/preview_render.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>
#include <utility>
namespace MM
{
namespace Internal
{
class PreviewDepthRange
{
    MMPreviewDepthRange range;

  public:
    using Point = std::array<double, 3>;
    PreviewDepthRange(double x, double y, double nearClip, double farClip, bool parallel = false)
    {
        mm_preview_depth_init(&range, x, y, nearClip, farClip, parallel);
    }
    void Include(const std::array<Point, 3> &triangle)
    {
        double points[3][3];
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = 0; j < 3; ++j)
                points[i][j] = triangle[i][j];
        mm_preview_depth_include(&range, points);
    }
    std::pair<double, double> Fit() const
    {
        double nearClip, farClip;
        mm_preview_depth_fit(&range, &nearClip, &farClip);
        return {nearClip, farClip};
    }
};
} // namespace Internal
} // namespace MM
#endif
