#ifndef KAUAI_MASKED_REGION_H
#define KAUAI_MASKED_REGION_H

#include "mm/masked_bitmap.h"
#include "region.h"

struct MaskedRegionSpans
{
    PRegion region;
    RegionScanner scanner;
    MMI32 left;

    explicit MaskedRegionSpans(PRegion value) : region(value), left(0)
    {
    }

    MMMaskedSpans Services()
    {
        return {this,
            [](void *context, const MMMaskedRect *bounds) {
                auto &state = *static_cast<MaskedRegionSpans *>(context);
                RC clip(bounds->left, bounds->top, bounds->right, bounds->bottom);
                state.left = bounds->left;
                state.scanner.Init(state.region, &clip);
            },
            [](void *context, MMI32 *left, MMI32 *right) -> int {
                auto &state = *static_cast<MaskedRegionSpans *>(context);
                if (state.scanner.XpCur() == klwMax)
                    return 0;
                *left = state.left + state.scanner.XpCur();
                *right = state.left + state.scanner.XpFetch();
                state.scanner.XpFetch();
                return 1;
            },
            [](void *context, MMI32 rows) { static_cast<MaskedRegionSpans *>(context)->scanner.ScanNext(rows); },
            [](void *context) -> MMI32 { return static_cast<MaskedRegionSpans *>(context)->scanner.DypCur(); }};
    }
};

#endif
