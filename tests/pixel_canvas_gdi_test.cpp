/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
int main()
{
    const int width = 37, height = 29;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits)
        return 1;
    auto oldBitmap = SelectObject(dc, bitmap);
    struct
    {
        WORD version, count;
        PALETTEENTRY entries[256];
    } identity = {0x300, 256, {}};
    for (int i = 0; i < 256; ++i)
    {
        identity.entries[i].peRed = static_cast<byte>(i);
        identity.entries[i].peFlags = PC_EXPLICIT;
    }
    auto nativePalette = CreatePalette(reinterpret_cast<LOGPALETTE *>(&identity));
    if (!nativePalette)
        return 1;
    SelectPalette(dc, nativePalette, FALSE);
    RealizePalette(dc);
    // Wrap a native DC without exposing its bits to GraphicsPort: even in an
    // SDL build this exercises the original GDI implementation as the oracle.
    auto port = GraphicsPort::PgptNew(dc);
    RC regionBounds(1, 2, 34, 27), hole(6, 7, 17, 19);
    auto region = Region::PregnNew(&regionBounds);
    if (!port || !region || !region->FDiffRc(&hole))
        return 1;
    region->AddRef();
    PRegion owned = region;
    port->ClipToRegn(&owned);
    std::vector<std::uint32_t> portable(width * height);
    KauaiPixels::Surface32 surface = {portable.data(), width, height, width * 4};
    AbstractColor colors[] = {AbstractColor(13, 98, 201), AbstractColor(210, 42, 8), kacrClear, kacrInvert};
    SystemRectangle nativeRect = {-3, 3, 36, 26}, nativeClip = {2, 1, 32, 28};
    RC rectangle(nativeRect), clip(nativeClip);
    int cases = 0;
    for (int oval = 0; oval < 2; ++oval)
        for (int fore = 0; fore < 4; ++fore)
            for (int back = 0; back < 4; ++back)
                for (int frame = 0; frame < 3; ++frame)
                {
                    for (int i = 0; i < width * height; ++i)
                        static_cast<std::uint32_t *>(bits)[i] = portable[i] = (i * 7919) & 0xFFFFFF;
                    GraphicsDrawingData drawing = {};
                    RC graphicsClip;
                    drawing.grfgdd = fgddPattern | (frame ? fgddFrame : 0);
                    drawing.acrFore = colors[fore];
                    drawing.acrBack = colors[back];
                    drawing.dxpPen = frame == 2 ? 30 : 3;
                    drawing.dypPen = 4;
                    drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                    KauaiPixels::Brush brush;
                    for (int row = 0; row < 8; ++row)
                        drawing.apt.rgb[row] = brush.pattern[row] = static_cast<byte>(0xA6 ^ (row * 31));
                    auto ink = [](int index) {
                        KauaiPixels::Ink result;
                        result.mode = index == 2   ? KauaiPixels::InkMode::clear
                                      : index == 3 ? KauaiPixels::InkMode::invert
                                                   : KauaiPixels::InkMode::color;
                        result.rgb = index == 0 ? 0x0D62C9 : 0xD22A08;
                        return result;
                    };
                    brush.foreground = ink(fore);
                    brush.background = ink(back);
                    if (oval)
                        port->DrawOval(&nativeRect, &drawing);
                    else
                        port->DrawRcs(&nativeRect, &drawing);
                    GraphicsPort::Flush();
                    if (oval)
                    {
                        if (!KauaiPixels::FillOval(surface, rectangle, brush, &clip, region,
                                                   frame ? drawing.dxpPen : -1, frame ? drawing.dypPen : -1))
                            return 1;
                    }
                    else
                        KauaiPixels::FillRect(surface, rectangle, brush, &clip, region, frame ? drawing.dxpPen : -1,
                                              frame ? drawing.dypPen : -1);
                    for (int i = 0; i < width * height; ++i)
                        if ((static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF) != (portable[i] & 0xFFFFFF))
                        {
                            std::fprintf(stderr, "GDI shape mismatch: oval=%d fore=%d back=%d frame=%d at (%d,%d)\n",
                                         oval, fore, back, frame, i % width, i / width);
                            return 1;
                        }
                    ++cases;
                }
    std::vector<std::vector<KauaiPixels::Point>> polygons = {
        {{2, 2}, {31, 2}, {2, 25}},
        {{2, 2}, {31, 25}, {31, 2}, {2, 25}},
        {{-12, 7}, {17, -5}, {35, 15}, {16, 28}},
        {{1, 1}, {34, 1}, {34, 27}, {1, 27}, {1, 1}, {8, 8}, {26, 8}, {26, 22}, {8, 22}, {8, 8}, {1, 1}},
        {{1, 1}, {34, 1}, {34, 27}, {1, 27}, {1, 1}, {8, 8}, {8, 22}, {26, 22}, {26, 8}, {8, 8}, {1, 1}},
        {{3, 3}, {3, 3}, {18, 25}, {33, 3}, {3, 3}},
        {{3, 9}, {30, 9}, {30, 10}, {3, 10}}};
    std::uint32_t random = 0xC0FFEE;
    for (int sample = 0; sample < 200; ++sample)
    {
        std::vector<KauaiPixels::Point> polygon(3 + sample % 13);
        for (auto &point : polygon)
        {
            random = random * 1664525u + 1013904223u;
            point.x = static_cast<std::int32_t>((random >> 8) % 81) - 20;
            random = random * 1664525u + 1013904223u;
            point.y = static_cast<std::int32_t>((random >> 8) % 61) - 15;
        }
        polygons.push_back(polygon);
    }
    int polygonCases = 0;
    for (const auto &polygon : polygons)
    {
        HQ handle = hqNil;
        if (!FAllocHq(&handle, kcbOlyBase + polygon.size() * sizeof(SystemPoint), fmemNil, mprNormal))
            return 1;
        auto native = static_cast<OLY *>(PvLockHq(handle));
        native->cpts = static_cast<long>(polygon.size());
        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            native->rgpts[i].x = polygon[i].x;
            native->rgpts[i].y = polygon[i].y;
        }
        UnlockHq(handle);
        for (int patterned = 0; patterned < 2; ++patterned)
        {
            for (int i = 0; i < width * height; ++i)
                static_cast<std::uint32_t *>(bits)[i] = portable[i] = 0x123456;
            GraphicsDrawingData drawing = {};
            RC graphicsClip;
            drawing.grfgdd = patterned ? fgddPattern : fgddFill;
            drawing.acrFore = patterned ? kacrInvert : colors[0];
            drawing.acrBack = kacrClear;
            drawing.prcsClip = &(graphicsClip = RC(nativeClip));
            KauaiPixels::Brush brush;
            brush.foreground = {0x0D62C9, patterned ? KauaiPixels::InkMode::invert : KauaiPixels::InkMode::color};
            brush.background.mode = KauaiPixels::InkMode::clear;
            if (patterned)
                for (int row = 0; row < 8; ++row)
                    drawing.apt.rgb[row] = brush.pattern[row] = static_cast<byte>(0xA6 ^ (row * 31));
            port->DrawPoly(handle, &drawing);
            GraphicsPort::Flush();
            if (!KauaiPixels::FillPolygon(surface, polygon.data(), polygon.size(), brush, &clip, region))
                return 1;
            for (int i = 0; i < width * height; ++i)
                if ((static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF) != (portable[i] & 0xFFFFFF))
                {
                    std::fprintf(stderr, "GDI polygon mismatch: case=%d at (%d,%d)\n", polygonCases, i % width,
                                 i / width);
                    return 1;
                }
            ++polygonCases;
        }
        FreePhq(&handle);
    }
    // Use a real offscreen port to exercise Studio's line-to-polygon adapter.
    // The DC-only reference port above continues to use original GDI drawing.
    vwig.hdcApp = dc;
    RC lineBounds(0, 0, width, height);
    auto linePort = GraphicsPort::PgptNewOffscreen(&lineBounds, 32);
    if (!linePort)
        return 1;
    region->AddRef();
    owned = region;
    linePort->ClipToRegn(&owned);
    auto linePixels = linePort->PrgbLockPixels();
    if (!linePixels)
        return 1;
    const auto linePitch = linePort->CbRow();
    for (int pen = 0; pen < 4; ++pen)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto initial = std::uint32_t((y * width + x) * 7919) & 0xFFFFFF;
                static_cast<std::uint32_t *>(bits)[y * width + x] = initial;
                std::memcpy(linePixels + y * linePitch + x * 4, &initial, 4);
            }
        GraphicsDrawingData drawing = {};
        RC graphicsClip;
        drawing.grfgdd = fgddFrame | fgddPattern;
        drawing.acrFore = kacrInvert;
        drawing.acrBack = kacrClear;
        drawing.dxpPen = pen;
        drawing.dypPen = 3;
        drawing.prcsClip = &(graphicsClip = RC(nativeClip));
        for (int row = 0; row < 8; ++row)
            drawing.apt.rgb[row] = static_cast<byte>(0xA6 ^ (row * 31));
        port->DrawOval(&nativeRect, &drawing);
        linePort->DrawOval(&nativeRect, &drawing);
        GraphicsPort::Flush();
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                std::uint32_t actual;
                std::memcpy(&actual, linePixels + y * linePitch + x * 4, 4);
                if ((actual & 0xFFFFFF) != (static_cast<std::uint32_t *>(bits)[y * width + x] & 0xFFFFFF))
                {
                    std::fprintf(stderr, "RGB oval adapter mismatch: pen=%d at (%d,%d)\n", pen, x, y);
                    return 1;
                }
            }
    }
    const SystemPoint segments[][2] = {{{2, 2}, {30, 2}},   {{30, 2}, {2, 2}},    {{3, 3}, {3, 24}},
                                       {{3, 24}, {3, 3}},   {{2, 2}, {30, 24}},   {{2, 24}, {30, 2}},
                                       {{30, 2}, {2, 24}},  {{14, 14}, {14, 14}}, {{-50, -30}, {60, 40}},
                                       {{2, 26}, {32, 25}}, {{30, 4}, {2, 5}}};
    const SystemPoint pens[] = {{1, 1}, {3, 5}, {10, 2}, {0, 4}};
    int lineCases = 0;
    for (const auto &segment : segments)
        for (const auto &pen : pens)
            for (int patterned = 0; patterned < 2; ++patterned)
            {
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        static_cast<std::uint32_t *>(bits)[y * width + x] =
                            reinterpret_cast<std::uint32_t *>(linePixels + y * linePitch)[x] = 0x123456;
                GraphicsDrawingData drawing = {};
                RC graphicsClip;
                drawing.grfgdd = patterned ? fgddPattern : fgddFill;
                drawing.acrFore = patterned ? kacrInvert : colors[0];
                drawing.acrBack = kacrClear;
                drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                drawing.dxpPen = pen.x;
                drawing.dypPen = pen.y;
                for (int row = 0; row < 8; ++row)
                    drawing.apt.rgb[row] = static_cast<byte>(0xA6 ^ (row * 31));
                SystemPoint first = segment[0], last = segment[1];
                port->DrawLine(&first, &last, &drawing);
                linePort->DrawLine(&first, &last, &drawing);
                GraphicsPort::Flush();
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        if ((static_cast<std::uint32_t *>(bits)[y * width + x] & 0xFFFFFF) !=
                            (reinterpret_cast<std::uint32_t *>(linePixels + y * linePitch)[x] & 0xFFFFFF))
                        {
                            std::fprintf(stderr, "GDI line mismatch: case=%d at (%d,%d)\n", lineCases, x, y);
                            return 1;
                        }
                ++lineCases;
            }
    linePort->Unlock();
    ReleasePpo(&linePort);
    // Compare non-integral up/down stretches as well as exact 2x copies. The
    // reference port is DC-only, so it cannot select the portable shortcut.
    info.bmiHeader.biWidth = 7;
    info.bmiHeader.biHeight = -5;
    void *sourceBits = nullptr;
    HDC sourceDc = CreateCompatibleDC(nullptr);
    auto sourceBitmap = CreateDIBSection(sourceDc, &info, DIB_RGB_COLORS, &sourceBits, nullptr, 0);
    if (!sourceDc || !sourceBitmap || !sourceBits)
        return 1;
    auto previousSourceBitmap = SelectObject(sourceDc, sourceBitmap);
    auto sourcePort = GraphicsPort::PgptNew(sourceDc);
    if (!sourcePort)
        return 1;
    for (int i = 0; i < 35; ++i)
        static_cast<std::uint32_t *>(sourceBits)[i] = i * 7919;
    KauaiPixels::Surface32 source = {sourceBits, 7, 5, 7 * 4};
    RC sources[] = {RC(0, 0, 7, 5), RC(1, 1, 6, 4), RC(0, 0, 7, 5), RC(0, 0, 7, 5)};
    RC destinations[] = {RC(0, 0, 14, 10), RC(-3, 4, 20, 18), RC(2, 2, 5, 4), RC(0, 0, 31, 23)};
    for (int index = 0; index < 4; ++index)
    {
        for (int i = 0; i < width * height; ++i)
            static_cast<std::uint32_t *>(bits)[i] = portable[i] = 0x171819;
        GraphicsDrawingData drawing = {};
        RC graphicsClip;
        drawing.prcsClip = &(graphicsClip = RC(nativeClip));
        SystemRectangle nativeSource = sources[index], nativeDestination = destinations[index];
        port->CopyPixels(sourcePort, &nativeSource, &nativeDestination, &drawing);
        GraphicsPort::Flush();
        if (!KauaiPixels::Copy(surface, source, sources[index], destinations[index], &clip, region))
            return 1;
        for (int i = 0; i < width * height; ++i)
            if ((static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF) != (portable[i] & 0xFFFFFF))
            {
                std::fprintf(stderr, "GDI copy mismatch: case=%d at (%d,%d) GDI=%x portable=%x\n", index, i % width,
                             i / width, static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF, portable[i] & 0xFFFFFF);
                return 1;
            }
    }
    ReleasePpo(&sourcePort);
    SelectObject(sourceDc, previousSourceBitmap);
    DeleteObject(sourceBitmap);
    DeleteDC(sourceDc);
    Color palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = {static_cast<byte>(i * 13), static_cast<byte>(i * 37), static_cast<byte>(i * 71), 0};
    // Exercise the real legacy two-pass mask renderer through a DC-only port.
    // An identity palette supplies the opaque white used by its SRCAND pass.
    vwig.hdcApp = dc;
    auto paletteList = DynamicArray::PglNew(sizeof(Color), 256);
    if (!paletteList || !paletteList->FSetIvMac(256))
        return 1;
    std::memcpy(paletteList->QvGet(0), palette, sizeof(palette));
    GraphicsPort::SetActiveColors(paletteList, fpalIdentity);
    auto effective = GraphicsPort::PglclrGetPalette();
    if (!effective)
        return 1;
    std::memcpy(palette, effective->QvGet(0), sizeof(palette));
    ReleasePpo(&effective);
    ReleasePpo(&paletteList);
    byte sprite[35] = {};
    for (int i = 0; i < 35; ++i)
        sprite[i] = i % 4 ? static_cast<byte>(i + 10) : 0;
    for (int i = 14; i < 21; ++i)
        sprite[i] = 0;
    sprite[0] = 7;
    sprite[34] = 9;
    RC spriteBounds(0, 0, 7, 5);
    struct
    {
        BITMAPINFOHEADER header;
        RGBQUAD colors[256];
    } maskInfo = {};
    maskInfo.header = info.bmiHeader;
    maskInfo.header.biWidth = width;
    maskInfo.header.biHeight = -height;
    maskInfo.header.biBitCount = 8;
    maskInfo.header.biClrUsed = 256;
    for (int i = 0; i < 256; ++i)
        maskInfo.colors[i] = {palette[i].bBlue, palette[i].bGreen, palette[i].bRed, 0};
    HDC indexedDc = CreateCompatibleDC(nullptr);
    void *indexedBits = nullptr;
    auto indexedBitmap = CreateDIBSection(indexedDc, reinterpret_cast<BITMAPINFO *>(&maskInfo), DIB_RGB_COLORS,
                                          &indexedBits, nullptr, 0);
    if (!indexedDc || !indexedBitmap || !indexedBits)
        return 1;
    auto previousIndexedBitmap = SelectObject(indexedDc, indexedBitmap);
    SelectPalette(indexedDc, nativePalette, FALSE);
    RealizePalette(indexedDc);
    auto indexedPort = GraphicsPort::PgptNew(indexedDc);
    if (!indexedPort)
        return 1;
    // Match the nearest-neighbor mode set by CopyPixels. Win32's default
    // BLACKONWHITE instead combines monochrome mask bits when shrinking.
    SetStretchBltMode(indexedDc, COLORONCOLOR);
    region->AddRef();
    owned = region;
    indexedPort->ClipToRegn(&owned);
    const int indexedPitch = (width + 3) & ~3;
    std::vector<byte> indexedPixels(indexedPitch * height);
    KauaiPixels::Surface8 indexedSurface = {indexedPixels.data(), width, height, indexedPitch};
    int maskCases = 0;
    for (int mode = 0; mode < 3; ++mode)
    {
        auto mbmp = MaskedBitmapMBMP::PmbmpNew(sprite, 7, 5, &spriteBounds, 5, 7, 0, mode ? fmbmpMask : fmbmpNil,
                                               mode == 1 ? 17 : 0);
        if (!mbmp)
            return 1;
        for (int index = 0; index < 4; ++index)
        {
            for (int i = 0; i < width * height; ++i)
                static_cast<std::uint32_t *>(bits)[i] = portable[i] = 0x173859;
            GraphicsDrawingData drawing = {};
            RC graphicsClip;
            drawing.prcsClip = &(graphicsClip = RC(nativeClip));
            SystemRectangle nativeDestination = destinations[index];
            port->DrawMbmp(mbmp, &nativeDestination, &drawing);
            GraphicsPort::Flush();
            if (!mbmp->FDraw32(surface, destinations[index], palette, 256, &clip, region))
                return 1;
            for (int i = 0; i < width * height; ++i)
                if ((static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF) != (portable[i] & 0xFFFFFF))
                {
                    std::fprintf(stderr, "GDI masked bitmap mismatch: mode=%d case=%d at (%d,%d) GDI=%x CPU=%x\n", mode,
                                 index, i % width, i / width, static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF,
                                 portable[i] & 0xFFFFFF);
                    return 1;
                }
            ++maskCases;
            std::memset(indexedBits, 125, indexedPitch * height);
            std::fill(indexedPixels.begin(), indexedPixels.end(), 125);
            indexedPort->DrawMbmp(mbmp, &nativeDestination, &drawing);
            GraphicsPort::Flush();
            if (!mbmp->FDraw8(indexedSurface, destinations[index], &clip, region))
                return 1;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < indexedPitch; ++x)
                {
                    const auto cpuIndex = indexedPixels[y * indexedPitch + x];
                    if (x >= width)
                    {
                        if (cpuIndex != 125)
                            return 1;
                        continue;
                    }
                    const auto gdiIndex = static_cast<byte *>(indexedBits)[y * indexedPitch + x];
                    if (std::memcmp(&palette[cpuIndex], &palette[gdiIndex], 3))
                    {
                        std::fprintf(stderr, "GDI indexed masked mismatch: mode=%d case=%d at (%d,%d) GDI=%d CPU=%d\n",
                                     mode, index, x, y, gdiIndex, cpuIndex);
                        return 1;
                    }
                }
            ++maskCases;
        }
        ReleasePpo(&mbmp);
    }
    ReleasePpo(&indexedPort);
    SelectObject(indexedDc, previousIndexedBitmap);
    DeleteObject(indexedBitmap);
    DeleteDC(indexedDc);
    struct
    {
        BITMAPINFOHEADER header;
        RGBQUAD colors[256];
    } indexedInfo = {};
    indexedInfo.header = info.bmiHeader;
    indexedInfo.header.biBitCount = 8;
    indexedInfo.header.biClrUsed = 256;
    for (int i = 0; i < 256; ++i)
    {
        indexedInfo.colors[i] = {palette[i].bBlue, palette[i].bGreen, palette[i].bRed, 0};
    }
    sourceDc = CreateCompatibleDC(nullptr);
    sourceBitmap = CreateDIBSection(sourceDc, reinterpret_cast<BITMAPINFO *>(&indexedInfo), DIB_RGB_COLORS, &sourceBits,
                                    nullptr, 0);
    if (!sourceDc || !sourceBitmap || !sourceBits)
        return 1;
    previousSourceBitmap = SelectObject(sourceDc, sourceBitmap);
    sourcePort = GraphicsPort::PgptNew(sourceDc);
    if (!sourcePort)
        return 1;
    SelectPalette(sourceDc, nativePalette, FALSE);
    RealizePalette(sourceDc);
    for (int i = 0; i < 40; ++i)
        static_cast<byte *>(sourceBits)[i] = static_cast<byte>(i * 47);
    KauaiPixels::Indexed8 indexed = {sourceBits, 7, 5, 8, palette, 256};
    for (int index = 0; index < 4; ++index)
    {
        for (int i = 0; i < width * height; ++i)
            static_cast<std::uint32_t *>(bits)[i] = portable[i] = 0x171819;
        GraphicsDrawingData drawing = {};
        RC graphicsClip;
        drawing.prcsClip = &(graphicsClip = RC(nativeClip));
        SystemRectangle nativeSource = sources[index], nativeDestination = destinations[index];
        port->CopyPixels(sourcePort, &nativeSource, &nativeDestination, &drawing);
        GraphicsPort::Flush();
        if (!KauaiPixels::CopyIndexed(surface, indexed, sources[index], destinations[index], &clip, region))
            return 1;
        for (int i = 0; i < width * height; ++i)
            if ((static_cast<std::uint32_t *>(bits)[i] & 0xFFFFFF) != (portable[i] & 0xFFFFFF))
            {
                std::fprintf(stderr, "GDI indexed copy mismatch: case=%d at (%d,%d)\n", index, i % width, i / width);
                return 1;
            }
    }
    ReleasePpo(&sourcePort);
    SelectObject(sourceDc, previousSourceBitmap);
    DeleteObject(sourceBitmap);
    DeleteDC(sourceDc);
    ReleasePpo(&region);
    ReleasePpo(&port);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DeleteObject(nativePalette);
    std::printf("Portable drawing matches GDI: %d rectangles/ovals, 4 RGB oval adapters, %d polygons, %d lines, 8 "
                "RGB/indexed stretches, %d "
                "masked stretches.\n",
                cases, polygonCases, lineCases, maskCases);
    return 0;
}
