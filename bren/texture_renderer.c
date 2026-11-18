#include "texture_renderer.h"
#include "brender.h"

static void describe_pixelmap(const br_pixelmap *pixelmap, MMTextureDescription *out)
{
    out->row_bytes = pixelmap->row_bytes;
    out->width = pixelmap->width;
    out->height = pixelmap->height;
    out->base_x = pixelmap->base_x;
    out->base_y = pixelmap->base_y;
    out->origin_x = pixelmap->origin_x;
    out->origin_y = pixelmap->origin_y;
    out->type = pixelmap->type;
    out->flags = pixelmap->flags;
}

static void describe_import(void *context, void *handle, MMTextureDescription *out, void **pixels)
{
    const br_pixelmap *pixelmap = (const br_pixelmap *)handle;
    (void)context;
    describe_pixelmap(pixelmap, out);
    *pixels = pixelmap->pixels;
}

static void free_import(void *context, void *handle)
{
    (void)context;
    BrPixelmapFree((br_pixelmap *)handle);
}

void mm_brender_texture_services(MMTextureServices *s)
{
    s->indexed_type = BR_PMT_INDEX_8;
    s->linear_flag = BR_PMF_LINEAR;
    s->describe_import = describe_import;
    s->free_import = free_import;
}

void mm_brender_texture_publish(void *view, void *owner, const MMTextureResource *resource)
{
    br_pixelmap *pixelmap = (br_pixelmap *)view;
    br_pixelmap *imported = (br_pixelmap *)resource->imported_owner;
    const MMTextureDescription *d = &resource->description;
    if (imported)
        *pixelmap = *imported;
    pixelmap->identifier = (char *)owner;
    pixelmap->pixels = resource->pixels;
    pixelmap->row_bytes = (br_int_16)d->row_bytes;
    pixelmap->width = (br_uint_16)d->width;
    pixelmap->height = (br_uint_16)d->height;
    pixelmap->base_x = (br_uint_16)d->base_x;
    pixelmap->base_y = (br_uint_16)d->base_y;
    pixelmap->origin_x = (br_int_16)d->origin_x;
    pixelmap->origin_y = (br_int_16)d->origin_y;
    pixelmap->type = (br_uint_8)d->type;
    pixelmap->flags = (br_uint_8)d->flags;
    if (imported)
        imported->identifier = (char *)owner;
}

void mm_brender_texture_sync(MMTextureResource *resource, const void *view)
{
    const br_pixelmap *pixelmap = (const br_pixelmap *)view;
    describe_pixelmap(pixelmap, &resource->description);
    resource->pixels = pixelmap->pixels;
}
