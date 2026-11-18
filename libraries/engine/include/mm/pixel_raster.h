#ifndef MM_ENGINE_PIXEL_RASTER_H
#define MM_ENGINE_PIXEL_RASTER_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Top-down DIB storage with byte pitches and MSB-first indexed pixels. */
typedef struct MMPixelSurface
{
    void *pixels;
    MMI32 width, height, pitch, depth;
} MMPixelSurface;

typedef struct MMPixelRect
{
    MMI32 left, top, right, bottom;
} MMPixelRect;

/* begin receives the visible rectangle. next returns a row's sorted, disjoint
 * spans in absolute destination coordinates within those bounds. */
typedef struct MMPixelSpans
{
    void *context;
    void (*begin)(void *, const MMPixelRect *);
    int (*next)(void *, MMI32 *left, MMI32 *right);
    void (*advance)(void *);
} MMPixelSpans;

typedef struct MMPixelImage
{
    MMPixelSurface surface;
    const unsigned char *palette;
    MMI32 colours;
} MMPixelImage;

typedef int (*MMPixelOverlap)(const void *source, size_t source_bytes, const void *target, size_t target_bytes);

enum
{
    MM_PIXEL_COPY_NEAR_SIZE = 1,
    MM_PIXEL_COPY_ALLOW_EMPTY = 2
};

int mm_pixel_surface_valid(const MMPixelSurface *);
int mm_pixel_image_valid(const MMPixelImage *);
/* Zero extents are valid. Reversed or unrepresentable extents fail. */
int mm_pixel_rect_extent(const MMPixelRect *, MMI32 *width, MMI32 *height);
void mm_pixel_rect_intersect(MMPixelRect *bounds, const MMPixelRect *clip);
/* position is in [0, destination) and both extents are positive. */
MMI32 mm_pixel_sample(MMI32 position, MMI32 source, MMI32 destination, int near);
/* The caller supplies a row and an in-range pixel position. */
MMU32 mm_pixel_read(const unsigned char *row, MMI32 x, int depth);
void mm_pixel_write(unsigned char *row, MMI32 x, int depth, MMU32 value);
/* Palettes contain blue, green, red and reserved bytes. Ties take the first index. */
MMU32 mm_pixel_rgb(const unsigned char *colour);
MMU32 mm_pixel_rgb555(MMU32 rgb);
unsigned char mm_pixel_nearest(const unsigned char *palette, MMI32 count, MMU32 rgb);
void mm_pixel_translate8(const unsigned char *source, MMI32 source_count, const unsigned char *target,
                         MMI32 target_count, unsigned char indices[256]);

/* Indexed palettes use BGR plus a reserved byte, with implicit black and white
 * when a 1-bit image has no palette. Unlisted source indices are black.
 * An omitted overlap service conservatively snapshots the source. */
int mm_pixel_copy(const MMPixelImage *target, const MMPixelImage *source, const MMPixelRect *source_rect,
                  const MMPixelRect *target_rect, const MMPixelRect *clip, const MMPixelSpans *, int options,
                  MMPixelOverlap);

#ifdef __cplusplus
}
#endif
#endif
