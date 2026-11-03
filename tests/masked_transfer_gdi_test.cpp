/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
static int Test(int depth)
{
    const int width = 37, height = 29, pitch = ((37 * depth + 31) / 32) * 4;
    vwig.hdcApp = GetDC(nullptr);
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!vwig.hdcApp || !palette || !palette->FSetIvMac(256))
        return 1;
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
    if (depth == 1) info.colors[1] = {255, 255, 255, 0};
    auto dc = CreateCompatibleDC(nullptr);
    void *bits = nullptr;
    auto bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits)
        return 1;
    auto previous = SelectObject(dc, bitmap);
    SetStretchBltMode(dc, BLACKONWHITE);
    auto reference = GraphicsPort::PgptNew(dc);
    RC bounds(0, 0, width, height), clip(2, 1, 35, 28), hole(9, 7, 19, 21);
    auto actual = GraphicsPort::PgptNewOffscreen(&bounds, depth);
    auto region = Region::PregnNew(&clip);
    if (!reference || !actual || !region || !region->FDiffRc(&hole))
        return 1;
    for (auto port : {reference, actual})
    {
        region->AddRef();
        PRegion owned = region;
        port->ClipToRegn(&owned);
    }
    auto pixels = actual->PrgbLockPixels();
    if (!pixels || actual->CbRow() != pitch)
        return 1;
    byte sprite[35] = {};
    for (int i = 0; i < 35; ++i)
        sprite[i] = i % 4 ? static_cast<byte>(i * 7 + 10) : 0;
    for (int i = 14; i < 21; ++i)
        sprite[i] = 0;
    sprite[0] = 17;
    sprite[3] = 200;
    sprite[34] = 249;
    std::vector<RC> destinations = {RC(0, 0, 14, 10), RC(2, 3, 19, 17), RC(-4, 8, 12, 28),
                                    RC(3, 2, 6, 5),   RC(0, 0, 30, 2),  RC(5, 7, 12, 12)};
    for (int h = 1; h <= 8; ++h)
        for (int w = 1; w <= 11; ++w)
            if (w != 7 || h != 5)
                destinations.emplace_back(3, 2, 3 + w, 2 + h);
    int cases = 0;
    for (int stretchMode : {BLACKONWHITE, COLORONCOLOR})
    {
        SetStretchBltMode(dc, stretchMode);
        if (stretchMode == COLORONCOLOR)
        {
            // Legacy indexed CopyPixels sets this persistent DC mode. Exercise
            // that state through the public port API as Studio does.
            RC cell(5, 5, 6, 6);
            GraphicsDrawingData drawing = {};
            RC graphicsClip;
            SystemRectangle nativeCell = cell;
            drawing.prcsClip = &(graphicsClip = RC(nativeCell));
            actual->CopyPixels(actual, &nativeCell, &nativeCell, &drawing);
        }
        for (int sourcePass = 0; sourcePass < 3; ++sourcePass)
        {
            for (int i = 0; i < 256; ++i)
            {
                Color color = sourcePass ? Color{static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0}
                                         : Color{static_cast<byte>(i * 17), static_cast<byte>(i * 37),
                                                 static_cast<byte>(i * 73), 0};
                if (sourcePass && (i == 17 || i == 200))
                    color = {255, 255, 255, 0};
                palette->Put(i, &color);
            }
            if (sourcePass == 2)
            {
                auto shortPalette = DynamicArray::PglNew(sizeof(Color), 33);
                if (!shortPalette || !shortPalette->FSetIvMac(33))
                    return 1;
                std::memcpy(shortPalette->QvGet(0), palette->QvGet(0), 33 * sizeof(Color));
                GraphicsPort::SetActiveColors(shortPalette, fpalIdentity);
                ReleasePpo(&shortPalette);
            }
            else
                GraphicsPort::SetActiveColors(palette, fpalIdentity);
            auto effective = GraphicsPort::PglclrGetPalette();
            if (!effective)
                return 1;
            Color source[256] = {}, destination[256];
            std::memcpy(source, effective->QvGet(0), effective->IvMac() * sizeof(Color));
            ReleasePpo(&effective);
            for (int destinationPass = 0; destinationPass < (depth == 4 || depth == 8 ? 5 : 1); ++destinationPass)
            {
                for (int i = 0; i < 256; ++i)
                {
                    destination[i] =
                        destinationPass == 0   ? source[i]
                        : destinationPass == 1 ? source[(i + 137) & 255]
                        : destinationPass == 2
                            ? Color{static_cast<byte>(255 - i), static_cast<byte>(255 - i), static_cast<byte>(255 - i),
                                    0}
                            : Color{static_cast<byte>(i * 73), static_cast<byte>(i * 17), static_cast<byte>(i * 37), 0};
                }
                if (destinationPass == 4)
                {
                    destination[3] = destination[254] = {255, 255, 255, 0};
                    destination[4] = destination[192] = {0, 0, 0, 0};
                    destination[17] = destination[200] = {128, 128, 128, 0};
                }
                for (int i = 0; i < 256; ++i)
                    palette->Put(i, &destination[i]);
                if (depth == 4 || depth == 8)
                    SetDIBColorTable(dc, 0, 1 << depth, reinterpret_cast<RGBQUAD *>(destination));
                if (depth == 4 || depth == 8)
                    actual->SetOffscreenColors(palette);
                for (int mode = 0; mode < 4; ++mode)
                {
                    RC spriteBounds(0, 0, 7, 5);
                    auto mbmp = MaskedBitmapMBMP::PmbmpNew(sprite, 7, 5, &spriteBounds, 2, 3, 0, mode ? fmbmpMask : 0,
                                                           mode == 1   ? 0
                                                           : mode == 2 ? 17
                                                                       : 200);
                    if (!mbmp)
                        return 1;
                    RC sourceBounds;
                    mbmp->GetRc(&sourceBounds);
                    for (int target = 0; target < static_cast<int>(destinations.size()); ++target)
                    {
                        for (int y = 0; y < height; ++y)
                            for (int x = 0; x < pitch; ++x)
                                static_cast<byte *>(bits)[y * pitch + x] = pixels[y * pitch + x] =
                                    static_cast<byte>(y * 17 + x * 31);
                        GraphicsDrawingData drawing = {};
                        RC graphicsClip;
                        SystemRectangle nativeClip = clip, nativeDestination = destinations[target];
                        drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                        if (depth == 1)
                        {
                            GraphicsDrawingData fill = drawing;
                            fill.grfgdd = fgddFill;
                            fill.acrFore = AbstractColor(13, 98, 201);
                            SystemRectangle swatch = {22, 22, 30, 24};
                            reference->DrawRcs(&swatch, &fill);
                            actual->DrawRcs(&swatch, &fill);
                        }
                        if (target == 5 && depth == 8)
                        {
                            // Unscaled private-palette drawing historically writes raw
                            // indices; preserve that separate path as well.
                            mbmp->Draw(static_cast<byte *>(bits), pitch, height,
                                       destinations[target].xpLeft - sourceBounds.xpLeft,
                                       destinations[target].ypTop - sourceBounds.ypTop, &clip, region);
                        }
                        else
                            reference->DrawMbmp(mbmp, &nativeDestination, &drawing);
                        actual->DrawMbmp(mbmp, &nativeDestination, &drawing);
                        if (depth == 1)
                        {
                            GraphicsDrawingData fill = drawing;
                            fill.grfgdd = fgddFill;
                            fill.acrFore = AbstractColor(14, 99, 202);
                            SystemRectangle swatch = {22, 22, 30, 24};
                            reference->DrawRcs(&swatch, &fill);
                            actual->DrawRcs(&swatch, &fill);
                        }
                        GraphicsPort::Flush();
                        for (int y = 0; y < height; ++y)
                            for (int x = 0; x < pitch; ++x)
                                if (static_cast<byte *>(bits)[y * pitch + x] != pixels[y * pitch + x])
                                {
                                    std::fprintf(stderr,
                                                 "Masked transfer mismatch: stretch=%d source=%d destination=%d "
                                                 "mode=%d target=%d "
                                                 "at=%d,%d expected=%u actual=%u\n",
                                                 stretchMode, sourcePass, destinationPass, mode, target, x, y,
                                                 static_cast<byte *>(bits)[y * pitch + x], pixels[y * pitch + x]);
                                    return 1;
                                }
#ifdef KAUAI_SDL3
                        if (depth != 8 && (actual->FHasNativeContext() || actual->FHasNativeMirror()))
                            return 1;
#endif
                        ++cases;
                    }
                    ReleasePpo(&mbmp);
                }
            }
        }
    }
    actual->Unlock();
    ReleasePpo(&actual);
    ReleasePpo(&reference);
    ReleasePpo(&region);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    ReleasePpo(&palette);
    std::printf("Masked transfer at depth %d: %d native/raw-index comparisons passed.\n", depth, cases);
    return 0;
}

int main()
{
    return Test(8) || Test(1) || Test(4) || Test(16) || Test(24);
}
