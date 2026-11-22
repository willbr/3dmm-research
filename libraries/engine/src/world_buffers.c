/* World buffer ownership/control converted from bren/bwld.cpp. */
#include "mm/world_buffers.h"
#include <string.h>
static int valid_scale(MMI32 scale)
{
    return scale == 1 || scale == 2 || scale == 4;
}
static void release_slot(MMWorldBuffers *w, int kind, const MMWorldBufferServices *s)
{
    void *object = w->owners[kind];
    w->owners[kind] = NULL;
    if (object)
    {
        if (kind == MM_WORLD_RGB_WORKING && w->rgb.pixels)
        {
            s->unlock(s->context, object);
            w->rgb.pixels = NULL;
        }
        if (kind == MM_WORLD_DEPTH_WORKING || kind == MM_WORLD_DEPTH_BACKGROUND)
            s->release_depth(s->context, object);
        else if (kind == MM_WORLD_CACHE)
            s->release_cache(s->context, object);
        else
            s->release(s->context, object);
    }
}
static void release_buffers(MMWorldBuffers *w, const MMWorldBufferServices *s)
{
    release_slot(w, MM_WORLD_RGB_WORKING, s);
    mm_pixel_buffer_reset(&w->background);
    release_slot(w, MM_WORLD_DEPTH_WORKING, s);
    release_slot(w, MM_WORLD_DEPTH_BACKGROUND, s);
}
static int create_slot(MMWorldBuffers *w, int kind, MMI32 width, MMI32 height,
                       const MMWorldBufferServices *s)
{
    release_slot(w, kind, s);
    w->owners[kind] = s->create(s->context, kind, width, height);
    return w->owners[kind] != NULL;
}
static int build(MMWorldBuffers *w, int hx, int hy, const MMWorldBufferServices *s)
{
    MMI32 width, height;
    const MMDepthBitmap *depth;
    MMWorldPixels rgb;
    if (!valid_scale(w->scale) || w->width <= 0 || w->height <= 0 || w->width > 4096 / w->scale ||
        w->height > 4096 / w->scale)
        return 0;
    w->half_x = hx != 0;
    w->half_y = hy != 0;
    width = w->width * w->scale;
    height = w->height * w->scale;
    if (w->scale == 1 && hx)
        width /= 2;
    if (w->scale == 1 && hy)
        height /= 2;
    if (!width || !height)
        return 0;
    w->buffer_width = width;
    w->buffer_height = height;
    if (!create_slot(w, MM_WORLD_DEPTH_BACKGROUND, width, height, s) ||
        !create_slot(w, MM_WORLD_DEPTH_WORKING, width, height, s))
        return 0;
    depth = s->depth_bitmap(s->context, w->owners[MM_WORLD_DEPTH_WORKING]);
    if (!mm_world_depth_valid(depth, width, height) || depth->row_bytes != width * 2)
        return 0;
    w->z.row = width * 2;
    w->z.width = width;
    w->z.height = height;
    w->z.x = width / 2;
    w->z.y = height / 2;
    w->z.type = s->depth_type;
    w->z.pixels = depth->pixels;
    if (!mm_pixel_buffer_init(&w->background, width, height, 8, 4) ||
        !create_slot(w, MM_WORLD_RGB_WORKING, width, height, s))
        return 0;
    w->rgb.row = width;
    w->rgb.width = width;
    w->rgb.height = height;
    w->rgb.x = width / 2;
    w->rgb.y = height / 2;
    w->rgb.type = s->rgb_type;
    if (!s->lock(s->context, w->owners[MM_WORLD_RGB_WORKING], &rgb))
        return 0;
    w->rgb.pixels = rgb.pixels;
    if (!mm_world_pixels_valid(&rgb, width, height))
    {
        s->unlock(s->context, w->owners[MM_WORLD_RGB_WORKING]);
        w->rgb.pixels = NULL;
        return 0;
    }
    return 1;
}
int mm_world_buffers_init(MMWorldBuffers *w, MMI32 width, MMI32 height, MMI32 scale, int hx,
                                int hy, const MMWorldBufferServices *s)
{
    w->width = width;
    w->height = height;
    w->scale = scale;
    if (!build(w, hx, hy, s))
        return 0;
    s->setup_scene(s->context);
    mm_world_buffers_dirty(w, 0);
    return 1;
}
void mm_world_buffers_dirty(MMWorldBuffers *w, int screen)
{
    MMPixelRect viewport;
    viewport.left = viewport.top = 0;
    viewport.right = w->buffer_width;
    viewport.bottom = w->buffer_height;
    mm_region_set_rect(&w->dirty_working, &viewport);
    if (screen)
        mm_region_set_rect(&w->dirty_screen, &viewport);
}
void mm_world_buffers_dispose(MMWorldBuffers *w, const MMWorldBufferServices *s)
{
    release_buffers(w, s);
    mm_region_set_rect(&w->dirty_working, NULL);
    mm_region_set_rect(&w->dirty_screen, NULL);
    release_slot(w, MM_WORLD_CACHE, s);
    w->rgb.pixels = w->z.pixels = NULL;
}
int mm_world_pixels_valid(const MMWorldPixels *pixels, MMI32 width, MMI32 height)
{
    return pixels && pixels->pixels && width > 0 && height > 0 && width <= 4096 && height <= 4096 && pixels->row >= width &&
           (size_t)height <= (size_t)-1 / (size_t)pixels->row &&
           pixels->bytes >= (size_t)pixels->row * (size_t)height;
}

