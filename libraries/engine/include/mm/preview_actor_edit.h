/* Private track edits for live Preview actors. */
#ifndef MM_ENGINE_PREVIEW_ACTOR_EDIT_H
#define MM_ENGINE_PREVIEW_ACTOR_EDIT_H
#include "actor_animation.h"
#ifdef __cplusplus
extern "C"
{
#endif
    struct MMMovieSessionGraph;
    typedef struct MMPreviewActorEditServices
    {
        void *context;
        const MMPreviewActorServices *playback;
        /* Exchange the host's output cue list with private scratch storage. */
        void (*swap_cues)(void *);
        /* Publish the validated draft while its owned tables are still live. */
        int (*write)(void *, const MMActorTrack *, int with_route);
        /* Borrow the source graph for an external template sound. */
        int (*sound_graph)(void *, const MMTag *, struct MMMovieSessionGraph *);
        int (*template_sound)(struct MMTemplateOwner *, MMI32 action, MMI32 cel, int *exists, MMTag *);
        MMI32 (*distance)(const MMActorPoint *);
    } MMPreviewActorEditServices;
    /* Edits run on a private body, motion list and track. The writer runs only
     * after a successful edit and the live actor/cues are restored on every
     * path after cloning. The range is published only after a successful write.
     * Services are synchronous, never throw, and propagate 1/0 or a negative
     * host fault. */
    int mm_preview_actor_write_costumes(MMPreviewActor *, const MMPreviewCostumeEdits *,
                                        const MMPreviewCostumeCatalog *, const MMPreviewActorEditServices *);
    int mm_preview_actor_write_animation(MMPreviewActor *, const MMActorAnimation *, MMActorAnimationRange *,
                                         const MMPreviewActorEditServices *);
#ifdef __cplusplus
}
#endif
#endif
