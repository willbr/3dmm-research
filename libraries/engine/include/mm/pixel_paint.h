#ifndef MM_ENGINE_PIXEL_PAINT_H
#define MM_ENGINE_PIXEL_PAINT_H
#include "pixel_raster.h"
#ifdef __cplusplus
extern "C" {
#endif

enum
{
    MM_PIXEL_COLOR,
    MM_PIXEL_CLEAR,
    MM_PIXEL_INVERT
};

typedef struct MMPixelColorInk
{
    MMU32 rgb;
    int mode;
} MMPixelColorInk;

typedef struct MMPixelColorBrush
{
    MMPixelColorInk foreground, background;
    unsigned char pattern[8];
} MMPixelColorBrush;

typedef struct MMPixelInk
{
    MMU32 and_mask, xor_mask;
} MMPixelInk;

/* (old & and_mask) ^ xor_mask. Pattern rows and bits are anchored at (0,0). */
typedef struct MMPixelBrush
{
    MMPixelInk foreground, background;
    unsigned char pattern[8];
} MMPixelBrush;

typedef struct MMPixelMono
{
    MMU32 background_rgb;
    unsigned char background_reference, foreground_reference, alternate_reference;
    int collapse_equal_colours;
} MMPixelMono;

typedef struct MMPixelCoverage
{
    const unsigned char *pixels;
    MMI32 width, height, pitch;
} MMPixelCoverage;

typedef struct MMPixelPoint
{
    MMI32 x, y;
} MMPixelPoint;

void mm_pixel_resolve_rgb(const MMPixelColorBrush *, MMPixelBrush *);
int mm_pixel_resolve_indexed(const MMPixelColorBrush *, const unsigned char *palette, MMI32 count, int highlight,
                             MMPixelBrush *);
unsigned char mm_pixel_monochrome_colour(MMU32 rgb, unsigned char reference, const MMPixelMono *,
                                        const unsigned char *palette);
/* Resolves the packed 1-, 4-, 16- and 24-bit formats. Mixed monochrome patterns
 * fail before publishing a brush. An omitted monochrome state uses black and white. */
int mm_pixel_resolve_packed(const MMPixelImage *, const MMPixelColorBrush *, const MMPixelMono *, MMPixelBrush *);

/* Brush painting treats 32-bit samples as native-order XRGB. For a single
 * span, the caller supplies a validated surface and in-range coordinates. */
void mm_pixel_paint_span(const MMPixelSurface *, const MMPixelBrush *, MMI32 y, MMI32 left, MMI32 right);
void mm_pixel_fill_rect(const MMPixelSurface *, const MMPixelBrush *, const MMPixelRect *, const MMPixelRect *clip,
                        const MMPixelSpans *, MMI32 frame_x, MMI32 frame_y);
int mm_pixel_invert_packed_rect(const MMPixelImage *, const MMPixelRect *, const MMPixelRect *clip,
                                const MMPixelSpans *);
int mm_pixel_fill_polygon(const MMPixelSurface *, const MMPixelBrush *, const MMPixelPoint *, size_t count,
                          const MMPixelRect *clip, const MMPixelSpans *);
/* Native XRGB blends an 8-bit mask with rounding. Raw coverage replaces a
 * packed sample when its mask is at least 128. */
int mm_pixel_draw_coverage(const MMPixelSurface *, const MMPixelCoverage *, MMI32 x, MMI32 y, MMU32 sample,
                            int blend_rgb, const MMPixelRect *clip, const MMPixelSpans *);

#ifdef __cplusplus
}
#endif
#endif
