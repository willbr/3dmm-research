/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "ellipse_geometry.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static bool Compare(int width, int height)
{
    std::vector<KauaiPixels::EllipseSpan> portable;
    if (!KauaiPixels::EllipseRows(width, height, 0, height, portable))
        return false;
    auto native = CreateEllipticRgn(0, 0, width + 1, height + 1);
    if (!native)
        return false;
    const auto bytes = GetRegionData(native, 0, nullptr);
    std::vector<std::uint8_t> data(bytes);
    const bool got = bytes && GetRegionData(native, bytes, reinterpret_cast<RGNDATA *>(data.data()));
    DeleteObject(native);
    if (!got)
        return false;
    RGNDATAHEADER header;
    std::memcpy(&header, data.data(), sizeof(header));
    std::vector<KauaiPixels::EllipseSpan> expected(height);
    for (DWORD i = 0; i < header.nCount; ++i)
    {
        RECT rectangle;
        std::memcpy(&rectangle, data.data() + sizeof(header) + i * sizeof(rectangle), sizeof(rectangle));
        for (int y = rectangle.top; y < rectangle.bottom && y < height; ++y)
            if (y >= 0)
                expected[y] = {rectangle.left, rectangle.right};
    }
    for (int y = 0; y < height; ++y)
        if (portable[y].left != expected[y].left || portable[y].right != expected[y].right)
        {
            std::fprintf(stderr, "Ellipse %dx%d row %d: portable [%d,%d), native [%d,%d)\n", width, height, y,
                         portable[y].left, portable[y].right, expected[y].left, expected[y].right);
            return false;
        }
    return true;
}
int main()
{
    unsigned cases = 0;
    for (int width = 1; width <= 128; ++width)
        for (int height = 1; height <= 128; ++height)
        {
            if (!Compare(width, height))
                return 1;
            ++cases;
        }
    for (int narrow : {1, 2, 3, 4, 7, 16, 31, 64, 128, 511, 1023, 2047, 2048, 2049})
        for (int extent : {257, 513, 1024, 2048, 2049, 4096})
        {
            if (!Compare(narrow, extent) || !Compare(extent, narrow))
                return 1;
            cases += 2;
        }
    std::uint32_t random = 147;
    for (unsigned i = 0; i < 256; ++i)
    {
        random = random * 1664525u + 1013904223u;
        const int width = 1 + (random >> 16) % (i < 128 ? 2048 : 8192);
        random = random * 1664525u + 1013904223u;
        const int height = 1 + (random >> 16) % (i < 128 ? 2048 : 8192);
        if (!Compare(width, height))
            return 1;
        ++cases;
    }
    std::printf("Portable ellipse geometry: %u native GDI region comparisons passed, including degenerate shapes, "
                "large/narrow bounds and the precision transition.\n",
                cases);
}
