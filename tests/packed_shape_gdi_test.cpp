/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
static int Fail(int line)
{
    std::fprintf(stderr, "Packed shape setup/check failed at line %d\n", line);
    return 1;
}
struct Dib
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    byte *pixels = nullptr;
    int pitch = 0;
    Color palette[256] = {};
    bool Init(int depth, int palettePass)
    {
        struct
        {
            BITMAPINFOHEADER header;
            Color colors[256];
        } info = {};
        info.header.biSize = sizeof(BITMAPINFOHEADER);
        info.header.biWidth = 37;
        info.header.biHeight = -29;
        info.header.biPlanes = 1;
        info.header.biBitCount = static_cast<WORD>(depth);
        if (depth == 16)
        {
            info.header.biCompression = BI_BITFIELDS;
            auto masks = reinterpret_cast<DWORD *>(info.colors);
            masks[0] = 0x7C00;
            masks[1] = 0x3E0;
            masks[2] = 0x1F;
        }
        if (depth <= 4)
        {
            const auto count = 1 << depth;
            info.header.biClrUsed = count;
            for (int i = 0; i < count; ++i)
            {
                const auto gray = static_cast<byte>((palettePass ? count - 1 - i : i) * 255 / (count - 1));
                palette[i] = {gray, gray, gray, 0};
                if (palettePass == 2)
                    palette[i] = {static_cast<byte>(i * 73 + 11), static_cast<byte>(i * 17 + 23),
                                  static_cast<byte>(i * 37 + 7), 0};
            }
            if (palettePass == 2)
            {
                palette[5] = {};
                palette[9] = {255, 255, 255, 0};
            }
            std::memcpy(info.colors, palette, count * sizeof(Color));
        }
        dc = CreateCompatibleDC(nullptr);
        void *storage = nullptr;
        bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &storage, nullptr, 0);
        if (!dc || !bitmap || !storage)
            return false;
        pixels = static_cast<byte *>(storage);
        pitch = ((37 * depth + 31) / 32) * 4;
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
static KauaiPixels::Ink TestInk(int index)
{
    constexpr std::uint32_t rgb[] = {0x0D62C9, 0xD22A08, 0xD2DCE6, 0xFFFFFF, 0};
    return {index >= 7  ? 0x0D62C9u
            : index < 5 ? rgb[index]
                        : 0,
            index == 5   ? KauaiPixels::InkMode::clear
            : index == 6 ? KauaiPixels::InkMode::invert
                         : KauaiPixels::InkMode::color};
}

