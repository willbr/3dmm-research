/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Masked bitmap stuff that a GUI app might need.

***************************************************************************/
#include "kauai_core.h"
#include "region.h"
#include "mbmp.h"
#include <cstdint>
ASSERTNAME

namespace
{
void DrawBitmap(const MMMaskedBitmap &bitmap, const RC &bounds, byte *pixels, LegacyLong stride,
                LegacyLong height, LegacyLong xref, LegacyLong yref, const RC *clip, PRegion region, bool mask)
{
    const auto left = static_cast<std::int64_t>(bounds.xpLeft) + xref;
    const auto top = static_cast<std::int64_t>(bounds.ypTop) + yref;
    if (left < INT32_MIN || left > INT32_MAX || top < INT32_MIN || top > INT32_MAX)
        return;
    const auto x = static_cast<MMI32>(left);
    const auto y = static_cast<MMI32>(top);
    MMMaskedRect rectangle;
    if (clip)
        rectangle = {clip->xpLeft, clip->ypTop, clip->xpRight, clip->ypBottom};
    const auto boundsClip = clip ? &rectangle : nullptr;
    const unsigned short one = 1;
    const bool big = !*reinterpret_cast<const byte *>(&one);
    if (mask)
        mm_masked_blit_mask(&bitmap, big, pixels, stride, height, x, y, boundsClip);
    else
        mm_masked_blit8(&bitmap, big, pixels, stride, height, x, y, boundsClip,
                         region ? region->Geometry() : nullptr);
}
} // namespace

void MaskedBitmapMBMP::Draw(byte *pixels, LegacyLong stride, LegacyLong height, LegacyLong xref, LegacyLong yref,
                            RC *clip, PRegion region)
{
    AssertThis(0);
    AssertIn(stride, 1, kcbMax);
    AssertIn(height, 1, kcbMax);
    AssertPvCb(pixels, LwMul(stride, height));
    AssertNilOrVarMem(clip);
    AssertNilOrPo(region, 0);

    DrawBitmap(_bitmap, _Qmbmph()->rc, pixels, stride, height, xref, yref, clip, region, false);
}

void MaskedBitmapMBMP::DrawMask(byte *pixels, LegacyLong stride, LegacyLong height, LegacyLong xref, LegacyLong yref,
                                RC *clip)
{
    AssertThis(0);
    AssertIn(stride, 1, kcbMax);
    AssertIn(height, 1, kcbMax);
    AssertPvCb(pixels, LwMul(stride, height));
    AssertNilOrVarMem(clip);

    DrawBitmap(_bitmap, _Qmbmph()->rc, pixels, stride, height, xref, yref, clip, nullptr, true);
}
