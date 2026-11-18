#ifndef MM_ENGINE_REGION_H
#define MM_ENGINE_REGION_H
#include "collection_storage.h"
#include "region_scan.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMRegionBands MMRegionBands;

/* Zero-initialize owning records before use. Work with them on one thread.
 * Allocator contexts must outlive the regions and cursors that retain them. */
typedef struct MMRegion
{
    MMPixelRect bounds;
    MMRegionBands *bands;
    MMI32 bias;
} MMRegion;

typedef struct MMRegionCursor
{
    MMRegionScan scan;
    MMRegionBands *bands;
} MMRegionCursor;

typedef enum MMRegionResult
{
    MM_REGION_FAILED,
    MM_REGION_UNCHANGED,
    MM_REGION_UPDATED
} MMRegionResult;

void mm_region_set_rect(MMRegion *, const MMPixelRect *);
void mm_region_offset(MMRegion *, MMI32 dx, MMI32 dy);
int mm_region_empty(const MMRegion *);
size_t mm_region_rectangle_capacity(const MMRegion *);
const void *mm_region_band_data(const MMRegionBands *);

/* Open replaces any previous cursor. Keep the open cursor at a fixed address.
 * It retains its bands even if the original region is replaced or scaled. */
void mm_region_cursor_open(MMRegionCursor *, const MMRegion *, const MMPixelRect *clip);
void mm_region_cursor_close(MMRegionCursor *);

/* A NULL right operand means the destination for union and intersection.
 * Difference takes left minus right. A NULL left means the destination.
 * The destination may alias an operand. Failed operations leave it intact.
 * A NULL allocator uses the C heap. */
MMRegionResult mm_region_apply(MMRegion *, MMRegionOperation, const MMRegion *left,
                                const MMRegion *right, const MMStorageAllocator *);
MMRegionResult mm_region_apply_rect(MMRegion *, MMRegionOperation, const MMPixelRect *,
                                     const MMRegion *, int subtract_from_rect, const MMStorageAllocator *);
MMRegionResult mm_region_scale(MMRegion *, MMI32 x_num, MMI32 x_den, MMI32 y_num, MMI32 y_den);

#ifdef __cplusplus
}
#endif
#endif
