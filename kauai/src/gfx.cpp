/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    GFX classes: graphics device (GDV), graphics environment (GraphicsEnvironment)

***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(GraphicsEnvironment)
RTCLASS(Polygon)

static_assert(kdtsSecond == 1000, "Transition clock ticks are milliseconds");

namespace
{
MMPixelRect ViewRect(const RC &rect)
{
    return {static_cast<MMI32>(rect.xpLeft), static_cast<MMI32>(rect.ypTop),
            static_cast<MMI32>(rect.xpRight), static_cast<MMI32>(rect.ypBottom)};
}

RC NativeRect(const MMPixelRect &rect)
{
    return RC(rect.left, rect.top, rect.right, rect.bottom);
}

MMPixelPoint ViewPoint(const PT &point)
{
    return {static_cast<MMI32>(point.xp), static_cast<MMI32>(point.yp)};
}
} // namespace

/***************************************************************************
    Constructor for Graphics environment.
***************************************************************************/
GraphicsEnvironment::GraphicsEnvironment(GraphicsPort *pgpt)
{
    AssertPo(pgpt, 0);

    _Init(pgpt);
    AssertThis(0);
}

/***************************************************************************
    Constructor for Graphics environment based on a pgob.
***************************************************************************/
GraphicsEnvironment::GraphicsEnvironment(PGraphicsObject pgob)
{
    AssertPo(pgob, 0);

    _Init(pgob->Pgpt()); // use the GraphicsObject's port
    SetGobRc(pgob);      // set the rc's according to the gob
    AssertThis(0);
}

/***************************************************************************
    Constructor for Graphics environment based on both a port and a pgob.
***************************************************************************/
GraphicsEnvironment::GraphicsEnvironment(PGraphicsObject pgob, PGraphicsPort pgpt)
{
    AssertPo(pgpt, 0);
    AssertPo(pgob, 0);

    _Init(pgpt);
    SetGobRc(pgob);
    AssertThis(0);
}

/***************************************************************************
    Destructor for the GraphicsEnvironment.
***************************************************************************/
GraphicsEnvironment::~GraphicsEnvironment(void)
{
    AssertThis(0);
    Mac(_pgpt->Unlock();) ReleasePpo(&_pgpt);
}

/***************************************************************************
    Fill in all fields of the gnv with default values.
***************************************************************************/
void GraphicsEnvironment::_Init(PGraphicsPort pgpt)
{
    PT pt;

    _pgpt = pgpt;
    pgpt->AddRef();
    Mac(pgpt->Lock();)
    pgpt->GetPtBase(&pt);
    mm_graphics_view_init(&_view, ViewPoint(pt));
    _xp = _yp = 0;

    _dsf.onn = vntl.OnnSystem();
    _dsf.grfont = fontNil;
    _dsf.dyp = vpappb->DypTextDef();
    _dsf.tah = tahLeft;
    _dsf.tav = tavTop;

    _gdd.dxpPen = _gdd.dypPen = 1;
    _gdd.prcsClip = pvNil;
}

/***************************************************************************
    Set the mapping and vis according to the gob.
***************************************************************************/
void GraphicsEnvironment::SetGobRc(PGraphicsObject pgob)
{
    RC rc;

    // set the mapping
    pgob->GetRc(&rc, cooGpt);
    SetRcDst(&rc);
    pgob->GetRc(&rc, cooLocal);
    SetRcSrc(&rc);
    pgob->GetRcVis(&rc, cooLocal);
    SetRcVis(&rc);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the gnv
***************************************************************************/
void GraphicsEnvironment::AssertValid(LegacyUlong grf)
{
    GraphicsEnvironment_PAR::AssertValid(0);
    AssertPo(_pgpt, 0);
    AssertPo(&_dsf, 0);
    Assert(!NativeRect(_view.source).FEmpty(), "empty src rectangle");
    Assert(!NativeRect(_view.destination).FEmpty(), "empty dst rectangle");
    Assert(_gdd.prcsClip == pvNil || _gdd.prcsClip == &_rcsClip, "bad _gdd.prcsClip");
}

/***************************************************************************
    Mark memory for the GraphicsEnvironment.
***************************************************************************/
void GraphicsEnvironment::MarkMem(void)
{
    AssertValid(0);
    GraphicsEnvironment_PAR::MarkMem();
    MarkMemObj(_pgpt);
}
#endif // DEBUG

/***************************************************************************
    Fill a rectangle with a two color pattern.
***************************************************************************/
void GraphicsEnvironment::FillRcApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;

    _gdd.grfgdd = fgddFill | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawRcs(&rcs, &_gdd);
}

/***************************************************************************
    Fill a rectangle with a color.
***************************************************************************/
void GraphicsEnvironment::FillRc(RC *prc, AbstractColor acr)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertPo(&acr, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;

    _gdd.grfgdd = fgddFill;
    _gdd.acrFore = acr;

    _pgpt->DrawRcs(&rcs, &_gdd);
}

/***************************************************************************
    Frame a rectangle with a two color pattern.
***************************************************************************/
void GraphicsEnvironment::FrameRcApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs) || _gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    _gdd.grfgdd = fgddFrame | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawRcs(&rcs, &_gdd);
}

/***************************************************************************
    Frame a rectangle with a color.
***************************************************************************/
void GraphicsEnvironment::FrameRc(RC *prc, AbstractColor acr)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertPo(&acr, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs) || _gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    _gdd.grfgdd = fgddFrame;
    _gdd.acrFore = acr;

    _pgpt->DrawRcs(&rcs, &_gdd);
}

/***************************************************************************
    For hilighting text.  On mac, interchanges the system hilite color and
    the background color.  On Win, just inverts.
***************************************************************************/
void GraphicsEnvironment::HiliteRc(RC *prc, AbstractColor acrBack)
{
    AssertThis(0);
    AssertVarMem(prc);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;
    _gdd.acrBack = acrBack;
    _pgpt->HiliteRcs(&rcs, &_gdd);
}

/***************************************************************************
    Fill an oval with a two color pattern.
***************************************************************************/
void GraphicsEnvironment::FillOvalApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;

    _gdd.grfgdd = fgddFill | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawOval(&rcs, &_gdd);
}

/***************************************************************************
    Fill an oval with a color.
***************************************************************************/
void GraphicsEnvironment::FillOval(RC *prc, AbstractColor acr)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertPo(&acr, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;

    _gdd.grfgdd = fgddFill;
    _gdd.acrFore = acr;

    _pgpt->DrawOval(&rcs, &_gdd);
}

