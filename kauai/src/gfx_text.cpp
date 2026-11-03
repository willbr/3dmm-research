/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Shared Studio text layout and rasterization; native font selection is an adapter.
#include "font_host.h"
#include "pixel_canvas.h"
#include <cstring>
#include <memory>
#include <new>
#include "frame.h"
#undef size
ASSERTNAME

bool GraphicsPort::_FPortableTextFace(FontDescription *font)
{
    if (!_fontFace || !_fontPortable || _fontDescription.onn != font->onn || _fontDescription.grfont != font->grfont ||
        _fontDescription.dyp != font->dyp)
    {
        std::unique_ptr<KauaiFonts::Face> face;
        String family;
        vntl.GetStn(font->onn, &family);
        try
        {
            face = KauaiFonts::CreateStudioFace(
                {family.Prgch(), static_cast<std::size_t>(family.Cch()),
                 sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes},
                font->dyp, (font->grfont & fontBold) != 0, (font->grfont & fontItalic) != 0,
                (font->grfont & fontUnderline) != 0);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        if (!face)
            return false;
        delete _fontFace;
        _fontFace = face.release();
        _fontPortable = true;
        _fontDescription = *font;
    }
    return true;
}

bool GraphicsPort::_FHostText(achar *text, long count, SystemPoint point, FontDescription *font,
                              KauaiFonts::Placement &placement, KauaiFonts::Mask *mask, bool opaque)
{
    if (count < 0 || !_FEnsureTextFace(font))
        return false;
    const KauaiFonts::Text input = {text, static_cast<std::size_t>(count),
                                    sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16
                                                       : KauaiFonts::Encoding::legacyBytes};
    KauaiFonts::Metrics metrics;
    return _fontFace->Measure(input, metrics) &&
           KauaiFonts::Place(metrics, point.x, point.y, static_cast<KauaiFonts::Horizontal>(font->tah),
                             static_cast<KauaiFonts::Vertical>(font->tav), placement) &&
           (!mask || _fontFace->Rasterize(input, *mask, opaque));
}

bool GraphicsPort::_FDrawPixelText(achar *text, long count, SystemPoint point, GraphicsDrawingData *drawing,
                                   FontDescription *font)
{
    if (!_prgbPixels ||
        (_cbitPixel != 1 && _cbitPixel != 4 && _cbitPixel != 8 && _cbitPixel != 16 && _cbitPixel != 24 &&
         _cbitPixel != 32) ||
        drawing->acrFore == kacrClear || drawing->acrFore == kacrInvert)
        return false;
    // GDI's RGB555 smoothing uses a different quantization path from its
    // 32-bit renderer. Keep that provider on its native compatibility path.
    if (_cbitPixel == 16 && std::strcmp(KauaiFonts::StudioFontProvider(), "freetype") != 0)
        return false;
    KauaiPixels::Brush brush;
    if (!_FGetPixelBrush(drawing, brush) || brush.foreground.mode != KauaiPixels::InkMode::color)
        return false;
    KauaiFonts::Placement placement;
    KauaiFonts::Mask mask;
    const bool opaque = drawing->acrBack != kacrClear && drawing->acrBack != kacrInvert;
    if (!_FHostText(text, count, point, font, placement, _cbitPixel < 16 ? &mask : nullptr, opaque))
        return false;
    const auto x = std::int64_t(placement.cell.left) + mask.left;
    const auto y = std::int64_t(placement.cell.top) + mask.top;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return false;
    KauaiPixels::Brush background;
    if (opaque)
    {
        GraphicsDrawingData fill = *drawing;
        fill.grfgdd = fgddFill;
        fill.acrFore = drawing->acrBack;
        if (!_FGetPixelBrush(&fill, background) || background.foreground.mode != KauaiPixels::InkMode::color)
            return false;
    }
    if (_cbitPixel < 16 && (drawing->acrBack == kacrInvert || (opaque && mask.background.empty())))
    {
        const auto &bounds = drawing->acrBack == kacrInvert ? placement.logical : placement.cell;
        SystemRectangle rectangle = {bounds.left, bounds.top, bounds.right, bounds.bottom};
        GraphicsDrawingData fill = *drawing;
        fill.grfgdd = fgddFill;
        fill.acrFore = drawing->acrBack;
        DrawRcs(&rectangle, &fill);
    }
    RC clip;
    if (drawing->prcsClip)
        clip = *drawing->prcsClip;
    Flush();
    KauaiPixels::Coverage8 coverage = {mask.coverage.data(), mask.width, mask.height, mask.pitch};
    KauaiPixels::Coverage8 backgroundCoverage = {mask.background.data(), mask.width, mask.height, mask.pitch};
    if (_cbitPixel >= 16)
    {
        const KauaiFonts::Text input = {text, static_cast<std::size_t>(count),
                                        sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16
                                                           : KauaiFonts::Encoding::legacyBytes};
        const auto mode = opaque                           ? KauaiFonts::Background::opaque
                          : drawing->acrBack == kacrInvert ? KauaiFonts::Background::invert
                                                           : KauaiFonts::Background::transparent;
        if (!_fontFace->DrawPacked(input, {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow, _cbitPixel}, placement,
                                   brush.foreground.rgb, mode, background.foreground.rgb,
                                   drawing->prcsClip ? &clip : nullptr, _pregnClip))
            return false;
    }
    else
    {
        _EnsurePalette();
        auto mono = _MonochromeBrushState(drawing);
        if (_cbitPixel == 1 && opaque)
        {
            mono.backgroundRgb = background.foreground.rgb;
            mono.backgroundReference = _PixelColorReference(drawing->acrBack);
        }
        const auto nearest = [&](std::uint32_t rgb, AbstractColor origin) -> std::uint32_t {
            if (_cbitPixel == 1)
                return KauaiPixels::ResolveMonochromeColor(rgb, _PixelColorReference(origin), mono, _rgclrPixels);
            long index = 0, distance = klwMax;
            for (long i = 0; i < (1L << _cbitPixel); ++i)
            {
                const auto &color = _rgclrPixels[i];
                const long red = color.bRed - static_cast<long>((rgb >> 16) & 255);
                const long green = color.bGreen - static_cast<long>((rgb >> 8) & 255);
                const long blue = color.bBlue - static_cast<long>(rgb & 255);
                const long candidate = red * red + green * green + blue * blue;
                if (candidate < distance)
                {
                    index = i;
                    distance = candidate;
                }
            }
            return static_cast<std::uint8_t>(index);
        };
        const auto paint = [&](const KauaiPixels::Coverage8 &source, std::uint32_t rgb, AbstractColor origin) {
            if (_cbitPixel == 8)
            {
                KauaiPixels::Surface8 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
                KauaiPixels::DrawCoverage(surface, source, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                          static_cast<std::uint8_t>(nearest(rgb, origin)),
                                          drawing->prcsClip ? &clip : nullptr, _pregnClip);
            }
            else
            {
                KauaiPixels::PackedSurface surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow, _cbitPixel};
                KauaiPixels::DrawCoverage(surface, source, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                          nearest(rgb, origin), drawing->prcsClip ? &clip : nullptr, _pregnClip);
            }
        };
        if (opaque)
            paint(backgroundCoverage, background.foreground.rgb, drawing->acrBack);
        paint(coverage, brush.foreground.rgb, drawing->acrFore);
    }
    if (_cbitPixel == 1 && opaque)
    {
        const auto rgb = background.foreground.rgb;
        _SetPixelBackgroundColor((std::uint32_t(_PixelColorReference(drawing->acrBack)) << 24) | ((rgb & 255) << 16) |
                                 (rgb & 0xFF00) | ((rgb >> 16) & 255));
    }
    if (font->grfont & fontBoxed)
    {
        SystemRectangle rectangle = {placement.logical.left, placement.logical.top, placement.logical.right,
                                     placement.logical.bottom};
        GraphicsDrawingData frame = *drawing;
        frame.grfgdd = fgddFrame | fgddPattern;
        frame.dxpPen = frame.dypPen = 1;
        frame.apt = vaptGray;
        frame.apt.MoveOrigin(-_ptBase.xp, -_ptBase.yp);
        frame.acrBack = kacrClear;
        DrawRcs(&rectangle, &frame);
    }
    return true;
}
