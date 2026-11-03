/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_buffer.h"
#include <cstdio>
#include <cstring>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
struct Dib
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    byte *pixels = nullptr;
    int pitch = 0;
    bool Init(int width, int height, int depth, const Color *colors)
    {
        struct
        {
            BITMAPINFOHEADER header;
            RGBQUAD colors[256];
        } info = {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = 1;
        info.header.biBitCount = static_cast<WORD>(depth);
        info.header.biClrUsed = depth <= 8 ? 1 << depth : 0;
        std::memcpy(info.colors, colors, sizeof(info.colors));
        if (depth == 1)
        {
            info.colors[0] = {};
            info.colors[1] = {255, 255, 255, 0};
        }
        if (depth == 16 || depth == 32)
        {
            info.header.biCompression = BI_BITFIELDS;
            auto masks = reinterpret_cast<DWORD *>(info.colors);
            masks[0] = depth == 16 ? 0x7C00 : 0xFF0000;
            masks[1] = depth == 16 ? 0x03E0 : 0x00FF00;
            masks[2] = depth == 16 ? 0x001F : 0x0000FF;
        }
        dc = CreateCompatibleDC(nullptr);
        void *bits = nullptr;
        bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
        pixels = static_cast<byte *>(bits);
        pitch = ((width * depth + 31) / 32) * 4;
        if (!dc || !bitmap || !pixels)
            return false;
        previous = SelectObject(dc, bitmap);
        return true;
    }
    ~Dib()
    {
        if (previous)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }
};
static bool Same(const byte *expected, const byte *actual, int pitch, int depth, int step)
{
    for (int y = 0; y < 29; ++y)
        for (int x = 0; x < pitch; ++x)
            if (!(depth == 32 && x % 4 == 3) && expected[y * pitch + x] != actual[y * pitch + x])
            {
                std::fprintf(stderr, "Pixel storage mismatch: depth=%d step=%d byte=%d,%d expected=%u actual=%u\n",
                             depth, step, x, y, expected[y * pitch + x], actual[y * pitch + x]);
                return false;
            }
    return true;
}
static bool IndexedPaletteChanges(PDynamicArray initialPalette, const Color *initialColors, PPicture picture)
{
    struct
    {
        WORD version = 0x300, count = 256;
        PALETTEENTRY entries[256] = {};
    } identityInfo;
    for (int i = 0; i < 256; ++i)
    {
        identityInfo.entries[i].peRed = static_cast<byte>(i);
        identityInfo.entries[i].peFlags = PC_EXPLICIT;
    }
    struct PaletteOwner
    {
        HPALETTE handle;
        ~PaletteOwner()
        {
            if (handle)
                DeleteObject(handle);
        }
    } identity{CreatePalette(reinterpret_cast<LOGPALETTE *>(&identityInfo))};
    if (!identity.handle)
        return false;
    for (int depth : {4, 8})
        for (int earlyMirror = 0; earlyMirror < 2; ++earlyMirror)
            for (int releasePrivate = 0; releasePrivate < 2; ++releasePrivate)
            {
                Dib dib;
                RC bounds(0, 0, 37, 29);
                if (!dib.Init(37, 29, depth, initialColors))
                    return false;
                // Match an offscreen port's logical identity palette, rather than
                // the default palette of a borrowed native DC.
                SelectPalette(dib.dc, identity.handle, FALSE);
                RealizePalette(dib.dc);
                SelectObject(dib.dc, GetStockObject(NULL_PEN));
                auto actual = GraphicsPort::PgptNewOffscreen(&bounds, depth);
                auto privatePalette = DynamicArray::PglNew(sizeof(Color), 256);
                auto globalPalette = DynamicArray::PglNew(sizeof(Color), 256);
                if (!actual || !privatePalette || !globalPalette || !privatePalette->FSetIvMac(256) ||
                    !globalPalette->FSetIvMac(256))
                    return false;
#ifdef KAUAI_SDL3
                if (actual->FHasNativeMirror() || actual->FHasNativeContext())
                    return false;
#endif
                auto pixels = actual->PrgbLockPixels();
                if (!pixels)
                    return false;
                for (int i = 0; i < dib.pitch * 29; ++i)
                    pixels[i] = dib.pixels[i] = static_cast<byte>(i * 37);
                SystemRectangle rectangle = {1, 2, 36, 27};
                GraphicsDrawingData drawing = {};
                auto drawPicture = [&]() {
                    const int saved = SaveDC(dib.dc);
                    const bool played = saved && PlayEnhMetaFile(dib.dc, picture->Hpic(), &rectangle);
                    if (saved)
                        RestoreDC(dib.dc, saved);
                    actual->DrawPic(picture, &rectangle, &drawing);
                    return played;
                };
                if (earlyMirror && !drawPicture())
                    return false;
                Color privateColors[256], globalColors[256];
                for (int i = 0; i < 256; ++i)
                {
                    privateColors[i] = {static_cast<byte>(i * 17), static_cast<byte>(i * 37), static_cast<byte>(i * 73),
                                        0};
                    globalColors[i] = {static_cast<byte>(255 - i), static_cast<byte>(i * 53), static_cast<byte>(i * 19),
                                       0};
                    privatePalette->Put(i, &privateColors[i]);
                    globalPalette->Put(i, &globalColors[i]);
                }
                actual->SetOffscreenColors(privatePalette);
                SetDIBColorTable(dib.dc, 0, 256, reinterpret_cast<RGBQUAD *>(privateColors));
                // A short update retains the remaining private colors.
                const int partialCount = depth == 4 ? 3 : 17;
                if (!privatePalette->FSetIvMac(partialCount))
                    return false;
                for (int i = 0; i < partialCount; ++i)
                    privatePalette->Put(i, &globalColors[i]);
                actual->SetOffscreenColors(privatePalette);
                SetDIBColorTable(dib.dc, 0, partialCount, reinterpret_cast<RGBQUAD *>(globalColors));
                GraphicsPort::SetActiveColors(globalPalette, fpalIdentity);
                if (releasePrivate)
                {
                    actual->SetOffscreenColors();
                    auto active = GraphicsPort::PglclrGetPalette();
                    if (!active || active->IvMac() != 256)
                        return false;
                    SetDIBColorTable(dib.dc, 0, 256, static_cast<RGBQUAD *>(active->QvGet(0)));
                    ReleasePpo(&active);
                }
                drawing.grfgdd = fgddFill;
                drawing.acrFore = AbstractColor(71, 112, 205);
                auto brush = CreateSolidBrush(PALETTERGB(71, 112, 205));
                if (!brush)
                    return false;
                auto oldBrush = SelectObject(dib.dc, brush);
                Rectangle(dib.dc, rectangle.left, rectangle.top, rectangle.right + 1, rectangle.bottom + 1);
                SelectObject(dib.dc, oldBrush);
                DeleteObject(brush);
                actual->DrawRcs(&rectangle, &drawing);
                GraphicsPort::Flush();
                if (!Same(dib.pixels, pixels, dib.pitch, depth, -2))
                    return false;
#ifdef KAUAI_SDL3
                if (actual->FHasNativeMirror() != bool(earlyMirror) || actual->FHasNativeContext() != bool(earlyMirror))
                    return false;
#endif
                if (!drawPicture())
                    return false;
                GraphicsPort::Flush();
                if (!Same(dib.pixels, pixels, dib.pitch, depth, -3))
                    return false;
#ifdef KAUAI_SDL3
                if (!actual->FHasNativeMirror() || !actual->FHasNativeContext())
                    return false;
#endif
                actual->Unlock();
                ReleasePpo(&actual);
                GraphicsPort::SetActiveColors(initialPalette, fpalIdentity);
                ReleasePpo(&privatePalette);
                ReleasePpo(&globalPalette);
            }
    return true;
}
int main()
{
#ifndef KAUAI_SDL3
    vwig.hdcApp = GetDC(nullptr);
    if (!vwig.hdcApp)
        return 1;
#endif
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!palette || !palette->FSetIvMac(256))
        return 1;
    for (int i = 0; i < 256; ++i)
    {
        Color color = {static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0};
        palette->Put(i, &color);
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    auto active = GraphicsPort::PglclrGetPalette();
    if (!active || active->IvMac() != 256)
        return 1;
    Color colors[256];
    std::memcpy(colors, active->QvGet(0), sizeof(colors));
    auto recording = CreateEnhMetaFile(nullptr, nullptr, nullptr, nullptr);
    if (!recording)
        return 1;
    Rectangle(recording, 1, 1, 12, 13);
    auto metafile = CloseEnhMetaFile(recording);
    RC pictureBounds(0, 0, 14, 15);
    auto picture = Picture::PpicNew(metafile, &pictureBounds);
    if (!picture)
        return 1;
    int cases = 0, scrollCases = 0;
    const auto baseline = KauaiPixels::PixelBuffers();
    if (!IndexedPaletteChanges(palette, colors, picture))
        return 1;
#ifdef KAUAI_SDL3
    // Destruction and palette bookkeeping must also work before a native
    // bitmap has ever been requested (including ports with a nonzero base).
    for (int depth : {1, 4, 8, 16, 24, 32})
        for (int repeat = 0; repeat < 4; ++repeat)
        {
            const int pitch = ((37 * depth + 31) / 32) * 4;
            RC offsetBounds(7, 11, 44, 40), lockedBounds, expectedBounds(0, 0, 37, 29);
            auto lazy = GraphicsPort::PgptNewOffscreen(&offsetBounds, depth);
            if (!lazy || lazy->FHasNativeMirror() || lazy->FHasNativeContext() || lazy->CbitPixel() != depth ||
                lazy->CbRow() != pitch)
                return 1;
            auto bytes = lazy->PrgbLockPixels(&lockedBounds);
            if (!bytes || lockedBounds != expectedBounds)
                return 1;
            for (int i = 0; i < pitch * 29; ++i)
                if (bytes[i] != 0)
                    return 1;
            lazy->Unlock();
            lazy->SetOffscreenColors(palette);
            if (lazy->FHasNativeMirror() || lazy->FHasNativeContext())
                return 1;
            if (repeat == 0 && depth == 32)
            {
                // A CPU-owned source also needs a mirror when a native-only
                // destination reads it, even if the source has never been drawn.
                Dib exported;
                if (!exported.Init(37, 29, 32, colors))
                    return 1;
                auto sink = GraphicsPort::PgptNew(exported.dc);
                if (!sink)
                    return 1;
                bytes = lazy->PrgbLockPixels();
                for (int i = 0; i < pitch * 29; ++i)
                    bytes[i] = static_cast<byte>(i * 37);
                SystemRectangle all = expectedBounds;
                GraphicsDrawingData copy = {};
                sink->CopyPixels(lazy, &all, &all, &copy);
                GraphicsPort::Flush();
                if (!lazy->FHasNativeMirror() || !lazy->FHasNativeContext() ||
                    !Same(bytes, exported.pixels, 148, 32, -1))
                    return 1;
                lazy->Unlock();
                ReleasePpo(&sink);
            }
            ReleasePpo(&lazy);
        }
#endif
    for (int depth : {1, 4, 8, 16, 24, 32})
    {
        Dib dib;
        if (!dib.Init(37, 29, depth, colors))
            return 1;
        auto reference = GraphicsPort::PgptNew(dib.dc);
        RC bounds(0, 0, 37, 29), clip(2, 1, 35, 28), hole(13, 9, 24, 19);
        auto actual = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        auto region = Region::PregnNew(&clip);
        if (!actual || !reference || !region || !region->FDiffRc(&hole))
            return 1;
#ifdef KAUAI_SDL3
        if (KauaiPixels::PixelBuffers().liveBuffers != baseline.liveBuffers + 1)
            return 1;
        if (actual->FHasNativeMirror() || actual->FHasNativeContext())
            return 1;
#endif
        for (auto port : {reference, actual})
        {
            region->AddRef();
            PRegion owned = region;
            port->ClipToRegn(&owned);
        }
        auto pixels = actual->PrgbLockPixels();
        if (!pixels || actual->CbRow() != dib.pitch)
            return 1;
        for (int i = 0; i < dib.pitch * 29; ++i)
            pixels[i] = dib.pixels[i] = static_cast<byte>(i * 37);
        GraphicsDrawingData drawing = {};
        RC graphicsClip;
        SystemRectangle nativeClip = clip, rectangle = {1, 2, 36, 27};
        drawing.prcsClip = &(graphicsClip = RC(nativeClip));
        for (int step = 0; step < 8; ++step)
        {
            // External CPU edits between native calls must survive the next
            // upload, including edits through an already-locked pixel pointer.
            pixels[3 * dib.pitch] = dib.pixels[3 * dib.pitch] = static_cast<byte>(step * 17);
            drawing.grfgdd = fgddFill;
            drawing.acrFore = AbstractColor(27, 85, 190);
            if (step == 1 || step == 3)
            {
                drawing.grfgdd = fgddPattern | (step == 3 ? fgddFrame : 0);
                drawing.acrFore = kacrInvert;
                drawing.acrBack = AbstractColor(220, 40, 70);
                drawing.dxpPen = 3;
                drawing.dypPen = 4;
                for (int row = 0; row < 8; ++row)
                    drawing.apt.rgb[row] = static_cast<byte>(0xA6 ^ (row * 31));
            }
            for (auto port : {reference, actual})
                switch (step)
                {
                case 0:
                case 2:
                    port->DrawRcs(&rectangle, &drawing);
                    break;
                case 1:
                case 3:
                    port->DrawOval(&rectangle, &drawing);
                    break;
                case 4:
                case 7:
                    port->DrawPic(picture, &rectangle, &drawing);
                    break;
                case 5:
                    port->ScrollRcs(&rectangle, 2, -3, &drawing);
                    break;
                case 6: {
                    SystemRectangle source = {0, 0, 28, 23}, target = {4, 3, 32, 26};
                    port->CopyPixels(port, &source, &target, &drawing);
                    break;
                }
                }
            GraphicsPort::Flush();
            if (!Same(dib.pixels, pixels, dib.pitch, depth, step))
                return 1;
#ifdef KAUAI_SDL3
            if ((actual->FHasNativeMirror() != (step >= 4 || (depth == 1 && step >= 1)) ||
                 actual->FHasNativeContext() != (step >= 4 || (depth == 1 && step >= 1))))
            {
                std::fprintf(stderr, "Unexpected native mirror state at step %d\n", step);
                return 1;
            }
#endif
            ++cases;
        }
        // A native-only destination must read the latest CPU-owned source,
        // and copying it back must update CPU ownership before the next read.
        Dib exported, expectedExport;
        if (!exported.Init(37, 29, depth, colors) || !expectedExport.Init(37, 29, depth, colors))
            return 1;
        auto sink = GraphicsPort::PgptNew(exported.dc);
        auto referenceSink = GraphicsPort::PgptNew(expectedExport.dc);
        if (!sink || !referenceSink)
            return 1;
        pixels[4 * dib.pitch] = dib.pixels[4 * dib.pitch] = 231;
        SystemRectangle all = bounds;
        GraphicsDrawingData copy = {};
        sink->CopyPixels(actual, &all, &all, &copy);
        referenceSink->CopyPixels(reference, &all, &all, &copy);
        GraphicsPort::Flush();
        if (!Same(expectedExport.pixels, exported.pixels, dib.pitch, depth, 7))
            return 1;
        ++cases;
        exported.pixels[4 * dib.pitch + 1] = expectedExport.pixels[4 * dib.pitch + 1] = 183;
        actual->CopyPixels(sink, &all, &all, &copy);
        reference->CopyPixels(referenceSink, &all, &all, &copy);
        GraphicsPort::Flush();
        if (!Same(dib.pixels, pixels, dib.pitch, depth, 8))
            return 1;
        ++cases;
        // A fresh CPU-only port proves that scrolling never requests a GDI
        // context or mirror, including the monochrome and packed formats.
        actual->Unlock();
        ReleasePpo(&actual);
        actual = GraphicsPort::PgptNewOffscreen(&bounds, depth);
        if (!actual || !(pixels = actual->PrgbLockPixels()))
            return 1;
        {
            RC edge(0, 0, 6, 29), otherEdge(30, 0, 37, 29);
            auto edges = Region::PregnNew(&edge);
            auto empty = Region::PregnNew();
            auto rectangular = Region::PregnNew(&clip);
            if (!edges || !empty || !rectangular || !edges->FUnionRc(&otherEdge))
                return 1;
            PRegion regions[] = {nullptr, rectangular, region, edges, empty};
            SystemRectangle rectangles[] = {{0, 0, 37, 29},   {4, 3, 33, 26},  {-8, -6, 21, 24},
                                            {25, 17, 44, 38}, {10, 8, 10, 14}, {14, 12, 10, 8}};
            SystemRectangle clipping[] = {{-3, 5, 31, 27}, {11, 10, 18, 17}};
            for (int regionIndex = 0; regionIndex < 5; ++regionIndex)
            {
                for (auto port : {reference, actual})
                {
                    PRegion owned = regions[regionIndex];
                    if (owned)
                        owned->AddRef();
                    port->ClipToRegn(&owned);
                    ReleasePpo(&owned);
                }
                for (int clipIndex = 0; clipIndex < 3; ++clipIndex)
                    for (auto scrollRect : rectangles)
                        for (int dx :
                             {-40, -17, -16, -15, -9, -8, -7, -4, -3, -2, -1, 0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 40})
                            for (int dy : {-32, -10, -5, -1, 0, 1, 5, 10, 32})
                            {
                                for (int i = 0; i < dib.pitch * 29; ++i)
                                    pixels[i] = dib.pixels[i] = static_cast<byte>(i * 37 + scrollCases * 13);
                                GraphicsDrawingData scroll = {};
                                RC graphicsScrollClip;
                                scroll.prcsClip =
                                    clipIndex ? &(graphicsScrollClip = RC(clipping[clipIndex - 1])) : nullptr;
                                reference->ScrollRcs(&scrollRect, dx, dy, &scroll);
                                actual->ScrollRcs(&scrollRect, dx, dy, &scroll);
                                GraphicsPort::Flush();
                                if (!Same(dib.pixels, pixels, dib.pitch, depth, scrollCases))
                                {
                                    std::fprintf(stderr,
                                                 "Scroll case: region=%d clip=%d rect=%d,%d,%d,%d delta=%d,%d\n",
                                                 regionIndex, clipIndex, scrollRect.left, scrollRect.top,
                                                 scrollRect.right, scrollRect.bottom, dx, dy);
                                    return 1;
                                }
#ifdef KAUAI_SDL3
                                const bool malformed =
                                    scrollRect.left > scrollRect.right || scrollRect.top > scrollRect.bottom;
                                if (!malformed && (actual->FHasNativeMirror() || actual->FHasNativeContext()))
                                {
                                    std::fprintf(stderr, "Packed scroll allocated native resources at depth %d\n",
                                                 depth);
                                    return 1;
                                }
#endif
                                ++scrollCases;
                                // Malformed rectangles retain the native fallback. Give the next
                                // valid comparison a fresh port so it must stay native-free.
                                if (scrollRect.left > scrollRect.right || scrollRect.top > scrollRect.bottom)
                                {
                                    actual->Unlock();
                                    ReleasePpo(&actual);
                                    actual = GraphicsPort::PgptNewOffscreen(&bounds, depth);
                                    if (!actual || !(pixels = actual->PrgbLockPixels()))
                                        return 1;
                                    auto owned = regions[regionIndex];
                                    if (owned)
                                        owned->AddRef();
                                    actual->ClipToRegn(&owned);
                                }
                            }
            }
            ReleasePpo(&rectangular);
            ReleasePpo(&empty);
            ReleasePpo(&edges);
        }
        actual->Unlock();
        ReleasePpo(&sink);
        ReleasePpo(&referenceSink);
        ReleasePpo(&actual);
        ReleasePpo(&reference);
        ReleasePpo(&region);
    }
    if (KauaiPixels::PixelBuffers().liveBuffers != baseline.liveBuffers ||
        KauaiPixels::PixelBuffers().liveBytes != baseline.liveBytes)
        return 1;
    ReleasePpo(&picture);
    ReleasePpo(&active);
    ReleasePpo(&palette);
    if (vwig.hdcApp)
        ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    std::printf("Pixel storage: %d mixed drawing/export comparisons across six packed formats; %d indexed/RGB scroll "
                "comparisons passed.\n",
                cases, scrollCases);
    return 0;
}
