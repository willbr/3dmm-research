/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Region code.

***************************************************************************/
#include "kauai_core.h"
#include "region.h"
ASSERTNAME

RTCLASS(Region)
RTCLASS(RegionScanner)

LegacyLong const kcdxpBlock = 100;

/***************************************************************************
    Region builder class.  For building the _pglxp of a region and getting
    its bounding rectangle and _dxp value.
***************************************************************************/
typedef class RegionBuilder *PRegionBuilder;
#define RegionBuilder_PAR BASE
#define kclsRegionBuilder 'rgbl'
class RegionBuilder : public RegionBuilder_PAR
{
  protected:
    LegacyLong _ypCur;
    RC _rcRef;
    RC _rc;
    PDynamicArray _pglxp;
    bool _fResize;

    LegacyLong _idypPrev;
    LegacyLong _idypCur;
    LegacyLong _ixpCur;

  public:
    RegionBuilder(void)
    {
        _pglxp = pvNil;
    }
    ~RegionBuilder(void)
    {
        ReleasePpo(&_pglxp);
    }

    bool FInit(RC *prcRef, PDynamicArray pglxp = pvNil);
    bool FStartRow(LegacyLong dyp, LegacyLong cxpMax);
    void EndRow(void);
    void AddXp(LegacyLong xp)
    {
        // can only be called while building a row.
        AssertThis(0);
        Assert(_idypCur != ivNil, "calling AddXp outside a row");
        Assert(_ixpCur < _pglxp->IvMac(), "overflow in AddXp");

        if (_rc.xpLeft > xp)
            _rc.xpLeft = xp;
        if (_rc.xpRight < xp)
            _rc.xpRight = xp;
        *(LegacyLong *)_pglxp->QvGet(_ixpCur++) = xp;
    }

    bool FDone(void)
    {
        AssertThis(0);
        return _ypCur >= _rcRef.ypBottom && _idypCur == ivNil;
    }
    PDynamicArray PglxpFree(RC *prc, LegacyLong *pdxp);
};

/***************************************************************************
    Initialize a region builder.
***************************************************************************/
bool RegionBuilder::FInit(RC *prcRef, PDynamicArray pglxp)
{
    AssertVarMem(prcRef);
    AssertNilOrPo(pglxp, 0);
    Assert(!prcRef->FEmpty(), "empty reference rectangle");

    ReleasePpo(&_pglxp);
    if (pvNil != pglxp)
    {
        _pglxp = pglxp;
        _pglxp->AddRef();
        _fResize = fFalse;
    }
    else
    {
        if (pvNil == (_pglxp = DynamicArray::PglNew(size(LegacyLong))))
            return fFalse;
        _pglxp->SetMinGrow(kcdxpBlock);
        _fResize = fTrue;
    }

    _rcRef = *prcRef;
    _ypCur = _rc.ypTop = _rc.ypBottom = prcRef->ypTop;
    _rc.xpLeft = prcRef->Dxp();
    _rc.xpRight = 0;

    _idypPrev = _idypCur = ivNil;
    _ixpCur = 0;
    return fTrue;
}

/***************************************************************************
    Begin a row.
***************************************************************************/
bool RegionBuilder::FStartRow(LegacyLong dyp, LegacyLong cxpMax)
{
    AssertThis(0);
    Assert(_idypCur == ivNil, "row already started");
    AssertIn(dyp, 0, kcbMax);
    Assert(_ypCur < _rcRef.ypBottom, "already filled");
    AssertIn(cxpMax, 0, kcbMax);
    LegacyLong ivLim;

    ivLim = _ixpCur + cxpMax + 2;
    if (_fResize)
    {
        ivLim = LwRoundAway(ivLim, kcdxpBlock);
        if (ivLim > _pglxp->IvMac() && !_pglxp->FSetIvMac(ivLim))
        {
            ReleasePpo(&_pglxp);
            return fFalse;
        }
    }
    else if (ivLim > _pglxp->IvMac())
    {
        Bug("overflowed provided pgl");
        return fFalse;
    }

    dyp = LwMin(dyp, _rcRef.ypBottom - _ypCur);
    _idypCur = _ixpCur;
    *(LegacyLong *)_pglxp->QvGet(_ixpCur++) = dyp;
    _ypCur += dyp;
    return fTrue;
}

