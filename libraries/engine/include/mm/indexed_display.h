#ifndef MM_ENGINE_INDEXED_DISPLAY_H
#define MM_ENGINE_INDEXED_DISPLAY_H
#include "pixel_raster.h"
#include "read_stream.h"
#ifdef __cplusplus
extern "C" {
#endif

enum
{
    MM_DOUBLE_UNAVAILABLE,
    MM_DOUBLE_CLIPPED,
    MM_DOUBLE_VERTICAL,
    MM_DOUBLE_BOTH
};

/* Choose the raw-index fast path and compute its source crop and target clip.
 * Only the two drawable modes publish rectangles. Drawing still uses the
 * original target origin after the source is cropped. */
int mm_indexed_double_plan(const MMPixelRect *source_bounds, const MMPixelRect *source, const MMPixelRect *target,
                           const MMPixelRect *clip, MMPixelRect *source_crop, MMPixelRect *target_clip);
/* Duplicate each source row and optionally each column. The source rectangle
 * is in source storage. A legacy paired-row write can extend past the bottom
 * clip when the region continues there, bounded by the target's storage. */
int mm_indexed_double_draw(const MMPixelSurface *target, const MMPixelSurface *source, const MMPixelRect *source_rect,
                           MMI32 x, MMI32 y, int double_width, const MMPixelRect *clip, const struct MMRegion *);

enum
{
    MM_DISPLAY_PALETTE_IDENTITY = 1,
    MM_DISPLAY_PALETTE_INIT_ANIMATION = 2,
    MM_DISPLAY_PALETTE_ANIMATE = 4
};
enum
{
    MM_DISPLAY_PALETTE_INVALID,
    MM_DISPLAY_PALETTE_UNCHANGED,
    MM_DISPLAY_PALETTE_CHANGED
};

/* The current palette holds 256 BGR0 entries and its visible count. Requests
 * may alias current entries. Animation limits updates to the current count. */
int mm_display_palette_set(unsigned char current[1024], size_t *count, const unsigned char *requested,
                           size_t requested_count, MMU32 flags);
/* Resolve a complete movie palette in place using Studio's identity colours. */
void mm_display_palette_resolve_identity(unsigned char colours[1024]);
/* Copy an authored BGRX range over the base palette, including the reserved
 * bytes. Invalid ranges leave the destination alone. */
int mm_display_palette_overlay(unsigned char *base, size_t base_count, size_t first,
                               const unsigned char *colours, size_t count);
/* Reverse the four bytes of each legacy colour word in place. */
void mm_palette_swap_word_order(unsigned char *colours, size_t count);
/* Read 256 native BGR0 colour words from a decoded GLCR. */
int mm_palette_read_256(const MMReadStream *, unsigned char colours[1024]);

#ifdef __cplusplus
}
#endif
#endif
