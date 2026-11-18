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
#include "mm/region.h"

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

    MMRegion _region{};
    BASE *_nativeCache = nullptr;
    PT _dptRgn{0, 0}; // pending translation of the optional runtime cache

    Region(void)
    {
    }

    bool _FApply(MMRegionOperation operation, PRegion first, PRegion second);
    bool _FApplyRc(MMRegionOperation operation, RC *rect, PRegion other, bool subtractFromRect = false);
    bool _FResult(MMRegionResult result);

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
    MMRegionCursor _cursor{};

  public:
    RegionScanner(void);
    ~RegionScanner(void);
    void Free(void);

    void Init(PRegion pregn, RC *prcRel);
    void InitRc(RC *prc, RC *prcRel);
    void ScanNext(LegacyLong rows)
    {
        mm_region_scan_advance(&_cursor.scan, rows);
    }
    LegacyLong XpFetch()
    {
        return mm_region_scan_fetch(&_cursor.scan);
    }
    bool FOn() const
    {
        return _cursor.scan.opens != 0;
    }
    LegacyLong XpCur() const
    {
        return _cursor.scan.x;
    }
    LegacyLong DypCur() const
    {
        return _cursor.scan.rows;
    }
    LegacyLong CxpCur() const
    {
        return mm_region_scan_edge_count(&_cursor.scan);
    }
};

#endif //! REGION_H