/***************************************************************************
    End a row in the region builder.
***************************************************************************/
void RegionBuilder::EndRow(void)
{
    AssertThis(0);
    Assert((_ixpCur - _idypCur) & 1, "not an even number of xp values");
    Assert(_ixpCur < _pglxp->IvMac(), "overflow in EndRow");
    LegacyLong czp;
    LegacyLong *qrgxp = (LegacyLong *)_pglxp->QvGet(0);

    qrgxp[_ixpCur++] = klwMax;
    if ((czp = _ixpCur - _idypCur) > 2)
        _rc.ypBottom = _ypCur;

    // see if this row matches the last one
    if (ivNil != _idypPrev && czp == _idypCur - _idypPrev &&
        FEqualRgb(&qrgxp[_idypPrev + 1], &qrgxp[_idypCur + 1], (czp - 1) * size(LegacyLong)))
    {
        // this row matches the previous row
        qrgxp[_idypPrev] += qrgxp[_idypCur];
        _ixpCur = _idypCur;
    }
    else if (0 == _idypCur && _ixpCur == 2)
    {
        // at the top and still empty
        _rc.ypTop = _rc.ypBottom = _ypCur;
        _ixpCur = 0;
    }
    else
        _idypPrev = _idypCur;

    _idypCur = ivNil;
}

/***************************************************************************
    Clean up and return all the relevant information.
***************************************************************************/
PDynamicArray RegionBuilder::PglxpFree(RC *prc, LegacyLong *pdxp)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertVarMem(pdxp);
    PDynamicArray pglxp;

    // see if the last row is empty
    if (_ypCur > _rc.ypBottom)
    {
        // last row is empty
        _ixpCur = _idypPrev;
    }

    *pdxp = 0;
    if (0 == _ixpCur)
    {
        // empty
        Assert(_rc.FEmpty(), 0);
        ReleasePpo(&_pglxp);
        _rc.Zero();
    }
    else
    {
        Assert(!_rc.FEmpty(), 0);
        if (_ixpCur <= 4)
        {
            // rectangular
            ReleasePpo(&_pglxp);
        }
        else
        {
            AssertDo(_pglxp->FSetIvMac(_ixpCur), 0);
            AssertDo(_pglxp->FEnsureSpace(0, fgrpShrink), 0);
            *pdxp = -_rc.xpLeft;
        }
        _rc.Offset(_rcRef.xpLeft, 0);
    }

    *prc = _rc;
    pglxp = _pglxp;
    _pglxp = pvNil;
    return pglxp;
}

/***************************************************************************
    Constructor for the region scanner class.
***************************************************************************/
RegionScanner::RegionScanner(void)
{
    _pglxpSrc = pvNil;
}

/***************************************************************************
    Destructor for the region scanner class.
***************************************************************************/
RegionScanner::~RegionScanner(void)
{
    Free();
}

/***************************************************************************
    Release our hold on any memory.
***************************************************************************/
void RegionScanner::Free(void)
{
    if (pvNil != _pglxpSrc)
    {
        _pglxpSrc->Unlock();
        ReleasePpo(&_pglxpSrc);
    }
}

/***************************************************************************
    Initializes a region scanner.  The scanner implicitly intersects the
    region with *prcRel and returns xp values relative to prcRel->xpLeft.
***************************************************************************/
void RegionScanner::Init(PRegion pregn, RC *prcRel)
{
    _InitCore(pregn->_pglxp, pregn->_rc, pregn->_dxp, *prcRel);
}

void RegionScanner::InitRc(RC *prc, RC *prcRel)
{
    _InitCore(pvNil, *prc, 0, *prcRel);
}

void RegionScanner::_InitCore(PDynamicArray bands, const RC &bounds, LegacyLong offset, const RC &clip)
{
    Free();

    const MMRegionWord *words = nullptr;
    size_t count = 0;
    _pglxpSrc = bands;
    if (bands != pvNil)
    {
        bands->AddRef();
        words = static_cast<const MMRegionWord *>(bands->PvLock(0));
        count = bands->IvMac();
    }

    const MMPixelRect area{static_cast<MMI32>(bounds.xpLeft), static_cast<MMI32>(bounds.ypTop),
                           static_cast<MMI32>(bounds.xpRight), static_cast<MMI32>(bounds.ypBottom)};
    const MMPixelRect relative{static_cast<MMI32>(clip.xpLeft), static_cast<MMI32>(clip.ypTop),
                               static_cast<MMI32>(clip.xpRight), static_cast<MMI32>(clip.ypBottom)};
    mm_region_scan_init(&_scan, &area, offset, words, count, &relative);
}