/***************************************************************************
    Frame an oval with a two color pattern.
***************************************************************************/
void GraphicsEnvironment::FrameOvalApt(RC *prc, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs) || _gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    _gdd.grfgdd = fgddFrame | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawOval(&rcs, &_gdd);
}

/***************************************************************************
    Frame an oval with a color.
***************************************************************************/
void GraphicsEnvironment::FrameOval(RC *prc, AbstractColor acr)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertPo(&acr, 0);

    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs) || _gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    _gdd.grfgdd = fgddFrame;
    _gdd.acrFore = acr;

    _pgpt->DrawOval(&rcs, &_gdd);
}

/***************************************************************************
    Draw a line with a pattern.  Sets the pen position to (xp2, yp2).
***************************************************************************/
void GraphicsEnvironment::LineApt(long xp1, long yp1, long xp2, long yp2, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);
    SystemPoint pts1, pts2;

    if (_gdd.dxpPen != 0 || _gdd.dypPen != 0)
    {
        _gdd.grfgdd = fgddFrame | fgddPattern;
        _gdd.apt = *papt;
        _OffsetPattern();
        _gdd.acrFore = acrFore;
        _gdd.acrBack = acrBack;

        _MapPtPts(xp1, yp1, &pts1);
        _MapPtPts(xp2, yp2, &pts2);
        _pgpt->DrawLine(&pts1, &pts2, &_gdd);
    }
    _xp = xp2;
    _yp = yp2;
}

/***************************************************************************
    Draw a line in a solid color.  Sets the pen position to (xp2, yp2).
***************************************************************************/
void GraphicsEnvironment::Line(long xp1, long yp1, long xp2, long yp2, AbstractColor acr)
{
    AssertThis(0);
    AssertPo(&acr, 0);
    SystemPoint pts1, pts2;

    if (_gdd.dxpPen != 0 || _gdd.dypPen != 0)
    {
        _gdd.grfgdd = fgddFrame;
        _gdd.acrFore = acr;

        _MapPtPts(xp1, yp1, &pts1);
        _MapPtPts(xp2, yp2, &pts2);
        _pgpt->DrawLine(&pts1, &pts2, &_gdd);
    }
    _xp = xp2;
    _yp = yp2;
}

/***************************************************************************
    Fill a polygon with a pattern.
***************************************************************************/
void GraphicsEnvironment::FillOgnApt(PPolygon pogn, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);
    HQ hqoly;

    if ((hqoly = _HqolyCreate(pogn, fognAutoClose)) == hqNil)
    {
        PushErc(ercGfxCantDraw);
        return;
    }

    _gdd.grfgdd = fgddFill | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawPoly(hqoly, &_gdd);
    FreePhq(&hqoly);
}

/***************************************************************************
    Fill a polygon with a color.
***************************************************************************/
void GraphicsEnvironment::FillOgn(PPolygon pogn, AbstractColor acr)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    AssertPo(&acr, 0);
    HQ hqoly;

    if ((hqoly = _HqolyCreate(pogn, fognAutoClose)) == hqNil)
    {
        PushErc(ercGfxCantDraw);
        return;
    }

    _gdd.grfgdd = fgddFill;
    _gdd.acrFore = acr;

    _pgpt->DrawPoly(hqoly, &_gdd);
    FreePhq(&hqoly);
}

/***************************************************************************
    Frame a polygon with a pattern.
    NOTE: Using kacrInvert produces slightly different results on the Mac.
    (Mac only does alternate winding).
***************************************************************************/
void GraphicsEnvironment::FrameOgnApt(PPolygon pogn, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);
    HQ hqoly;

    if (_gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    if (hqNil == (hqoly = _HqolyFrame(pogn, fognAutoClose)))
    {
        PushErc(ercGfxCantDraw);
        return;
    }

    _gdd.grfgdd = fgddFrame | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawPoly(hqoly, &_gdd);
    FreePhq(&hqoly);
}

/***************************************************************************
    Frame a polygon with a color.
    NOTE: Using kacrInvert produces slightly different results on the Mac.
***************************************************************************/
void GraphicsEnvironment::FrameOgn(PPolygon pogn, AbstractColor acr)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    AssertPo(&acr, 0);
    HQ hqoly;

    if (_gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    if (hqNil == (hqoly = _HqolyFrame(pogn, fognAutoClose)))
    {
        PushErc(ercGfxCantDraw);
        return;
    }

    _gdd.grfgdd = fgddFrame;
    _gdd.acrFore = acr;

    _pgpt->DrawPoly(hqoly, &_gdd);
    FreePhq(&hqoly);
}

/***************************************************************************
    Frame a poly-line with a pattern.
    NOTE: Using kacrInvert produces slightly different results on the Mac.
***************************************************************************/
void GraphicsEnvironment::FramePolyLineApt(PPolygon pogn, AbstractPattern *papt, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    AssertVarMem(papt);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);
    HQ hqoly;

    if (_gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    if (hqNil == (hqoly = _HqolyFrame(pogn, fognNil)))
    {
        PushErc(ercGfxCantDraw);
        return;
    }

    _gdd.grfgdd = fgddFrame | fgddPattern;
    _gdd.apt = *papt;
    _OffsetPattern();
    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;

    _pgpt->DrawPoly(hqoly, &_gdd);
    FreePhq(&hqoly);
}

/***************************************************************************
    Frame a poly-line with a color.
    NOTE: Using kacrInvert produces slightly different results on the Mac.
***************************************************************************/
void GraphicsEnvironment::FramePolyLine(PPolygon pogn, AbstractColor acr)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    AssertPo(&acr, 0);
    HQ hqoly;

    if (_gdd.dxpPen == 0 && _gdd.dypPen == 0)
        return;

    if (hqNil == (hqoly = _HqolyCreate(pogn, fognNil)))
    {
        PushErc(ercGfxCantDraw);
        return;
    }

    _gdd.grfgdd = fgddFrame;
    _gdd.acrFore = acr;

    _pgpt->DrawPoly(hqoly, &_gdd);
    FreePhq(&hqoly);
}

