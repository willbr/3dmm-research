#ifndef MM_ENGINE_GRAPHICS_VIEW_H
#define MM_ENGINE_GRAPHICS_VIEW_H
#include "pixel_paint.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMGraphicsView
{
    MMPixelRect source, destination, visible, clip;
    int has_clip;
} MMGraphicsView;

/* Source coordinates are logical. Destination, visibility and clipping are
 * in port coordinates. Setting the destination reopens visibility and clip. */
void mm_graphics_view_init(MMGraphicsView *, MMPixelPoint port_base);
void mm_graphics_view_set_source(MMGraphicsView *, const MMPixelRect *);
void mm_graphics_view_set_destination(MMGraphicsView *, const MMPixelRect *, MMPixelPoint port_base);
MMPixelRect mm_graphics_view_destination(const MMGraphicsView *, MMPixelPoint port_base);
void mm_graphics_view_set_visible(MMGraphicsView *, const MMPixelRect *);
void mm_graphics_view_intersect_visible(MMGraphicsView *, const MMPixelRect *);
void mm_graphics_view_clip(MMGraphicsView *, const MMPixelRect *);

MMPixelPoint mm_graphics_view_map_point(const MMGraphicsView *, MMPixelPoint);
MMPixelRect mm_graphics_view_map_rect(const MMGraphicsView *, MMPixelRect, int reverse);
MMPixelPoint mm_graphics_view_pattern_origin(const MMGraphicsView *);
MMPixelPoint mm_graphics_view_scale_size(const MMGraphicsView *, MMPixelPoint);

/* The horizontal strip comes first. The second strip excludes the first.
 * Either output can be NULL. */
void mm_graphics_scroll_exposed(const MMPixelRect *, MMI32 dx, MMI32 dy, MMPixelRect *, MMPixelRect *);
void mm_graphics_line_polygon(MMPixelPoint first, MMPixelPoint last, MMPixelPoint pen, MMPixelPoint polygon[6]);

#ifdef __cplusplus
}
#endif
#endif
