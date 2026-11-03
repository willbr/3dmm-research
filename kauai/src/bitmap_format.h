/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef BITMAP_FORMAT_H
#define BITMAP_FORMAT_H
#include <cstdint>
#pragma pack(push, 2)
struct BitmapFileHeader
{
    std::uint16_t bfType;
    std::uint32_t bfSize;
    std::uint16_t bfReserved1, bfReserved2;
    std::uint32_t bfOffBits;
};
struct BitmapInfoHeader
{
    std::uint32_t biSize;
    std::int32_t biWidth, biHeight;
    std::uint16_t biPlanes, biBitCount;
    std::uint32_t biCompression, biSizeImage;
    std::int32_t biXPelsPerMeter, biYPelsPerMeter;
    std::uint32_t biClrUsed, biClrImportant;
};
#pragma pack(pop)
static_assert(sizeof(BitmapFileHeader) == 14, "BMP file header layout drift");
static_assert(sizeof(BitmapInfoHeader) == 40, "BMP info header layout drift");
constexpr std::uint32_t BitmapCompressionRgb = 0;
constexpr std::uint32_t BitmapCompressionRle8 = 1;
#endif