/***************************************************************************
    Convert an Polygon into a polygon record (hqoly).  This maps the points and
    optionally closes the polygon and/or calculates the bounds (Mac only).
***************************************************************************/
HQ GraphicsEnvironment::_HqolyCreate(PPolygon pogn, ulong grfogn)
{
    AssertThis(0);
    AssertPo(pogn, 0);
    HQ hqoly;
    long ipt;
    long cb;
    long cpt;
    OLY *poly;
    PT *ppt;
    SystemPoint *ppts;

    if ((cpt = pogn->IvMac()) < 2)
        return hqNil;

    cb = kcbOlyBase + LwMul(cpt, size(SystemPoint));
    if (cpt < 3)
        grfogn &= ~fognAutoClose;
    else if (grfogn & fognAutoClose)
        cb += size(SystemPoint);

    if (!FAllocHq(&hqoly, cb, fmemNil, mprNormal))
        return hqNil;

    poly = (OLY *)PvLockHq(hqoly);
#ifdef MAC
    poly->cb = (short)cb;
    Assert(cb == poly->cb, "polygon too big");
    poly->rcs.left = kswMax;
    poly->rcs.right = kswMin;
    poly->rcs.top = kswMax;
    poly->rcs.bottom = kswMin;
#else  //! MAC
    poly->cpts = cpt + FPure(grfogn & fognAutoClose);
#endif //! MAC

    ppt = pogn->PrgptLock();
    ppts = (SystemPoint *)poly->rgpts;
    for (ipt = 0; ipt < cpt; ipt++, ppt++, ppts++)
    {
        _MapPtPts(ppt->xp, ppt->yp, ppts);
#ifdef MAC
        // Compute bounding rectangle.
        if (poly->rcs.top > ppts->v)
            poly->rcs.top = ppts->v;
        if (poly->rcs.left > ppts->h)
            poly->rcs.left = ppts->h;
        if (poly->rcs.bottom < ppts->v)
            poly->rcs.bottom = ppts->v;
        if (poly->rcs.right < ppts->h)
            poly->rcs.right = ppts->h;
#endif // MAC
    }
    if (grfogn & fognAutoClose)
        *ppts = poly->rgpts[0];

    pogn->Unlock();
    UnlockHq(hqoly);
    return hqoly;
}

/***************************************************************************
    Convert a polygon (Polygon) into a polygon record (hqoly).  On Windows,
    this actually generates a new polygon that is the outline of the framed
    path (which we'll tell GDI to fill).  On the Mac, this just calls
    _HqolyCreate.
***************************************************************************/
HQ GraphicsEnvironment::_HqolyFrame(PPolygon pogn, ulong grfogn)
{
#if defined(WIN) || defined(KAUAI_SDL3)
    AssertThis(0);
    AssertPo(pogn, 0);
    HQ hqoly;

    PPolygon pognUse;
    PT rgptPen[4]; // Pen rectangle vectors.

    rgptPen[0].xp = 0;
    rgptPen[0].yp = 0;
    rgptPen[1].xp = _gdd.dxpPen;
    rgptPen[1].yp = 0;
    rgptPen[2].xp = _gdd.dxpPen;
    rgptPen[2].yp = _gdd.dypPen;
    rgptPen[3].xp = 0;
    rgptPen[3].yp = _gdd.dypPen;

    if (pvNil == (pognUse = pogn->PognTraceRgpt(rgptPen, 4, grfogn)))
        return pvNil;
    hqoly = _HqolyCreate(pognUse, grfogn);
    ReleasePpo(&pognUse);

    return hqoly;
#elif defined(MAC)
    return _HqolyCreate(pogn, grfogn);
#endif // MAC
}

/***************************************************************************
    Scroll the given rectangle by (dxp, dyp).  If prc1 is not nil fill it
    with the first uncovered rectangle.  If prc2 is not nil fill it
    with the second uncovered rectangle (if there is one).
***************************************************************************/
void GraphicsEnvironment::ScrollRc(RC *prc, long dxp, long dyp, RC *prc1, RC *prc2)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertNilOrVarMem(prc1);
    AssertNilOrVarMem(prc2);
    SystemPoint pts;
    SystemRectangle rcs;
    PT pt;

    if (!_FMapRcRcs(prc, &rcs) || dxp == 0 && dyp == 0)
    {
        if (pvNil != prc1)
            prc1->Zero();
        if (pvNil != prc2)
            prc2->Zero();
        return;
    }

    _MapPtPts(prc->xpLeft + dxp, prc->ypTop + dyp, &pts);
    pt = pts;
    _pgpt->ScrollRcs(&rcs, pt.xp - rcs.left, pt.yp - rcs.top, &_gdd);

    GetBadRcForScroll(prc, dxp, dyp, prc1, prc2);
}

/***************************************************************************
    Static method to get the RC's that are uncovered during a scroll
    operation.
***************************************************************************/
void GraphicsEnvironment::GetBadRcForScroll(RC *prc, long dxp, long dyp, RC *prc1, RC *prc2)
{
    AssertNilOrVarMem(prc1);
    AssertNilOrVarMem(prc2);

    const auto area = ViewRect(*prc);
    MMPixelRect first, second;
    mm_graphics_scroll_exposed(&area, dxp, dyp, prc1 ? &first : nullptr, prc2 ? &second : nullptr);
    if (prc1)
        *prc1 = NativeRect(first);
    if (prc2)
        *prc2 = NativeRect(second);
}

/***************************************************************************
    Get the source rectangle.
***************************************************************************/
void GraphicsEnvironment::GetRcSrc(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    *prc = NativeRect(_view.source);
}

/***************************************************************************
    Set the source rectangle.
***************************************************************************/
void GraphicsEnvironment::SetRcSrc(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    const auto rect = ViewRect(*prc);
    mm_graphics_view_set_source(&_view, &rect);
    AssertThis(0);
}

/***************************************************************************
    Get the destination rectangle.
***************************************************************************/
void GraphicsEnvironment::GetRcDst(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    PT pt;
    _pgpt->GetPtBase(&pt);
    *prc = NativeRect(mm_graphics_view_destination(&_view, ViewPoint(pt)));
}

/***************************************************************************
    Set the destination rectangle.  Also opens up the vis rc and clipping
    and sets default font and pen values.
***************************************************************************/
void GraphicsEnvironment::SetRcDst(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    PT pt;

    _pgpt->GetPtBase(&pt);
    const auto rect = ViewRect(*prc);
    mm_graphics_view_set_destination(&_view, &rect, ViewPoint(pt));
    _PublishClip();

    _dsf.onn = vntl.OnnSystem();
    _dsf.grfont = fontNil;
    _dsf.dyp = vpappb->DypTextDef();
    _dsf.tah = tahLeft;
    _dsf.tav = tavTop;
    _gdd.dxpPen = _gdd.dypPen = 1;

    AssertThis(0);
}

/***************************************************************************
    Set the visible rectangle.  When ClipRc is called, the rc is first
    intersected with the vis rc.  Passing pvNil opens up the vis rectangle
    (so there is no natural clipping).  This should be called _after_
    SetRcDst, since SetRcDst opens up the vis rc.  This also opens the
    clipping to the vis rc.
***************************************************************************/
void GraphicsEnvironment::SetRcVis(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);
    const auto rect = prc ? ViewRect(*prc) : MMPixelRect{};
    mm_graphics_view_set_visible(&_view, prc ? &rect : nullptr);
    _PublishClip();
    AssertThis(0);
}

