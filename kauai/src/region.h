/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Region stuff.

***************************************************************************/
#ifndef REGION_H
#define REGION_H

typedef class RegionScanner *PRegionScanner;

/***************************************************************************
    The region class.
***************************************************************************/
typedef class Region *PRegion;
#define Region_PAR BASE
#define kclsRegion 'REGN'
class Region : public Region_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    friend class RegionScanner;

    // The _pglxp contains a bunch of rows.  Each row consists of:
    // dyp, xp0, xp1, ..., klwMax.
    // The dyp is the height of the strip in pixels, the xp's are boundary points
    // The xp's are relative to _rc.xpLeft + _dxp. _pglxp is nil iff the region
    // is strictly rectangular.
    RC _rc;     // bounding rectangle
    LegacyLong _dxp;  // additional offset for xp values
    PDynamicArray _pglxp; // region data - see above
    BASE *_nativeCache; // optional runtime-owned resource; no native handle in geometry
    PT _dptRgn; // pending translation of the optional runtime cache

    Region(void)
    {
    }

    bool _FUnionCore(RC *prc, PRegionScanner pregsc1, PRegionScanner pregsc2);
    bool _FIntersectCore(RC *prc, PRegionScanner pregsc1, PRegionScanner pregsc2);
    bool _FDiffCore(RC *prc, PRegionScanner pregsc1, PRegionScanner pregsc2);

  public:
    static PRegion PregnNew(RC *prc = pvNil);
    ~Region(void);

    void SetRc(RC *prc = pvNil);
    void Offset(LegacyLong xp, LegacyLong yp);
    bool FEmpty(RC *prc = pvNil);
    bool FIsRc(RC *prc = pvNil);
    void Scale(LegacyLong lwNumX, LegacyLong lwDenX, LegacyLong lwNumY, LegacyLong lwDenY);

    bool FUnion(PRegion pregn1, PRegion pregn2 = pvNil);
    bool FUnionRc(RC *prc, PRegion pregn2 = pvNil);
    bool FIntersect(PRegion pregn1, PRegion pregn2 = pvNil);
    bool FIntersectRc(RC *prc, PRegion pregn = pvNil);
    bool FDiff(PRegion pregn1, PRegion pregn2 = pvNil);
    bool FDiffRc(RC *prc, PRegion pregn = pvNil);
    bool FDiffFromRc(RC *prc, PRegion pregn = pvNil);

    // Host-only operations, implemented by the native GUI adapter.
    CoreNative::RegionHandle HrgnCreate(void);
    CoreNative::RegionHandle HrgnEnsure(void);
};

/***************************************************************************
    Region scanner class.
***************************************************************************/
#define RegionScanner_PAR BASE
#define kclsRegionScanner 'rgsc'
class RegionScanner : public RegionScanner_PAR
{
    RTCLASS_DEC

  protected:
    PDynamicArray _pglxpSrc;    // the list of points
    LegacyLong *_pxpLimSrc; // the end of the list
    LegacyLong *_pxpLimCur; // the end of the current row

    LegacyLong _xpMinRow;   // the first xp value of the active part of the current row
    LegacyLong *_pxpMinRow; // the beginning of the active part of the currrent row
    LegacyLong *_pxpLimRow; // the end of the active part of the current row
    LegacyLong *_pxp;       // the current postion in the current row

    LegacyLong _dxp;     // this gets added to all source xp values
    LegacyLong _xpRight; // the right edge of the active area - left edge is 0

    // current state
    bool _fOn;    // whether the current xp is a transition to on or off
    LegacyLong _xp;     // the current xp
    LegacyLong _dyp;    // the remaining height that this scan is effective for
    LegacyLong _dypTot; // the remaining total height of the active area

    // When scanning rectangles, _pglxpSrc will be nil and _pxpSrc et al will
    // point into this.
    LegacyLong _rgxpRect[4];

    void _InitCore(PDynamicArray pglxp, RC *prc, RC *prcRel);
    void _ScanNextCore(void);

  public:
    RegionScanner(void);
    ~RegionScanner(void);
    void Free(void);

    void Init(PRegion pregn, RC *prcRel);
    void InitRc(RC *prc, RC *prcRel);
    void ScanNext(LegacyLong dyp)
    {
        _dypTot -= dyp;
        if ((_dyp -= dyp) <= 0)
            _ScanNextCore();
        _pxp = _pxpMinRow;
        _xp = _xpMinRow;
        _fOn = fTrue;
    }
    LegacyLong XpFetch(void)
    {
        if (_pxp < _pxpLimRow - 1)
        {
            _xp = _dxp + *_pxp++;
            _fOn = !_fOn;
        }
        else if (_pxp < _pxpLimRow)
        {
            AssertH(_fOn);
            _xp = LwMin(_xpRight, _dxp + *_pxp++);
            _fOn = !_fOn;
        }
        else
        {
            _xp = klwMax;
            _fOn = fTrue;
        }
        return _xp;
    }

    bool FOn(void)
    {
        return _fOn;
    }
    LegacyLong XpCur(void)
    {
        return _xp;
    }
    LegacyLong DypCur(void)
    {
        return _dyp;
    }
    LegacyLong CxpCur(void)
    {
        return _pxpLimRow - _pxpMinRow + 1;
    }
};

#endif //! REGION_H
