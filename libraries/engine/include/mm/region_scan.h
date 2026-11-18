#ifndef MM_ENGINE_REGION_SCAN_H
#define MM_ENGINE_REGION_SCAN_H
#include "pixel_raster.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Matches Kauai's 32-bit scan-band words, including the Windows long ABI. */
#ifdef _WIN32
typedef long MMRegionWord;
#else
typedef MMI32 MMRegionWord;
#endif

#define MM_REGION_END 2147483647L
#define MM_REGION_END_HEIGHT 32767

typedef struct MMRegionScan
{
    const MMRegionWord *words;
    size_t count, next_band, first_edge, end_edge, edge;
    MMRegionWord rectangle[4];
    MMI32 bias, width, rows_left, rows;
    MMI32 first, x;
    int opens;
} MMRegionScan;

/* A borrowed band consists of a positive height, pairs of ascending x edges,
 * and MM_REGION_END. An empty band has only the height and sentinel. Edges
 * are relative to bounds.left + offset. With NULL bands the bounds form a
 * rectangle. Keep bands and the initialized scanner in place while scanning.
 * Returned edges are relative to clip.left. At the end of a band x is
 * MM_REGION_END and opens is true. */
void mm_region_scan_init(MMRegionScan *, const MMPixelRect *bounds, MMI32 offset,
                         const MMRegionWord *bands, size_t count, const MMPixelRect *clip);
void mm_region_scan_advance(MMRegionScan *, MMI32 rows);
MMI32 mm_region_scan_fetch(MMRegionScan *);
MMI32 mm_region_scan_edge_count(const MMRegionScan *);
/* Read the next pair of edges relative to the current clip. */
int mm_region_scan_pair(MMRegionScan *, MMI32 *left, MMI32 *right);

typedef enum MMRegionOperation
{
    /* Each bit selects one combination of left and right membership. */
    MM_REGION_UNION = 14,
    MM_REGION_INTERSECTION = 8,
    MM_REGION_DIFFERENCE = 2
} MMRegionOperation;

typedef struct MMRegionBandSink
{
    void *context;
    int (*start)(void *, MMI32 rows, MMI32 max_edges);
    void (*edge)(void *, MMI32 x);
    void (*end)(void *);
} MMRegionBandSink;

/* Two distinct scanners start at the same clip. DIFFERENCE subtracts right from left.
 * The sink receives complete bands up to rows, in top-to-bottom order. */
int mm_region_combine(MMRegionOperation, MMRegionScan *left, MMRegionScan *right,
                      MMI32 rows, const MMRegionBandSink *);

#ifdef __cplusplus
}
#endif
#endif
