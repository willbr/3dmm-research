/* SPDX-License-Identifier: Apache-2.0
 * C++ adapter for the ellipse geometry in libraries/engine/src/ellipse_geometry.c.
 * See docs/licenses/ellipse-Apache-2.0.txt and ellipse-NOTICE.txt.
 */
#include "ellipse_geometry.h"
#include "mm/ellipse_geometry.h"
#include <new>

namespace KauaiPixels
{
bool EllipseRows(std::int32_t width, std::int32_t height, std::int32_t firstRow, std::int32_t rows,
                 std::vector<EllipseSpan> &spans)
{
    if (width < 0 || height < 0 || rows < 0 || width > MM_ELLIPSE_MAX_EXTENT || height > MM_ELLIPSE_MAX_EXTENT ||
        rows > MM_ELLIPSE_MAX_EXTENT)
        return false;
    try
    {
        std::vector<MMEllipseSpan> output(static_cast<std::size_t>(rows));
        if (!mm_ellipse_rows(width, height, firstRow, rows, output.data()))
            return false;
        std::vector<EllipseSpan> result(output.size());
        for (std::size_t i = 0; i < output.size(); ++i)
            result[i] = {output[i].left, output[i].right};
        spans.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace KauaiPixels
