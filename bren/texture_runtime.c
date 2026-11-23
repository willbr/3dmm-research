#include "texture_renderer.h"

void mm_brender_texture_host_init(MMBRenderTextureHost *host, const MMTextureServices *native,
                                  const MMStorageAllocator *objects, void (*mark_memory)(void *))
{
    const unsigned short order = 1;
    host->services = *native;
    host->services.host_big_endian = *(const unsigned char *)&order == 0;
    mm_brender_texture_services(&host->services);
    host->owner.services = &host->services;
    host->owner.objects = objects;
    host->owner.object_bytes = mm_brender_texture_owner_size();
    host->owner.mark_memory = mark_memory;
}

int mm_brender_texture_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                            void **out, MMI32 *bytes)
{
    (void)cache;
    (void)type;
    (void)number;
    return mm_texture_fetch(block, out, bytes, mm_kauai_texture_host());
}

MMTextureOwner *mm_brender_texture_from_chunk(void *file, MMU32 type, MMU32 number)
{
    return mm_texture_read(file, type, number, mm_kauai_texture_host());
}

MMTextureOwner *mm_brender_texture_from_stream(const MMReadStream *stream)
{
    return mm_texture_read_stream(stream, mm_kauai_texture_host());
}

MMTextureOwner *mm_brender_texture_from_bitmap(void *filename, void *palette)
{
    return mm_texture_bitmap(filename, palette, mm_kauai_texture_host());
}

MMTextureOwner *mm_brender_texture_wrap(void *pixelmap)
{
    return mm_texture_wrap(pixelmap, mm_kauai_texture_host());
}

int mm_brender_texture_write_chunk(MMTextureOwner *owner, const MMChunkEdit *edit,
                                    const MMBlockFileServices *files, MMU32 type, MMU32 *number)
{
    MMTextureServices services = *mm_kauai_texture_host()->services;
    MMChunkPayloadServices payloads = *services.payloads;
    payloads.files = files;
    services.payloads = &payloads;
    mm_kauai_texture_validate(owner);
    return mm_texture_write_chunk(mm_texture_owner_resource(owner), edit, type, number, &services);
}

void mm_brender_texture_mark(MMTextureOwner *owner)
{
    if (owner)
    {
        mm_kauai_texture_validate(owner);
        mm_texture_owner_services()->mark_memory(owner);
        if (owner->binding.cache && !owner->binding.attached)
            mm_resource_cache_mark(owner->binding.cache);
    }
}
