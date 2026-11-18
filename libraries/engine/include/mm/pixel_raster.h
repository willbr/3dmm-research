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

struct MMRegion;

typedef void (*MMPixelDrawSpan)(void *context, MMI32 y, MMI32 left, MMI32 right);
/* A visible rectangle is the intersection of a shape, surface and optional
 * clip. The span visitor consumes a visible rectangle in region band order. */
int mm_pixel_visible_rect(const MMPixelSurface *, const MMPixelRect *shape, const MMPixelRect *clip, MMPixelRect *);
void mm_pixel_visit_spans(const MMPixelRect *visible, const struct MMRegion *, MMPixelDrawSpan, void *context);

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
/* Unsigned floor(a * b / divisor) with nonzero divisor and a one-word result. */
MMU32 mm_pixel_muldiv(MMU32 a, MMU32 b, MMU32 divisor);
/* Round the edge crossing toward positive infinity. Requires y0 <= y < y1. */
MMI32 mm_pixel_edge_ceil(MMI32 x0, MMI32 y0, MMI32 x1, MMI32 y1, MMI32 y);
/* position is in [0, destination) and both extents are positive. */
MMI32 mm_pixel_sample(MMI32 position, MMI32 source, MMI32 destination, int near_size);
/* The caller supplies a row and an in-range pixel position. */
MMU32 mm_pixel_read(const unsigned char *row, MMI32 x, int depth);
void mm_pixel_write(unsigned char *row, MMI32 x, int depth, MMU32 value);
/* Set or clear an in-range span of 1-bit pixels, preserving adjacent bits. */
void mm_pixel_mask_span(unsigned char *row, MMI32 left, MMI32 right, int marked);
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
                  const MMPixelRect *target_rect, const MMPixelRect *clip, const struct MMRegion *, int options,
                  MMPixelOverlap);

/* Expand indices to native-order XRGB32. Unlisted indices leave pixels intact.
 * Source and target storage must be disjoint. */
int mm_pixel_expand_indexed(const MMPixelSurface *target, const MMPixelImage *source, const MMPixelRect *source_rect,
                            const MMPixelRect *target_rect, const MMPixelRect *clip, const struct MMRegion *);
/* Capture native-order XRGB32 as exact palette indices. A pixel missing from
 * the palette fails the entire operation before publication. Supports overlap. */
int mm_pixel_snapshot_indexed(const MMPixelImage *target, const MMPixelSurface *source,
                              const MMPixelRect *source_rect, const MMPixelRect *target_rect);

/* Move raw samples within the visible rectangle and region. Cells exposed by
 * the move and row padding retain their bytes. The legacy monochrome option
 * applies the full-byte pass before edge bits on leftward 1-bit moves. */
int mm_pixel_scroll(const MMPixelSurface *, const MMPixelRect *rectangle, MMI32 dx, MMI32 dy,
                    const MMPixelRect *clip, const struct MMRegion *, int legacy_monochrome);

#ifdef __cplusplus
}
#endif
#endif
