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
struct RegionSpans
{
    PRegion region;
    RegionScanner scanner;
    MMI32 left;

    explicit RegionSpans(PRegion value) : region(value), left(0)
    {
    }

    MMMaskedSpans Services()
    {
        return {this,
            [](void *context, const MMMaskedRect *bounds) {
                auto &state = *static_cast<RegionSpans *>(context);
                RC clip(bounds->left, bounds->top, bounds->right, bounds->bottom);
                state.left = bounds->left;
                state.scanner.Init(state.region, &clip);
            },
            [](void *context, MMI32 *left, MMI32 *right) -> int {
                auto &state = *static_cast<RegionSpans *>(context);
                if (state.scanner.XpCur() == klwMax)
                    return 0;
                *left = state.left + state.scanner.XpCur();
                *right = state.left + state.scanner.XpFetch();
                state.scanner.XpFetch();
                return 1;
            },
            [](void *context) { static_cast<RegionSpans *>(context)->scanner.ScanNext(1); }};
    }
};

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
    else if (!region)
        mm_masked_blit8(&bitmap, big, pixels, stride, height, x, y, boundsClip, nullptr);
    else
    {
        RegionSpans source(region);
        const auto spans = source.Services();
        mm_masked_blit8(&bitmap, big, pixels, stride, height, x, y, boundsClip, &spans);
    }
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