/***************************************************************************
    Intersect the current vis rectangle with the given rectangle and make
    that the new vis rectangle.  Opens the clipping to the vis rectangle
    also.
***************************************************************************/
void GraphicsEnvironment::IntersectRcVis(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    const auto rect = ViewRect(*prc);
    mm_graphics_view_intersect_visible(&_view, &rect);
    _PublishClip();
    AssertThis(0);
}

/***************************************************************************
    Set the clipping (in source coordinates).  If prc is pvNil, opens up
    the clipping (to the vis rectangle).  Otherwise, sets the clipping
    to the intersection of the vis rectangle and *prc.
***************************************************************************/
void GraphicsEnvironment::ClipRc(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);
    const auto rect = prc ? ViewRect(*prc) : MMPixelRect{};
    mm_graphics_view_clip(&_view, prc ? &rect : nullptr);
    _PublishClip();
}

/***************************************************************************
    Clip to the source rectangle.
***************************************************************************/
void GraphicsEnvironment::ClipToSrc(void)
{
    AssertThis(0);
    mm_graphics_view_clip(&_view, &_view.source);
    _PublishClip();
}

void GraphicsEnvironment::_PublishClip()
{
    if (_view.has_clip)
    {
        auto rect = NativeRect(_view.clip);
        _rcsClip = MacWin(SystemRectangle(rect), rect);
        _gdd.prcsClip = &_rcsClip;
    }
    else
        _gdd.prcsClip = nullptr;
}

void GraphicsEnvironment::_OffsetPattern()
{
    const auto origin = mm_graphics_view_pattern_origin(&_view);
    _gdd.apt.MoveOrigin(origin.x, origin.y);
}

/***************************************************************************
    Set the pen size (in source coordinates).
***************************************************************************/
void GraphicsEnvironment::SetPenSize(long dxpPen, long dypPen)
{
    AssertThis(0);
    AssertIn(dxpPen, 0, kswMax);
    AssertIn(dypPen, 0, kswMax);
    const auto pen = mm_graphics_view_scale_size(&_view,
                                                {static_cast<MMI32>(dxpPen), static_cast<MMI32>(dypPen)});
    _gdd.dxpPen = pen.x;
    _gdd.dypPen = pen.y;
}

/***************************************************************************
    Set the current font info.
***************************************************************************/
void GraphicsEnvironment::SetFont(long onn, ulong grfont, long dypFont, long tah, long tav)
{
    AssertThis(0);
    _dsf.onn = onn;
    _dsf.grfont = grfont;
    _dsf.dyp = mm_graphics_view_scale_size(&_view, {0, static_cast<MMI32>(dypFont)}).y;
    _dsf.tah = tah;
    _dsf.tav = tav;
    AssertThis(0);
}

/***************************************************************************
    Set the current font.
***************************************************************************/
void GraphicsEnvironment::SetOnn(long onn)
{
    AssertThis(0);
    _dsf.onn = onn;
    AssertThis(0);
}

/***************************************************************************
    Set the current font style.
***************************************************************************/
void GraphicsEnvironment::SetFontStyle(ulong grfont)
{
    AssertThis(0);
    _dsf.grfont = grfont;
    AssertThis(0);
}

/***************************************************************************
    Set the current font size.
***************************************************************************/
void GraphicsEnvironment::SetFontSize(long dyp)
{
    AssertThis(0);
    _dsf.dyp = mm_graphics_view_scale_size(&_view, {0, static_cast<MMI32>(dyp)}).y;
    AssertThis(0);
}

/***************************************************************************
    Set the current font alignment.
***************************************************************************/
void GraphicsEnvironment::SetFontAlign(long tah, long tav)
{
    AssertThis(0);
    _dsf.tah = tah;
    _dsf.tav = tav;
    AssertThis(0);
}

/******************************************************************************
    Set the current font.  Font size must be specified in Dst units.
******************************************************************************/
void GraphicsEnvironment::SetDsf(FontDescription *pdsf)
{
    AssertThis(0);
    AssertPo(pdsf, 0);

    _dsf = *pdsf;
    AssertThis(0);
}

/******************************************************************************
    Get the current font.  Font size is specified in Dst units.
******************************************************************************/
void GraphicsEnvironment::GetDsf(FontDescription *pdsf)
{
    AssertThis(0);
    AssertVarMem(pdsf);
    *pdsf = _dsf;
}

/******************************************************************************
    Draw some text.
******************************************************************************/
void GraphicsEnvironment::DrawRgch(achar *prgch, long cch, long xp, long yp, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);

    SystemPoint pts;

    if (cch == 0)
        return;

    _gdd.acrFore = acrFore;
    _gdd.acrBack = acrBack;
    _MapPtPts(xp, yp, &pts);
    _pgpt->DrawRgch(prgch, cch, pts, &_gdd, &_dsf);
}

/***************************************************************************
    Draw the given string.
***************************************************************************/
void GraphicsEnvironment::DrawStn(PString pstn, long xp, long yp, AbstractColor acrFore, AbstractColor acrBack)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    AssertPo(&acrFore, 0);
    AssertPo(&acrBack, 0);

    DrawRgch(pstn->Prgch(), pstn->Cch(), xp, yp, acrFore, acrBack);
}

/******************************************************************************
    Return the bounding box of the text.  If the GraphicsEnvironment has any scaling, this
    is approximate.  This even works if cch is 0 (just gives the height).
******************************************************************************/
void GraphicsEnvironment::GetRcFromRgch(RC *prc, achar *prgch, long cch, long xp, long yp)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch);

    SystemPoint pts;
    SystemRectangle rcs;

    _MapPtPts(xp, yp, &pts);
    _pgpt->GetRcsFromRgch(&rcs, prgch, cch, pts, &_dsf);
    *prc = rcs;
    *prc = _MapRc(*prc, true);
}

/******************************************************************************
    Return the bounding box of the text.  If the GraphicsEnvironment has any scaling, this
    is approximate.  This even works if the string is empty (gives the height).
******************************************************************************/
void GraphicsEnvironment::GetRcFromStn(RC *prc, PString pstn, long xp, long yp)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertPo(pstn, 0);

    GetRcFromRgch(prc, pstn->Prgch(), pstn->Cch(), xp, yp);
}

