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

namespace
{
MMPixelRect PixelRect(const RC &rect)
{
    return {static_cast<MMI32>(rect.xpLeft), static_cast<MMI32>(rect.ypTop),
            static_cast<MMI32>(rect.xpRight), static_cast<MMI32>(rect.ypBottom)};
}

void CopyBounds(const MMRegion &region, RC *out)
{
    if (out)
    {
        const auto &bounds = region.bounds;
        out->Set(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }
}
} // namespace

RegionScanner::RegionScanner() = default;

RegionScanner::~RegionScanner()
{
    Free();
}

void RegionScanner::Free()
{
    mm_region_cursor_close(&_cursor);
}

void RegionScanner::Init(PRegion region, RC *clip)
{
    const auto bounds = PixelRect(*clip);
    mm_region_cursor_open(&_cursor, &region->_region, &bounds);
}

void RegionScanner::InitRc(RC *rect, RC *clip)
{
    MMRegion region{};
    region.bounds = PixelRect(*rect);
    const auto bounds = PixelRect(*clip);
    mm_region_cursor_open(&_cursor, &region, &bounds);
}

PRegion Region::PregnNew(RC *rect)
{
    auto *region = NewObj Region;
    if (region && rect)
        region->_region.bounds = PixelRect(*rect);
    return region;
}

Region::~Region()
{
    mm_region_set_rect(&_region, nullptr);
    ReleasePpo(&_nativeCache);
}

void Region::SetRc(RC *rect)
{
    if (rect)
    {
        const auto bounds = PixelRect(*rect);
        mm_region_set_rect(&_region, &bounds);
    }
    else
        mm_region_set_rect(&_region, nullptr);
    ReleasePpo(&_nativeCache);
}

void Region::Offset(LegacyLong dx, LegacyLong dy)
{
    mm_region_offset(&_region, dx, dy);
    _dptRgn.Offset(dx, dy);
}

bool Region::FEmpty(RC *rect)
{
    CopyBounds(_region, rect);
    return mm_region_empty(&_region);
}

bool Region::FIsRc(RC *rect)
{
    CopyBounds(_region, rect);
    return !_region.bands;
}

void Region::Scale(LegacyLong xNum, LegacyLong xDen, LegacyLong yNum, LegacyLong yDen)
{
    Assert(xDen > 0 && xNum >= 0 && yDen > 0 && yNum >= 0, "bad scaling");
    _FResult(mm_region_scale(&_region, xNum, xDen, yNum, yDen));
}

bool Region::FUnion(PRegion first, PRegion second)
{
    return _FApply(MM_REGION_UNION, first, second);
}

bool Region::FUnionRc(RC *rect, PRegion other)
{
    return _FApplyRc(MM_REGION_UNION, rect, other);
}

bool Region::FIntersect(PRegion first, PRegion second)
{
    return _FApply(MM_REGION_INTERSECTION, first, second);
}

bool Region::FIntersectRc(RC *rect, PRegion other)
{
    return _FApplyRc(MM_REGION_INTERSECTION, rect, other);
}

bool Region::FDiff(PRegion subtract, PRegion from)
{
    return _FApply(MM_REGION_DIFFERENCE, from, subtract);
}

bool Region::FDiffRc(RC *rect, PRegion from)
{
    return _FApplyRc(MM_REGION_DIFFERENCE, rect, from);
}

bool Region::FDiffFromRc(RC *rect, PRegion subtract)
{
    return _FApplyRc(MM_REGION_DIFFERENCE, rect, subtract, true);
}

bool Region::_FApply(MMRegionOperation operation, PRegion first, PRegion second)
{
    AssertThis(0);
    AssertNilOrPo(first, 0);
    AssertNilOrPo(second, 0);
    const auto allocator = Group::CollectionAllocator();
    return _FResult(mm_region_apply(&_region, operation, first ? &first->_region : nullptr,
                                   second ? &second->_region : nullptr, &allocator));
}

bool Region::_FApplyRc(MMRegionOperation operation, RC *rect, PRegion other, bool subtractFromRect)
{
    AssertThis(0);
    AssertVarMem(rect);
    AssertNilOrPo(other, 0);
    const auto allocator = Group::CollectionAllocator();
    const auto bounds = PixelRect(*rect);
    return _FResult(mm_region_apply_rect(&_region, operation, &bounds, other ? &other->_region : nullptr,
                                        subtractFromRect, &allocator));
}

bool Region::_FResult(MMRegionResult result)
{
    if (result == MM_REGION_UPDATED)
        ReleasePpo(&_nativeCache);
    return result != MM_REGION_FAILED;
}

#ifdef DEBUG
void Region::AssertValid(LegacyUlong)
{
    Region_PAR::AssertValid(0);
    Assert(_region.bias <= 0 && (!_region.bias || _region.bands), "bad region band bias");
}

void Region::MarkMem()
{
    AssertValid(0);
    Region_PAR::MarkMem();
    MarkPv(_region.bands);
    MarkPv(const_cast<void *>(mm_region_band_data(_region.bands)));
    MarkMemObj(_nativeCache);
}
#endif
