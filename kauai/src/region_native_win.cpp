/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "frame.h"
ASSERTNAME

// Optional native cache. Core Region owns only its BASE lifetime, never HRGN.
class NativeRegionCache : public BASE
{
  public:
    HRGN handle = hNil;
    ~NativeRegionCache() override
    {
        if (handle)
            DeleteObject(handle);
    }
};

HRGN Region::HrgnCreate(void)
{
    RGNDATAHEADER *prd;
    SystemRectangle *prcs;
    RegionScanner regsc;
    long crcMac, crc;
    HRGN hrgn;
    long yp, dyp;

    const auto &bounds = _region.bounds;
    RC clip(bounds.left, bounds.top, bounds.right, bounds.bottom);
    crcMac = mm_region_rectangle_capacity(&_region);
    if (!FAllocPv((void **)&prd, size(RGNDATAHEADER) + LwMul(crcMac, size(RECT)), fmemNil, mprNormal))
    {
        return hNil;
    }

    prcs = (SystemRectangle *)(prd + 1);
    regsc.Init(this, &clip);
    crc = 0;
    if (!mm_region_empty(&_region))
    {
        yp = bounds.top;
        for (;;)
        {
            dyp = regsc.DypCur();
            while (regsc.XpCur() < klwMax)
            {
                prcs->left = regsc.XpCur() + bounds.left;
                prcs->right = regsc.XpFetch() + bounds.left;
                regsc.XpFetch();
                prcs->top = yp;
                prcs->bottom = yp + dyp;
                prcs++;
                crc++;
            }
            if ((yp += dyp) >= bounds.bottom)
                break;
            regsc.ScanNext(dyp);
        }
    }
    AssertIn(crc, 0, crcMac + 1);
    prd->dwSize = size(RGNDATAHEADER);
    prd->iType = RDH_RECTANGLES;
    prd->nCount = crc;
    prd->nRgnSize = 0;
    prd->rcBound.left = bounds.left;
    prd->rcBound.top = bounds.top;
    prd->rcBound.right = bounds.right;
    prd->rcBound.bottom = bounds.bottom;

    hrgn = ExtCreateRegion(pvNil, size(RGNDATAHEADER) + LwMul(crc, size(RECT)), (RGNDATA *)prd);
    FreePpv((void **)&prd);
    return hrgn;
}

HRGN Region::HrgnEnsure(void)
{
    if (_nativeCache)
    {
        auto *cache = static_cast<NativeRegionCache *>(_nativeCache);
        if (_dptRgn.xp || _dptRgn.yp)
        {
            OffsetRgn(cache->handle, _dptRgn.xp, _dptRgn.yp);
            _dptRgn.xp = _dptRgn.yp = 0;
        }
        return cache->handle;
    }
    auto *cache = NewObj NativeRegionCache;
    if (!cache)
        return hNil;
    cache->handle = HrgnCreate();
    if (!cache->handle)
    {
        ReleasePpo(&cache);
        return hNil;
    }
    _nativeCache = cache;
    _dptRgn.xp = _dptRgn.yp = 0;
    return cache->handle;
}