/***************************************************************************
    Static method to create a new region and set it to a rectangle.
***************************************************************************/
PRegion Region::PregnNew(RC *prc)
{
    PRegion pregn;

    if (pvNil == (pregn = NewObj Region))
        return pvNil;
    if (pvNil != prc)
        pregn->_rc = *prc;
    AssertPo(pregn, 0);
    return pregn;
}

/***************************************************************************
    Destructor for a region.
***************************************************************************/
Region::~Region(void)
{
    ReleasePpo(&_pglxp);
    ReleasePpo(&_nativeCache);
}

/***************************************************************************
    Make the region rectangular.
***************************************************************************/
void Region::SetRc(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);

    if (pvNil == prc)
        _rc.Zero();
    else
        _rc = *prc;
    ReleasePpo(&_pglxp);
    ReleasePpo(&_nativeCache);
    _dxp = 0;
    AssertThis(0);
}

/***************************************************************************
    Offset the region.
***************************************************************************/
void Region::Offset(LegacyLong xp, LegacyLong yp)
{
    AssertThis(0);
    _rc.Offset(xp, yp);
    _dptRgn.Offset(xp, yp);
}

/***************************************************************************
    Return whether the region is empty and if prc is not nil, fill in *prc
    with the region's bounding rectangle.
***************************************************************************/
bool Region::FEmpty(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);

    if (pvNil != prc)
        *prc = _rc;
    return _rc.FEmpty();
}

/***************************************************************************
    Return whether the region is rectangular and if prc is not nil, fill in
    *prc with the region's bounding rectangle.
***************************************************************************/
bool Region::FIsRc(RC *prc)
{
    AssertThis(0);
    if (pvNil != prc)
        *prc = _rc;
    return _pglxp == pvNil;
}

/***************************************************************************
    Scale the x values of the region by the given amount.
***************************************************************************/
void Region::Scale(LegacyLong lwNumX, LegacyLong lwDenX, LegacyLong lwNumY, LegacyLong lwDenY)
{
    AssertThis(0);
    Assert(lwDenX > 0 && lwNumX >= 0, "bad X scaling");
    Assert(lwDenY > 0 && lwNumY >= 0, "bad Y scaling");
    // Invalidate before every scale path, including empty/rectangular results.
    ReleasePpo(&_nativeCache);
    RC rcScaled;
    LegacyLong yp, ypScaled, dyp, dypScaled;
    LegacyLong xp1, xp2;
    RAT ratXp(lwNumX, lwDenX);
    RAT ratYp(lwNumY, lwDenY);

    rcScaled.xpLeft = ratXp.LwScale(_rc.xpLeft);
    rcScaled.xpRight = ratXp.LwScale(_rc.xpRight);
    rcScaled.ypTop = ratYp.LwScale(_rc.ypTop);
    rcScaled.ypBottom = ratYp.LwScale(_rc.ypBottom);

    if (rcScaled.FEmpty())
    {
        _rc.Zero();
        ReleasePpo(&_pglxp);
        return;
    }

    if (pvNil == _pglxp)
    {
        _rc = rcScaled;
        _dxp = 0;
        return;
    }

    RegionScanner regsc;
    RegionBuilder regbl;

    regsc.Init(this, &_rc);
    AssertDo(regbl.FInit(&rcScaled, _pglxp), 0);
    yp = _rc.ypTop;
    ypScaled = rcScaled.ypTop;

    for (;;)
    {
        dyp = regsc.DypCur();
        dypScaled = ratYp.LwScale(yp + dyp) - ypScaled;
        yp += dyp;
        if (dypScaled <= 0)
        {
            regsc.ScanNext(dyp);
            continue;
        }
        ypScaled += dypScaled;

        AssertDo(regbl.FStartRow(dypScaled, regsc.CxpCur()), 0);
        if (regsc.XpCur() == klwMax)
            goto LEndRow;

        xp1 = ratXp.LwScale(regsc.XpCur());
        xp2 = ratXp.LwScale(regsc.XpFetch());
        for (;;)
        {
            if (xp1 >= xp2)
            {
                // empty run - ignore both values
                if (regsc.XpFetch() == klwMax)
                    break;
                xp1 = ratXp.LwScale(regsc.XpCur());
            }
            else
            {
                // write xp1 and advance
                regbl.AddXp(xp1);
                xp1 = xp2;
            }

            // fetch a new xp2
            if (regsc.XpFetch() == klwMax)
            {
                // write the last off
                regbl.AddXp(xp1);
                break;
            }
            xp2 = ratXp.LwScale(regsc.XpCur());
        }

    LEndRow:
        regbl.EndRow();
        if (regbl.FDone())
            break;

        regsc.ScanNext(dyp);
    }

    ReleasePpo(&_pglxp);
    ReleasePpo(&_nativeCache);

    // force the region scanner to let go of the pglxp before the
    // region builder tries to resize it.
    regsc.Free();

    _pglxp = regbl.PglxpFree(&_rc, &_dxp);
}