/***************************************************************************
    Copy bits from a GraphicsEnvironment to this one.
***************************************************************************/
void GraphicsEnvironment::CopyPixels(PGraphicsEnvironment pgnvSrc, RC *prcSrc, RC *prcDst)
{
    AssertThis(0);
    AssertPo(pgnvSrc, 0);
    AssertVarMem(prcSrc);
    AssertVarMem(prcDst);
    SystemRectangle rcsSrc, rcsDst;

    if (!pgnvSrc->_FMapRcRcs(prcSrc, &rcsSrc) || !_FMapRcRcs(prcDst, &rcsDst))
        return;
    _pgpt->CopyPixels(pgnvSrc->_pgpt, &rcsSrc, &rcsDst, &_gdd);
}

/***************************************************************************
    Get the old palette in *ppglclrOld, allocate a transitionary palette
    in *ppglclrTrans, and get init the palette animation.  On failure set
    the new palette and set *ppglclrOld and *ppglclrTrans to nil.
    If cbitPixel is not zero and not the depth of this device, this sets
    the palette and returns false.
***************************************************************************/
bool GraphicsEnvironment::_FInitPaletteTrans(PDynamicArray pglclr, PDynamicArray *ppglclrOld, PDynamicArray *ppglclrTrans, long cbitPixel)
{
    AssertNilOrPo(pglclr, 0);
    AssertVarMem(ppglclrOld);
    AssertVarMem(ppglclrTrans);
    long cclr = pvNil == pglclr ? 256 : pglclr->IvMac();

    *ppglclrOld = pvNil;
    *ppglclrTrans = pvNil;

    // get the current palette and set up the temporary transitionary palette
    if (0 != cbitPixel && _pgpt->CbitPixel() != cbitPixel || pvNil == (*ppglclrOld = GraphicsPort::PglclrGetPalette()) ||
        0 == (cclr = LwMin((*ppglclrOld)->IvMac(), cclr)) || pvNil == (*ppglclrTrans = DynamicArray::PglNew(size(Color), cclr)))
    {
        ReleasePpo(ppglclrOld);
        if (pvNil != pglclr)
            GraphicsPort::SetActiveColors(pglclr, fpalIdentity);
        return fFalse;
    }

    AssertDo((*ppglclrTrans)->FSetIvMac(cclr), 0);
    GraphicsPort::SetActiveColors(*ppglclrOld, fpalIdentity | fpalInitAnim);
    return fTrue;
}

/***************************************************************************
    Transition the palette.  Merge pglclrOld and pglclrNew into pglclrTrans
    and animate the palette to pglclrTrans.  If either source palette is nil,
    *pclrSub is used in place of the nil palette.  acrSub must be an RGB color.
***************************************************************************/
void GraphicsEnvironment::_PaletteTrans(PDynamicArray pglclrOld, PDynamicArray pglclrNew, long lwNum, long lwDen, PDynamicArray pglclrTrans, Color *pclrSub)
{
    AssertNilOrPo(pglclrOld, 0);
    AssertNilOrPo(pglclrNew, 0);
    AssertIn(lwDen, 1, kcbMax);
    AssertIn(lwNum, 0, lwDen + 1);
    AssertNilOrVarMem(pclrSub);

    static_assert(sizeof(Color) == 4, "Transition palette colour layout");
    const auto lock = [](PDynamicArray palette) -> const byte * {
        static const byte empty[4] = {};
        if (!palette)
            return nullptr;
        return palette->IvMac() ? static_cast<const byte *>(palette->PvLock(0)) : empty;
    };
    const auto *old = lock(pglclrOld);
    const auto *next = lock(pglclrNew);
    const auto count = pglclrTrans->IvMac();
    auto *out = count ? static_cast<byte *>(pglclrTrans->PvLock(0)) : nullptr;
    mm_graphics_transition_palette(out, count, old, pglclrOld ? pglclrOld->IvMac() : 0,
                                   next, pglclrNew ? pglclrNew->IvMac() : 0,
                                   reinterpret_cast<const byte *>(pclrSub), lwNum, lwDen);
    if (count)
        pglclrTrans->Unlock();
    if (pglclrNew && pglclrNew->IvMac())
        pglclrNew->Unlock();
    if (pglclrOld && pglclrOld->IvMac())
        pglclrOld->Unlock();

    GraphicsPort::SetActiveColors(pglclrTrans, fpalIdentity | fpalAnimate);
}

/***************************************************************************
    Create a temporary GraphicsEnvironment that is a copy of the given rectangle in this
    GraphicsEnvironment.  This is used for several transitions.
***************************************************************************/
bool GraphicsEnvironment::_FEnsureTempGnv(PGraphicsEnvironment *ppgnv, RC *prc)
{
    PGraphicsPort pgpt;
    PGraphicsEnvironment pgnv;

    if (pvNil == (pgpt = GraphicsPort::PgptNewOffscreen(prc, 8)) || pvNil == (pgnv = NewObj GraphicsEnvironment(pgpt)))
    {
        ReleasePpo(&pgpt);
        *ppgnv = pvNil;
        return fFalse;
    }

    ReleasePpo(&pgpt);
#ifdef KAUAI_PORTABLE_RUNTIME
    if (_pgpt->CbitPixel() == 32)
    {
        // The SDL display stores RGB, but dissolve operates on palette indices.
        // Capture the already-paletted movie losslessly instead of invoking the
        // unsupported general true-color-to-indexed copy policy.
        SystemRectangle source, destination;
        if (!_FMapRcRcs(prc, &source) || !pgnv->_FMapRcRcs(prc, &destination) ||
            !pgnv->_pgpt->FCopyIndexedSnapshot(_pgpt, &source, &destination))
        {
            PushErc(ercGfxCantDraw);
            ReleasePpo(&pgnv);
            *ppgnv = pvNil;
            return fFalse;
        }
    }
    else
#endif
        pgnv->CopyPixels(this, prc, prc);
    GraphicsPort::Flush();
    *ppgnv = pgnv;
    return fTrue;
}

