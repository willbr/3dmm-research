/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dib_frame.h"
#include "pixel_canvas.h"
#include "dib_frame_fixture.h"
#include <cstdio>
#include <cstring>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
int main()
{
    constexpr int width = 40, height = 32;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = 40;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    auto dc = CreateCompatibleDC(nullptr);
    void *bits = nullptr;
    auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap)
        return 1;
    auto previous = SelectObject(dc, bitmap);
    auto draw = DrawDibOpen();
    if (!draw)
        return 1;
    RC portBounds(0, 0, width, height);
#ifdef KAUAI_SDL3
    auto port = GraphicsPort::PgptNewOffscreen(&portBounds, 32);
    if (!port)
        return 1;
#endif
    unsigned cases = 0, fallbackCases = 0;
    for (int depth : {24, 32})
        for (bool bottomUp : {true, false})
            for (int w : {1, 3, 7, 16, 31, 63, 320})
                for (int h : {1, 3, 4, 9, 17, 240})
                    for (int resize = 0; resize < 9; ++resize)
                        for (int clipped = 0; clipped < 2; ++clipped)
                            for (int mode : {BLACKONWHITE, COLORONCOLOR})
                            {
                                auto input = MakeDib(w, h, depth, bottomUp);
                                KauaiPixels::DibFrame frame;
                                if (!frame.Read(input.data(), input.size()))
                                    return 1;
                                RC destination(2, 3,
                                               2 + (resize == 0   ? w
                                                    : resize == 1 ? w + 1
                                                    : resize == 2 ? w * 2
                                                    : resize == 3 ? (w + 1) / 2
                                                    : resize == 4 ? (w * 3 + 1) / 2
                                                    : resize == 5 ? w + 3
                                                    : resize == 6 ? (w + 2) / 3
                                                    : resize == 7 ? w * 2
                                                    : w > 1       ? w - 1
                                                                  : 1),
                                               3 + (resize == 0   ? h
                                                    : resize == 1 ? h + 1
                                                    : resize == 2 ? h * 2
                                                    : resize == 3 ? (h + 1) / 2
                                                    : resize == 4 ? (h * 3 + 1) / 2
                                                    : resize == 5 ? h + 2
                                                    : resize == 6 ? h * 2
                                                    : resize == 7 ? (h + 2) / 3
                                                    : h > 1       ? h - 1
                                                                  : 1));
                                RC clip(3, 4, 35, 28), hole(8, 6, 17, 12);
                                if (!clipped)
                                    destination.Offset(-5, -5);
                                auto region = Region::PregnNew(&portBounds);
                                if (!region || !region->FDiffRc(&hole))
                                    return 1;
                                std::vector<std::uint32_t> actual(width * height, 0x123456);
                                for (int i = 0; i < width * height; ++i)
                                    static_cast<std::uint32_t *>(bits)[i] = actual[i];
                                SelectClipRgn(dc, clipped ? region->HrgnEnsure() : nullptr);
                                SetStretchBltMode(dc, mode);
                                if (clipped)
                                    IntersectClipRect(dc, clip.xpLeft, clip.ypTop, clip.xpRight, clip.ypBottom);
                                const bool drawn =
                                    DrawDibDraw(draw, dc, destination.xpLeft, destination.ypTop, destination.Dxp(),
                                                destination.Dyp(), reinterpret_cast<BITMAPINFOHEADER *>(input.data()),
                                                nullptr, 0, 0, -1, -1, DDF_BACKGROUNDPAL) != 0;
                                if (!drawn && bottomUp)
                                {
                                    std::fprintf(stderr, "Native DrawDib rejected depth %d %dx%d resize %d\n", depth, w,
                                                 h, resize);
                                    return 1;
                                }
                                GdiFlush();
                                if (!KauaiPixels::DrawDibFrame({actual.data(), width, height, width * 4}, frame,
                                                               destination, clipped ? &clip : nullptr,
                                                               clipped ? region : nullptr))
                                    return 1;
                                // DrawDibDraw's default source-height handling is
                                // not a logical top-down DIB read. Keep that native
                                // route in the adapter; the bounded decoder's
                                // top-down semantics have an independent pixel test.
                                if (bottomUp && std::memcmp(actual.data(), bits, width * height * 4))
                                {
                                    std::fprintf(
                                        stderr,
                                        "DIB native mismatch: depth %d, bottom %d, size %dx%d, resize %d, clip %d\n",
                                        depth, bottomUp, w, h, resize, clipped);
                                    for (int y = 0; y < height; ++y)
                                        for (int x = 0; x < width; ++x)
                                            if (actual[y * width + x] !=
                                                static_cast<std::uint32_t *>(bits)[y * width + x])
                                            {
                                                std::fprintf(stderr, "First pixel (%d,%d): portable %08x native %08x\n",
                                                             x, y, actual[y * width + x],
                                                             static_cast<std::uint32_t *>(bits)[y * width + x]);
                                                return 1;
                                            }
                                    return 1;
                                }
#ifdef KAUAI_SDL3
                                auto pixels = port->PrgbLockPixels();
                                for (int i = 0; i < width * height; ++i)
                                    reinterpret_cast<std::uint32_t *>(pixels)[i] = 0x123456;
                                port->Unlock();
                                PRegion owned = nullptr;
                                if (clipped)
                                {
                                    region->AddRef();
                                    owned = region;
                                }
                                port->ClipToRegn(&owned);
                                GraphicsDrawingData drawing = {};
                                RC graphicsClip;
                                SystemRectangle rectangle = destination, nativeClip = clip;
                                if (clipped)
                                    drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                                // A null native drawing handle makes accidental fallback observable.
                                port->DrawDib(bottomUp ? nullptr : draw,
                                              reinterpret_cast<BITMAPINFOHEADER *>(input.data()), &rectangle, &drawing);
                                pixels = port->PrgbLockPixels();
                                const bool same = std::memcmp(pixels, bits, width * height * 4) == 0;
                                port->Unlock();
                                if (!same)
                                {
                                    std::fputs("DIB adapter mismatch\n", stderr);
                                    return 1;
                                }
#endif
                                ReleasePpo(&region);
                                if (bottomUp)
                                    ++cases;
#ifdef KAUAI_SDL3
                                else
                                    ++fallbackCases;
#endif
                            }
#ifdef KAUAI_SDL3
    ReleasePpo(&port);
#endif
    DrawDibClose(draw);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    std::printf("DIB frames: %u native DrawDib comparisons passed", cases);
#ifdef KAUAI_SDL3
    std::printf("; actual SDL adapter and %u retained top-down fallback checks passed", fallbackCases);
#endif
    std::puts(".");
}
