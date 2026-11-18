/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Port lifetime-independent geometry and clipping; no window-system operations.
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#endif
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
#endif
