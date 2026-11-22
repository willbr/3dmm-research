#ifndef MM_CAPTION_RENDER_H
#define MM_CAPTION_RENDER_H
#include "caption_layout.h"
#include "collection_storage.h"
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
    MMTextBoxBounds bounds;
    MMU32 chunk;
    int story;
    MMI32 overflow;
} MMCaptionRow;
typedef struct MMCaptionStack { size_t first, last; } MMCaptionStack;
void mm_caption_observe(MMCaptionStack *, MMCaptionRow *, size_t count, MMI32 frame);
int mm_caption_hit(const MMCaptionStack *, const MMCaptionRow *, MMI32 x, MMI32 y, const MMU32 *hidden,
                   MMU32 *chunk);
/* Visit from back to front. A draft (row zero) replaces the first visible
 * hidden box, or follows the stack. A failed callback ends the traversal. */
int mm_caption_draw_stack(const MMCaptionStack *, const MMCaptionRow *, const MMU32 *hidden, int have_draft,
                          MMI32 draft_scroll, void *context, int (*draw)(void *, size_t row, MMI32 scroll));

/* Prepare scrolling for all credits beginning on this frame. The host
 * supplies each layout height by one-based row. Step follows a successful
 * prepare for this frame and visits the same rows. */
int mm_caption_measure_scroll(MMCaptionRow *, size_t count, MMI32 frame, int *needs_scroll,
                              void *context, int (*layout)(void *, size_t row, MMI32 *height));
void mm_caption_step_scroll(MMCaptionRow *, size_t count, MMI32 frame);
void mm_caption_reset_scroll(MMCaptionRow *, size_t count);
MMI32 mm_caption_scroll_overflow(MMI32 layout_height, MMI32 top, MMI32 bottom);

typedef struct MMCaptionRowInput
{
    MMI32 first, end;
    MMTextBoxBounds bounds;
    MMU32 chunk;
    int story;
} MMCaptionRowInput;

typedef struct MMCaptionPresentation
{
    MMCollectionStorage storage;
    MMU32 count;
    MMCaptionStack stack;
    MMI32 draft_scroll;
} MMCaptionPresentation;

typedef void MMCaptionReadRow(void *, size_t index, MMCaptionRowInput *);
int mm_caption_presentation_init(MMCaptionPresentation *, const MMStorageAllocator *);
void mm_caption_presentation_dispose(MMCaptionPresentation *);
void mm_caption_presentation_clear(MMCaptionPresentation *);
/* Install authored rows in source order. The zero-based reader supplies every
 * row and does not reenter the presentation. Failure preserves the old rows. */
int mm_caption_presentation_load(MMCaptionPresentation *, size_t count, MMCaptionReadRow *, void *);
void mm_caption_presentation_observe(MMCaptionPresentation *, MMI32 frame);
int mm_caption_presentation_hit(const MMCaptionPresentation *, MMI32 x, MMI32 y, const MMU32 *hidden, MMU32 *chunk);
int mm_caption_presentation_measure(MMCaptionPresentation *, MMI32 frame, int *needs_scroll,
                                     void *, int (*layout)(void *, size_t row, MMI32 *height));
void mm_caption_presentation_scroll(MMCaptionPresentation *, MMI32 frame);
void mm_caption_presentation_rewind(MMCaptionPresentation *);
void mm_caption_presentation_draft(MMCaptionPresentation *, MMI32 prior_scroll, MMI32 height, MMI32 top, MMI32 bottom);

typedef struct MMCaptionCaretStop
{
    MMI32 offset;
    float x, y, height;
} MMCaptionCaretStop;

typedef struct MMCaptionCaretBuilder
{
    const MMI32 *offsets;
    MMI32 length, left, top, last_offset;
    float last_y;
} MMCaptionCaretBuilder;

typedef struct MMCaptionCaretServices
{
    void *context;
    int (*advance)(void *, const unsigned short *, MMI32 length, MMI32 *pixels);
    int (*stop)(void *, const MMCaptionCaretStop *, int replace_last);
} MMCaptionCaretServices;

/* Write length+1 editor UTF-8 offsets from the layout's UTF-16 text. CRLF
 * uses one byte, and the interior of a CRLF or surrogate pair uses -1. */
void mm_caption_caret_offsets(const unsigned short *, MMI32 length, MMI32 *offsets);
/* Set last_offset to -1 before the first positioned run. The builder borrows
 * the offset table. The host measures only partial runs and owns published stops.
 * Callbacks use the layout convention of zero success or negative failure. */
int mm_caption_caret_run(MMCaptionCaretBuilder *, const MMCaptionLayoutRun *, const MMCaptionCaretServices *);
MMI32 mm_caption_scroll_to_caret(MMI32 scroll, MMI32 layout_height, MMI32 top, MMI32 bottom, float y, float height);
/* Select the last visual stop for a repeated offset at a wrap boundary.
 * A negative offset resets draft scrolling. Returns whether scroll changed. */
int mm_caption_presentation_caret(MMCaptionPresentation *, MMI32 offset, MMI32 height, MMI32 top, MMI32 bottom,
                                   size_t count, void *, void (*read)(void *, size_t index, MMCaptionCaretStop *));

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

typedef struct MMCaptionOutput
{
    unsigned char *rgba;
    size_t rgba_bytes;
    MMU32 width, height;
    const MMPreviewProjection *camera, *view;
    const float *depth;
    size_t depth_count;
    MMU32 *covered;
    int authored;
} MMCaptionOutput;
typedef int MMCaptionPaint(void *, size_t row, MMI32 scroll, MMCaptionCanvas *);

enum { MM_CAPTION_PAINT_MEMORY = -1, MM_CAPTION_PAINT_FAILED, MM_CAPTION_PAINT_OK };
/* Observe the frame and draw directly in the authored view or project the
 * reusable work-view layer. Row zero selects the draft. A failed paint
 * never projects an incomplete layer. The callback borrows the canvas
 * during the call and must not reenter the presentation. */
int mm_caption_presentation_paint(MMCaptionPresentation *, MMI32 frame, const MMU32 *hidden, int have_draft,
                                  const MMCaptionOutput *, void *, MMCaptionPaint *);

/* Return the usable line width, or zero for unsupported box or text sizes. */
MMI32 mm_caption_layout_width(const MMTextBoxBounds *, size_t text_length);

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
