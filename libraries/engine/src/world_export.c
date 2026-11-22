/* Bitmap export policy converted from bren/bwld.cpp. */
#include "mm/world_export.h"
#include "mm/pixel_storage.h"
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
/* Sample cached depth rows directly, preserving the original exporter's
 * treatment of stored origins. An absent cache leaves the default fills. */
static int fill_background(const MMWorldBuffers *b, const MMWorldPixels *rgb, const MMDepthBitmap *depth,
                           MMI32 width, MMI32 height, MMI32 scale, const MMWorldBufferServices *s)
{
    void *bitmap = NULL, *source = NULL;
    int ok = 0;
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
    if (!mm_world_fill_background_rgb(s->masked_bitmap(s->context, bitmap), b->width, b->height, rgb, width, height))
        goto cleanup;
    ok = mm_world_depth_resample(s->depth_bitmap(s->context, source), depth, width, height, scale, 0, 0);
cleanup:
    if (bitmap)
        s->release_bitmap(s->context, bitmap);
    if (source)
        s->release_depth(s->context, source);
    return ok;
}
static int write_pixels(void *port, const MMWorldPixels *pixels, MMI32 width, MMI32 height,
                        const MMWorldExportServices *s)
{
    MMWorldPixels locked;
    void *palette = s->palette(s->context);
    int ok = 0;
    if (!palette)
        return 0;
    if (!pixels && s->buffers.lock(s->buffers.context, port, &locked))
        pixels = &locked;
    if (pixels && mm_world_pixels_valid(pixels, width, height))
        ok = s->write(s->context, pixels, palette, width, height);
    if (pixels == &locked)
        s->buffers.unlock(s->buffers.context, port);
    s->buffers.release(s->buffers.context, palette);
    return ok;
}
int mm_world_export_current(const MMWorldBuffers *b, const MMWorldExportServices *s)
{
    return write_pixels(b->owners[MM_WORLD_RGB_WORKING], NULL, b->buffer_width, b->buffer_height, s);
}
int mm_world_export_scaled(const MMWorldBuffers *b, MMI32 requested,
                                 const MMWorldExportServices *s)
{
    MMPixelBuffer rgb = {0};
    MMDepthOwner *depth = NULL;
    MMWorldPixels pixels;
    const MMDepthBitmap *span;
    MMWorldPixelView rgb_view, depth_view;
    MMWorldMaterials materials;
    MMI32 scale, width, height;
    int wire, flat, no_texture, ok = 0, ready;
    const MMWorldBufferServices *buffers = &s->buffers;
    const MMWorldRenderServices *render = &s->render;
    if (!mm_world_export_size(b, requested, &scale, &width, &height))
        return 0;
    if (!mm_pixel_buffer_init(&rgb, width, height, 8, 4))
        return 0;
    depth = mm_depth_owner_new(width, height, NULL, NULL);
    if (!depth)
        goto cleanup;
    span = mm_depth_owner_bitmap(depth);
    pixels.pixels = (unsigned char *)rgb.surface.pixels;
    pixels.row = rgb.surface.pitch;
    pixels.bytes = rgb.bytes;
    if (!fill_background(b, &pixels, span, width, height, scale, buffers))
        goto cleanup;
    render->options(render->context, &wire, &flat, &no_texture);
    mm_world_render_styles(render->frame.world, wire ? render->edges : render->faces, render);
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
    depth_view.pixels = span->pixels;
    s->bounds_callback(s->context, 0);
    render->options(render->context, &wire, &flat, &no_texture);
    ready = mm_world_materials_begin(&materials, flat, no_texture, &render->materials);
    if (ready)
        s->raster(s->context, &render->frame, &rgb_view, &depth_view);
    mm_world_materials_end(&materials, &render->materials);
    s->bounds_callback(s->context, 1);
    render->flush(render->context);
    if (ready)
        ok = write_pixels(NULL, &pixels, width, height, s);
cleanup:
    mm_pixel_buffer_reset(&rgb);
    mm_depth_owner_release(depth);
    return ok;
}
