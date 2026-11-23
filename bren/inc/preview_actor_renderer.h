#ifndef MM_BRENDER_PREVIEW_ACTOR_RENDERER_H
#define MM_BRENDER_PREVIEW_ACTOR_RENDERER_H

#include "mm/preview_actor_edit.h"
#include "mm/preview_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Borrow the binding for one synchronous actor operation. The renderer
 * provides the track, template, material and body effects. The host resolves
 * document-specific resources, binds text fonts and supplies diagnostics.
 * Host callbacks must not unwind and may report a failure through fault. */
typedef struct MMBRenderPreviewActor
{
    const MMPreviewActor *actor;
    const struct MMMovieSessionGraph *graph;
    const MMStorageAllocator *track_allocator;
    int trash_unused;
    void *context;
    MMResourceCache *(*resolve)(void *, MMI32 source, MMU32 type, MMU32 number);
    int (*bind)(void *, struct MMTemplateOwner *);
    int (*fault)(void *);
    void (*diagnostic)(void *, int);
} MMBRenderPreviewActor;

struct MMPreviewLiveProp;
void mm_brender_preview_actor_services(MMBRenderPreviewActor *, MMPreviewActorServices *);
int mm_brender_preview_actor_load(MMPreviewActor *, MMU32 number, MMWide *draw_clock, MMBRenderPreviewActor *);
int mm_brender_preview_prop_create(MMPreviewActor *, const struct MMPreviewLiveProp *, int order, int system,
                                  MMWide *draw_clock, MMBRenderPreviewActor *);
void mm_brender_preview_timeline_services(MMBRenderPreviewActor *, MMActorEffectRenderer *,
                                          MMPreviewTimelineServices *);
void mm_brender_preview_actor_edit_services(MMPreviewActorEditServices *);
MMPreviewCostumeCatalog mm_brender_preview_actor_costumes(void);
int mm_brender_preview_actor_replace_events(MMPreviewActor *, MMI32 source, MMBlockTransform,
                                            const MMPreviewActorServices *);

#ifdef __cplusplus
}
#endif
#endif
