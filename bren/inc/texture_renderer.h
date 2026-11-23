#ifndef MM_BRENDER_TEXTURE_RENDERER_H
#define MM_BRENDER_TEXTURE_RENDERER_H

#include "mm/texture_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

void mm_brender_texture_services(MMTextureServices *);
/* The C owner and its BRender view share one allocation. The view borrows
 * pixels and preserves imported renderer fields such as the palette. */
MMU32 mm_brender_texture_owner_size(void);
void *mm_brender_texture_view(MMTextureOwner *);

typedef struct MMBRenderTextureHost
{
    MMTextureServices services;
    MMTextureOwnerHost owner;
} MMBRenderTextureHost;

/* The host supplies memory, file, bitmap and diagnostics effects. Keep the
 * initialized binding at a stable address for the lifetime of its maps. */
void mm_brender_texture_host_init(MMBRenderTextureHost *, const MMTextureServices *, const MMStorageAllocator *,
                                  void (*mark_memory)(void *));
const MMTextureOwnerHost *mm_kauai_texture_host(void);
void mm_kauai_texture_validate(MMTextureOwner *);

/* The cache uses this reader's address as the TMAP reader identity. */
int mm_brender_texture_read(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *);
MMTextureOwner *mm_brender_texture_from_chunk(void *file, MMU32 type, MMU32 number);
MMTextureOwner *mm_brender_texture_from_stream(const MMReadStream *);
MMTextureOwner *mm_brender_texture_from_bitmap(void *filename, void *palette);
MMTextureOwner *mm_brender_texture_wrap(void *pixelmap);
int mm_brender_texture_write_chunk(MMTextureOwner *, const MMChunkEdit *, const MMBlockFileServices *,
                                    MMU32 type, MMU32 *number);
void mm_brender_texture_mark(MMTextureOwner *);

#ifdef __cplusplus
}
#endif
#endif
