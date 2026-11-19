#ifndef MM_CAPTION_RENDER_H
#define MM_CAPTION_RENDER_H
#include "pixel_raster.h"
#include "preview_render.h"
#include "text_box_runtime.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_CAPTION_WIDTH = 544, MM_CAPTION_HEIGHT = 306, MM_CAPTION_BORDER = 5, MM_CAPTION_INDENT = 14 };

/* The stack uses one-based row indices. Zero marks a missing neighbour.
 * Observe all authored rows at each seek or frame entry. Newly visible rows
 * append in authored order and visible survivors keep their current order. */
typedef struct MMCaptionRow
{
    MMI32 first, end, scroll;
    size_t previous, next;
    int visible;
} MMCaptionRow;
typedef struct MMCaptionStack { size_t first, last; } MMCaptionStack;
void mm_caption_observe(MMCaptionStack *, MMCaptionRow *, size_t count, MMI32 frame);
MMI32 mm_caption_scroll_overflow(MMI32 layout_height, MMI32 top, MMI32 bottom);

typedef struct MMCaptionCanvas
{
    unsigned char *rgba;
    size_t stride;
    MMPixelRect clip;
    float scale_x, scale_y;
} MMCaptionCanvas;
typedef struct MMCaptionMask
{
    MMI32 left, top, width, height, pitch;
} MMCaptionMask;
typedef struct MMCaptionGlyph
{
    MMCaptionMask mask;
    const unsigned char *ink, *paper;
    MMI32 x, y, ascent, raster_ascent;
    float density;
    int supersampled;
    MMU32 foreground, background;
} MMCaptionGlyph;

/* Surface and mask storage are borrowed. The RGBA surfaces are tightly
 * packed. Mask planes contain pitch * height bytes when present. */
void mm_caption_layer_size(MMU32 width, MMU32 height, MMU32 *layer_width, MMU32 *layer_height);
int mm_caption_viewport(MMPixelRect *, MMU32 width, MMU32 height, float aspect);
int mm_caption_canvas(MMCaptionCanvas *, unsigned char *, size_t bytes, MMU32 width, MMU32 height, float aspect);
int mm_caption_box_visible(const MMTextBoxBounds *);
void mm_caption_box(MMCaptionCanvas *, const MMTextBoxBounds *, MMU32 colour);
int mm_caption_raster_scale(const MMCaptionCanvas *, MMI32 font_pixels);
void mm_caption_glyph(MMCaptionCanvas *, const MMTextBoxBounds *, MMI32 scroll, const MMCaptionGlyph *);
void mm_caption_mask_half_size(const MMCaptionMask *, MMCaptionMask *);
void mm_caption_mask_half(const MMCaptionMask *, const unsigned char *, const MMCaptionMask *, unsigned char *);
void mm_caption_mask_expand(const MMCaptionMask *, const unsigned char *, const MMCaptionMask *, unsigned char *,
                            int scale);

/* Project the caption plane into a camera view. Depth is optional and
 * ignored unless it supplies exactly one camera-forward distance per pixel. */
void mm_caption_quad(const unsigned char *texture, size_t texture_bytes, MMU32 tw, MMU32 th,
                     const MMPreviewProjection *camera, const MMPreviewProjection *view,
                     unsigned char *rgba, size_t rgba_bytes, MMU32 width, MMU32 height,
                     const float *depth, size_t depth_count, MMU32 *covered);

#ifdef __cplusplus
}
#endif
#endif
