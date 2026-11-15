/* Preview metadata replay and presentation cue traversal. */
#ifndef MM_ENGINE_PREVIEW_METADATA_H
#define MM_ENGINE_PREVIEW_METADATA_H
#include "preview_actor.h"
#include "collections.h"
#include "wide.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_PREVIEW_MARK_ENTRANCE,
        MM_PREVIEW_MARK_EXIT,
        MM_PREVIEW_MARK_ACTION,
        MM_PREVIEW_MARK_ROTATION,
        MM_PREVIEW_MARK_SCALE,
        MM_PREVIEW_MARK_STRETCH,
        MM_PREVIEW_MARK_MOVE,
        MM_PREVIEW_MARK_PATH,
        MM_PREVIEW_MARK_COSTUME,
        MM_PREVIEW_MARK_STEP,
        MM_PREVIEW_MARK_FREEZE
    };
    typedef struct MMPreviewMarker
    {
        MMI32 frame, kind, values[4];
        MMU32 number;
    } MMPreviewMarker;
    typedef struct MMPreviewSpan
    {
        MMWide first, end;
        MMI32 action;
        int frozen;
    } MMPreviewSpan;
    typedef struct MMPreviewTimelineServices
    {
        void *context;
        MMActorTimelineServices timeline;
        void (*reset)(void *);
        int (*clone)(void *, void **owner, void **route, void **events);
        void (*release)(void *, void *owner);
        int (*apply)(void *, MMActorState *, void *owner, MMI32 index);
        int (*marker)(void *, const MMPreviewMarker *), (*span)(void *, const MMPreviewSpan *);
        void (*extend)(void *, MMWide end);
        int (*fault)(void *);
    } MMPreviewTimelineServices;
    typedef struct MMPreviewCue
    {
        MMI32 frame, source, actor, style, volume, motion_cel;
        MMU32 type, number, scene_type, scene_number, child;
        int actor_sound, loop, queue, silent, motion;
    } MMPreviewCue;
    void mm_preview_actor_cue(MMPreviewCue *, const MMActorSound *, MMI32 frame, MMI32 actor);
    typedef struct MMPreviewCueGroup
    {
        void *handle;
        MMU32 count;
        int kind, swap;
    } MMPreviewCueGroup;
    enum
    {
        MM_PREVIEW_CUE_FRAMES,
        MM_PREVIEW_CUE_STARTS,
        MM_PREVIEW_CUE_ACTOR
    };
    typedef struct MMPreviewPresentationServices
    {
        void *context;
        void (*reset)(void *), (*transition)(void *, MMI32);
        int (*header)(void *, MMU32 scene, MMI32 *transition);
        int (*group)(void *, MMU32 number, int kind, MMPreviewCueGroup *);
        void (*release)(void *, MMPreviewCueGroup *);
        void (*event)(void *, const MMPreviewCueGroup *, MMU32 row, MMI32 *kind, MMI32 *frame);
        MMU32 (*word)(void *, const MMPreviewCueGroup *, MMU32 row, MMU32 word_index);
        int (*child)(void *, MMU32 scene, MMU32 slot, int actor, MMU32 *number);
        int (*actor_header)(void *, MMU32, MMActorHeader *);
        void (*actor_sound)(void *, const MMPreviewCueGroup *, MMU32, MMActorSound *);
        int (*caption)(void *, MMU32);
        int (*pause)(void *, MMI32 frame, MMI32 kind, MMI32 milliseconds);
        int (*cue)(void *, const MMPreviewCue *);
    } MMPreviewPresentationServices;
    typedef struct MMPreviewCueOrder
    {
        void *context;
        MMU32 count;
        const MMStorageAllocator *allocator;
        void (*get)(void *, MMU32, MMPreviewCue *);
        void (*put)(void *, MMU32, const MMPreviewCue *);
    } MMPreviewCueOrder;
    /* Synchronous borrowed services never throw; effects return 1/0, -1 allocation,
     * -2 other exception. Completed output prefixes survive failure. C always releases
     * private track/group owners, including exceptions. Metadata replay never mutates
     * the live actor; shared evaluators mutate only the clone's event frame caches.
     * The replay budget is last-actor_first <=100000. Marker budget is checked before
     * classification, including ignored sound rows once 100000 markers exist. Span
     * ends use two-word integers so INT32_MAX+1 remains representable. Label/font and
     * numeric display formatting are typed output services. Cue sorting sets scene
     * identity first, is stable by frame then scene-before-actor, and falls back to
     * allocation-free stable merging after scratch failure. Getters cannot mutate/reenter.
     * Scene event and actor groups are already layout-validated by their readers. */
    int mm_preview_actor_timeline(const MMPreviewActor *, MMI32 first, MMI32 last, const MMPreviewTimelineServices *);
    int mm_preview_presentation(MMU32 scene, int sound_sheet, int include_captions,
                                const MMPreviewPresentationServices *);
    void mm_preview_cues_sort(MMU32 scene_type, MMU32 scene_number, const MMPreviewCueOrder *);
#ifdef __cplusplus
}
#endif
#endif