/***************************************************************************
    Union the two regions and leave the result in this one.  If pregn2 is
    nil, this region is used for pregn2.
***************************************************************************/
bool Region::FUnion(PRegion pregn1, PRegion pregn2)
{
    AssertThis(0);
    AssertPo(pregn1, 0);
    AssertNilOrPo(pregn2, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc;

    if (pvNil == pregn2)
    {
        if (pregn1->FEmpty())
            return fTrue;
        pregn2 = this;
    }

    rc.Union(&pregn1->_rc, &pregn2->_rc);
    if (pvNil == pregn1->_pglxp && rc == pregn1->_rc || pvNil == pregn2->_pglxp && rc == pregn2->_rc)
    {
        // union is a rectangle
        SetRc(&rc);
        return fTrue;
    }

    regsc1.Init(pregn1, &rc);
    regsc2.Init(pregn2, &rc);
    return _FCombine(MM_REGION_UNION, &rc, &regsc1, &regsc2);
}

/***************************************************************************
    Union the given rectangle and region and leave the result in this region.
    If pregn is nil, this region is used for pregn.
***************************************************************************/
bool Region::FUnionRc(RC *prc, PRegion pregn)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertNilOrPo(pregn, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc;

    if (pvNil == pregn)
    {
        if (prc->FEmpty())
            return fTrue;
        pregn = this;
    }

    rc.Union(prc, &pregn->_rc);
    if (rc == *prc)
    {
        // *prc totally contains the region
        SetRc(&rc);
        return fTrue;
    }

    // if this one is rectangular and already contains *prc, then
    // we're done
    if (this == pregn && pvNil == _pglxp && rc == _rc)
        return fTrue;

    regsc1.InitRc(prc, &rc);
    regsc2.Init(pregn, &rc);
    return _FCombine(MM_REGION_UNION, &rc, &regsc1, &regsc2);
}

/***************************************************************************
    Intersect the two regions and leave the result in this one.  If pregn2
    is nil, this region is used for pregn2.
***************************************************************************/
bool Region::FIntersect(PRegion pregn1, PRegion pregn2)
{
    AssertThis(0);
    AssertPo(pregn1, 0);
    AssertNilOrPo(pregn2, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc;

    if (pvNil == pregn2)
        pregn2 = this;

    if (!rc.FIntersect(&pregn1->_rc, &pregn2->_rc))
    {
        // result is empty
        SetRc(&rc);
        return fTrue;
    }

    // if pregn1 is rectangular and contains this one, then
    // we're done
    if (this == pregn2 && pvNil == pregn1->_pglxp && rc == _rc)
        return fTrue;
    if (pvNil == pregn1->_pglxp && pvNil == pregn2->_pglxp)
    {
        // both rectangles, so the intersection is a rectangle
        SetRc(&rc);
        return fTrue;
    }

    regsc1.Init(pregn1, &rc);
    regsc2.Init(pregn2, &rc);
    return _FCombine(MM_REGION_INTERSECTION, &rc, &regsc1, &regsc2);
}

/***************************************************************************
    Intersect the given rectangle and region and leave the result in this
    region.  If pregn is nil, this region is used for pregn.
***************************************************************************/
bool Region::FIntersectRc(RC *prc, PRegion pregn)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertNilOrPo(pregn, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc;

    if (pvNil == pregn)
        pregn = this;

    if (!rc.FIntersect(prc, &pregn->_rc))
    {
        // result is empty
        SetRc(&rc);
        return fTrue;
    }

    // if *prc contains this region, then we're done
    if (this == pregn && rc == _rc)
        return fTrue;
    if (pvNil == pregn->_pglxp)
    {
        // both rectangles, so the intersection is a rectangle
        SetRc(&rc);
        return fTrue;
    }

    regsc1.InitRc(prc, &rc);
    regsc2.Init(pregn, &rc);
    return _FCombine(MM_REGION_INTERSECTION, &rc, &regsc1, &regsc2);
}

/***************************************************************************
    Compute regn2 minus regn1 and put the result in this region.  If
    pregn2 is nil, this region is used.
***************************************************************************/
bool Region::FDiff(PRegion pregn1, PRegion pregn2)
{
    AssertThis(0);
    AssertPo(pregn1, 0);
    AssertNilOrPo(pregn2, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc, rcT;

    if (pvNil == pregn2)
    {
        // use this region as the first operand
        if (!rc.FIntersect(&_rc, &pregn1->_rc))
            return fTrue;
        pregn2 = this;
    }

    if (pregn2->_rc.FEmpty())
    {
        rc.Zero();
        SetRc(&rc);
        return fTrue;
    }

    rc = pregn2->_rc;
    regsc1.Init(pregn1, &rc);
    regsc2.Init(pregn2, &rc);
    return _FCombine(MM_REGION_DIFFERENCE, &rc, &regsc2, &regsc1);
}

/***************************************************************************
    Compute regn minus the given rectangle and put the result in this region.
    If pregn is nil, this region is used.
***************************************************************************/
bool Region::FDiffRc(RC *prc, PRegion pregn)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertNilOrPo(pregn, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc;

    if (pvNil == pregn)
    {
        if (!rc.FIntersect(&_rc, prc))
            return fTrue;
        pregn = this;
    }

    if (pregn->_rc.FEmpty() || prc->FContains(&pregn->_rc))
    {
        // the rectangle contains the entire region or the region is empty
        rc.Zero();
        SetRc(&rc);
        return fTrue;
    }

    rc = pregn->_rc;
    regsc1.InitRc(prc, &rc);
    regsc2.Init(pregn, &rc);
    return _FCombine(MM_REGION_DIFFERENCE, &rc, &regsc2, &regsc1);
}

/***************************************************************************
    Compute *prc minus the given region and put the result in this region.
    If pregn is nil, this region is used.
***************************************************************************/
bool Region::FDiffFromRc(RC *prc, PRegion pregn)
{
    AssertThis(0);
    AssertVarMem(prc);
    AssertNilOrPo(pregn, 0);
    RegionScanner regsc1;
    RegionScanner regsc2;
    RC rc;

    if (pvNil == pregn)
        pregn = this;

    if (!rc.FIntersect(prc, &pregn->_rc))
    {
        SetRc(prc);
        return fTrue;
    }

    if (pvNil == pregn->_pglxp && pregn->_rc.FContains(prc))
    {
        // the region is rectangle and contains *prc, so the diff is empty
        rc.Zero();
        SetRc(&rc);
        return fTrue;
    }

    regsc1.InitRc(prc, prc);
    regsc2.Init(pregn, prc);
    return _FCombine(MM_REGION_DIFFERENCE, prc, &regsc1, &regsc2);
}


bool Region::_FCombine(MMRegionOperation operation, RC *bounds, PRegionScanner first, PRegionScanner second)
{
    RegionBuilder builder;
    if (!builder.FInit(bounds))
        return fFalse;

    const MMRegionBandSink sink{
        &builder,
        [](void *context, MMI32 height, MMI32 maxEdges) -> int {
            return static_cast<RegionBuilder *>(context)->FStartRow(height, maxEdges);
        },
        [](void *context, MMI32 x) { static_cast<RegionBuilder *>(context)->AddXp(x); },
        [](void *context) { static_cast<RegionBuilder *>(context)->EndRow(); }};
    if (!mm_region_combine(operation, &first->_scan, &second->_scan, bounds->Dyp(), &sink))
        return fFalse;

    ReleasePpo(&_pglxp);
    ReleasePpo(&_nativeCache);
    _pglxp = builder.PglxpFree(&_rc, &_dxp);
    AssertThis(0);
    return fTrue;
}


#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Region.
***************************************************************************/
void Region::AssertValid(LegacyUlong grf)
{
    Region_PAR::AssertValid(0);
    AssertNilOrPo(_pglxp, 0);
    Assert(_dxp <= 0, "bad _dxp");
    Assert(_dxp == 0 || _pglxp != pvNil, "_dxp should be zero");
    // REVIEW shonk: Region::AssertValid: fill this in
}

/***************************************************************************
    Mark memory for the Region.
***************************************************************************/
void Region::MarkMem(void)
{
    AssertValid(0);
    Region_PAR::MarkMem();
    MarkMemObj(_pglxp);
    MarkMemObj(_nativeCache);
}
#endif // DEBUG
