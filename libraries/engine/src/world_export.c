/* Bitmap export policy converted from bren/bwld.cpp. */
#include "mm/world_export.h"
#include <string.h>
int mm_world_export_size(const MMWorldBuffers *b, MMI32 requested, MMI32 *scale, MMI32 *width,
                               MMI32 *height)
{
    if (b->width <= 0 || b->height <= 0 || b->width > 4096 || b->height > 4096 || requested < 1 || requested > 16)
        return 0;
    while (requested > 1 && (b->width > 4096 / requested || b->height > 4096 / requested))
        --requested;
    *scale = requested;
    *width = b->width * requested;
    *height = b->height * requested;
    return 1;
}
int mm_world_export_background(const MMWorldBuffers *b, void *rgb, void *depth, MMI32 scale,
                                     const MMWorldBufferServices *s)
{
    void *bitmap = NULL, *source = NULL, *full = NULL;
    MMWorldDepthSpan src, dst;
    MMI32 chosen, width, height;
    int ok = 0;
    if (!mm_world_export_size(b, scale, &chosen, &width, &height) || chosen != scale)
        return 0;
    if (!b->owners[MM_WORLD_CACHE])
        return 1;
    bitmap = s->fetch(s->context, b->owners[MM_WORLD_CACHE], MM_WORLD_RGB_BACKGROUND, b->rgb_type,
                      b->rgb_number);
    if (!bitmap)
        goto cleanup;
    source =
        s->fetch(s->context, b->owners[MM_WORLD_CACHE], MM_WORLD_DEPTH_BACKGROUND, b->z_type, b->z_number);
    if (!source)
        goto cleanup;
    full = s->create(s->context, MM_WORLD_RGB_BACKGROUND, b->width, b->height);
    if (!full)
        goto cleanup;
    s->draw_bitmap(s->context, full, bitmap);
    s->copy_rgb(s->context, full, rgb, b->width, b->height, width, height);
    s->depth_span(s->context, source, &src);
    s->depth_span(s->context, depth, &dst);
    ok = mm_world_depth_resample(&src, &dst, width, height, scale, 0, 0);
cleanup:
    if (bitmap)
        s->release_bitmap(s->context, bitmap);
    if (source)
        s->release_depth(s->context, source);
    if (full)
        s->release(s->context, full);
    return ok;
}
static int valid_pixels(const MMWorldPixels *p, MMI32 width, MMI32 height)
{
    return width > 0 && height > 0 && width <= 4096 && height <= 4096 && p->pixels && p->row >= width &&
           (size_t)height <= (size_t)-1 / (size_t)p->row && p->bytes >= (size_t)p->row * (size_t)height;
}
static int write_pixels(void *port, MMI32 width, MMI32 height, const MMWorldExportServices *s)
{
    MMWorldPixels pixels;
    void *palette = s->palette(s->context);
    int ok = 0;
    if (!palette)
        return 0;
    if (s->lock(s->context, port, &pixels))
    {
        if (valid_pixels(&pixels, width, height))
            ok = s->write(s->context, &pixels, palette, width, height);
        s->buffers.unlock(s->buffers.context, port);
    }
    s->buffers.release(s->buffers.context, palette);
    return ok;
}
int mm_world_export_current(const MMWorldBuffers *b, const MMWorldExportServices *s)
{
    return write_pixels(b->owners[MM_WORLD_RGB_WORKING], b->buffer_width, b->buffer_height, s);
}
int mm_world_export_scaled(const MMWorldBuffers *b, MMI32 requested,
                                 const MMWorldExportServices *s)
{
    void *rgb = NULL, *depth = NULL;
    MMWorldPixels pixels;
    MMWorldDepthSpan span;
    MMWorldPixelView rgb_view, depth_view;
    MMWorldMaterials materials;
    MMI32 scale, width, height;
    int wire, flat, no_texture, ok = 0, ready;
    const MMWorldBufferServices *buffers = &s->buffers;
    const MMWorldRenderServices *render = &s->render;
    if (!mm_world_export_size(b, requested, &scale, &width, &height))
        return 0;
    rgb = buffers->create(buffers->context, MM_WORLD_RGB_WORKING, width, height);
    if (!rgb)
        goto cleanup;
    depth = buffers->create(buffers->context, MM_WORLD_DEPTH_WORKING, width, height);
    if (!depth || !mm_world_export_background(b, rgb, depth, scale, buffers))
        goto cleanup;
    render->options(render->context, &wire, &flat, &no_texture);
    mm_world_render_styles(render->frame.world, wire ? render->edges : render->faces, render);
    if (!s->lock(s->context, rgb, &pixels))
        goto cleanup;
    buffers->depth_span(buffers->context, depth, &span);
    if (!valid_pixels(&pixels, width, height) || !span.pixels || span.row != width * 2 ||
        span.bytes < span.row * height)
    {
        buffers->unlock(buffers->context, rgb);
        goto cleanup;
    }
    memset(&rgb_view, 0, sizeof(rgb_view));
    rgb_view.row = width;
    rgb_view.width = width;
    rgb_view.height = height;
    rgb_view.x = width / 2;
    rgb_view.y = height / 2;
    rgb_view.type = b->rgb.type;
    rgb_view.pixels = pixels.pixels;
    depth_view = rgb_view;
    depth_view.row = width * 2;
    depth_view.type = b->z.type;
    depth_view.pixels = span.pixels;
    s->bounds_callback(s->context, 0);
    render->options(render->context, &wire, &flat, &no_texture);
    ready = mm_world_materials_begin(&materials, flat, no_texture, &render->materials);
    if (ready)
        s->raster(s->context, &render->frame, &rgb_view, &depth_view);
    mm_world_materials_end(&materials, &render->materials);
    s->bounds_callback(s->context, 1);
    render->flush(render->context);
    buffers->unlock(buffers->context, rgb);
    if (ready)
        ok = write_pixels(rgb, width, height, s);
cleanup:
    if (rgb)
        buffers->release(buffers->context, rgb);
    if (depth)
        buffers->release_depth(buffers->context, depth);
    return ok;
}
