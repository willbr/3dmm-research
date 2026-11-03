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
    SystemPoint *ppts1 = &first, *ppts2 = &last, *pptsDst;
    LegacyLong dxp, dyp, dypPen;
    poly->cpts = 6;

    if (ppts2->x < ppts1->x)
    {
        // swap them
        SystemPoint *pptsT;

        pptsT = ppts2;
        ppts2 = ppts1;
        ppts1 = pptsT;
    }
    Assert(ppts2->x >= ppts1->x, "points still not in order");

    dxp = ppts2->x - ppts1->x;
    dyp = ppts2->y - ppts1->y;
    dypPen = penY;

    pptsDst = poly->rgpts;
    *pptsDst = *ppts1;
    if (dyp < 0)
    {
        dypPen = -dypPen;
        pptsDst[1].x = pptsDst[0].x;
        pptsDst[1].y = pptsDst[0].y - dypPen;
        pptsDst++;
    }
    pptsDst[1].x = pptsDst[0].x + penX;
    pptsDst[1].y = pptsDst[0].y;
    pptsDst++;
    pptsDst[1].x = pptsDst[0].x + dxp;
    pptsDst[1].y = pptsDst[0].y + dyp;
    pptsDst++;
    pptsDst[1].x = pptsDst[0].x;
    pptsDst[1].y = pptsDst[0].y + dypPen;
    pptsDst++;
    pptsDst[1].x = pptsDst[0].x - penX;
    pptsDst[1].y = pptsDst[0].y;
    if (dyp >= 0)
    {
        pptsDst++;
        pptsDst[1].x = pptsDst[0].x - dxp;
        pptsDst[1].y = pptsDst[0].y - dyp;
    }
    Assert(pptsDst == &poly->rgpts[4], "wrote off end of OLY");
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
