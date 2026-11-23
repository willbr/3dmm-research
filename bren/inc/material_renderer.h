#ifndef MM_BRENDER_MATERIAL_RENDERER_H
#define MM_BRENDER_MATERIAL_RENDERER_H

#include "mm/material_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the renderer operations. The provider resolves texture references
 * to pixelmaps and supplies the native path for imports. */
void mm_brender_material_services(MMMaterialServices *);
struct br_material;
struct br_material *mm_brender_material_handle(MMMaterialOwner *);
MMMaterialOwner *mm_brender_material_owner(const void *handle);

#ifdef __cplusplus
}
#endif
#endif