void GraphicsEnvironment::_Transition(MMGraphicsTransition config, AbstractColor fill, PGraphicsEnvironment source,
                                     RC *sourceRect, RC *destination, PDynamicArray palette, Random *random)
{
    AssertThis(0);
    AssertPo(&fill, 0);
    if (config.kind == MM_GRAPHICS_DISSOLVE)
    {
        AssertNilOrPo(source, 0);
        AssertNilOrVarMem(sourceRect);
    }
    else
    {
        AssertPo(source, 0);
        AssertVarMem(sourceRect);
    }
    AssertVarMem(destination);
    AssertNilOrPo(palette, 0);
    Assert((config.kind != MM_GRAPHICS_WIPE && config.kind != MM_GRAPHICS_SLIDE) ||
               (sourceRect->Dyp() == destination->Dyp() && sourceRect->Dxp() == destination->Dxp()), "rc's are scaled");

    struct TransitionContext
    {
        PGraphicsEnvironment target, source, snapshot;
        RC *sourceRect, *destination;
        AbstractColor fill;
        PDynamicArray palette, original, blend;
        PRegion iris;
        Color fillColor;
        Random *random;
        PGraphicsPort lockedSource, lockedSnapshot;
    } context{this, source, nullptr, sourceRect, destination, fill, palette, nullptr, nullptr, nullptr, {}, random, nullptr, nullptr};
    if (config.kind == MM_GRAPHICS_FADE)
        fill.GetClr(&context.fillColor);
    config.fill = fill != kacrClear;
    config.palette = palette != nullptr;
    config.has_source = source != nullptr;
    if (sourceRect)
        config.source = ViewRect(*sourceRect);
    config.destination = ViewRect(*destination);
    config.view = &_view;
    if (config.kind == MM_GRAPHICS_IRIS)
    {
        PT base;
        _pgpt->GetPtBase(&base);
        config.port_base = ViewPoint(base);
    }

    const MMGraphicsTransitionServices services{
        &context,
        [](void *) -> MMU32 { return TsCurrent(); },
        [](void *) { GraphicsPort::Flush(); },
        [](void *p, MMGraphicsTransitionKind effect) -> int {
            auto &state = *static_cast<TransitionContext *>(p);
            if (effect == MM_GRAPHICS_SLIDE)
                return state.target->_FEnsureTempGnv(&state.snapshot, state.destination);
            state.iris = Region::PregnNew(state.destination);
            return state.iris != nullptr;
        },
        [](void *p) -> int { return static_cast<TransitionContext *>(p)->target->_pgpt->CbitPixel(); },
        [](void *p, int depth) -> int {
            auto &state = *static_cast<TransitionContext *>(p);
            return state.target->_FInitPaletteTrans(state.palette, &state.original, &state.blend, depth);
        },
        [](void *p, MMGraphicsPalettePhase phase, MMU32 step, MMU32 total) {
            auto &state = *static_cast<TransitionContext *>(p);
            auto old = phase == MM_GRAPHICS_PALETTE_FROM_FILL ? nullptr : state.original;
            auto next = state.palette;
            if (phase == MM_GRAPHICS_PALETTE_TO_FILL)
                next = nullptr;
            else if (phase == MM_GRAPHICS_PALETTE_FROM_FILL && !next)
                next = state.original;
            state.target->_PaletteTrans(old, next, step, total, state.blend,
                                        phase == MM_GRAPHICS_PALETTE_BLEND ? nullptr : &state.fillColor);
        },
        [](void *p, int originalIfUnspecified) {
            auto &state = *static_cast<TransitionContext *>(p);
            auto next = state.palette ? state.palette : originalIfUnspecified ? state.original : nullptr;
            GraphicsPort::SetActiveColors(next, fpalIdentity);
        },
        [](void *p, int snapshot, int fillPhase, const MMPixelRect *src, const MMPixelRect *dst) {
            auto &state = *static_cast<TransitionContext *>(p);
            auto target = snapshot ? state.snapshot : state.target;
            auto destination = NativeRect(*dst);
            if (fillPhase)
                target->FillRc(&destination, state.fill);
            else
            {
                auto source = NativeRect(*src);
                target->CopyPixels(state.source, &source, &destination);
            }
        },
        [](void *p, MMPixelPoint delta) {
            auto &state = *static_cast<TransitionContext *>(p);
            state.snapshot->ScrollRc(state.destination, delta.x, delta.y);
        },
        [](void *p) {
            auto &state = *static_cast<TransitionContext *>(p);
            state.target->CopyPixels(state.snapshot, state.destination, state.destination);
        },
        [](void *p, int fillPhase, const MMPixelRect *include, const MMPixelRect *exclude) -> int {
            auto &state = *static_cast<TransitionContext *>(p);
            auto port = state.target->_pgpt;
            auto outer = NativeRect(*include), inner = NativeRect(*exclude);
            PRegion clip = nullptr;
            port->ClipToRegn(&clip);
            state.iris->SetRc(&outer);
            bool ok = state.iris->FDiffRc(&inner) && (!clip || state.iris->FIntersect(clip));
            port->ClipToRegn(&clip);
            if (!ok)
                return false;
            port->ClipToRegn(&state.iris);
            if (fillPhase)
                state.target->FillRc(state.destination, state.fill);
            else
                state.target->CopyPixels(state.source, state.sourceRect, state.destination);
            GraphicsPort::Flush();
            port->ClipToRegn(&state.iris);
            return true;
        },
        [](void *p, MMGraphicsDissolvePixels *pixels) -> int {
            auto &state = *static_cast<TransitionContext *>(p);
            RC bounds;
            if (state.source)
            {
                AssertVarMem(state.sourceRect);
                Assert(state.sourceRect->Dxp() == state.destination->Dxp() &&
                           state.sourceRect->Dyp() == state.destination->Dyp(), "rc's are scaled");
                auto port = state.source->Pgpt();
                auto bytes = port->CbitPixel() == 8 ? port->PrgbLockPixels(&bounds) : nullptr;
                if (!bytes)
                {
                    Bug("Can't dissolve from this GraphicsPort");
                    return false;
                }
                state.lockedSource = port;
                auto mapped = state.source->_MapRc(*state.sourceRect);
                if (!bounds.FContains(&mapped))
                {
                    Bug("Source rectangle is outside the source bitmap");
                    return false;
                }
                pixels->source = {bytes, bounds.Dxp(), bounds.Dyp(), static_cast<MMI32>(port->CbRow()), 8};
                pixels->source_origin = {mapped.xpLeft - bounds.xpLeft, mapped.ypTop - bounds.ypTop};
            }
            if (!state.target->_FEnsureTempGnv(&state.snapshot, state.destination))
                return false;
            auto port = state.snapshot->Pgpt();
            auto bytes = port->PrgbLockPixels(&bounds);
            if (!bytes)
                return false;
            state.lockedSnapshot = port;
            pixels->destination = {bytes, bounds.Dxp(), bounds.Dyp(), static_cast<MMI32>(port->CbRow()), 8};
            return true;
        },
        [](void *p) {
            auto &state = *static_cast<TransitionContext *>(p);
            if (state.lockedSnapshot)
                state.lockedSnapshot->Unlock();
            if (state.lockedSource)
                state.lockedSource->Unlock();
        },
        [](void *p, MMU32 limit) -> MMU32 {
            return static_cast<TransitionContext *>(p)->random->LwNext(static_cast<LegacyLong>(limit));
        }};
    mm_graphics_transition_run(&config, &services);
    ReleasePpo(&context.iris);
    ReleasePpo(&context.snapshot);
    ReleasePpo(&context.original);
    ReleasePpo(&context.blend);
}

