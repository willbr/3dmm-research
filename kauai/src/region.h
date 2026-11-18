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
#include "mm/region_scan.h"

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

    bool _FCombine(MMRegionOperation operation, RC *bounds, PRegionScanner first, PRegionScanner second);

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
    friend class Region;
    PDynamicArray _pglxpSrc;
    MMRegionScan _scan;

    void _InitCore(PDynamicArray bands, const RC &bounds, LegacyLong offset, const RC &clip);

  public:
    RegionScanner(void);
    ~RegionScanner(void);
    void Free(void);

    void Init(PRegion pregn, RC *prcRel);
    void InitRc(RC *prc, RC *prcRel);
    void ScanNext(LegacyLong rows)
    {
        mm_region_scan_advance(&_scan, rows);
    }
    LegacyLong XpFetch()
    {
        return mm_region_scan_fetch(&_scan);
    }
    bool FOn() const
    {
        return _scan.opens != 0;
    }
    LegacyLong XpCur() const
    {
        return _scan.x;
    }
    LegacyLong DypCur() const
    {
        return _scan.rows;
    }
    LegacyLong CxpCur() const
    {
        return mm_region_scan_edge_count(&_scan);
    }
};

#endif //! REGION_H
