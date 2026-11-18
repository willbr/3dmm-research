/* World buffer ownership/control converted from bren/bwld.cpp. */
#include "mm/world_buffers.h"
#include <string.h>
#define MAXIMUM 2147483647
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
        if (kind == MM_WORLD_RGB_WORKING)
            s->unlock(s->context, object);
        s->release(s->context, object);
    }
}
static void release_buffers(MMWorldBuffers *w, const MMWorldBufferServices *s)
{
    release_slot(w, MM_WORLD_RGB_WORKING, s);
    release_slot(w, MM_WORLD_RGB_STRETCH, s);
    release_slot(w, MM_WORLD_RGB_BACKGROUND, s);
    release_slot(w, MM_WORLD_DEPTH_WORKING, s);
    release_slot(w, MM_WORLD_DEPTH_BACKGROUND, s);
}
static int create_slot(MMWorldBuffers *w, int kind, MMI32 width, MMI32 height,
                       const MMWorldBufferServices *s)
{
    release_slot(w, kind, s);
    w->owners[kind] = s->create(s->context, kind, width, height);
    s->publish(s->context, w);
    return w->owners[kind] != NULL;
}
static int build(MMWorldBuffers *w, int hx, int hy, const MMWorldBufferServices *s)
{
    MMI32 width, height;
    MMWorldDepthSpan span;
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
    s->publish(s->context, w);
    if (!create_slot(w, MM_WORLD_DEPTH_BACKGROUND, width, height, s) ||
        !create_slot(w, MM_WORLD_DEPTH_WORKING, width, height, s))
        return 0;
    s->depth_span(s->context, w->owners[MM_WORLD_DEPTH_WORKING], &span);
    w->z.row = width * 2;
    w->z.width = width;
    w->z.height = height;
    w->z.x = width / 2;
    w->z.y = height / 2;
    w->z.type = s->depth_type;
    w->z.pixels = span.pixels;
    s->publish(s->context, w);
    if (!create_slot(w, MM_WORLD_RGB_BACKGROUND, width, height, s) ||
        !create_slot(w, MM_WORLD_RGB_WORKING, width, height, s))
        return 0;
    w->rgb.row = width;
    w->rgb.width = width;
    w->rgb.height = height;
    w->rgb.x = width / 2;
    w->rgb.y = height / 2;
    w->rgb.type = s->rgb_type;
    w->rgb.pixels = s->lock(s->context, w->owners[MM_WORLD_RGB_WORKING]);
    s->publish(s->context, w);
    release_slot(w, MM_WORLD_RGB_STRETCH, s);
    if (w->scale == 1 && !hx && hy && !create_slot(w, MM_WORLD_RGB_STRETCH, w->width, w->height, s))
        return 0;
    s->publish(s->context, w);
    return 1;
}
int mm_world_buffers_init(MMWorldBuffers *w, MMI32 width, MMI32 height, MMI32 scale, int hx,
                                int hy, const MMWorldBufferServices *s)
{
    w->width = width;
    w->height = height;
    w->scale = scale;
    s->publish(s->context, w);
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
    s->publish(s->context, w);
}
int mm_world_depth_resample(const MMWorldDepthSpan *src, const MMWorldDepthSpan *dst, MMI32 width,
                                  MMI32 height, MMI32 scale, int hx, int hy)
{
    MMI32 y, x, sy, sx, n;
    if (width <= 0 || height <= 0 || width > MAXIMUM / 2 || src->row < 0 || dst->row < width * 2 || src->bytes < 0 ||
        dst->bytes < 0 || !src->pixels || !dst->pixels || scale < 1 || scale > 16)
        return 0;
    if (height > MAXIMUM / dst->row || dst->row * height > dst->bytes)
        return 0;
    for (y = 0; y < height; ++y)
    {
        sy = scale > 1 ? y / scale : y * (hy ? 2 : 1);
        if (src->row && sy > src->bytes / src->row)
            return 0;
        n = sy * src->row;
        for (x = 0; x < width; ++x)
        {
            sx = scale > 1 ? x / scale : x * (hx ? 2 : 1);
            if (sx >= src->row / 2 || src->bytes - n < 2 || sx > (src->bytes - n - 2) / 2)
                return 0;
            memcpy(dst->pixels + y * dst->row + x * 2, src->pixels + n + sx * 2, 2);
        }
    }
    return 1;
}
static int scaled(MMWorldBuffers *w, void *bitmap, void *depth, const MMWorldBufferServices *s)
{
    void *full = s->create(s->context, MM_WORLD_RGB_BACKGROUND, w->width, w->height), *native;
    MMWorldDepthSpan src, dst;
    int ok;
    if (!full)
        return 0;
    s->draw_bitmap(s->context, full, bitmap);
    s->copy_rgb(s->context, full, w->owners[MM_WORLD_RGB_BACKGROUND], w->width, w->height, w->buffer_width,
                w->buffer_height);
    s->release(s->context, full);
    native = s->create(s->context, MM_WORLD_DEPTH_BACKGROUND, w->width, w->height);
    if (!native)
        return 0;
    s->draw_depth(s->context, depth, native, w->width, w->height);
    s->depth_span(s->context, native, &src);
    s->depth_span(s->context, w->owners[MM_WORLD_DEPTH_BACKGROUND], &dst);
    ok = mm_world_depth_resample(&src, &dst, w->buffer_width, w->buffer_height, w->scale, 0, 0);
    s->release(s->context, native);
    return ok;
}
int mm_world_buffers_background(MMWorldBuffers *w, void *cache, MMU32 rgb_type, MMU32 rgb_number,
                                      MMU32 z_type, MMU32 z_number, const MMWorldBufferServices *s)
{
    void *bitmap = s->fetch(s->context, cache, MM_WORLD_RGB_BACKGROUND, rgb_type, rgb_number), *depth, *full;
    MMWorldDepthSpan src, dst;
    int ok;
    if (!bitmap)
        return 0;
    depth = s->fetch(s->context, cache, MM_WORLD_DEPTH_BACKGROUND, z_type, z_number);
    if (!depth)
    {
        s->release(s->context, bitmap);
        return 0;
    }
    s->priority(s->context, bitmap);
    s->priority(s->context, depth);
    if (w->scale > 1)
    {
        ok = scaled(w, bitmap, depth, s);
        s->release(s->context, bitmap);
        s->release(s->context, depth);
        if (!ok)
            return 0;
    }
    else if (w->half_x || w->half_y)
    {
        full = s->create(s->context, MM_WORLD_RGB_BACKGROUND, w->width, w->height);
        /* The native path checked the existing destination instead of full. */
        if (!full)
        {
            s->release(s->context, bitmap);
            s->release(s->context, depth);
            return 0;
        }
        s->draw_bitmap(s->context, full, bitmap);
        s->release(s->context, bitmap);
        s->copy_rgb(s->context, full, w->owners[MM_WORLD_RGB_BACKGROUND], w->width, w->height, w->buffer_width,
                    w->buffer_height);
        s->release(s->context, full);
        s->depth_span(s->context, depth, &src);
        s->depth_span(s->context, w->owners[MM_WORLD_DEPTH_BACKGROUND], &dst);
        ok = mm_world_depth_resample(&src, &dst, w->buffer_width, w->buffer_height, 1, w->half_x, w->half_y);
        s->release(s->context, depth);
        if (!ok)
            return 0;
    }
    else
    {
        s->draw_bitmap(s->context, w->owners[MM_WORLD_RGB_BACKGROUND], bitmap);
        s->release(s->context, bitmap);
        release_slot(w, MM_WORLD_DEPTH_BACKGROUND, s);
        w->owners[MM_WORLD_DEPTH_BACKGROUND] = depth;
        s->publish(s->context, w);
    }
    mm_world_buffers_dirty(w, 0);
    s->changed(s->context);
    s->retain(s->context, cache);
    release_slot(w, MM_WORLD_CACHE, s);
    w->owners[MM_WORLD_CACHE] = cache;
    w->rgb_type = rgb_type;
    w->rgb_number = rgb_number;
    w->z_type = z_type;
    w->z_number = z_number;
    s->publish(s->context, w);
    return 1;
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
    for (i = 0; i <= MM_WORLD_RGB_STRETCH; ++i)
        w->owners[i] = NULL;
    w->scale = scale;
    s->publish(s->context, w);
    if (!build(w, hx, hy, s) || (w->owners[MM_WORLD_CACHE] &&
                                 !mm_world_buffers_background(w, w->owners[MM_WORLD_CACHE], w->rgb_type,
                                                                    w->rgb_number, w->z_type, w->z_number, s)))
    {
        release_buffers(w, s);
        *w = old;
        s->publish(s->context, w);
        return 0;
    }
    release_buffers(&old, s);
    mm_world_buffers_dirty(w, 1);
    s->changed(s->context);
    s->publish(s->context, w);
    return 1;
}
int mm_world_buffers_resolution(MMWorldBuffers *w, MMI32 scale, int reduced,
                                      const MMWorldBufferServices *s)
{
    if (!valid_scale(scale) || (reduced && scale != 1))
        return 0;
    return mm_world_buffers_mode(w, reduced, reduced, scale, s);
}
