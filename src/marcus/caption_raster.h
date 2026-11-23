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

typedef struct MarcusCaptionSample
{
    unsigned char *rgba;
    size_t capacity;
} MarcusCaptionSample;

typedef struct MarcusCaptionSampleMask
{
    const unsigned char *coverage;
    size_t bytes;
    int width, height, pitch;
} MarcusCaptionSampleMask;

typedef int MarcusCaptionSampleRead(void *, const MMCaptionStyle *, const char *, size_t, MarcusCaptionSampleMask *);

/* Start with a zeroed sample. The reader lends separate mask storage through
 * the call. Success lends RGBA pixels until the next successful sample or
 * clear. Failure preserves the sample and output view. */
int marcus_caption_sample(MarcusCaptionSample *, const char *family, void *, MarcusCaptionSampleRead *, MarcusImage *);
void marcus_caption_sample_clear(MarcusCaptionSample *);

#ifdef __cplusplus
}
#endif
#endif
