#ifndef MM_BRENDER_SCENE_PICK_H
#define MM_BRENDER_SCENE_PICK_H

#include "mm/render_frame.h"
#include "mm/world_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hits follow the SDK traversal order. A nonzero visitor result stops the
 * scan. Actor handles are borrowed for the duration of the synchronous call. */
typedef int MMBRenderActorHit(void *context, void *actor, MMI32 near_z);
struct MMPreviewPickServices;

void mm_brender_world_visit(const MMWorldBuffers *, const MMRenderFrame *, MMI32 x, MMI32 y,
                           MMBRenderActorHit *, void *context);
void *mm_brender_world_pick(MMWorldCamera *, const MMWorldBuffers *, const MMRenderFrame *, MMI32 x, MMI32 y);
void mm_brender_preview_pick_services(struct MMPreviewPickServices *);

#ifdef __cplusplus
}
#endif
#endif