int main()
{
    using namespace KauaiPixels;
#ifndef KAUAI_SDL3
    vwig.hdcApp = GetDC(nullptr);
    if (!vwig.hdcApp)
        return Fail(__LINE__);
#endif
    int cases = 0;
    RC bounds(0, 0, 37, 29), rectangle(-3, 3, 36, 26), clip(2, 1, 32, 28), regionBounds(1, 2, 34, 27),
        hole(6, 7, 17, 19);
    auto region = Region::PregnNew(&regionBounds);
    auto empty = Region::PregnNew();
    if (!region || !empty || !region->FDiffRc(&hole))
        return Fail(__LINE__);
    Point points[] = {{-2, 5}, {31, 1}, {26, 25}, {10, 12}, {3, 28}};
    HQ polygon = hqNil;
    if (!FAllocHq(&polygon, kcbOlyBase + 5 * sizeof(SystemPoint), fmemNil, mprNormal))
        return Fail(__LINE__);
    auto native = static_cast<OLY *>(PvLockHq(polygon));
    native->cpts = 5;
    for (int i = 0; i < 5; ++i)
    {
        native->rgpts[i].x = points[i].x;
        native->rgpts[i].y = points[i].y;
    }
    UnlockHq(polygon);
    AbstractColor colors[] = {AbstractColor(13, 98, 201),
                              AbstractColor(210, 42, 8),
                              AbstractColor(210, 220, 230),
                              AbstractColor(255, 255, 255),
                              AbstractColor(0, 0, 0),
                              kacrClear,
                              kacrInvert,
                              AbstractColor(11),
                              AbstractColor(12)};
    constexpr int colorCount = 9;
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!palette || !palette->FSetIvMac(256))
        return Fail(__LINE__);
    for (int i = 0; i < 256; ++i)
    {
        Color c{static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0};
        palette->Put(i, &c);
    }
    Color indexedColor{201, 98, 13, 0};
    palette->Put(11, &indexedColor);
    palette->Put(12, &indexedColor);
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    for (int depth : {1, 4, 16, 24})
        for (int palettePass = 0; palettePass < (depth == 4 ? 3 : 1); ++palettePass)
        {
            Dib dib;
            if (!dib.Init(depth, palettePass))
                return Fail(__LINE__);
            auto reference = GraphicsPort::PgptNew(dib.dc);
            auto adapter = GraphicsPort::PgptNewOffscreen(&bounds, depth);
            if (!reference || !adapter)
                return Fail(__LINE__);
            if (depth == 4)
            {
                for (int i = 0; i < 16; ++i)
                    palette->Put(i, &dib.palette[i]);
                adapter->SetOffscreenColors(palette);
            }
            auto adapterBytes = adapter->PrgbLockPixels();
            std::vector<byte> bytes(dib.pitch * 29);
            PackedSurface surface{bytes.data(),
                                  37,
                                  29,
                                  dib.pitch,
                                  depth,
                                  depth <= 4 ? dib.palette : nullptr,
                                  depth <= 4 ? 1 << depth : 0};
            const auto setClip = [&](PRegion clipping) {
                for (auto port : {reference, adapter})
                {
                    auto owned = clipping;
                    if (owned)
                        owned->AddRef();
                    port->ClipToRegn(&owned);
                    ReleasePpo(&owned);
                }
            };
            // Highlight a fresh port before any native mono fill can allocate a
            // context. Packed writes must preserve partial final bytes and padding.
            setClip(region);
            for (unsigned i = 0; i < bytes.size(); ++i)
                dib.pixels[i] = bytes[i] = adapterBytes[i] = static_cast<byte>(i * 37 + 19);
            GraphicsDrawingData highlight = {};
            highlight.prcsClip = &clip;
            SystemRectangle highlighted = rectangle;
            reference->HiliteRcs(&highlighted, &highlight);
            adapter->HiliteRcs(&highlighted, &highlight);
            if (!InvertRect(surface, rectangle, &clip, region))
                return Fail(__LINE__);
            GraphicsPort::Flush();
            if (std::memcmp(dib.pixels, bytes.data(), bytes.size()) ||
                std::memcmp(dib.pixels, adapterBytes, bytes.size()))
                return Fail(__LINE__);
#ifdef KAUAI_SDL3
            if (adapter->FHasNativeMirror() || adapter->FHasNativeContext())
                return Fail(__LINE__);
#endif
            if (depth == 1)
                for (int shape = 0; shape < 4; ++shape)
                    for (int fore = 0; fore < colorCount; ++fore)
                    {
                        for (unsigned i = 0; i < bytes.size(); ++i)
                            dib.pixels[i] = bytes[i] = adapterBytes[i] = static_cast<byte>(i * 37 + 19);
                        GraphicsDrawingData drawing = {};
                        drawing.grfgdd = fgddFill;
                        drawing.acrFore = colors[fore];
                        drawing.dxpPen = 3;
                        drawing.dypPen = 4;
                        drawing.prcsClip = &clip;
                        SystemRectangle nativeRect = rectangle;
                        for (auto port : {reference, adapter})
                            if (shape == 0)
                                port->DrawRcs(&nativeRect, &drawing);
                            else if (shape == 1)
                                port->DrawOval(&nativeRect, &drawing);
                            else if (shape == 2)
                                port->DrawPoly(polygon, &drawing);
                            else
                            {
                                SystemPoint first = {-2, 4}, last = {34, 25};
                                port->DrawLine(&first, &last, &drawing);
                            }
                        Brush flat;
                        flat.foreground = TestInk(fore);
                        MonochromeBrushState state;
                        state.foregroundReference = fore < 5 ? 2 : 0;
                        bool drawn = true;
                        if (shape == 0)
                            drawn = FillRect(surface, rectangle, flat, &clip, region, -1, -1, state);
                        else if (shape == 1)
                            drawn = FillOval(surface, rectangle, flat, &clip, region, -1, -1, state);
                        else if (shape == 2)
                            drawn = FillPolygon(surface, points, 5, flat, &clip, region, state);
                        GraphicsPort::Flush();
                        if (!drawn || std::memcmp(dib.pixels, adapterBytes, bytes.size()) ||
                            (shape < 3 && std::memcmp(dib.pixels, bytes.data(), bytes.size())))
                            return Fail(__LINE__);
#ifdef KAUAI_SDL3
                        if (adapter->FHasNativeMirror() || adapter->FHasNativeContext())
                            return Fail(__LINE__);
#endif
                    }
            for (auto clipping : {static_cast<PRegion>(nullptr), region, empty})
            {
                setClip(clipping);
                for (int shape = 0; shape < 5; ++shape)
                    for (int fore = 0; fore < colorCount; ++fore)
                        for (int back = 0; back < colorCount; ++back)
                            for (int pattern = 0; pattern < 4; ++pattern)
                                for (int frame = 0; frame < 3; ++frame)
                                {
                                    GdiFlush();
                                    for (unsigned i = 0; i < bytes.size(); ++i)
                                        dib.pixels[i] = bytes[i] = adapterBytes[i] =
                                            static_cast<byte>(i * 37 + cases * 13);
                                    GraphicsDrawingData drawing = {};
                                    drawing.grfgdd =
                                        (pattern ? fgddPattern : fgddFill) | ((frame && shape < 2) ? fgddFrame : 0);
                                    drawing.acrFore = colors[fore];
                                    drawing.acrBack = colors[back];
                                    drawing.dxpPen = frame == 2 ? 30 : 3;
                                    drawing.dypPen = 4;
                                    drawing.prcsClip = &clip;
                                    Brush brush;
                                    brush.foreground = TestInk(fore);
                                    brush.background = TestInk(back);
                                    if (pattern)
                                        for (int row = 0; row < 8; ++row)
                                            drawing.apt.rgb[row] = brush.pattern[row] =
                                                pattern == 2   ? 0
                                                : pattern == 3 ? 255
                                                               : static_cast<byte>(0xA6 ^ (row * 31));
                                    MonochromeBrushState mono;
                                    if (depth == 1)
                                    {
                                        const auto background = GetBkColor(dib.dc);
                                        mono.backgroundRgb = (std::uint32_t(GetRValue(background)) << 16) |
                                                             (std::uint32_t(GetGValue(background)) << 8) |
                                                             GetBValue(background);
                                        mono.backgroundReference = static_cast<std::uint8_t>(background >> 24);
                                        mono.foregroundReference = fore < 5 ? 2 : 0;
                                        mono.alternateReference = back < 5 ? 2 : 0;
                                        mono.collapseEqualColors = fore == back;
                                    }
                                    SystemRectangle nativeRect = rectangle;
                                    for (auto port : {reference, adapter})
                                        if (shape == 0)
                                            port->DrawRcs(&nativeRect, &drawing);
                                        else if (shape == 1)
                                            port->DrawOval(&nativeRect, &drawing);
                                        else if (shape == 2)
                                            port->DrawPoly(polygon, &drawing);
                                        else if (shape == 3)
                                        {
                                            SystemPoint first = {-2, 4}, last = {34, 25};
                                            port->DrawLine(&first, &last, &drawing);
                                        }
                                        else
                                            port->HiliteRcs(&nativeRect, &drawing);
                                    bool success = true;
                                    if (shape == 0)
                                        success =
                                            FillRect(surface, rectangle, brush, &clip, clipping,
                                                     frame ? drawing.dxpPen : -1, frame ? drawing.dypPen : -1, mono);
                                    else if (shape == 1)
                                        success =
                                            FillOval(surface, rectangle, brush, &clip, clipping,
                                                     frame ? drawing.dxpPen : -1, frame ? drawing.dypPen : -1, mono);
                                    else if (shape == 2)
                                        success = FillPolygon(surface, points, 5, brush, &clip, clipping, mono);
                                    else if (shape == 4)
                                    {
                                        success = InvertRect(surface, rectangle, &clip, clipping);
                                    }
                                    // Lines exercise the GraphicsPort adapter's existing line-to-polygon conversion.
                                    GraphicsPort::Flush();
                                    const bool coreSupported = depth != 1 || shape == 4 || pattern != 1 || fore == back;
                                    if (shape != 3 && success != coreSupported)
                                        return Fail(__LINE__);
                                    for (unsigned i = 0; i < bytes.size(); ++i)
                                        if ((!coreSupported && shape != 3 &&
                                             bytes[i] != static_cast<byte>(i * 37 + cases * 13)) ||
                                            dib.pixels[i] != adapterBytes[i] ||
                                            (coreSupported && shape != 3 && dib.pixels[i] != bytes[i]))
                                        {
                                            std::fprintf(
                                                stderr,
                                                "Packed shape mismatch depth %d shape %d fore/back %d/%d pattern "
                                                "%d frame %d region %d byte %u expected %u core %u adapter %u\n",
                                                depth, shape, fore, back, pattern, frame,
                                                clipping == nullptr  ? 0
                                                : clipping == region ? 1
                                                                     : 2,
                                                i, dib.pixels[i], bytes[i], adapterBytes[i]);
                                            return Fail(__LINE__);
                                        }
#ifdef KAUAI_SDL3
                                    if (depth != 1 && (adapter->FHasNativeMirror() || adapter->FHasNativeContext()))
                                    {
                                        std::fprintf(stderr, "Packed shape allocated native drawing resources\n");
                                        return Fail(__LINE__);
                                    }
#endif
                                    ++cases;
                                }
            }
            adapter->Unlock();
            ReleasePpo(&adapter);
            ReleasePpo(&reference);
        }
    GraphicsPort::SetActiveColors(nullptr, fpalIdentity);
    ReleasePpo(&palette);
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    FreePhq(&polygon);
    ReleasePpo(&region);
    ReleasePpo(&empty);
    std::printf(
        "Packed shapes: %d native comparisons of 4-bit/RGB555/BGR24 shapes, monochrome highlight/fallback, patterns, "
        "clipping, raw Boolean operations and padding passed.\n",
        cases);
}
