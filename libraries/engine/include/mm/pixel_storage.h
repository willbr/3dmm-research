#ifndef MM_ENGINE_PIXEL_STORAGE_H
#define MM_ENGINE_PIXEL_STORAGE_H
#include "pixel_raster.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMPixelBuffer
{
    MMPixelSurface surface;
    size_t bytes;
} MMPixelBuffer;

/* Compute signed DIB-compatible pitches and byte counts without wide C
 * integers. Row alignment is 4 or 8 bytes. Either zero extent permits a
 * zero-byte layout, while owned surfaces require two positive extents. */
int mm_pixel_layout(MMI32 width, MMI32 height, MMI32 depth, MMI32 alignment, MMI32 *pitch, size_t *bytes);
/* Initialize a zeroed or previously owned buffer. Replacement is zeroed and
 * allocation or layout failure leaves the existing owner intact. */
int mm_pixel_buffer_init(MMPixelBuffer *, MMI32 width, MMI32 height, MMI32 depth, MMI32 alignment);
void mm_pixel_buffer_reset(MMPixelBuffer *);

#ifdef __cplusplus
}
#endif
#endif
