/* World buffer ownership/control converted from bren/bwld.cpp. */
#include "three_mm/world_buffers.h"
#include <string.h>
#define MAXIMUM 2147483647
static int valid_scale(ThreeMMI32 scale)
{
    return scale == 1 || scale == 2 || scale == 4;
}
static void release_slot(ThreeMMWorldBuffers *w, int kind, const ThreeMMWorldBufferServices *s)
{
    void *object = w->owners[kind];
    w->owners[kind] = NULL;
    if (object)
    {
        if (kind == THREE_MM_WORLD_RGB_WORKING)
            s->unlock(s->context, object);
        s->release(s->context, object);
    }
}
static void release_buffers(ThreeMMWorldBuffers *w, const ThreeMMWorldBufferServices *s)
{
    release_slot(w, THREE_MM_WORLD_RGB_WORKING, s);
    release_slot(w, THREE_MM_WORLD_RGB_STRETCH, s);
    release_slot(w, THREE_MM_WORLD_RGB_BACKGROUND, s);
    release_slot(w, THREE_MM_WORLD_DEPTH_WORKING, s);
    release_slot(w, THREE_MM_WORLD_DEPTH_BACKGROUND, s);
}
static int create_slot(ThreeMMWorldBuffers *w, int kind, ThreeMMI32 width, ThreeMMI32 height,
                       const ThreeMMWorldBufferServices *s)
{
    release_slot(w, kind, s);
    w->owners[kind] = s->create(s->context, kind, width, height);
    s->publish(s->context, w);
    return w->owners[kind] != NULL;
}
static int build(ThreeMMWorldBuffers *w, int hx, int hy, const ThreeMMWorldBufferServices *s)
{
    ThreeMMI32 width, height;
    ThreeMMWorldDepthSpan span;
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
    if (!create_slot(w, THREE_MM_WORLD_DEPTH_BACKGROUND, width, height, s) ||
        !create_slot(w, THREE_MM_WORLD_DEPTH_WORKING, width, height, s))
        return 0;
    s->depth_span(s->context, w->owners[THREE_MM_WORLD_DEPTH_WORKING], &span);
    w->z.row = width * 2;
    w->z.width = width;
    w->z.height = height;
    w->z.x = width / 2;
    w->z.y = height / 2;
    w->z.type = s->depth_type;
    w->z.pixels = span.pixels;
    s->publish(s->context, w);
    if (!create_slot(w, THREE_MM_WORLD_RGB_BACKGROUND, width, height, s) ||
        !create_slot(w, THREE_MM_WORLD_RGB_WORKING, width, height, s))
        return 0;
    w->rgb.row = width;
    w->rgb.width = width;
    w->rgb.height = height;
    w->rgb.x = width / 2;
    w->rgb.y = height / 2;
    w->rgb.type = s->rgb_type;
    w->rgb.pixels = s->lock(s->context, w->owners[THREE_MM_WORLD_RGB_WORKING]);
    s->publish(s->context, w);
    release_slot(w, THREE_MM_WORLD_RGB_STRETCH, s);
    if (w->scale == 1 && !hx && hy && !create_slot(w, THREE_MM_WORLD_RGB_STRETCH, w->width, w->height, s))
        return 0;
    s->publish(s->context, w);
    return 1;
}
int three_mm_world_buffers_init(ThreeMMWorldBuffers *w, ThreeMMI32 width, ThreeMMI32 height, ThreeMMI32 scale, int hx,
                                int hy, const ThreeMMWorldBufferServices *s)
{
    w->width = width;
    w->height = height;
    w->scale = scale;
    s->publish(s->context, w);
    if (!build(w, hx, hy, s))
        return 0;
    s->setup_scene(s->context);
    if (!create_slot(w, THREE_MM_WORLD_DIRTY_WORKING, w->buffer_width, w->buffer_height, s) ||
        !create_slot(w, THREE_MM_WORLD_DIRTY_SCREEN, 0, 0, s))
        return 0;
    return 1;
}
void three_mm_world_buffers_dispose(ThreeMMWorldBuffers *w, const ThreeMMWorldBufferServices *s)
{
    release_buffers(w, s);
    release_slot(w, THREE_MM_WORLD_DIRTY_WORKING, s);
    release_slot(w, THREE_MM_WORLD_DIRTY_SCREEN, s);
    release_slot(w, THREE_MM_WORLD_CACHE, s);
    w->rgb.pixels = w->z.pixels = NULL;
    s->publish(s->context, w);
}
static int resample(const ThreeMMWorldDepthSpan *src, const ThreeMMWorldDepthSpan *dst, ThreeMMI32 width,
                    ThreeMMI32 height, ThreeMMI32 scale, int hx, int hy)
{
    ThreeMMI32 y, x, sy, sx, n;
    if (width <= 0 || height <= 0 || width > MAXIMUM / 2 || src->row < 0 || dst->row < width * 2 || src->bytes < 0 ||
        dst->bytes < 0 || !src->pixels || !dst->pixels || !valid_scale(scale))
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
static int scaled(ThreeMMWorldBuffers *w, void *bitmap, void *depth, const ThreeMMWorldBufferServices *s)
{
    void *full = s->create(s->context, THREE_MM_WORLD_RGB_BACKGROUND, w->width, w->height), *native;
    ThreeMMWorldDepthSpan src, dst;
    int ok;
    if (!full)
        return 0;
    s->draw_bitmap(s->context, full, bitmap);
    s->copy_rgb(s->context, full, w->owners[THREE_MM_WORLD_RGB_BACKGROUND], w->width, w->height, w->buffer_width,
                w->buffer_height);
    s->release(s->context, full);
    native = s->create(s->context, THREE_MM_WORLD_DEPTH_BACKGROUND, w->width, w->height);
    if (!native)
        return 0;
    s->draw_depth(s->context, depth, native, w->width, w->height);
    s->depth_span(s->context, native, &src);
    s->depth_span(s->context, w->owners[THREE_MM_WORLD_DEPTH_BACKGROUND], &dst);
    ok = resample(&src, &dst, w->buffer_width, w->buffer_height, w->scale, 0, 0);
    s->release(s->context, native);
    return ok;
}
int three_mm_world_buffers_background(ThreeMMWorldBuffers *w, void *cache, ThreeMMU32 rgb_type, ThreeMMU32 rgb_number,
                                      ThreeMMU32 z_type, ThreeMMU32 z_number, const ThreeMMWorldBufferServices *s)
{
    void *bitmap = s->fetch(s->context, cache, THREE_MM_WORLD_RGB_BACKGROUND, rgb_type, rgb_number), *depth, *full;
    ThreeMMWorldDepthSpan src, dst;
    int ok;
    if (!bitmap)
        return 0;
    depth = s->fetch(s->context, cache, THREE_MM_WORLD_DEPTH_BACKGROUND, z_type, z_number);
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
        full = s->create(s->context, THREE_MM_WORLD_RGB_BACKGROUND, w->width, w->height);
        /* The native path checked the existing destination instead of full. */
        if (!full)
        {
            s->release(s->context, bitmap);
            s->release(s->context, depth);
            return 0;
        }
        s->draw_bitmap(s->context, full, bitmap);
        s->release(s->context, bitmap);
        s->copy_rgb(s->context, full, w->owners[THREE_MM_WORLD_RGB_BACKGROUND], w->width, w->height, w->buffer_width,
                    w->buffer_height);
        s->release(s->context, full);
        s->depth_span(s->context, depth, &src);
        s->depth_span(s->context, w->owners[THREE_MM_WORLD_DEPTH_BACKGROUND], &dst);
        ok = resample(&src, &dst, w->buffer_width, w->buffer_height, 1, w->half_x, w->half_y);
        s->release(s->context, depth);
        if (!ok)
            return 0;
    }
    else
    {
        s->draw_bitmap(s->context, w->owners[THREE_MM_WORLD_RGB_BACKGROUND], bitmap);
        s->release(s->context, bitmap);
        release_slot(w, THREE_MM_WORLD_DEPTH_BACKGROUND, s);
        w->owners[THREE_MM_WORLD_DEPTH_BACKGROUND] = depth;
        s->publish(s->context, w);
    }
    s->dirty(s->context, 1, 0, 1);
    s->retain(s->context, cache);
    release_slot(w, THREE_MM_WORLD_CACHE, s);
    w->owners[THREE_MM_WORLD_CACHE] = cache;
    w->rgb_type = rgb_type;
    w->rgb_number = rgb_number;
    w->z_type = z_type;
    w->z_number = z_number;
    s->publish(s->context, w);
    return 1;
}
int three_mm_world_buffers_mode(ThreeMMWorldBuffers *w, int hx, int hy, ThreeMMI32 scale,
                                const ThreeMMWorldBufferServices *s)
{
    ThreeMMWorldBuffers old;
    unsigned i;
    if (!valid_scale(scale))
        return 0;
    hx = hx != 0;
    hy = hy != 0;
    if (hx == w->half_x && hy == w->half_y && scale == w->scale)
        return 1;
    old = *w;
    for (i = 0; i <= THREE_MM_WORLD_RGB_STRETCH; ++i)
        w->owners[i] = NULL;
    w->scale = scale;
    s->publish(s->context, w);
    if (!build(w, hx, hy, s) || (w->owners[THREE_MM_WORLD_CACHE] &&
                                 !three_mm_world_buffers_background(w, w->owners[THREE_MM_WORLD_CACHE], w->rgb_type,
                                                                    w->rgb_number, w->z_type, w->z_number, s)))
    {
        release_buffers(w, s);
        *w = old;
        s->publish(s->context, w);
        return 0;
    }
    release_buffers(&old, s);
    s->dirty(s->context, 1, 1, 1);
    s->publish(s->context, w);
    return 1;
}
int three_mm_world_buffers_resolution(ThreeMMWorldBuffers *w, ThreeMMI32 scale, int reduced,
                                      const ThreeMMWorldBufferServices *s)
{
    if (!valid_scale(scale) || (reduced && scale != 1))
        return 0;
    return three_mm_world_buffers_mode(w, reduced, reduced, scale, s);
}
