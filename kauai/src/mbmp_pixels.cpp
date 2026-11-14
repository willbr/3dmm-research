/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include "kauai_core.h"
#include "mbmp.h"
ASSERTNAME
bool MaskedBitmapMBMP::_FGetRleBitmap(KauaiPixels::RleBitmap8 *bitmap)
{
    const auto header = _Qmbmph();
    const auto width = std::int64_t(header->rc.xpRight) - header->rc.xpLeft;
    const auto height = std::int64_t(header->rc.ypBottom) - header->rc.ypTop;
    if (width <= 0 || height <= 0)
    {
        *bitmap = {};
        return true;
    }
    const auto encodedOffset = std::int64_t(sizeof(MaskedBitmapOnFile)) + _bitmap.row_bytes;
    const auto total = _bitmap.bytes;
    if (width > INT32_MAX || height > INT32_MAX || _bitmap.row_bytes != height * sizeof(short) || encodedOffset > total)
        return false;
    *bitmap = {reinterpret_cast<const std::uint8_t *>(header) + encodedOffset,
               static_cast<std::size_t>(total - encodedOffset),
               _Qrgcb(),
               static_cast<std::int32_t>(width),
               static_cast<std::int32_t>(height),
               header->fMask != 0,
               header->bFill};
    return true;
}
bool MaskedBitmapMBMP::FDraw32(const KauaiPixels::Surface32 &destination, const RC &rectangle, const Color *palette,
                               LegacyLong colors, const RC *clip, PRegion region)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!_FGetRleBitmap(&bitmap))
        return false;
    return !bitmap.width || KauaiPixels::DrawRle(destination, bitmap, rectangle, palette, colors, clip, region);
}
bool MaskedBitmapMBMP::FDraw8(const KauaiPixels::Surface8 &destination, const RC &rectangle, const RC *clip,
                              PRegion region)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!_FGetRleBitmap(&bitmap))
        return false;
    return !bitmap.width || KauaiPixels::DrawRle(destination, bitmap, rectangle, clip, region);
}
bool MaskedBitmapMBMP::FDraw8(const KauaiPixels::Surface8 &destination, const RC &rectangle,
                              const KauaiPixels::MaskedTransfer8 &transfer, const RC *clip, PRegion region,
                              bool andShrink)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!_FGetRleBitmap(&bitmap))
        return false;
    return !bitmap.width || KauaiPixels::DrawRle(destination, bitmap, rectangle, transfer, clip, region, andShrink);
}
bool MaskedBitmapMBMP::FDrawPacked(const KauaiPixels::PackedSurface &destination, const RC &rectangle,
                                   const Color *palette, LegacyLong colors, const RC *clip, PRegion region,
                                   bool andShrink)
{
    KauaiPixels::RleBitmap8 bitmap;
    if (!_FGetRleBitmap(&bitmap))
        return false;
    return !bitmap.width ||
           KauaiPixels::DrawRle(destination, bitmap, rectangle, palette, colors, clip, region, andShrink);
}
