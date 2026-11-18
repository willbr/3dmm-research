#include "mm/dib_frame.h"
#include "mm/region.h"
#include "mm/runtime_math.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

int mm_pixel_surface_valid(const MMPixelSurface *surface)
{
    const MMI32 depth = surface->depth;
    const MMI32 width = surface->width, pitch = surface->pitch, height = surface->height;
    if (!surface->pixels || width <= 0 || height <= 0 || pitch <= 0 ||
        (size_t)pitch != (MMU32)pitch || (size_t)height != (MMU32)height ||
        (size_t)height > (size_t)-1 / (size_t)pitch)
        return 0;
    if (depth == 1 || depth == 4)
        return (width - 1) / (8 / depth) < pitch;
    return (depth == 8 || depth == 16 || depth == 24 || depth == 32) && width <= pitch / (depth / 8);
}

int mm_pixel_image_valid(const MMPixelImage *image)
{
    MMI32 depth = image->surface.depth;
    if (!mm_pixel_surface_valid(&image->surface))
        return 0;
    if (depth == 1)
        return image->colours == (image->palette ? 2 : 0);
    return depth >= 16 || (image->palette && image->colours > 0 && image->colours <= (1 << depth));
}

int mm_pixel_rect_extent(const MMPixelRect *rect, MMI32 *width, MMI32 *height)
{
    MMU32 dx = (MMU32)rect->right - (MMU32)rect->left;
    MMU32 dy = (MMU32)rect->bottom - (MMU32)rect->top;
    if (rect->right < rect->left || rect->bottom < rect->top || dx > 2147483647L || dy > 2147483647L)
        return 0;
    *width = (MMI32)dx;
    *height = (MMI32)dy;
    return 1;
}

void mm_pixel_rect_intersect(MMPixelRect *bounds, const MMPixelRect *clip)
{
    if (clip->left > bounds->left)
        bounds->left = clip->left;
    if (clip->right < bounds->right)
        bounds->right = clip->right;
    if (clip->top > bounds->top)
        bounds->top = clip->top;
    if (clip->bottom < bounds->bottom)
        bounds->bottom = clip->bottom;
}

int mm_pixel_visible_rect(const MMPixelSurface *surface, const MMPixelRect *shape, const MMPixelRect *clip,
                          MMPixelRect *bounds)
{
    bounds->left = bounds->top = 0;
    bounds->right = surface->width;
    bounds->bottom = surface->height;
    mm_pixel_rect_intersect(bounds, shape);
    if (clip)
        mm_pixel_rect_intersect(bounds, clip);
    return bounds->left < bounds->right && bounds->top < bounds->bottom;
}

typedef struct PixelSpanPaint
{
    MMPixelDrawSpan draw;
    void *context;
} PixelSpanPaint;

static int paint_band(void *context, const MMPixelRect *rect)
{
    PixelSpanPaint *p = context;
    MMI32 y;
    for (y = rect->top; y < rect->bottom; ++y)
        p->draw(p->context, y, rect->left, rect->right);
    return 1;
}

void mm_pixel_visit_spans(const MMPixelRect *bounds, const struct MMRegion *region, MMPixelDrawSpan draw, void *context)
{
    PixelSpanPaint paint;
    paint.draw = draw;
    paint.context = context;
    if (region)
        mm_region_visit_rectangles(region, bounds, paint_band, &paint);
    else
        paint_band(&paint, bounds);
}

MMU32 mm_pixel_muldiv(MMU32 factor, MMU32 source, MMU32 divisor)
{
    MMRuntimeCounter input, frequency, quotient;
    if (!source || factor <= (MMU32)0xffffffffUL / source)
        return factor * source / divisor;
    input.low = factor;
    frequency.low = divisor;
    input.high = frequency.high = 0;
    mm_runtime_counter_scale(input, source, frequency, &quotient);
    return quotient.low;
}

MMI32 mm_pixel_edge_ceil(MMI32 x0, MMI32 y0, MMI32 x1, MMI32 y1, MMI32 y)
{
    MMU32 height = (MMU32)y1 - (MMU32)y0;
    MMU32 offset = (MMU32)y - (MMU32)y0;
    MMU32 dx;
    if (x1 < x0)
    {
        dx = (MMU32)x0 - (MMU32)x1;
        return mm_signed_word((MMU32)x0 - mm_pixel_muldiv(offset, dx, height));
    }
    /* ceil(offset * dx / height) equals dx minus the remaining floor. */
    dx = (MMU32)x1 - (MMU32)x0;
    return mm_signed_word((MMU32)x1 - mm_pixel_muldiv(height - offset, dx, height));
}

