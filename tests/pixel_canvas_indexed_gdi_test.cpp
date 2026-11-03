/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
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
    const int width = 37, height = 29, pitch = 40;
    vwig.hdcApp = GetDC(nullptr);
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!vwig.hdcApp || !palette || !palette->FSetIvMac(256))
        return 1;
    Color colors[256];
    for (int i = 0; i < 256; ++i)
    {
        colors[i] = {static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0};
        palette->Put(i, &colors[i]);
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    auto active = GraphicsPort::PglclrGetPalette();
    if (!active)
        return 1;
    std::memcpy(colors, active->QvGet(0), sizeof(colors));
    struct
    {
        BITMAPINFOHEADER header;
        RGBQUAD palette[256];
    } info = {};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biClrUsed = 256;
    std::memcpy(info.palette, colors, sizeof(colors));
    auto dc = CreateCompatibleDC(nullptr);
    void *bits = nullptr;
    auto bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits)
        return 1;
    auto oldBitmap = SelectObject(dc, bitmap);
    auto reference = GraphicsPort::PgptNew(dc);
    RC bounds(0, 0, width, height), clip(2, 1, 35, 28), hole(9, 7, 19, 21);
    auto actual = GraphicsPort::PgptNewOffscreen(&bounds, 8);
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
    AbstractColor inks[] = {kacrClear,
                            kacrInvert,
                            AbstractColor(byte(53)),
                            AbstractColor(byte(214)),
                            AbstractColor(13, 98, 201),
                            AbstractColor(220, 14, 77),
                            AbstractColor(128, 128, 128),
                            AbstractColor(byte(128))};
    SystemRectangle rectangle = {-3, 3, 36, 26}, nativeClip = {2, 1, 35, 28};
    SystemPoint triangle[3] = {{-8, 4}, {31, 25}, {24, -6}};
    HQ polygon;
    if (!FAllocHq(&polygon, kcbOlyBase + 3 * sizeof(SystemPoint), fmemClear, mprNormal))
        return 1;
    auto oly = static_cast<OLY *>(PvLockHq(polygon));
    oly->cpts = 3;
    std::memcpy(oly->rgpts, triangle, sizeof(triangle));
    UnlockHq(polygon);
    int cases = 0;
    for (int palettePass = 0; palettePass < 4; ++palettePass)
    {
        if (palettePass)
        {
            for (int i = 0; i < 256; ++i)
            {
                colors[i] =
                    palettePass != 2
                        ? Color{static_cast<byte>(i * 17), static_cast<byte>(i * 37), static_cast<byte>(i * 73), 0}
                        : Color{static_cast<byte>(255 - i), static_cast<byte>(255 - i), static_cast<byte>(255 - i), 0};
                palette->Put(i, &colors[i]);
            }
            if (palettePass == 3)
            {
                colors[17] = colors[200] = Color{128, 128, 128, 0};
                colors[3] = colors[254] = Color{255, 255, 255, 0};
                colors[4] = colors[192] = Color{0, 0, 0, 0};
                for (int i = 0; i < 256; ++i)
                    palette->Put(i, &colors[i]);
            }
            SetDIBColorTable(dc, 0, 256, reinterpret_cast<RGBQUAD *>(colors));
            actual->SetOffscreenColors(palette);
        }
        for (int fore = 0; fore < 8; ++fore)
            for (int back = 0; back < 8; ++back)
                for (int shape = 0; shape < 15; ++shape)
                {
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < pitch; ++x)
                            static_cast<byte *>(bits)[y * pitch + x] = pixels[y * pitch + x] =
                                static_cast<byte>(y * 17 + x * 31);
                    GraphicsDrawingData drawing = {};
                    RC graphicsClip;
                    drawing.acrFore = inks[fore];
                    drawing.acrBack = inks[back];
                    drawing.prcsClip = &(graphicsClip = RC(nativeClip));
                    drawing.grfgdd = (shape == 0 || shape == 4 || shape == 8) ? fgddFill : fgddPattern;
                    if (shape == 2 || shape == 3)
                    {
                        drawing.grfgdd |= fgddFrame;
                        drawing.dxpPen = shape == 2 ? 3 : 30;
                        drawing.dypPen = 4;
                    }
                    if (shape >= 10)
                    {
                        drawing.grfgdd |= fgddFrame;
                        drawing.dxpPen = shape == 10 ? 3 : shape == 11 ? 30 : shape == 14 ? 4 : 0;
                        drawing.dypPen = shape == 13 || shape == 14 ? 0 : 4;
                    }
                    for (int row = 0; row < 8; ++row)
                        drawing.apt.rgb[row] = static_cast<byte>(0xA6 ^ (row * 31));
                    for (auto port : {reference, actual})
                    {
                        if (shape >= 8)
                            port->DrawOval(&rectangle, &drawing);
                        else if (shape < 4)
                            port->DrawRcs(&rectangle, &drawing);
                        else if (shape < 6)
                            port->DrawPoly(polygon, &drawing);
                        else if (shape == 6)
                        {
                            drawing.dxpPen = 3;
                            drawing.dypPen = 5;
                            SystemPoint a = {2, 3}, b = {32, 24};
                            port->DrawLine(&a, &b, &drawing);
                        }
                        else
                            port->HiliteRcs(&rectangle, &drawing);
                    }
                    GraphicsPort::Flush();
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < pitch; ++x)
                            if (static_cast<byte *>(bits)[y * pitch + x] != pixels[y * pitch + x])
                            {
                                std::fprintf(stderr,
                                             "Indexed draw mismatch: palette=%d fore=%d back=%d shape=%d at=%d,%d "
                                             "expected=%u actual=%u\n",
                                             palettePass, fore, back, shape, x, y,
                                             static_cast<byte *>(bits)[y * pitch + x], pixels[y * pitch + x]);
                                return 1;
                            }
                    ++cases;
                }
    }
    FreePhq(&polygon);
    actual->Unlock();
    ReleasePpo(&actual);
    ReleasePpo(&reference);
    ReleasePpo(&region);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
    ReleasePpo(&active);
    ReleasePpo(&palette);
    std::printf("Indexed shapes: %d native comparisons including palettes, patterns, clipping, inversion and row "
                "padding passed.\n",
                cases);
    return 0;
}
