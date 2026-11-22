#include "asset_resources.h"
#include "model_renderer.h"
#include "material_renderer.h"
#include "texture_renderer.h"

static MMMaterialShade shade;

static int native_big_endian(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

void mm_brender_asset_model_bind(MMModelServices *s)
{
    s->host_big_endian = native_big_endian();
    mm_brender_model_services(s);
}

int mm_brender_model_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                          void **out, MMI32 *bytes)
{
    (void)cache;
    (void)type;
    (void)number;
    return mm_model_fetch(block, out, bytes, mm_kauai_model_host());
}

MMModelOwner *mm_brender_model_fetch(MMResourceCache *cache, MMU32 type, MMU32 number)
{
    return mm_resource_cache_fetch_owned_chunk(cache, type, number, mm_brender_model_read, mm_model_owner_services());
}

MMI32 mm_brender_model_extent(MMModelOwner *owner, unsigned axis)
{
    return mm_model_extent(mm_model_owner_resource(owner), axis, mm_kauai_model_host()->renderer);
}

void mm_brender_model_mark(MMModelOwner *owner)
{
    const MMResourceObject *binding;
    if (!owner)
        return;
    mm_model_owner_services()->mark_memory(owner);
    binding = mm_model_owner_binding(owner);
    if (binding->cache && !binding->attached)
        mm_resource_cache_mark(binding->cache);
}

static void *texture_pixelmap(void *context, void *texture)
{
    (void)context;
    return mm_brender_texture_view(texture);
}

static void *input_material(void *context, const void *input, MMI32 index)
{
    (void)context;
    return ((MMMaterialOwner *const *)input)[index];
}

static void *material_handle(void *context, void *material)
{
    (void)context;
    return mm_material_owner_resource(material)->handle;
}

void mm_brender_asset_material_bind(MMMaterialServices *s, int debug)
{
    s->host_big_endian = native_big_endian();
    s->texture_type = 0x544d4150UL;
    s->transform_type = 0x54585846UL;
    /* Historic native cache charges for unpacked MTRL and CMTL. */
    s->material_bytes = sizeof(void *) == 8 ? 48 : 32;
    s->custom_bytes = sizeof(void *) == 8 ? 64 : 44;
    if (debug && sizeof(void *) == 4)
    {
        s->material_bytes += 4;
        s->custom_bytes += 4;
    }
    mm_brender_material_services(s);
    s->texture_objects = mm_texture_owner_services();
    s->material_reader = mm_brender_material_read;
    s->material_objects = mm_material_owner_services();
    s->model_reader = mm_brender_model_read;
    s->model_objects = mm_model_owner_services();
    s->texture_pixelmap = texture_pixelmap;
    s->input_material = input_material;
    s->material_handle = material_handle;
}

int mm_brender_material_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                             void **out, MMI32 *bytes)
{
    return mm_material_fetch(cache, type, number, block, out, bytes, mm_kauai_material_host());
}

MMMaterialOwner *mm_brender_material_fetch(MMResourceCache *cache, MMU32 type, MMU32 number)
{
    return mm_resource_cache_fetch_owned_chunk(cache, type, number, mm_brender_material_read,
                                               mm_material_owner_services());
}

MMMaterialOwner *mm_brender_material_new(MMI32 base, MMI32 range)
{
    return mm_material_new(base, range, mm_kauai_material_host());
}

void mm_brender_material_mark(MMMaterialOwner *owner)
{
    if (owner)
        mm_material_owner_services()->mark_memory(owner);
}

void mm_brender_material_read_view(MMMaterialOwner *owner, MMMaterialAppearance *out)
{
    mm_material_read_view(mm_material_owner_resource(owner), out, mm_kauai_material_host()->services);
}

int mm_brender_material_write_view(MMMaterialOwner *owner, const MMArchiveWriter *writer, MMU32 type,
                                   MMU32 *number, const MMMaterialAppearance *appearance)
{
    return mm_material_write_view(mm_material_owner_resource(owner), writer, type, number, appearance,
                                  mm_kauai_material_host()->services);
}

const MMMaterialShade *mm_brender_material_shade(void)
{
    return &shade;
}

int mm_brender_material_shade_take(MMTextureOwner *texture)
{
    return mm_material_shade_set(&shade, texture, mm_kauai_material_host()->services);
}

int mm_brender_material_shade_stream(const MMReadStream *stream)
{
    return mm_brender_material_shade_take(mm_kauai_asset_texture_stream(stream));
}

void mm_brender_material_shade_clear(void)
{
    mm_material_shade_dispose(&shade, mm_kauai_material_host()->services);
}

int mm_brender_costume_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                            void **out, MMI32 *bytes)
{
    return mm_custom_material_fetch(cache, type, number, block, out, bytes, mm_kauai_material_host());
}

MMMaterialOwner *mm_brender_costume_fetch(MMResourceCache *cache, MMU32 type, MMU32 number)
{
    return mm_resource_cache_fetch_owned_chunk(cache, type, number, mm_brender_costume_read,
                                               mm_material_owner_services());
}
