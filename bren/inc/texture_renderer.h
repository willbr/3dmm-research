#ifndef MM_BRENDER_TEXTURE_RENDERER_H
#define MM_BRENDER_TEXTURE_RENDERER_H

#include "mm/texture_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

void mm_brender_texture_services(MMTextureServices *);
/* The view borrows storage. An imported renderer allocation stays the owner
 * recorded in the resource, including any palette or other renderer fields. */
void mm_brender_texture_publish(void *view, void *owner, const MMTextureResource *);
void mm_brender_texture_sync(MMTextureResource *, const void *view);

#ifdef __cplusplus
}
#endif
#endif
