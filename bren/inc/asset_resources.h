#ifndef MM_BRENDER_ASSET_RESOURCES_H
#define MM_BRENDER_ASSET_RESOURCES_H

#include "mm/material_resource.h"
#include "mm/model_resource.h"
#include "mm/texture_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native memory, codec, file and diagnostic providers. */
const MMModelOwnerHost *mm_kauai_model_host(void);
const MMMaterialOwnerHost *mm_kauai_material_host(void);
MMTextureOwner *mm_kauai_asset_texture_stream(const MMReadStream *);
void mm_kauai_material_validate(MMMaterialOwner *);

/* Bind the cache readers and renderer operations before exposing a host. */
void mm_brender_asset_model_bind(MMModelServices *);
void mm_brender_asset_material_bind(MMMaterialServices *, int debug);

int mm_brender_model_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMModelOwner *mm_brender_model_fetch(MMResourceCache *, MMU32 type, MMU32 number);
MMI32 mm_brender_model_extent(MMModelOwner *, unsigned axis);
void mm_brender_model_mark(MMModelOwner *);

int mm_brender_material_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMMaterialOwner *mm_brender_material_fetch(MMResourceCache *, MMU32 type, MMU32 number);
MMMaterialOwner *mm_brender_material_new(MMI32 base, MMI32 range);
void mm_brender_material_mark(MMMaterialOwner *);
void mm_brender_material_read_view(MMMaterialOwner *, MMMaterialAppearance *);
int mm_brender_material_write_view(MMMaterialOwner *, const MMArchiveWriter *, MMU32 type, MMU32 *number,
                                   const MMMaterialAppearance *);

const MMMaterialShade *mm_brender_material_shade(void);
int mm_brender_material_shade_take(MMTextureOwner *);
int mm_brender_material_shade_stream(const MMReadStream *);
void mm_brender_material_shade_clear(void);

int mm_brender_costume_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMMaterialOwner *mm_brender_costume_fetch(MMResourceCache *, MMU32 type, MMU32 number);

#ifdef __cplusplus
}
#endif
#endif
