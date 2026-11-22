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
    MMMaskedOwner *bitmap;
    MMDepthOwner *source;
    int ok = 0;
    if (!b->cache)
        return 1;
    bitmap = mm_resource_cache_fetch_owned_chunk(b->cache, b->rgb_type, b->rgb_number, s->read_colour,
                                                 mm_masked_owner_services());
    if (!bitmap)
        return 0;
    source = mm_resource_cache_fetch_owned_chunk(b->cache, b->z_type, b->z_number, s->read_depth,
                                                 mm_depth_owner_services());
    if (source && mm_world_fill_background_rgb(mm_masked_owner_bitmap(bitmap), b->width, b->height, rgb, width, height))
        ok = mm_world_depth_resample(mm_depth_owner_bitmap(source), depth, width, height, scale, 0, 0);
    mm_masked_owner_release(bitmap);
    mm_depth_owner_release(source);
    return ok;
}
static int write_pixels(const MMWorldPixels *pixels, MMI32 width, MMI32 height,
                        const MMWorldExportServices *s)
{
    void *palette = s->palette(s->context);
    int ok = 0;
    if (!palette)
        return 0;
    if (mm_world_pixels_valid(pixels, width, height))
        ok = s->write(s->context, pixels, palette, width, height);
    s->release_palette(s->context, palette);
    return ok;
}
int mm_world_export_current(const MMWorldBuffers *b, const MMWorldExportServices *s)
{
    MMWorldPixels pixels = mm_world_buffer_pixels(&b->working);
    return write_pixels(&pixels, b->buffer_width, b->buffer_height, s);
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
    pixels = mm_world_buffer_pixels(&rgb);
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
        ok = write_pixels(&pixels, width, height, s);
cleanup:
    mm_pixel_buffer_reset(&rgb);
    mm_depth_owner_release(depth);
    return ok;
}
