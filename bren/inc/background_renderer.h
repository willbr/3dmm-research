#ifndef MM_BRENDER_BACKGROUND_RENDERER_H
#define MM_BRENDER_BACKGROUND_RENDERER_H

#include "mm/background_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

struct br_actor;

typedef struct MMBRenderBackgroundHost
{
    MMBackgroundOwnerHost owner;
    MMBlockTransform decode;
    MMI32 cache_bytes;
    void (*invalidate)(void **object, MMI32 *bytes);
} MMBRenderBackgroundHost;

typedef struct MMBRenderBackgroundWorld
{
    void *context;
    void (*add_light)(void *, struct br_actor *);
    int (*images)(void *, MMResourceCache *, MMU32 colour, MMU32 depth);
    void (*camera)(void *, const MMBackgroundCameraData *);
    void (*dirty)(void *);
} MMBRenderBackgroundWorld;

const MMBRenderBackgroundHost *mm_kauai_background_host(void);
void mm_kauai_background_validate(MMBackgroundOwner *);
void mm_kauai_background_mark(MMBackgroundOwner *);
void mm_kauai_background_camera_diagnostic(void *, int issue);
void mm_brender_background_light_services(MMBackgroundServices *);
int mm_brender_background_read(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *);
MMBackgroundOwner *mm_brender_background_fetch(MMResourceCache *, MMU32 type, MMU32 number);
void mm_brender_background_lights_on(MMBackgroundOwner *, const MMBRenderBackgroundWorld *);
int mm_brender_background_select(MMBackgroundOwner *, MMU32 slot, const MMBRenderBackgroundWorld *);

#ifdef __cplusplus
}
#endif
#endif