MMWorldPixels mm_world_background_pixels(const MMWorldBuffers *w)
{
    MMWorldPixels pixels;
    pixels.pixels = (unsigned char *)w->background.surface.pixels;
    pixels.row = w->background.surface.pitch;
    pixels.bytes = w->background.bytes;
    return pixels;
}

int mm_world_copy_rgb(const MMWorldPixels *src, MMI32 sw, MMI32 sh, const MMWorldPixels *dst, MMI32 tw, MMI32 th)
{
    const unsigned char *in;
    unsigned char *out;
    MMI32 x, y, sy;
    int near_size;

    if (!mm_world_pixels_valid(src, sw, sh) || !mm_world_pixels_valid(dst, tw, th))
        return 0;
    near_size = sw - tw >= -1 && sw - tw <= 1 && sh - th >= -1 && sh - th <= 1;
    for (y = 0; y < th; ++y)
    {
        sy = mm_pixel_sample(y, sh, th, near_size);
        in = src->pixels + (size_t)sy * src->row;
        out = dst->pixels + (size_t)y * dst->row;
        if (sw == tw)
            memcpy(out, in, (size_t)sw);
        else
            for (x = 0; x < tw; ++x)
                out[x] = in[mm_pixel_sample(x, sw, tw, near_size)];
    }
    return 1;
}

static void draw_background_rgb(const MMMaskedBitmap *source, MMI32 width, MMI32 height, const MMWorldPixels *target)
{
    MMMaskedRect clip = {0, 0, 0, 0};
    clip.right = width;
    clip.bottom = height;
    mm_masked_blit8(source, mm_masked_native_big_endian(), target->pixels, target->row, height, 0, 0, &clip, NULL);
}

int mm_world_fill_background_rgb(const MMMaskedBitmap *bitmap, MMI32 width, MMI32 height,
                                 const MMWorldPixels *target, MMI32 target_width, MMI32 target_height)
{
    MMPixelBuffer full = {0};
    MMWorldPixels pixels;
    int copied;

    if (!bitmap || width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        !mm_world_pixels_valid(target, target_width, target_height))
        return 0;
    if (width == target_width && height == target_height)
    {
        draw_background_rgb(bitmap, width, height, target);
        return 1;
    }
    if (!mm_pixel_buffer_init(&full, width, height, 8, 4))
        return 0;
    pixels.pixels = (unsigned char *)full.surface.pixels;
    pixels.row = full.surface.pitch;
    pixels.bytes = full.bytes;
    draw_background_rgb(bitmap, width, height, &pixels);
    copied = mm_world_copy_rgb(&pixels, width, height, target, target_width, target_height);
    mm_pixel_buffer_reset(&full);
    return copied;
}

int mm_world_depth_valid(const MMDepthBitmap *depth, MMI32 width, MMI32 height)
{
    return depth && depth->pixels && width > 0 && height > 0 && depth->row_bytes > 0 &&
           width <= depth->row_bytes / 2 && height <= depth->bytes / depth->row_bytes;
}

int mm_world_draw_depth(const MMDepthBitmap *source, const MMDepthBitmap *target, MMI32 width, MMI32 height)
{
    MMDepthRect clip = {0, 0, 0, 0};

    if (!source || !mm_world_depth_valid(target, width, height))
        return 0;
    clip.right = width;
    clip.bottom = height;
    /* Retain the valid prefix of a short or displaced cached source. */
    mm_depth_draw(source, target->pixels, target->row_bytes, height, 0, 0, &clip, NULL, 0);
    return 1;
}

