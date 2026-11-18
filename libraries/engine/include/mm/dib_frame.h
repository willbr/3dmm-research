#ifndef MM_ENGINE_DIB_FRAME_H
#define MM_ENGINE_DIB_FRAME_H
#include "pixel_raster.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMDibInfo
{
    MMI32 width, height, depth, pitch;
    int bottom_up;
    size_t bytes;
} MMDibInfo;

/* Reads the 40-byte BI_RGB header and returns the full encoded extent. */
int mm_dib_info(const void *data, size_t available, MMDibInfo *);
/* Decode a complete 24/32-bit DIB to a disjoint top-down BGRX32 surface of
 * the same dimensions. Zeroes X for 24-bit input and preserves X for 32-bit. */
int mm_dib_decode(const void *data, size_t bytes, const MMPixelSurface *target);
/* Stretch a decoded BGRX32 frame. Input depth selects the legacy 24- or
 * 32-bit sampling rule. With no overlap service the caller supplies disjoint
 * buffers. With a service, overlapping buffers are rejected. */
int mm_dib_draw(const MMPixelSurface *target, const MMPixelSurface *frame, size_t frame_bytes, int input_depth,
                const MMPixelRect *target_rect, const MMPixelRect *clip, const MMPixelSpans *, MMPixelOverlap);

#ifdef __cplusplus
}
#endif
#endif
