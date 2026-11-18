#include "mm/pixel_raster.h"
#include "mm/runtime_math.h"
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

MMI32 mm_pixel_sample(MMI32 position, MMI32 source, MMI32 destination, int near)
{
    MMU32 factor, divisor;
    MMRuntimeCounter input, frequency, quotient;
    if (near)
        return position < source ? position : source - 1;
    /* COLORONCOLOR uses the source cell under the centre of the target pixel. */
    factor = (MMU32)position * 2 + 1;
    divisor = (MMU32)destination * 2;
    if (factor <= (MMU32)0xffffffffUL / (MMU32)source)
        return (MMI32)(factor * (MMU32)source / divisor);
    input.low = factor;
    frequency.low = divisor;
    input.high = frequency.high = 0;
    mm_runtime_counter_scale(input, (MMU32)source, frequency, &quotient);
    return (MMI32)quotient.low;
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

int mm_pixel_copy(const MMPixelImage *target, const MMPixelImage *source, const MMPixelRect *source_rect,
                  const MMPixelRect *target_rect, const MMPixelRect *clip, const MMPixelSpans *spans, int options,
                  MMPixelOverlap overlaps)
{
    const MMPixelSurface *src = &source->surface, *dst = &target->surface;
    const unsigned char *input = (const unsigned char *)src->pixels, *src_row;
    unsigned char *snapshot = NULL, *dst_row;
    unsigned char source_palette[1024], target_palette[1024];
    MMU32 mapping[256], raw;
    MMI32 sw, sh, dw, dh, source_colours, target_colours, i, x, y, sx, sy, left, right;
    MMPixelRect bounds;
    size_t source_bytes, target_bytes;
    int near, raw_copy;

    if (!mm_pixel_image_valid(target) || !mm_pixel_image_valid(source) ||
        !mm_pixel_rect_extent(source_rect, &sw, &sh) || !mm_pixel_rect_extent(target_rect, &dw, &dh))
        return 0;
    if (!sw || !sh || !dw || !dh)
        return (options & MM_PIXEL_COPY_ALLOW_EMPTY) != 0;
    near = (options & MM_PIXEL_COPY_NEAR_SIZE) && sw - dw >= -1 && sw - dw <= 1 && sh - dh >= -1 && sh - dh <= 1;
    /* The legacy near-size true-colour-to-8-bit host path approximates colours. */
    if (near && src->depth >= 16 && dst->depth == 8)
        return 0;

    source_colours = snapshot_palette(source, source_palette);
    target_colours = snapshot_palette(target, target_palette);
    raw_copy = src->depth == dst->depth &&
               (!target_colours || (target_colours == (1 << dst->depth) &&
                                    palette_matches(source_palette, source_colours, target_palette, target_colours)));
    if (source_colours)
        for (i = 0; i < (1 << src->depth); ++i)
            mapping[i] = raw_copy ? (MMU32)i : encode(i < source_colours ? mm_pixel_rgb(source_palette + 4 * i) : 0,
                                                      dst->depth, target_palette, target_colours);

    source_bytes = (size_t)src->pitch * (size_t)src->height;
    target_bytes = (size_t)dst->pitch * (size_t)dst->height;
    if (!overlaps || overlaps(input, source_bytes, dst->pixels, target_bytes))
    {
        snapshot = (unsigned char *)malloc(source_bytes);
        if (!snapshot)
            return 0;
        memcpy(snapshot, input, source_bytes);
        input = snapshot;
    }

    bounds.left = bounds.top = 0;
    bounds.right = dst->width;
    bounds.bottom = dst->height;
    mm_pixel_rect_intersect(&bounds, target_rect);
    if (clip)
        mm_pixel_rect_intersect(&bounds, clip);
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom)
        goto done;
    if (spans)
        spans->begin(spans->context, &bounds);
    for (y = bounds.top; y < bounds.bottom; ++y)
    {
        if (spans && y != bounds.top)
            spans->advance(spans->context);
        sy = source_rect->top + mm_pixel_sample(y - target_rect->top, sh, dh, near);
        if (sy < 0 || sy >= src->height)
            continue;
        src_row = input + (size_t)sy * (size_t)src->pitch;
        dst_row = (unsigned char *)dst->pixels + (size_t)y * (size_t)dst->pitch;
        left = bounds.left;
        right = bounds.right;
        if (spans && !spans->next(spans->context, &left, &right))
            continue;
        do
        {
            for (x = left; x < right; ++x)
            {
                sx = source_rect->left + mm_pixel_sample(x - target_rect->left, sw, dw, near);
                if (sx < 0 || sx >= src->width)
                    continue;
                raw = mm_pixel_read(src_row, sx, src->depth);
                if (source_colours)
                    raw = mapping[raw];
                else if (!raw_copy)
                    raw = encode(src->depth == 16 ? expand555(raw) : raw & 0xffffffUL, dst->depth, target_palette,
                                 target_colours);
                mm_pixel_write(dst_row, x, dst->depth, raw);
            }
        } while (spans && spans->next(spans->context, &left, &right));
    }
done:
    free(snapshot);
    return 1;
}
