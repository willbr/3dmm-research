#ifndef MARCUS_CAPTION_RASTER_H
#define MARCUS_CAPTION_RASTER_H

#include "caption_runs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusCaptionRasterCache
{
    struct MarcusCaptionRasterEntry **entries;
    size_t count, capacity, bytes;
} MarcusCaptionRasterCache;

enum { MARCUS_CAPTION_RASTER_OK, MARCUS_CAPTION_RASTER_METRICS,
       MARCUS_CAPTION_RASTER_SOURCE, MARCUS_CAPTION_RASTER_MEMORY };

void marcus_caption_raster_clear(MarcusCaptionRasterCache *);
/* Start with a zeroed cache. Scale exceeds one and the budget is fixed for the
 * cache's lifetime. Success lends the mask planes until the next cache mutation.
 * Failure leaves the existing cache and output unchanged. Present source planes
 * contain pitch times height bytes. */
int marcus_caption_raster_get(MarcusCaptionRasterCache *, const MarcusCaptionRun *, int scale,
                              size_t budget, const MarcusCaptionFonts *, MarcusCaptionRaster *);

#ifdef __cplusplus
}
#endif
#endif
