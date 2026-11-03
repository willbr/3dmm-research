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
struct Dib
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    byte *pixels = nullptr;
    int width = 0, height = 0, pitch = 0, depth = 0;
    Color palette[256] = {};
    bool Init(int w, int h, int bits, unsigned palettePass)
    {
        width = w;
        height = h;
        depth = bits;
        pitch = ((w * bits + 31) / 32) * 4;
        struct
        {
            BITMAPINFOHEADER header;
            Color colors[256];
        } info = {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = w;
        info.header.biHeight = -h;
        info.header.biPlanes = 1;
        info.header.biBitCount = static_cast<WORD>(bits);
        info.header.biClrUsed = bits <= 8 ? 1u << bits : 0;
        for (unsigned i = 0; i < 256; ++i)
            info.colors[i] = palette[i] = palettePass == 0 ? Color{byte(i), byte(i), byte(i), 0}
                                                           : Color{byte(i * 73), byte(i * 17), byte(i * 37), 0};
        if (bits == 1)
        {
            info.colors[0] = palette[0] = {};
            info.colors[1] = palette[1] = {255, 255, 255, 0};
        }
        if (bits == 16 || bits == 32)
        {
            info.header.biCompression = BI_BITFIELDS;
            DWORD masks[] = {bits == 16 ? 0x7C00u : 0xFF0000u, bits == 16 ? 0x3E0u : 0xFF00u,
                             bits == 16 ? 0x1Fu : 0xFFu};
            std::memcpy(info.colors, masks, sizeof(masks));
        }
        dc = CreateCompatibleDC(nullptr);
        void *storage = nullptr;
        bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &storage, nullptr, 0);
        if (!dc || !bitmap || !storage)
            return false;
        pixels = static_cast<byte *>(storage);
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
    KauaiPixels::PackedSurface Surface(void *data = nullptr) const
    {
        return {data ? data : pixels,       width, height, pitch, depth, depth <= 8 ? palette : nullptr,
                depth <= 8 ? 1 << depth : 0};
    }
};
int main()
{
    const int width = 37, height = 29, sw = 11, sh = 9;
    vwig.hdcApp = GetDC(nullptr);
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!vwig.hdcApp || !palette || !palette->FSetIvMac(256))
        return 1;
    for (int i = 0; i < 256; ++i)
    {
        Color color{byte(i), byte(i), byte(i), 0};
        palette->Put(i, &color);
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    unsigned cases = 0, nativePaletteCases = 0;
#ifdef KAUAI_SDL3
    constexpr bool checkAdapter = true;
#else
    // Only SDL builds use the new portable GraphicsPort adapter. The native
    // build still exercises the SDK-free copy routine against direct DIBs.
    constexpr bool checkAdapter = false;
#endif
    for (int sourceDepth : {1, 4, 8, 16, 24, 32})
        for (int destinationDepth : {1, 4, 8, 16, 24, 32})
            for (unsigned sourcePalette = 0; sourcePalette < 2; ++sourcePalette)
                for (unsigned destinationPalette = 0; destinationPalette < 2; ++destinationPalette)
                {
                    Dib source, destination;
                    if (!source.Init(sw, sh, sourceDepth, sourcePalette) ||
                        !destination.Init(width, height, destinationDepth, destinationPalette))
                        return 1;
                    Dib coverageSource, coverageDestination;
                    if (destinationDepth == 8 && sourceDepth >= 16)
                    {
                        if (!coverageSource.Init(sw, sh, 8, 0) || !coverageDestination.Init(width, height, 8, 0))
                            return 1;
                        GdiFlush();
                        std::memset(coverageSource.pixels, 255, coverageSource.pitch * sh);
                    }
                    RC bounds(0, 0, width, height), sourceBounds(0, 0, sw, sh);
                    auto actualPort = GraphicsPort::PgptNewOffscreen(&bounds, destinationDepth);
                    auto sourcePort = GraphicsPort::PgptNewOffscreen(&sourceBounds, sourceDepth);
                    if (!actualPort || !sourcePort)
                        return 1;
                    for (int i = 0; i < 256; ++i)
                        palette->Put(i, &source.palette[i]);
                    sourcePort->SetOffscreenColors(palette);
                    for (int i = 0; i < 256; ++i)
                        palette->Put(i, &destination.palette[i]);
                    actualPort->SetOffscreenColors(palette);
                    auto adapterPixels = actualPort->PrgbLockPixels();
                    auto adapterSource = sourcePort->PrgbLockPixels();
                    if (!adapterPixels || !adapterSource)
                        return 1;
                    if (actualPort->CbRow() != destination.pitch || sourcePort->CbRow() != source.pitch)
                    {
                        std::fprintf(stderr, "Pitch mismatch %d->%d source %ld/%d destination %ld/%d\n", sourceDepth,
                                     destinationDepth, sourcePort->CbRow(), source.pitch, actualPort->CbRow(),
                                     destination.pitch);
                        return 1;
                    }
                    SetTextColor(destination.dc, PALETTERGB(255, 255, 255));
                    SetBkColor(destination.dc, PALETTERGB(0, 0, 0));
                    SetBkColor(source.dc, RGB(255, 255, 255));
                    SetStretchBltMode(destination.dc, COLORONCOLOR);
                    for (int i = 0; i < source.pitch * sh; ++i)
                        adapterSource[i] = source.pixels[i] = static_cast<byte>(i * 37 + (i / source.pitch) * 17);
                    std::vector<byte> actual(destination.pitch * height);
                    RC clipping(2, 1, 24, 22), regionBounds(0, 0, 35, 27), hole(9, 7, 15, 15);
                    auto region = Region::PregnNew(&regionBounds);
                    if (!region || !region->FDiffRc(&hole))
                        return 1;
                    for (int clipped = 0; clipped < 2; ++clipped)
                    {
                        SelectClipRgn(destination.dc, clipped ? region->HrgnEnsure() : nullptr);
                        if (clipped)
                            IntersectClipRect(destination.dc, clipping.xpLeft, clipping.ypTop, clipping.xpRight,
                                              clipping.ypBottom);
                        PRegion owned = nullptr;
                        if (clipped)
                        {
                            region->AddRef();
                            owned = region;
                        }
                        actualPort->ClipToRegn(&owned);
                        for (auto src : {sourceBounds, RC(2, 1, 9, 7), RC(-2, -1, 9, 8), RC(3, 2, 13, 11)})
                            for (int dw : {1, 5, 10, 11, 12, 17, 23})
                                for (int dh : {1, 5, 8, 9, 10, 13, 19})
                                {
                                    for (unsigned i = 0; i < actual.size(); ++i)
                                        adapterPixels[i] = actual[i] = destination.pixels[i] =
                                            static_cast<byte>(i * 19 + 23);
                                    RC dst(clipped ? -3 : 3, clipped ? -2 : 2, (clipped ? -3 : 3) + dw,
                                           (clipped ? -2 : 2) + dh);
                                    StretchBlt(destination.dc, dst.xpLeft, dst.ypTop, dw, dh, source.dc, src.xpLeft,
                                               src.ypTop, src.Dxp(), src.Dyp(), SRCCOPY);
                                    SystemRectangle nativeSource = src, nativeDestination = dst;
                                    GraphicsDrawingData drawing = {};
                                    RC graphicsClip;
                                    SystemRectangle nativeClip = clipping;
                                    if (clipped)
                                        drawing.prcsClip = &(graphicsClip = RC(nativeClip));
#ifdef KAUAI_SDL3
                                    actualPort->CopyPixels(sourcePort, &nativeSource, &nativeDestination, &drawing);
#endif
                                    GdiFlush();
                                    const bool nativePalette = sourceDepth >= 16 && destinationDepth == 8 &&
                                                               std::abs(src.Dxp() - dw) <= 1 &&
                                                               std::abs(src.Dyp() - dh) <= 1;
                                    const bool copied = KauaiPixels::CopyPacked(
                                        destination.Surface(actual.data()), source.Surface(), src, dst,
                                        clipped ? &clipping : nullptr, clipped ? region : nullptr);
                                    if (copied == nativePalette)
                                        return 1;
                                    if (nativePalette)
                                    {
                                        // GDI's independently-created inverse palettes may disagree.
                                        // Check native fallback coverage with an exact indexed-copy
                                        // oracle; color fidelity for that policy remains unproven.
                                        ++nativePaletteCases;
                                        std::memset(coverageDestination.pixels, 0, coverageDestination.pitch * height);
                                        SelectClipRgn(coverageDestination.dc, clipped ? region->HrgnEnsure() : nullptr);
                                        if (clipped)
                                            IntersectClipRect(coverageDestination.dc, clipping.xpLeft, clipping.ypTop,
                                                              clipping.xpRight, clipping.ypBottom);
                                        SetStretchBltMode(coverageDestination.dc, COLORONCOLOR);
                                        if (!StretchBlt(coverageDestination.dc, dst.xpLeft, dst.ypTop, dw, dh,
                                                        coverageSource.dc, src.xpLeft, src.ypTop, src.Dxp(), src.Dyp(),
                                                        SRCCOPY))
                                            return 1;
                                        GdiFlush();
                                    }
                                    if (nativePalette)
                                        for (unsigned i = 0; i < actual.size(); ++i)
                                            if (actual[i] != static_cast<byte>(i * 19 + 23))
                                                return 1;
                                    for (int y = 0; y < height; ++y)
                                        for (int x = 0; x < destination.pitch; ++x)
                                            if (!(destinationDepth == 32 && (x & 3) == 3) &&
                                                ((!nativePalette &&
                                                  actual[y * destination.pitch + x] !=
                                                      destination.pixels[y * destination.pitch + x]) ||
                                                 (checkAdapter &&
                                                  (!nativePalette ||
                                                   !coverageDestination.pixels[y * destination.pitch + x]) &&
                                                  adapterPixels[y * destination.pitch + x] !=
                                                      (nativePalette
                                                           ? static_cast<byte>((y * destination.pitch + x) * 19 + 23)
                                                           : destination.pixels[y * destination.pitch + x]))))
                                            {
                                                std::fprintf(
                                                    stderr,
                                                    "Packed copy mismatch %d->%d palettes %u/%u size %dx%d byte %d,%d "
                                                    "expected %u actual %u adapter %u src %d,%d,%d,%d\n",
                                                    sourceDepth, destinationDepth, sourcePalette, destinationPalette,
                                                    dw, dh, x, y, destination.pixels[y * destination.pitch + x],
                                                    actual[y * destination.pitch + x],
                                                    adapterPixels[y * destination.pitch + x], src.xpLeft, src.ypTop,
                                                    src.xpRight, src.ypBottom);
                                                return 1;
                                            }
                                    ++cases;
                                }
                    }
#ifdef KAUAI_SDL3
                    if (destinationDepth == 32 && (actualPort->FHasNativeMirror() || actualPort->FHasNativeContext() ||
                                                   sourcePort->FHasNativeMirror() || sourcePort->FHasNativeContext()))
                    {
                        std::fprintf(stderr, "Portable %d->32 copy allocated native drawing resources\n", sourceDepth);
                        return 1;
                    }
#endif
                    ReleasePpo(&region);
                    sourcePort->Unlock();
                    actualPort->Unlock();
                    ReleasePpo(&sourcePort);
                    ReleasePpo(&actualPort);
                }
    for (int depth : {1, 4, 8, 16, 24, 32})
    {
        Dib dib;
        if (!dib.Init(width, height, depth, 1))
            return 1;
        std::vector<byte> actual(dib.pitch * height);
        for (auto dst : {RC(3, 2, 22, 17), RC(0, 0, 19, 15), RC(1, 0, 20, 15), RC(0, 1, 19, 16), RC(3, 2, 30, 25)})
        {
            GdiFlush();
            for (unsigned i = 0; i < actual.size(); ++i)
                actual[i] = dib.pixels[i] = static_cast<byte>(i * 37 + 19);
            RC src(1, 1, 20, 16);
            SetStretchBltMode(dib.dc, COLORONCOLOR);
            if (!StretchBlt(dib.dc, dst.xpLeft, dst.ypTop, dst.Dxp(), dst.Dyp(), dib.dc, src.xpLeft, src.ypTop,
                            src.Dxp(), src.Dyp(), SRCCOPY))
                return 1;
            GdiFlush();
            auto self = dib.Surface(actual.data());
            if (!KauaiPixels::CopyPacked(self, self, src, dst))
                return 1;
            for (unsigned i = 0; i < actual.size(); ++i)
                if (!(depth == 32 && (i & 3) == 3) && actual[i] != dib.pixels[i])
                {
                    std::fprintf(stderr, "Packed self-copy mismatch depth %d offset %u expected %u actual %u\n", depth,
                                 i, dib.pixels[i], actual[i]);
                    return 1;
                }
            ++cases;
        }
    }
    ReleasePpo(&palette);
    GraphicsPort::SetActiveColors(nullptr, fpalIdentity);
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    std::printf(
        "Packed copies: %u exact native comparisons and %u native-palette fallback coverage/rejection checks passed.\n",
        cases - nativePaletteCases, nativePaletteCases);
}
