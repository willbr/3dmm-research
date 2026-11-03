/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Shared CPU-backed drawing for the original Studio GraphicsPort. Native
// synchronization, palette activation and color references stay in adapters.
#include "pixel_canvas.h"
#include "dib_frame.h"
#include <vector>
#include <new>
#include "frame.h"
#undef size
ASSERTNAME

bool GraphicsPort::DrawVideoFrame(const KauaiPixels::DibFrame &frame, SystemRectangle *rectangle,
                                  GraphicsDrawingData *drawing)
{
    if (!_prgbPixels || _cbitPixel != 32)
        return false;
    RC clip;
    if (drawing->prcsClip)
        clip = *drawing->prcsClip;
    Flush();
    return KauaiPixels::DrawDibFrame({_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow}, frame, RC(*rectangle),
                                     drawing->prcsClip ? &clip : nullptr, _pregnClip);
}
bool GraphicsPort::_FGetPixelBrush(GraphicsDrawingData *drawing, KauaiPixels::Brush &brush, bool hilite)
{
    const auto ink = [&](AbstractColor color, KauaiPixels::Ink &result) {
        if (color == kacrClear)
            result.mode = KauaiPixels::InkMode::clear;
        else if (color == kacrInvert)
            result.mode = KauaiPixels::InkMode::invert;
        else
        {
            if (B3Lw(color._lu) == kbIndexAcr)
            {
                if (!_prgclr || B0Lw(color._lu) >= _cclrPal)
                    return false;
                const auto &entry = _prgclr[B0Lw(color._lu)];
                result.rgb = (std::uint32_t(entry.bRed) << 16) | (std::uint32_t(entry.bGreen) << 8) | entry.bBlue;
            }
            else
                result.rgb = color._lu & 0xFFFFFF;
        }
        return true;
    };
    if (!ink(hilite ? kacrInvert : drawing->acrFore, brush.foreground))
        return false;
    if (!hilite && (drawing->grfgdd & fgddPattern))
    {
        if (!ink(drawing->acrBack, brush.background))
            return false;
        CopyPb(drawing->apt.rgb, brush.pattern, 8);
    }
    return true;
}
KauaiPixels::MonochromeBrushState GraphicsPort::_MonochromeBrushState(GraphicsDrawingData *drawing, bool selectBrush)
{
    KauaiPixels::MonochromeBrushState state;
    if (_cbitPixel != 1)
        return state;
    {
        auto color = _PixelBackgroundColor();
        if (selectBrush)
        {
            auto ink = drawing->acrFore;
            bool solid = true;
            if ((drawing->grfgdd & fgddPattern) && drawing->acrFore != drawing->acrBack)
            {
                if (drawing->apt.FSolidBack())
                    ink = drawing->acrBack;
                else
                    solid = drawing->apt.FSolidFore();
            }
            if (!solid || ink == kacrInvert)
                _monoBrushValid = false;
            else if (ink != kacrClear)
            {
                if (!_monoBrushValid || _monoBrushColor != ink)
                {
                    _monoBrushColor = ink;
                    _monoBrushBackground = color;
                    _monoBrushValid = true;
                }
                color = _monoBrushBackground;
            }
        }
        state.backgroundRgb = ((color & 0xFFu) << 16) | (color & 0xFF00u) | ((color >> 16) & 0xFFu);
        state.backgroundReference = B3Lw(color);
    }
    const auto reference = [&](AbstractColor color) { return _PixelColorReference(color); };
    state.foregroundReference = reference(drawing->acrFore);
    if (drawing->grfgdd & fgddPattern)
        state.alternateReference = reference(drawing->acrBack);
    state.collapseEqualColors = (drawing->grfgdd & fgddPattern) && drawing->acrFore == drawing->acrBack;
    return state;
}
bool GraphicsPort::_FDrawPixelRect(SystemRectangle *rectangle, GraphicsDrawingData *drawing, bool hilite)
{
    if (!_prgbPixels)
        return false;
    _EnsurePalette();
    if (_cbitPixel == 8)
    {
        KauaiPixels::Brush8 brush;
        if (!_FGetPixelBrush8(drawing, brush, hilite))
            return false;
        RC bounds(*rectangle), clip;
        if (drawing->prcsClip)
            clip = *drawing->prcsClip;
        Flush();
        KauaiPixels::Surface8 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
        const bool frame = !hilite && (drawing->grfgdd & fgddFrame);
        KauaiPixels::FillRect(surface, bounds, brush, drawing->prcsClip ? &clip : nullptr, _pregnClip,
                              frame ? drawing->dxpPen : -1, frame ? drawing->dypPen : -1);
        return true;
    }
    KauaiPixels::Brush brush;
    if (!_FGetPixelBrush(drawing, brush, hilite))
        return false;
    RC bounds(*rectangle), clip;
    if (drawing->prcsClip)
        clip = *drawing->prcsClip;
    // Some legacy bitmap/metafile fallbacks do not maintain _cactDraw. While
    // mixing those with CPU writes, drain GDI unconditionally at this boundary.
    Flush();
    const bool frame = !hilite && (drawing->grfgdd & fgddFrame);
    if (_cbitPixel != 32)
    {
        KauaiPixels::PackedSurface surface = {_prgbPixels,
                                              _rcOff.Dxp(),
                                              _rcOff.Dyp(),
                                              _cbRow,
                                              _cbitPixel,
                                              _cbitPixel <= 4 ? _rgclrPixels : nullptr,
                                              _cbitPixel <= 4 ? 1 << _cbitPixel : 0};
        if (hilite)
            return KauaiPixels::InvertRect(surface, bounds, drawing->prcsClip ? &clip : nullptr, _pregnClip);
        return KauaiPixels::FillRect(surface, bounds, brush, drawing->prcsClip ? &clip : nullptr, _pregnClip,
                                     frame ? drawing->dxpPen : -1, frame ? drawing->dypPen : -1,
                                     _MonochromeBrushState(drawing, true));
    }
    KauaiPixels::Surface32 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
    KauaiPixels::FillRect(surface, bounds, brush, drawing->prcsClip ? &clip : nullptr, _pregnClip,
                          frame ? drawing->dxpPen : -1, frame ? drawing->dypPen : -1);
    return true;
}
bool GraphicsPort::_FDrawPixelOval(SystemRectangle *rectangle, GraphicsDrawingData *drawing)
{
    if (!_prgbPixels)
        return false;
    _EnsurePalette();
    RC bounds(*rectangle), clip;
    if (drawing->prcsClip)
        clip = *drawing->prcsClip;
    const bool frame = (drawing->grfgdd & fgddFrame) != 0;
    if (_cbitPixel == 8)
    {
        KauaiPixels::Brush8 brush;
        if (!_FGetPixelBrush8(drawing, brush))
            return false;
        Flush();
        KauaiPixels::Surface8 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
        return KauaiPixels::FillOval(surface, bounds, brush, drawing->prcsClip ? &clip : nullptr, _pregnClip,
                                     frame ? drawing->dxpPen : -1, frame ? drawing->dypPen : -1);
    }
    KauaiPixels::Brush brush;
    if (!_FGetPixelBrush(drawing, brush))
        return false;
    Flush();
    if (_cbitPixel != 32)
        return KauaiPixels::FillOval({_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow, _cbitPixel,
                                      _cbitPixel <= 4 ? _rgclrPixels : nullptr, _cbitPixel <= 4 ? 1 << _cbitPixel : 0},
                                     bounds, brush, drawing->prcsClip ? &clip : nullptr, _pregnClip,
                                     frame ? drawing->dxpPen : -1, frame ? drawing->dypPen : -1,
                                     _MonochromeBrushState(drawing, true));
    KauaiPixels::Surface32 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
    return KauaiPixels::FillOval(surface, bounds, brush, drawing->prcsClip ? &clip : nullptr, _pregnClip,
                                 frame ? drawing->dxpPen : -1, frame ? drawing->dypPen : -1);
}
bool GraphicsPort::_FDrawPixelPolygon(const OLY *polygon, GraphicsDrawingData *drawing)
{
    if (!_prgbPixels || polygon->cpts < 0)
        return false;
    _EnsurePalette();
    KauaiPixels::Brush brush;
    if (!_FGetPixelBrush(drawing, brush))
        return false;
    std::vector<KauaiPixels::Point> points;
    if (static_cast<std::size_t>(polygon->cpts) > points.max_size())
        return false;
    try
    {
        points.resize(polygon->cpts);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    for (long i = 0; i < polygon->cpts; ++i)
        points[i] = {static_cast<std::int32_t>(polygon->rgpts[i].x), static_cast<std::int32_t>(polygon->rgpts[i].y)};
    RC clip;
    if (drawing->prcsClip)
        clip = *drawing->prcsClip;
    Flush();
    if (_cbitPixel == 8)
    {
        KauaiPixels::Brush8 indexed;
        if (!_FGetPixelBrush8(drawing, indexed))
            return false;
        KauaiPixels::Surface8 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
        return KauaiPixels::FillPolygon(surface, points.data(), static_cast<std::size_t>(polygon->cpts), indexed,
                                        drawing->prcsClip ? &clip : nullptr, _pregnClip);
    }
    if (_cbitPixel != 32)
        return KauaiPixels::FillPolygon(
            {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow, _cbitPixel, _cbitPixel <= 4 ? _rgclrPixels : nullptr,
             _cbitPixel <= 4 ? 1 << _cbitPixel : 0},
            points.data(), static_cast<std::size_t>(polygon->cpts), brush, drawing->prcsClip ? &clip : nullptr,
            _pregnClip, _MonochromeBrushState(drawing, true));
    KauaiPixels::Surface32 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
    return KauaiPixels::FillPolygon(surface, points.data(), static_cast<std::size_t>(polygon->cpts), brush,
                                    drawing->prcsClip ? &clip : nullptr, _pregnClip);
}
bool GraphicsPort::_FGetPixelBrush8(GraphicsDrawingData *drawing, KauaiPixels::Brush8 &brush, bool hilite)
{
    _EnsurePalette();
    KauaiPixels::Brush rgb;
    return _FGetPixelBrush(drawing, rgb, hilite) && KauaiPixels::ResolveBrush8(rgb, _rgclrPixels, 256, brush, hilite);
}

bool GraphicsPort::_FDrawPackedCopy(PGraphicsPort source, SystemRectangle *sourceRect, SystemRectangle *destinationRect,
                                    GraphicsDrawingData *drawing)
{
    if (!_prgbPixels || !source->_prgbPixels)
        return false;
    _EnsurePalette();
    source->_EnsurePalette();
    RC src(*sourceRect), dst(*destinationRect), clip;
    if (drawing->prcsClip)
        clip = *drawing->prcsClip;
    Flush();
    KauaiPixels::PackedSurface destination = {_prgbPixels,
                                              _rcOff.Dxp(),
                                              _rcOff.Dyp(),
                                              _cbRow,
                                              _cbitPixel,
                                              _cbitPixel <= 8 ? _rgclrPixels : nullptr,
                                              _cbitPixel <= 8 ? 1 << _cbitPixel : 0};
    KauaiPixels::PackedSurface input = {source->_prgbPixels,
                                        source->_rcOff.Dxp(),
                                        source->_rcOff.Dyp(),
                                        source->_cbRow,
                                        source->_cbitPixel,
                                        source->_cbitPixel <= 8 ? source->_rgclrPixels : nullptr,
                                        source->_cbitPixel <= 8 ? 1 << source->_cbitPixel : 0};
    return KauaiPixels::CopyPacked(destination, input, src, dst, drawing->prcsClip ? &clip : nullptr, _pregnClip);
}

bool GraphicsPort::_FScrollPixels(SystemRectangle *prcs, long dxp, long dyp, GraphicsDrawingData *pgdd)
{
    // Reversed rectangles are outside the core API's contract. Preserve the
    // existing native fallback for those malformed legacy calls.
    if (_prgbPixels && prcs->left <= prcs->right && prcs->top <= prcs->bottom)
    {
        Flush();
        RC rectangle(*prcs), clip;
        if (pgdd->prcsClip)
            clip = *pgdd->prcsClip;
        if (_cbitPixel == 8)
        {
            KauaiPixels::Surface8 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
            if (KauaiPixels::Scroll(surface, rectangle, dxp, dyp, pgdd->prcsClip ? &clip : nullptr, _pregnClip))
                return true;
        }
        else if (_cbitPixel == 32)
        {
            KauaiPixels::Surface32 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
            if (KauaiPixels::Scroll(surface, rectangle, dxp, dyp, pgdd->prcsClip ? &clip : nullptr, _pregnClip))
                return true;
        }
        else
        {
            KauaiPixels::PackedSurface surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow, _cbitPixel};
            // Preserve the original monochrome byte-edge copy order.
            if (KauaiPixels::Scroll(surface, rectangle, dxp, dyp, pgdd->prcsClip ? &clip : nullptr, _pregnClip, {true}))
                return true;
        }
    }
    return false;
}