int mm_world_depth_resample(const MMDepthBitmap *src, const MMDepthBitmap *dst, MMI32 width,
                                  MMI32 height, MMI32 scale, int hx, int hy)
{
    MMI32 y, x, sy, sx, n;
    const unsigned char *source;
    unsigned char *target;
    if (!mm_world_depth_valid(dst, width, height) || !src || src->row_bytes < 0 || src->bytes < 0 ||
        !src->pixels || scale < 1 || scale > 16)
        return 0;
    source = (const unsigned char *)src->pixels;
    target = (unsigned char *)dst->pixels;
    for (y = 0; y < height; ++y)
    {
        sy = scale > 1 ? y / scale : y * (hy ? 2 : 1);
        if (src->row_bytes && sy > src->bytes / src->row_bytes)
            return 0;
        n = sy * src->row_bytes;
        for (x = 0; x < width; ++x)
        {
            sx = scale > 1 ? x / scale : x * (hx ? 2 : 1);
            if (sx >= src->row_bytes / 2 || src->bytes - n < 2 || sx > (src->bytes - n - 2) / 2)
                return 0;
            memcpy(target + y * dst->row_bytes + x * 2, source + n + sx * 2, 2);
        }
    }
    return 1;
}
static int load_scaled_depth(const MMWorldBuffers *w, const MMDepthBitmap *src, const MMDepthBitmap *dst)
{
    MMDepthOwner *native = mm_depth_owner_new(w->width, w->height, NULL, NULL);
    const MMDepthBitmap *intermediate;
    int ok;
    if (!native)
        return 0;
    intermediate = mm_depth_owner_bitmap(native);
    ok = mm_world_draw_depth(src, intermediate, w->width, w->height) &&
         mm_world_depth_resample(intermediate, dst, w->buffer_width, w->buffer_height, w->scale, 0, 0);
    mm_depth_owner_release(native);
    return ok;
}
int mm_world_buffers_background(MMWorldBuffers *w, void *cache, MMU32 rgb_type, MMU32 rgb_number,
                                      MMU32 z_type, MMU32 z_number, const MMWorldBufferServices *s)
{
    void *bitmap = s->fetch(s->context, cache, MM_WORLD_COLOR_ASSET, rgb_type, rgb_number), *depth;
    const MMDepthBitmap *src, *dst;
    MMWorldPixels target = mm_world_background_pixels(w);
    int ok, resized = w->scale > 1 || w->half_x || w->half_y;
    if (!bitmap)
        return 0;
    depth = s->fetch(s->context, cache, MM_WORLD_DEPTH_BACKGROUND, z_type, z_number);
    if (!depth)
    {
        s->release_bitmap(s->context, bitmap);
        return 0;
    }
    s->priority(s->context, bitmap);
    s->priority_depth(s->context, depth);
    ok = mm_world_fill_background_rgb(s->masked_bitmap(s->context, bitmap), w->width, w->height, &target,
                                     w->buffer_width, w->buffer_height);
    s->release_bitmap(s->context, bitmap);
    if (ok && resized)
    {
        src = s->depth_bitmap(s->context, depth);
        dst = s->depth_bitmap(s->context, w->owners[MM_WORLD_DEPTH_BACKGROUND]);
        ok = w->scale > 1 ? load_scaled_depth(w, src, dst)
                          : mm_world_depth_resample(src, dst, w->buffer_width, w->buffer_height, 1, w->half_x, w->half_y);
    }
    if (!ok || resized)
        s->release_depth(s->context, depth);
    else
    {
        release_slot(w, MM_WORLD_DEPTH_BACKGROUND, s);
        w->owners[MM_WORLD_DEPTH_BACKGROUND] = depth;
    }
    if (!ok)
        return 0;
    mm_world_buffers_dirty(w, 0);
    s->changed(s->context);
    s->retain_cache(s->context, cache);
    release_slot(w, MM_WORLD_CACHE, s);
    w->owners[MM_WORLD_CACHE] = cache;
    w->rgb_type = rgb_type;
    w->rgb_number = rgb_number;
    w->z_type = z_type;
    w->z_number = z_number;
    return 1;
}
int mm_world_buffers_restore_background(MMWorldBuffers *w, const MMWorldBufferServices *s)
{
    void *cache = w->owners[MM_WORLD_CACHE];
    return cache && mm_world_buffers_background(w, cache, w->rgb_type, w->rgb_number, w->z_type, w->z_number, s);
}

int mm_world_buffers_mode(MMWorldBuffers *w, int hx, int hy, MMI32 scale,
                                const MMWorldBufferServices *s)
{
    MMWorldBuffers old;
    unsigned i;
    if (!valid_scale(scale))
        return 0;
    hx = hx != 0;
    hy = hy != 0;
    if (hx == w->half_x && hy == w->half_y && scale == w->scale)
        return 1;
    /* The snapshot borrows the region bands. A failing mode change has not
     * changed them, and releasing the old buffers never releases regions. */
    old = *w;
    for (i = 0; i <= MM_WORLD_RGB_WORKING; ++i)
        w->owners[i] = NULL;
    memset(&w->background, 0, sizeof(w->background));
    w->rgb.pixels = NULL;
    w->scale = scale;
    if (!build(w, hx, hy, s) || (w->owners[MM_WORLD_CACHE] &&
                                 !mm_world_buffers_restore_background(w, s)))
    {
        release_buffers(w, s);
        *w = old;
        return 0;
    }
    release_buffers(&old, s);
    mm_world_buffers_dirty(w, 1);
    s->changed(s->context);
    return 1;
}
int mm_world_buffers_resolution(MMWorldBuffers *w, MMI32 scale, int reduced,
                                      const MMWorldBufferServices *s)
{
    if (!valid_scale(scale) || (reduced && scale != 1))
        return 0;
    return mm_world_buffers_mode(w, reduced, reduced, scale, s);
}
