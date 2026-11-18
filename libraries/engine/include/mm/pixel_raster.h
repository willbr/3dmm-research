#ifndef MM_ENGINE_PIXEL_RASTER_H
#define MM_ENGINE_PIXEL_RASTER_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Top-down DIB storage with byte pitches and MSB-first indexed pixels. */
typedef struct MMPixelSurface
{
    void *pixels;
    MMI32 width, height, pitch, depth;
} MMPixelSurface;

int mm_pixel_surface_valid(const MMPixelSurface *);
/* The caller supplies a row and an in-range pixel position. */
MMU32 mm_pixel_read(const unsigned char *row, MMI32 x, int depth);
void mm_pixel_write(unsigned char *row, MMI32 x, int depth, MMU32 value);
/* Palettes contain blue, green, red and reserved bytes. Ties take the first index. */
MMU32 mm_pixel_rgb(const unsigned char *colour);
MMU32 mm_pixel_rgb555(MMU32 rgb);
unsigned char mm_pixel_nearest(const unsigned char *palette, MMI32 count, MMU32 rgb);
void mm_pixel_translate8(const unsigned char *source, MMI32 source_count, const unsigned char *target,
                         MMI32 target_count, unsigned char indices[256]);

#ifdef __cplusplus
}
#endif
#endif
