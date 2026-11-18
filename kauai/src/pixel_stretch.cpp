/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_stretch.h"
#include "kauai_core.h"
#include "region.h"
#include "mm/indexed_display.h"
#include <limits>
ASSERTNAME

namespace
{
MMPixelRect RasterRect(const RC &rect)
{
    return {rect.xpLeft, rect.ypTop, rect.xpRight, rect.ypBottom};
}
bool FitsRaster(long value)
{
    return value >= std::numeric_limits<MMI32>::min() && value <= std::numeric_limits<MMI32>::max();
}
void Stretch(byte *src, long sourcePitch, long sourceHeight, const RC *source, byte *dst, long targetPitch,
             long targetHeight, long x, long y, const RC *clip, Region *region, bool doubleWidth)
{
    if (!source || !FitsRaster(sourcePitch) || !FitsRaster(sourceHeight) || !FitsRaster(targetPitch) ||
        !FitsRaster(targetHeight) || !FitsRaster(x) || !FitsRaster(y))
        return;
    // The original helpers use the full row pitch as the drawable width.
    const MMPixelSurface from = {src, MMI32(sourcePitch), MMI32(sourceHeight), MMI32(sourcePitch), 8};
    const MMPixelSurface to = {dst, MMI32(targetPitch), MMI32(targetHeight), MMI32(targetPitch), 8};
    const auto rect = RasterRect(*source);
    const auto clipping = clip ? RasterRect(*clip) : MMPixelRect{};
    mm_indexed_double_draw(&to, &from, &rect, MMI32(x), MMI32(y), doubleWidth, clip ? &clipping : nullptr,
                           region ? region->Geometry() : nullptr);
}
} // namespace

IndexedDoubleStretch PlanDoubleStretch(const RC &sourceBounds, const RC &source, const RC &destination,
                                       const RC *clip, RC &sourceCrop, RC &destinationClip)
{
    static_assert(int(IndexedDoubleStretch::unavailable) == MM_DOUBLE_UNAVAILABLE &&
                      int(IndexedDoubleStretch::clipped) == MM_DOUBLE_CLIPPED &&
                      int(IndexedDoubleStretch::vertical) == MM_DOUBLE_VERTICAL &&
                      int(IndexedDoubleStretch::both) == MM_DOUBLE_BOTH,
                  "Indexed double-stretch modes");
    const auto bounds = RasterRect(sourceBounds), from = RasterRect(source), to = RasterRect(destination);
    const auto clipping = clip ? RasterRect(*clip) : MMPixelRect{};
    MMPixelRect cropped, visible;
    const auto result = mm_indexed_double_plan(&bounds, &from, &to, clip ? &clipping : nullptr, &cropped, &visible);
    if (result == MM_DOUBLE_VERTICAL || result == MM_DOUBLE_BOTH)
    {
        sourceCrop = RC(cropped.left, cropped.top, cropped.right, cropped.bottom);
        destinationClip = RC(visible.left, visible.top, visible.right, visible.bottom);
    }
    return static_cast<IndexedDoubleStretch>(result);
}

void DoubleStretch(byte *src, long sourcePitch, long sourceHeight, RC *source, byte *dst, long targetPitch,
                   long targetHeight, long x, long y, RC *clip, Region *region)
{
    Stretch(src, sourcePitch, sourceHeight, source, dst, targetPitch, targetHeight, x, y, clip, region, true);
}
void DoubleVertStretch(byte *src, long sourcePitch, long sourceHeight, RC *source, byte *dst, long targetPitch,
                       long targetHeight, long x, long y, RC *clip, Region *region)
{
    Stretch(src, sourcePitch, sourceHeight, source, dst, targetPitch, targetHeight, x, y, clip, region, false);
}
