/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Port lifetime-independent geometry and clipping; no window-system operations.
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#include "pixel_canvas.h"
#endif
#include "pixel_stretch.h"
#include <cstring>
#include "frame.h"
ASSERTNAME
RTCLASS(GraphicsPort)
long vcactRealize;

/***************************************************************************
    Clip to the region specified by *ppregn.  *ppregn is set to the previous
    clip region (may be pvNil).  The GraphicsPort takes over ownership of the region
    and relinquishes ownership of the old region.
***************************************************************************/
void GraphicsPort::ClipToRegn(PRegion *ppregn)
{
    if (_ptBase.xp != 0 || _ptBase.yp != 0)
    {
        if (pvNil != *ppregn)
            (*ppregn)->Offset(-_ptBase.xp, -_ptBase.yp);
        if (pvNil != _pregnClip)
            _pregnClip->Offset(_ptBase.xp, _ptBase.yp);
    }
    SwapVars(&_pregnClip, ppregn);
    _fNewClip = fTrue;
}

/***************************************************************************
    Set the base PT for the GraphicsPort.  This affects the mapping of any attached
    GNVs.
***************************************************************************/
void GraphicsPort::SetPtBase(PT *ppt)
{
    AssertThis(0);
    AssertVarMem(ppt);
    _ptBase = *ppt;
}

/***************************************************************************
    Get the base PT for the GraphicsPort.
***************************************************************************/
void GraphicsPort::GetPtBase(PT *ppt)
{
    AssertThis(0);
    AssertVarMem(ppt);
    *ppt = _ptBase;
}

#if defined(WIN) || defined(KAUAI_SDL3)
bool GraphicsPort::_FCopyIndexedDouble(const MMPixelSurface &input, const RC &source, const RC &destination,
                                       GraphicsDrawingData *drawing)
{
    RC bounds(0, 0, input.width, input.height), cropped, clip;
    const auto mode = PlanDoubleStretch(bounds, source, destination, drawing->prcsClip, cropped, clip);
    if (mode == IndexedDoubleStretch::unavailable)
        return false;
    if (mode != IndexedDoubleStretch::clipped)
    {
        Flush();
        const auto stretch = mode == IndexedDoubleStretch::vertical ? DoubleVertStretch : DoubleStretch;
        stretch(static_cast<byte *>(input.pixels), input.pitch, input.height, &cropped, _prgbPixels, _cbRow,
                _rcOff.Dyp(), destination.xpLeft, destination.ypTop, &clip, _pregnClip);
    }
    return true;
}
#endif

void GraphicsPort::CopyIndexedPixels(const MMPixelSurface &input, SystemRectangle *destination,
                                     GraphicsDrawingData *drawing)
{
    if (input.depth != 8 || !mm_pixel_surface_valid(&input))
        return;
    SystemRectangle source{0, 0, input.width, input.height};
    RC bounds(source);
#ifdef KAUAI_SDL3
    if (_prgbPixels && _cbitPixel == 8 && !_PixelOwnPalette() &&
        _FCopyIndexedDouble(input, bounds, RC(*destination), drawing))
        return;
    if (_prgbPixels && _prgclr)
    {
        _fPixelColorOnColor = true;
        KauaiPixels::PackedSurface pixels{input.pixels, input.width, input.height, input.pitch, 8, _prgclr, 256};
        if (_FDrawPackedCopy(pixels, &source, destination, drawing))
            return;
    }
#elif defined(WIN)
    if (_cbitPixel == 8 && !_fOwnPalette && _FCopyIndexedDouble(input, bounds, RC(*destination), drawing))
        return;
#endif
    auto port = PgptNewOffscreen(&bounds, 8);
    if (!port)
        return;
    auto pixels = port->PrgbLockPixels();
    const auto pitch = port->CbRow();
    const auto *row = static_cast<const byte *>(input.pixels);
    for (MMI32 y = 0; y < input.height; ++y)
        std::memcpy(pixels + size_t(y) * pitch, row + size_t(y) * input.pitch, input.width);
    port->Unlock();
    CopyPixels(port, &source, destination, drawing);
    Flush();
    port->Release();
}

#ifdef DEBUG
/***************************************************************************
    Mark memory for the GraphicsPort.
***************************************************************************/
void GraphicsPort::MarkMem(void)
{
    AssertValid(0);
    GraphicsPort_PAR::MarkMem();
    MarkMemObj(_pregnClip);
}

/******************************************************************************
    Assert the validity of a polygon descriptor.
******************************************************************************/
void OLY::AssertValid(LegacyUlong grf)
{
    AssertThisMem();
    AssertPvCb(rgpts, LwMul(Cpts(), size(SystemPoint)));
}

#endif // DEBUG

// The original six-point line footprint, shared by native and CPU ports.
void BuildLinePolygon(OLY *poly, SystemPoint first, SystemPoint last, LegacyLong penX, LegacyLong penY)
{
    MMPixelPoint footprint[6];
    mm_graphics_line_polygon({static_cast<MMI32>(first.x), static_cast<MMI32>(first.y)},
                             {static_cast<MMI32>(last.x), static_cast<MMI32>(last.y)},
                             {static_cast<MMI32>(penX), static_cast<MMI32>(penY)}, footprint);
    poly->cpts = 6;
    for (int i = 0; i < 6; ++i)
    {
        poly->rgpts[i].x = footprint[i].x;
        poly->rgpts[i].y = footprint[i].y;
    }
}

#ifdef KAUAI_SDL3
PGraphicsPort GraphicsPort::PgptNewSDLWindow(CoreNative::WindowHandle window)
{
    auto &host = KauaiSDL::Main();
    if (!host.IsWindow(window))
        return nullptr;
    RC bounds(0, 0, host.Width(), host.Height());
    auto port = PgptNewOffscreen(&bounds, 32);
    if (port)
        host.BindPixels(port->_prgbPixels, port->_cbRow);
    return port;
}

void GraphicsPort::PresentTransitionFrame()
{
    KauaiSDL::Main().PresentBoundPixels(_prgbPixels);
}
#endif