void GraphicsEnvironment::Wipe(long gfd, AbstractColor fill, PGraphicsEnvironment source, RC *sourceRect,
                               RC *destination, ulong duration, PDynamicArray palette)
{
    MMGraphicsTransition config{};
    config.kind = MM_GRAPHICS_WIPE;
    config.duration = static_cast<MMU32>(duration);
    config.directions = static_cast<MMU32>(gfd);
    _Transition(config, fill, source, sourceRect, destination, palette);
}

void GraphicsEnvironment::Slide(long gfd, AbstractColor fill, PGraphicsEnvironment source, RC *sourceRect,
                                RC *destination, ulong duration, PDynamicArray palette)
{
    MMGraphicsTransition config{};
    config.kind = MM_GRAPHICS_SLIDE;
    config.duration = static_cast<MMU32>(duration);
    config.directions = static_cast<MMU32>(gfd);
    _Transition(config, fill, source, sourceRect, destination, palette);
}

/***************************************************************************
    Dissolve to an optional fill colour and then to the supplied source.
    A zero grid dimension selects indexed pixel dissolve.
***************************************************************************/
void GraphicsEnvironment::Dissolve(long cellsWide, long cellsHigh, AbstractColor fill, PGraphicsEnvironment source,
                                   RC *sourceRect, RC *destination, ulong duration, PDynamicArray palette)
{
    AssertIn(cellsWide, 0, kcbMax);
    AssertIn(cellsHigh, 0, kcbMax);
    Random random;
    MMGraphicsTransition config{};
    config.kind = MM_GRAPHICS_DISSOLVE;
    config.duration = static_cast<MMU32>(duration);
    config.dissolve_cells = {static_cast<MMI32>(cellsWide), static_cast<MMI32>(cellsHigh)};
    _Transition(config, fill, source, sourceRect, destination, palette, &random);
}

void GraphicsEnvironment::Fade(long cactMax, AbstractColor fill, PGraphicsEnvironment source, RC *sourceRect,
                               RC *destination, ulong duration, PDynamicArray palette)
{
    AssertIn(cactMax, 0, 257);
    AssertPo(&fill, facrRgb);
    MMGraphicsTransition config{};
    config.kind = MM_GRAPHICS_FADE;
    config.duration = static_cast<MMU32>(duration);
    config.fade_steps = static_cast<MMI32>(cactMax);
    _Transition(config, fill, source, sourceRect, destination, palette);
}

void GraphicsEnvironment::Iris(long gfd, long xp, long yp, AbstractColor fill, PGraphicsEnvironment source, RC *sourceRect,
                               RC *destination, ulong duration, PDynamicArray palette)
{
    MMGraphicsTransition config{};
    config.kind = MM_GRAPHICS_IRIS;
    config.duration = static_cast<MMU32>(duration);
    config.directions = static_cast<MMU32>(gfd);
    config.focus = {static_cast<MMI32>(xp), static_cast<MMI32>(yp)};
    _Transition(config, fill, source, sourceRect, destination, palette);
}

/***************************************************************************
    Draw the picture in the given rectangle.
***************************************************************************/
void GraphicsEnvironment::DrawPic(PPicture ppic, RC *prc)
{
    AssertThis(0);
    AssertPo(ppic, 0);
    AssertVarMem(prc);
    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;
    _pgpt->DrawPic(ppic, &rcs, &_gdd);
}

/***************************************************************************
    Draw the mbmp with reference point at the given point.
***************************************************************************/
void GraphicsEnvironment::DrawMbmp(PMaskedBitmapMBMP pmbmp, long xp, long yp)
{
    AssertThis(0);
    AssertPo(pmbmp, 0);
    RC rc;
    SystemRectangle rcs;

    pmbmp->GetRc(&rc);
    rc.Offset(xp - rc.xpLeft, yp - rc.ypTop);
    if (!_FMapRcRcs(&rc, &rcs))
        return;
    _pgpt->DrawMbmp(pmbmp, &rcs, &_gdd);
}

/***************************************************************************
    Draw the mbmp in the given rectangle.
***************************************************************************/
void GraphicsEnvironment::DrawMbmp(PMaskedBitmapMBMP pmbmp, RC *prc)
{
    AssertThis(0);
    AssertPo(pmbmp, 0);
    AssertVarMem(prc);
    SystemRectangle rcs;

    if (!_FMapRcRcs(prc, &rcs))
        return;
    _pgpt->DrawMbmp(pmbmp, &rcs, &_gdd);
}

RC GraphicsEnvironment::_MapRc(const RC &rect, bool reverse) const
{
    return NativeRect(mm_graphics_view_map_rect(&_view, ViewRect(rect), reverse));
}

PT GraphicsEnvironment::_MapPoint(long x, long y) const
{
    const auto point = mm_graphics_view_map_point(&_view, {static_cast<MMI32>(x), static_cast<MMI32>(y)});
    return PT(point.x, point.y);
}

/***************************************************************************
    Map a rectangle to a system rectangle.  Return true iff the result
    is non-empty.
***************************************************************************/
bool GraphicsEnvironment::_FMapRcRcs(RC *prc, SystemRectangle *prcs)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertVarMem(prcs);
    RC rc = _MapRc(*prc);
    *prcs = SystemRectangle(rc);
    return prcs->left < prcs->right && prcs->top < prcs->bottom;
}

/***************************************************************************
    Map an (xp, yp) pair to a system point.
***************************************************************************/
void GraphicsEnvironment::_MapPtPts(long xp, long yp, SystemPoint *ppts)
{
    AssertThis(0);
    AssertVarMem(ppts);
    PT pt = _MapPoint(xp, yp);
    *ppts = SystemPoint(pt);
}

#ifdef MAC
/***************************************************************************
    Set the port associated with the GraphicsEnvironment as the current port and set the
    clipping as in the GraphicsEnvironment and set the pen and fore/back color to defaults.
***************************************************************************/
void GraphicsEnvironment::Set(void)
{
    AssertThis(0);
    _pgpt->Set(_gdd.prcsClip);
    ForeColor(blackColor);
    BackColor(whiteColor);
    PenNormal();
}

/***************************************************************************
    Restore the port.  Balances a call to GraphicsEnvironment::Set.
***************************************************************************/
void GraphicsEnvironment::Restore(void)
{
    _pgpt->Restore();
}
#endif // MAC

