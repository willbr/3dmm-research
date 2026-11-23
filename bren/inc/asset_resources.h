#ifndef MM_BRENDER_ASSET_RESOURCES_H
#define MM_BRENDER_ASSET_RESOURCES_H

#include "mm/font_resource.h"
#include "mm/material_resource.h"
#include "mm/model_resource.h"
#include "mm/string_value.h"
#include "mm/texture_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native memory, codec, file and diagnostic providers. */
int mm_kauai_resource_name(const MMResourceCache *, MMU32 type, MMU32 number, const MMStringValue *);
const MMFontOwnerHost *mm_kauai_font_host(void);
void mm_kauai_font_missing_model(void *, MMU32 id);
const MMModelOwnerHost *mm_kauai_model_host(void);
void mm_kauai_model_validate(const MMModelOwner *);
const MMMaterialOwnerHost *mm_kauai_material_host(void);
void mm_kauai_material_validate(MMMaterialOwner *);

/* Bind the cache readers and renderer operations before exposing a host. */
void mm_brender_asset_model_bind(MMModelServices *);
void mm_brender_asset_material_bind(MMMaterialServices *, int debug);

void mm_brender_asset_font_bind(MMFontResourceServices *);
int mm_brender_font_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMFontOwner *mm_brender_font_fetch(MMResourceCache *, MMU32 type, MMU32 number);
MMModelOwner *mm_brender_font_glyph(const MMFontOwner *, MMU32 id);

int mm_brender_model_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMModelOwner *mm_brender_model_fetch(MMResourceCache *, MMU32 type, MMU32 number);
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
int mm_brender_material_shade_chunk(MMResourceCache *, MMU32 type, MMU32 number);
void mm_brender_material_shade_clear(void);

int mm_brender_costume_read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
MMMaterialOwner *mm_brender_costume_fetch(MMResourceCache *, MMU32 type, MMU32 number);

#ifdef __cplusplus
}
#endif
#endif