bool GraphicsPort::_FCopyPixels(PGraphicsPort pgptSrc, SystemRectangle *prcsSrc, SystemRectangle *prcsDst,
                                GraphicsDrawingData *pgdd)
{
    if (!_prgbPixels || !pgptSrc->_prgbPixels)
        return false;
    long dxpSrc, dxpDst;

    if ((_cbitPixel != 8 || pgptSrc->_cbitPixel != 8) && _FDrawPackedCopy(pgptSrc, prcsSrc, prcsDst, pgdd))
    {
        _fPixelColorOnColor = true;
        return true;
    }

    // see if we're doing an offscreen (2x, 2x) stretch or (1x, 2x) stretch -
    // optimize for these
    if (pgptSrc->_cbitPixel == 8 && _cbitPixel == 8 &&
        ((dxpSrc = prcsSrc->right - prcsSrc->left) == (dxpDst = prcsDst->right - prcsDst->left) ||
         2 * dxpSrc == dxpDst) &&
        2 * (prcsSrc->bottom - prcsSrc->top) == prcsDst->bottom - prcsDst->top && !pgptSrc->_PixelOwnPalette() &&
        !_PixelOwnPalette())
    {
        RC rcClip;
        RC rcDouble(*prcsDst);
        RC rcSrc(*prcsSrc);

        if (!rcSrc.FIntersect(&pgptSrc->_rcOff))
            return true;

        if (dxpSrc == dxpDst)
        {
            rcDouble.xpLeft += rcSrc.xpLeft - prcsSrc->left;
            rcDouble.xpRight += rcSrc.xpRight - prcsSrc->right;
        }
        else
        {
            rcDouble.xpLeft += 2 * (rcSrc.xpLeft - prcsSrc->left);
            rcDouble.xpRight += 2 * (rcSrc.xpRight - prcsSrc->right);
        }
        rcDouble.ypTop += 2 * (rcSrc.ypTop - prcsSrc->top);
        rcDouble.ypBottom += 2 * (rcSrc.ypBottom - prcsSrc->bottom);

        if (pvNil != pgdd->prcsClip)
        {
            rcClip = *pgdd->prcsClip;
            if (!rcClip.FIntersect(&rcDouble))
                return true;
        }
        else
            rcClip = rcDouble;

        Flush();

        if (dxpSrc == dxpDst)
        {
            DoubleVertStretch(pgptSrc->_prgbPixels, pgptSrc->_cbRow, pgptSrc->_rcOff.Dyp(), &rcSrc, _prgbPixels, _cbRow,
                              _rcOff.Dyp(), prcsDst->left, prcsDst->top, &rcClip, _pregnClip);
        }
        else
        {
            DoubleStretch(pgptSrc->_prgbPixels, pgptSrc->_cbRow, pgptSrc->_rcOff.Dyp(), &rcSrc, _prgbPixels, _cbRow,
                          _rcOff.Dyp(), prcsDst->left, prcsDst->top, &rcClip, _pregnClip);
        }
        return true;
    }

    _fPixelColorOnColor = true;
    if (_FDrawPackedCopy(pgptSrc, prcsSrc, prcsDst, pgdd))
        return true;
    return false;
}