MMI32 mm_pixel_sample(MMI32 position, MMI32 source, MMI32 destination, int near_size)
{
    if (near_size)
        return position < source ? position : source - 1;
    /* COLORONCOLOR uses the source cell under the centre of the target pixel. */
    return (MMI32)mm_pixel_muldiv((MMU32)position * 2 + 1, (MMU32)source, (MMU32)destination * 2);
}

static MMI32 sample_edge(MMI32 position, MMI32 source, MMI32 destination)
{
    /* For reduction, compute the ceiling as source minus the remaining floor. */
    if (source > destination)
        return source - (MMI32)mm_pixel_muldiv((MMU32)(destination - position), (MMU32)source, (MMU32)destination);
    return (MMI32)mm_pixel_muldiv((MMU32)position, (MMU32)source, (MMU32)destination);
}
MMU32 mm_pixel_read(const unsigned char *row, MMI32 x, int depth)
{
    const unsigned char *pixel;
    MMU32 raw;
    if (depth == 1)
        return (row[x / 8] >> (7 - (x & 7))) & 1;
    if (depth == 4)
        return (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
    pixel = row + (size_t)x * (depth / 8);
    raw = pixel[0];
    if (depth >= 16)
        raw |= (MMU32)pixel[1] << 8;
    if (depth >= 24)
        raw |= (MMU32)pixel[2] << 16;
    if (depth == 32)
        raw |= (MMU32)pixel[3] << 24;
    return raw;
}

void mm_pixel_write(unsigned char *row, MMI32 x, int depth, MMU32 raw)
{
    unsigned char *pixel;
    unsigned shift, mask;
    if (depth == 1 || depth == 4)
    {
        shift = depth == 1 ? 7 - (x & 7) : (x & 1) ? 0 : 4;
        mask = depth == 1 ? 1 : 15;
        pixel = row + x / (8 / depth);
        *pixel = (unsigned char)((*pixel & ~(mask << shift)) | ((raw & mask) << shift));
        return;
    }
    pixel = row + (size_t)x * (depth / 8);
    pixel[0] = (unsigned char)raw;
    if (depth >= 16)
        pixel[1] = (unsigned char)(raw >> 8);
    if (depth >= 24)
        pixel[2] = (unsigned char)(raw >> 16);
    if (depth == 32)
        pixel[3] = (unsigned char)(raw >> 24);
}

void mm_pixel_mask_span(unsigned char *row, MMI32 left, MMI32 right, int marked)
{
    MMI32 bytes;
    while (left < right && (left & 7))
        mm_pixel_write(row, left++, 1, marked != 0);
    bytes = (right - left) / 8;
    if (bytes > 0)
    {
        memset(row + left / 8, marked ? 255 : 0, (size_t)bytes);
        left += bytes * 8;
    }
    while (left < right)
        mm_pixel_write(row, left++, 1, marked != 0);
}

MMU32 mm_pixel_rgb(const unsigned char *colour)
{
    return ((MMU32)colour[2] << 16) | ((MMU32)colour[1] << 8) | colour[0];
}

MMU32 mm_pixel_rgb555(MMU32 rgb)
{
    return ((rgb >> 9) & 0x7c00) | ((rgb >> 6) & 0x3e0) | ((rgb >> 3) & 31);
}

unsigned char mm_pixel_nearest(const unsigned char *palette, MMI32 count, MMU32 rgb)
{
    MMI32 i, red, green, blue, distance = 2147483647L, candidate;
    unsigned char best = 0;
    for (i = 0; i < count; ++i, palette += 4)
    {
        red = (MMI32)palette[2] - (MMI32)((rgb >> 16) & 255);
        green = (MMI32)palette[1] - (MMI32)((rgb >> 8) & 255);
        blue = (MMI32)palette[0] - (MMI32)(rgb & 255);
        candidate = red * red + green * green + blue * blue;
        if (candidate < distance)
        {
            distance = candidate;
            best = (unsigned char)i;
        }
    }
    return best;
}

static int palette_matches(const unsigned char *source, MMI32 source_count, const unsigned char *target, MMI32 count)
{
    MMI32 i;
    for (i = 0; i < count; ++i)
        if ((i < source_count ? mm_pixel_rgb(source + 4 * i) : 0) != mm_pixel_rgb(target + 4 * i))
            return 0;
    return 1;
}

void mm_pixel_translate8(const unsigned char *source, MMI32 source_count, const unsigned char *target,
                         MMI32 target_count, unsigned char indices[256])
{
    MMI32 i;
    int identity = target_count == 256 && palette_matches(source, source_count, target, 256);
    for (i = 0; i < 256; ++i)
        indices[i] = identity ? (unsigned char)i
                             : mm_pixel_nearest(target, target_count, i < source_count ? mm_pixel_rgb(source + 4 * i) : 0);
}

static MMI32 snapshot_palette(const MMPixelImage *image, unsigned char palette[1024])
{
    MMI32 depth = image->surface.depth, count;
    if (depth >= 16)
        return 0;
    count = depth == 1 ? 2 : image->colours;
    if (image->palette)
        memcpy(palette, image->palette, (size_t)count * 4);
    else
    {
        memset(palette, 0, 4);
        memset(palette + 4, 255, 3);
        palette[7] = 0;
    }
    return count;
}

static MMU32 expand555(MMU32 raw)
{
    MMU32 red = (raw >> 10) & 31, green = (raw >> 5) & 31, blue = raw & 31;
    return (((red << 3) | (red >> 2)) << 16) | (((green << 3) | (green >> 2)) << 8) | (blue << 3) | (blue >> 2);
}

static MMU32 encode(MMU32 rgb, MMI32 depth, const unsigned char *palette, MMI32 colours)
{
    if (colours)
        return mm_pixel_nearest(palette, colours, rgb);
    return depth == 16 ? mm_pixel_rgb555(rgb) : rgb;
}

typedef struct SampledCopy
{
    const MMPixelSurface *src, *dst;
    const MMPixelRect *from, *to;
    const unsigned char *input, *target_palette, *indexed_palette;
    const MMU32 *mapping;
    MMI32 sw, sh, dw, dh, target_colours, indexed_colours;
    int near_size, dib24, raw_copy;
} SampledCopy;

static int set_geometry(SampledCopy *copy, const MMPixelSurface *dst, const MMPixelSurface *src,
                        const MMPixelRect *from, const MMPixelRect *to)
{
    copy->src = src;
    copy->dst = dst;
    copy->from = from;
    copy->to = to;
    copy->input = (const unsigned char *)src->pixels;
    return mm_pixel_rect_extent(from, &copy->sw, &copy->sh) && mm_pixel_rect_extent(to, &copy->dw, &copy->dh);
}

static void copy_span(void *context, MMI32 y, MMI32 left, MMI32 right)
{
    const SampledCopy *copy = (const SampledCopy *)context;
    const MMPixelSurface *src = copy->src, *dst = copy->dst;
    MMI32 dy = y - copy->to->top, sx, sy, x, dx;
    const unsigned char *src_row;
    unsigned char *dst_row;
    MMU32 raw;
    /* The 24-bit path measures vertical edges from the bottom of the DIB. */
    if (copy->dib24)
        sy = copy->sh - 1 - sample_edge(copy->dh - 1 - dy, copy->sh, copy->dh);
    else
        sy = copy->from->top + mm_pixel_sample(dy, copy->sh, copy->dh, copy->near_size);
    if (sy < 0 || sy >= src->height)
        return;
    src_row = copy->input + (size_t)sy * (size_t)src->pitch;
    dst_row = (unsigned char *)dst->pixels + (size_t)y * (size_t)dst->pitch;
    for (x = left; x < right; ++x)
    {
        dx = x - copy->to->left;
        sx = copy->from->left + (copy->dib24 ? sample_edge(dx, copy->sw, copy->dw)
                                            : mm_pixel_sample(dx, copy->sw, copy->dw, copy->near_size));
        if (sx < 0 || sx >= src->width)
            continue;
        if (copy->raw_copy && src->depth == 32)
        {
            memcpy(dst_row + (size_t)x * 4, src_row + (size_t)sx * 4, 4);
            continue;
        }
        raw = mm_pixel_read(src_row, sx, src->depth);
        if (copy->indexed_palette)
        {
            if (raw < (MMU32)copy->indexed_colours)
            {
                raw = mm_pixel_rgb(copy->indexed_palette + 4 * raw);
                memcpy(dst_row + (size_t)x * 4, &raw, 4);
            }
            continue;
        }
        if (copy->mapping)
            raw = copy->mapping[raw];
        else if (!copy->raw_copy)
            raw = encode(src->depth == 16 ? expand555(raw) : raw & 0xffffffUL, dst->depth, copy->target_palette,
                         copy->target_colours);
        mm_pixel_write(dst_row, x, dst->depth, raw);
    }
}

static void draw_copy(SampledCopy *copy, const MMPixelRect *clip, const struct MMRegion *region)
{
    MMPixelRect bounds;
    if (mm_pixel_visible_rect(copy->dst, copy->to, clip, &bounds))
        mm_pixel_visit_spans(&bounds, region, copy_span, copy);
}

int mm_pixel_copy(const MMPixelImage *target, const MMPixelImage *source, const MMPixelRect *source_rect,
                  const MMPixelRect *target_rect, const MMPixelRect *clip, const struct MMRegion *region, int options,
                  MMPixelOverlap overlaps)
{
    const MMPixelSurface *src = &source->surface, *dst = &target->surface;
    SampledCopy copy = {0};
    unsigned char *snapshot = NULL;
    unsigned char source_palette[1024], target_palette[1024];
    MMU32 mapping[256];
    MMI32 x_diff, y_diff, source_colours, i;
    size_t source_bytes, target_bytes;

    if (!mm_pixel_image_valid(target) || !mm_pixel_image_valid(source) ||
        !set_geometry(&copy, dst, src, source_rect, target_rect))
        return 0;
    if (!copy.sw || !copy.sh || !copy.dw || !copy.dh)
        return (options & MM_PIXEL_COPY_ALLOW_EMPTY) != 0;
    x_diff = copy.sw - copy.dw;
    y_diff = copy.sh - copy.dh;
    copy.near_size = (options & MM_PIXEL_COPY_NEAR_SIZE) && x_diff >= -1 && x_diff <= 1 && y_diff >= -1 && y_diff <= 1;
    /* The legacy near-size true-colour-to-8-bit host path approximates colours. */
    if (copy.near_size && src->depth >= 16 && dst->depth == 8)
        return 0;

    source_colours = snapshot_palette(source, source_palette);
    copy.target_palette = target_palette;
    copy.target_colours = snapshot_palette(target, target_palette);
    copy.raw_copy = src->depth == dst->depth &&
                    (!copy.target_colours || (copy.target_colours == (1 << dst->depth) &&
                     palette_matches(source_palette, source_colours, target_palette, copy.target_colours)));
    if (source_colours)
    {
        copy.mapping = mapping;
        for (i = 0; i < (1 << src->depth); ++i)
            mapping[i] = copy.raw_copy ? (MMU32)i
                         : encode(i < source_colours ? mm_pixel_rgb(source_palette + 4 * i) : 0, dst->depth,
                                  target_palette, copy.target_colours);
    }

    source_bytes = (size_t)src->pitch * (size_t)src->height;
    target_bytes = (size_t)dst->pitch * (size_t)dst->height;
    if (!overlaps || overlaps(copy.input, source_bytes, dst->pixels, target_bytes))
    {
        snapshot = (unsigned char *)malloc(source_bytes);
        if (!snapshot)
            return 0;
        memcpy(snapshot, copy.input, source_bytes);
        copy.input = snapshot;
    }
    draw_copy(&copy, clip, region);
    free(snapshot);
    return 1;
}

int mm_pixel_expand_indexed(const MMPixelSurface *target, const MMPixelImage *source, const MMPixelRect *source_rect,
                            const MMPixelRect *target_rect, const MMPixelRect *clip, const struct MMRegion *region)
{
    SampledCopy copy = {0};
    if (target->depth != 32 || source->surface.depth != 8 || !mm_pixel_surface_valid(target) ||
        !mm_pixel_image_valid(source) || !set_geometry(&copy, target, &source->surface, source_rect, target_rect) ||
        !copy.sw || !copy.sh || !copy.dw || !copy.dh)
        return 0;
    copy.indexed_palette = source->palette;
    copy.indexed_colours = source->colours;
    draw_copy(&copy, clip, region);
    return 1;
}

int mm_dib_draw(const MMPixelSurface *target, const MMPixelSurface *frame, size_t frame_bytes, int input_depth,
                const MMPixelRect *target_rect, const MMPixelRect *clip, const struct MMRegion *region, MMPixelOverlap overlaps)
{
    SampledCopy copy = {0};
    MMPixelRect source_rect;
    if (target->depth != 32 || frame->depth != 32 || (input_depth != 24 && input_depth != 32) ||
        !mm_pixel_surface_valid(target) || !mm_pixel_surface_valid(frame) ||
        (size_t)frame->pitch * (size_t)frame->height > frame_bytes)
        return 0;
    source_rect.left = source_rect.top = 0;
    source_rect.right = frame->width;
    source_rect.bottom = frame->height;
    if (!set_geometry(&copy, target, frame, &source_rect, target_rect))
        return 0;
    if (!copy.dw || !copy.dh)
        return 1;
    if (overlaps && overlaps(frame->pixels, frame_bytes, target->pixels, (size_t)target->pitch * (size_t)target->height))
        return 0;
    copy.raw_copy = 1;
    copy.dib24 = input_depth == 24;
    draw_copy(&copy, clip, region);
    return 1;
}

static int compare_colour_key(const void *first, const void *second)
{
    MMU32 a = *(const MMU32 *)first, b = *(const MMU32 *)second;
    return a < b ? -1 : a > b;
}

static int palette_index(const MMU32 *entries, MMI32 count, MMU32 rgb)
{
    int lo = 0, hi = (int)count, mid;
    MMU32 key = (rgb & 0xffffffUL) << 8;
    while (lo < hi)
    {
        mid = lo + (hi - lo) / 2;
        if (entries[mid] < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < count && (entries[lo] & 0xffffff00UL) == key ? (int)(entries[lo] & 255) : -1;
}

static int rect_inside(const MMPixelRect *rect, const MMPixelSurface *image, MMI32 *width, MMI32 *height)
{
    return rect->left >= 0 && rect->top >= 0 && rect->right <= image->width && rect->bottom <= image->height &&
           mm_pixel_rect_extent(rect, width, height) && *width > 0 && *height > 0;
}

int mm_pixel_snapshot_indexed(const MMPixelImage *target, const MMPixelSurface *source,
                              const MMPixelRect *source_rect, const MMPixelRect *target_rect)
{
    const MMPixelSurface *dst = &target->surface;
    const unsigned char *from;
    unsigned char *indices, *to;
    MMU32 entries[256], rgb;
    MMI32 width, height, tw, th, i, x, y;
    int index;
    if (dst->depth != 8 || source->depth != 32 || !mm_pixel_image_valid(target) || !mm_pixel_surface_valid(source) ||
        !rect_inside(source_rect, source, &width, &height) || !rect_inside(target_rect, dst, &tw, &th) ||
        width != tw || height != th)
        return 0;

    /* The low byte orders duplicate colours by original palette index. */
    for (i = 0; i < target->colours; ++i)
        entries[i] = (mm_pixel_rgb(target->palette + 4 * i) << 8) | (MMU32)i;
    qsort(entries, (size_t)target->colours, sizeof(entries[0]), compare_colour_key);
    indices = (unsigned char *)malloc((size_t)width * (size_t)height);
    if (!indices)
        return 0;

    /* Preflight every pixel before publishing any part of the transition. */
    for (y = 0; y < height; ++y)
    {
        from = (const unsigned char *)source->pixels + (size_t)(source_rect->top + y) * (size_t)source->pitch +
               (size_t)source_rect->left * 4;
        to = indices + (size_t)y * (size_t)width;
        for (x = 0; x < width; ++x)
        {
            memcpy(&rgb, from + (size_t)x * 4, 4);
            index = palette_index(entries, target->colours, rgb);
            if (index < 0)
            {
                free(indices);
                return 0;
            }
            to[x] = (unsigned char)index;
        }
    }
    for (y = 0; y < height; ++y)
    {
        to = (unsigned char *)dst->pixels + (size_t)(target_rect->top + y) * (size_t)dst->pitch + target_rect->left;
        memcpy(to, indices + (size_t)y * (size_t)width, (size_t)width);
    }
    free(indices);
    return 1;
}
