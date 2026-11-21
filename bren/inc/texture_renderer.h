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

#ifdef __cplusplus
}
#endif
#endif