bool GraphicsPort::_FDrawPixelMbmp(PMaskedBitmapMBMP pmbmp, SystemRectangle *prcs, GraphicsDrawingData *pgdd)
{
    RC rcSrc, rcDst(*prcs), rcClip = rcDst;
    if (pgdd->prcsClip && !rcClip.FIntersect(pgdd->prcsClip))
        return true;
    pmbmp->GetRc(&rcSrc);
    if (rcSrc.FEmpty())
        return true;
    if ((_cbitPixel == 1 || _cbitPixel == 4 || _cbitPixel == 16 || _cbitPixel == 24) && _prgbPixels && _prgclr &&
        _cclrPal > 0)
    {
        _EnsurePalette();
        Flush();
        if (pmbmp->FDrawPacked({_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow, _cbitPixel,
                                _cbitPixel == 4 ? _rgclrPixels : nullptr, _cbitPixel == 4 ? 16 : 0},
                               rcDst, _prgclr, _cclrPal, &rcClip, _pregnClip, !_fPixelColorOnColor))
        {
            if (_cbitPixel == 1)
            {
                _SetPixelBackgroundColor(0x02000000); // Legacy PALETTERGB black.
            }
            return true;
        }
    }
    if (_cbitPixel == 32 && _prgbPixels && _prgclr && _cclrPal > 0)
    {
        Flush();
        KauaiPixels::Surface32 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
        if (pmbmp->FDraw32(surface, rcDst, _prgclr, _cclrPal, &rcClip, _pregnClip))
            return true;
    }
    if (_cbitPixel == 8 && _prgbPixels && (rcSrc.Dxp() != rcDst.Dxp() || rcSrc.Dyp() != rcDst.Dyp()))
    {
        _EnsurePalette();
        Flush();
        KauaiPixels::Surface8 surface = {_prgbPixels, _rcOff.Dxp(), _rcOff.Dyp(), _cbRow};
        if (!_PixelOwnPalette() && pmbmp->FDraw8(surface, rcDst, &rcClip, _pregnClip))
            return true;
        if (_PixelOwnPalette())
        {
            KauaiPixels::MaskedTransfer8 transfer;
            if (KauaiPixels::ResolveMaskedTransfer8(_prgclr, _cclrPal, _rgclrPixels, 256, transfer) &&
                pmbmp->FDraw8(surface, rcDst, transfer, &rcClip, _pregnClip, !_fPixelColorOnColor))
                return true;
        }
    }
    if (_cbitPixel == 8 && _prgbPixels && rcSrc.Dxp() == rcDst.Dxp() && rcSrc.Dyp() == rcDst.Dyp())
    {
        if (!rcClip.FIntersect(&_rcOff))
            return true;
        Flush();
        pmbmp->Draw(_prgbPixels, _cbRow, _rcOff.Dyp(), rcDst.xpLeft - rcSrc.xpLeft, rcDst.ypTop - rcSrc.ypTop, &rcClip,
                    _pregnClip);
        return true;
    }
    return false;
}