/***************************************************************************
    Create a new polygon by tracing the outline of this one with a
    convex polygon.
***************************************************************************/
PPolygon Polygon::PognTraceOgn(PPolygon pogn, ulong grfogn)
{
    AssertThis(0);
    AssertPo(pogn, 0);

    PPolygon pognNew = PognTraceRgpt(pogn->PrgptLock(), pogn->IvMac(), grfogn);

    pogn->Unlock();
    return pognNew;
}

/***************************************************************************
    Create a new polygon by tracing the outline of this one with a
    convex polygon.
***************************************************************************/
PPolygon Polygon::PognTraceRgpt(PT *prgpt, long cpt, ulong grfogn)
{
    AssertThis(0);
    AssertIn(cpt, 2, kcbMax);
    AssertPvCb(prgpt, LwMul(cpt, size(PT)));

    PT *prgptThis;
    AddEdgeInfo aei;
    long iptLast = IvMac() - 1;

    if (2 > cpt || iptLast < 0)
        return pvNil;

    aei.prgpt = prgpt;
    aei.cpt = cpt;
    if (pvNil == (aei.pogn = PognNew(IvMac() * 2 + cpt)))
        return pvNil;
    aei.pogn->SetMinGrow(cpt);

    prgptThis = PrgptLock();
    if (iptLast == 0 || prgptThis[0] == prgptThis[1])
    {
        // if all the points of the polygon are the same, doing this ensures
        // that we will encircle the pen
        aei.ptCur = prgpt[0] + prgptThis[0];
        if (!aei.pogn->FPush(&aei.ptCur))
            goto LError;
        aei.iptPenCur = 1;
    }
    else
    {
        aei.iptPenCur =
            IptFindLeftmost(prgpt, cpt, prgptThis[0].xp - prgptThis[1].xp, prgptThis[0].yp - prgptThis[1].yp);
    }
    aei.ptCur = prgpt[aei.iptPenCur] + prgptThis[0];
    if (!aei.pogn->FPush(&aei.ptCur))
        goto LError;

    // Walk to end, adding vertices.
    for (aei.dipt = 1, aei.ipt = 0; aei.ipt < iptLast; aei.ipt++)
    {
        if (!_FAddEdge(&aei))
            goto LError;
    }
    if (fognAutoClose & grfogn)
    {
        if (!_FAddEdge(&aei))
            goto LError;
        aei.ipt = 0;
        aei.dipt = iptLast;
        if (!_FAddEdge(&aei))
            goto LError;
    }

    // Walk back to start.
    for (aei.dipt = aei.ipt = iptLast; aei.ipt > 0; aei.ipt--)
    {
        if (!_FAddEdge(&aei))
        {
        LError:
            ReleasePpo(&aei.pogn);
            break;
        }
    }

    Unlock();
    if (pvNil != aei.pogn)
        aei.pogn->FEnsureSpace(0, fgrpShrink);
    return aei.pogn;
}

/***************************************************************************
    Add the vertices encountered while walking an edge of the input polygon.
***************************************************************************/
bool Polygon::_FAddEdge(AddEdgeInfo *paei)
{
    AssertVarMem(paei);
    AssertIn(paei->cpt, 1, kcbMax);
    AssertPvCb(paei->prgpt, LwMul(paei->cpt, size(PT)));
    AssertPo(paei->pogn, 0);
    AssertIn(paei->dipt, LwMin(IvMac() - 1, 1), LwMax(IvMac(), 2));
    AssertIn(paei->ipt, 0, IvMac());
    AssertIn(paei->iptPenCur, 0, paei->cpt);

    PT *prgptThis = (PT *)QvGet(0); // Already locked in PognTraceRgpt().
    long iptEnd = (paei->ipt + paei->dipt) % IvMac();
    long iptPenNew = IptFindLeftmost(paei->prgpt, paei->cpt, prgptThis[iptEnd].xp - prgptThis[paei->ipt].xp,
                                     prgptThis[iptEnd].yp - prgptThis[paei->ipt].yp);

    // Add vertices from current to leftmost.
    if (paei->iptPenCur != iptPenNew)
    {
        PT pt;
        long ipt = paei->iptPenCur;
        PT dpt = paei->ptCur - paei->prgpt[ipt];

        do
        {
            ipt = (ipt + 1) % paei->cpt;
            pt = paei->prgpt[ipt] + dpt;
            if (!paei->pogn->FPush(&pt))
                return fFalse;
        } while (ipt != iptPenNew);
    }

    // Add vertex at endpoint.
    paei->iptPenCur = iptPenNew;
    paei->ptCur = prgptThis[iptEnd] + paei->prgpt[iptPenNew];
    return paei->pogn->FPush(&paei->ptCur);
}

/***************************************************************************
    Find the leftmost vertex of the rgpt looking down the vector.
    dxp, dyp : Direction of vector to look down.
***************************************************************************/
long IptFindLeftmost(PT *prgpt, long cpt, long dxp, long dyp)
{
    AssertPvCb(prgpt, LwMul(cpt, size(PT)));
    AssertIn(cpt, 2, kcbMax);

    long ipt, iptLeftmost;
    long dzpMac; // Maximum cross product (z vector) found.
    long dzp;

    // reduces the chance of overflow
    if (1 < (dzp = LwGcd(dxp, dyp)))
    {
        dxp /= dzp;
        dyp /= dzp;
    }

    for (dzpMac = klwMin, ipt = 0; ipt < cpt; ipt++)
    {
        dzp = LwMul(dyp, prgpt[ipt].xp) - LwMul(dxp, prgpt[ipt].yp);
        if (dzp > dzpMac)
        {
            dzpMac = dzp;
            iptLeftmost = ipt;
        }
    }
    return iptLeftmost;
}

/***************************************************************************
    -- Allocate a new Polygon and ensure that it has space for cptInit elements.
***************************************************************************/
PPolygon Polygon::PognNew(long cptInit)
{
    AssertIn(cptInit, 0, kcbMax);
    PPolygon pogn;

    if (pvNil == (pogn = NewObj Polygon()))
        return pvNil;
    if (cptInit > 0 && !pogn->FEnsureSpace(cptInit, fgrpNil))
    {
        ReleasePpo(&pogn);
        return pvNil;
    }
    AssertPo(pogn, 0);
    return pogn;
}

/***************************************************************************
    Constructor for Polygon.
***************************************************************************/
Polygon::Polygon(void) : DynamicArray(size(PT))
{
    AssertThis(0);
}


#ifdef KAUAI_SDL3
bool GraphicsEnvironment::DrawVideoFrame(const KauaiPixels::DibFrame &frame, RC *rectangle)
{
    SystemRectangle mapped;
    return rectangle && _FMapRcRcs(rectangle, &mapped) && _pgpt->DrawVideoFrame(frame, &mapped, &_gdd);
}
#endif
