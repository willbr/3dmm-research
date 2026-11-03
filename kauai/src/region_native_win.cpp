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

    crcMac = _pglxp == pvNil ? 1 : _pglxp->IvMac() / 2;
    if (!FAllocPv((void **)&prd, size(RGNDATAHEADER) + LwMul(crcMac, size(RECT)), fmemNil, mprNormal))
    {
        return hNil;
    }

    prcs = (SystemRectangle *)(prd + 1);
    regsc.Init(this, &_rc);
    crc = 0;
    if (!_rc.FEmpty())
    {
        yp = _rc.ypTop;
        for (;;)
        {
            dyp = regsc.DypCur();
            while (regsc.XpCur() < klwMax)
            {
                prcs->left = regsc.XpCur() + _rc.xpLeft;
                prcs->right = regsc.XpFetch() + _rc.xpLeft;
                regsc.XpFetch();
                prcs->top = yp;
                prcs->bottom = yp + dyp;
                prcs++;
                crc++;
            }
            if ((yp += dyp) >= _rc.ypBottom)
                break;
            regsc.ScanNext(dyp);
        }
    }
    AssertIn(crc, 0, crcMac + 1);
    prd->dwSize = size(RGNDATAHEADER);
    prd->iType = RDH_RECTANGLES;
    prd->nCount = crc;
    prd->nRgnSize = 0;
    prd->rcBound.left = _rc.xpLeft;
    prd->rcBound.top = _rc.ypTop;
    prd->rcBound.right = _rc.xpRight;
    prd->rcBound.bottom = _rc.ypBottom;

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
