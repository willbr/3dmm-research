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
    const auto &bounds = _region.bounds;
    const auto capacity = static_cast<LegacyLong>(mm_region_rectangle_capacity(&_region));
    if (!FAllocPv((void **)&prd, size(RGNDATAHEADER) + LwMul(capacity, size(RECT)), fmemNil, mprNormal))
        return hNil;

    auto first = reinterpret_cast<SystemRectangle *>(prd + 1);
    auto next = first;
    const auto collect = [](void *context, const MMPixelRect *rect) -> int {
        auto &out = *static_cast<SystemRectangle **>(context);
        *out++ = {rect->left, rect->top, rect->right, rect->bottom};
        return true;
    };
    mm_region_visit_rectangles(&_region, &bounds, collect, &next);
    const auto crc = static_cast<LegacyLong>(next - first);
    AssertIn(crc, 0, capacity + 1);
    prd->dwSize = size(RGNDATAHEADER);
    prd->iType = RDH_RECTANGLES;
    prd->nCount = crc;
    prd->nRgnSize = 0;
    prd->rcBound.left = bounds.left;
    prd->rcBound.top = bounds.top;
    prd->rcBound.right = bounds.right;
    prd->rcBound.bottom = bounds.bottom;

    auto hrgn = ExtCreateRegion(pvNil, size(RGNDATAHEADER) + LwMul(crc, size(RECT)), (RGNDATA *)prd);
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
