/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include "kauai_core.h"
#include "mbmp.h"
ASSERTNAME
namespace
{
bool GetRleBitmap(PMaskedBitmapMBMP owner, KauaiPixels::RleBitmap8 *bitmap)
{
    MMMaskedRle view;
    if (!mm_masked_rle_view(mm_masked_owner_bitmap(owner), mm_masked_native_big_endian(), &view))
        return false;
    *bitmap = {view.data, view.bytes, static_cast<const std::int16_t *>(view.row_bytes),
               view.width, view.height, view.solid != 0, view.fill};
    return true;
}
} // namespace

bool DrawMaskedBitmap32(PMaskedBitmapMBMP owner, const KauaiPixels::Surface32 &destination, const RC &rectangle, const Color *palette,
                               LegacyLong colors, const RC *clip, PRegion region)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!GetRleBitmap(owner, &bitmap))
        return false;
    return !bitmap.width || KauaiPixels::DrawRle(destination, bitmap, rectangle, palette, colors, clip, region);
}
bool DrawMaskedBitmap8(PMaskedBitmapMBMP owner, const KauaiPixels::Surface8 &destination, const RC &rectangle, const RC *clip,
                              PRegion region)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!GetRleBitmap(owner, &bitmap))
        return false;
    return !bitmap.width || KauaiPixels::DrawRle(destination, bitmap, rectangle, clip, region);
}
bool DrawMaskedBitmap8(PMaskedBitmapMBMP owner, const KauaiPixels::Surface8 &destination, const RC &rectangle,
                              const KauaiPixels::MaskedTransfer8 &transfer, const RC *clip, PRegion region,
                              bool andShrink)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!GetRleBitmap(owner, &bitmap))
        return false;
    return !bitmap.width || KauaiPixels::DrawRle(destination, bitmap, rectangle, transfer, clip, region, andShrink);
}
bool DrawMaskedBitmapPacked(PMaskedBitmapMBMP owner, const KauaiPixels::PackedSurface &destination, const RC &rectangle,
                                   const Color *palette, LegacyLong colors, const RC *clip, PRegion region,
                                   bool andShrink)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!GetRleBitmap(owner, &bitmap))
        return false;
    return !bitmap.width ||
           KauaiPixels::DrawRle(destination, bitmap, rectangle, palette, colors, clip, region, andShrink);
}
