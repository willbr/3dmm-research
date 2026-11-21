/* Preview actor ownership and playback control over the shared actor evaluators. */
#ifndef MM_ENGINE_PREVIEW_ACTOR_H
#define MM_ENGINE_PREVIEW_ACTOR_H
#include "actor_timeline.h"
#include "actor_pose.h"
#include "actor_effect.h"
#include "actor_track.h"
#include "actor_sound.h"
#include "resource_cache.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewActorShape
    {
        float rotation[9], scale[3];
    } MMPreviewActorShape;
    typedef struct MMPreviewActor
    {
        MMActorHeader header;
        MMActorState state, orientation;
        MMPreviewActorShape shape;
        MMI32 original[3];
        void *tmpl, *body, *route, *events;
        int inactive, orientation_reset, sound_display;
        MMI32 event_source;
    } MMPreviewActor;
    typedef struct MMPreviewActorDefinition
    {
        MMActorHeader header;
        float point[3];
    } MMPreviewActorDefinition;
    typedef struct MMPreviewActorServices
    {
        void *context;
        MMActorTimelineServices timeline;
        int (*fault)(void *);
        int (*header)(void *, MMU32, MMActorHeader *);
        int (*route)(void *, MMU32, void **);
        int (*events)(void *, MMU32, void **);
        int (*fetch)(void *, const MMActorHeader *, void **);
        int (*bind)(void *, void *), (*body_create)(void *, void *, void **);
        int (*prop)(void *, void *), (*text)(void *, void *);
        void (*release_body)(void *, void *), (*release_template)(void *, void *);
        /* With tags supplied, load transfers typed PATH/GGAE ownership.
         * Services without tags supply borrowed evaluator tables. */
        const MMTagServices *tags;
        const MMStorageAllocator *track_allocator;
        int trash_unused;
        void (*rest)(void *, void *, unsigned short angles[3]);
        MMI32 (*scalar)(float);
        float (*scalar_float)(MMI32);
        void (*locate)(void *, void *, const MMActorBodyPose *);
        int (*pose)(void *, void *, void *, MMI32 action, MMI32 cel);
        int (*apply)(void *, MMI32 index);
        void (*clear_cues)(void *), (*clear_motion)(void *);
        int (*default_costume)(void *, void *, void *), (*motion_cues)(void *);
        int (*duplicate_body)(void *, void *original, void **draft);
        int (*adjust_text_body)(void *, void *text, void *body);
        int (*apply_material_override)(void *);
    } MMPreviewActorServices;
    /* Unique owning template/body handles. The track handles can also own
     * their tables when the services supply tags. Effects are synchronous
     * and do not throw: 1/0 success/failure, -1 allocation, -2 other exception.
     * Timeline resolvers report exceptions through fault after the shared evaluator
     * returns. Completed state/table/resource prefixes remain on failure, as in the
     * native provider. Disposal releases body before template. Resource output
     * handles must be published before a provider reports failure if ownership has
     * transferred. The native host transports exceptions after C returns. */
    void mm_preview_actor_init(MMPreviewActor *);
    void mm_preview_actor_dispose(MMPreviewActor *, const MMPreviewActorServices *);
    int mm_preview_actor_load(MMPreviewActor *, MMU32 number, const MMChunkAccess *, int require_template_target,
                              const MMPreviewActorServices *);
    /* Create a new actor with owned C tables. Supply the native tag services. */
    int mm_preview_actor_create(MMPreviewActor *, const MMPreviewActorDefinition *, const MMPreviewActorServices *);
    /* Borrow typed handles from an actor with owned tracks. */
    MMActorTrack mm_preview_actor_track(const MMPreviewActor *);

    typedef struct MMPreviewActorDraft
    {
        MMPreviewActor original;
        MMActorSounds motion;
        int active;
    } MMPreviewActorDraft;
    /* Use an actor with owned tracks and a zero-initialized draft. A successful begin installs a
     * separate body, track, and motion list until end restores the original. */
    int mm_preview_actor_begin_draft(MMPreviewActor *, MMActorSounds *, MMPreviewActorDraft *,
                                     const MMPreviewActorServices *);
    void mm_preview_actor_end_draft(MMPreviewActor *, MMActorSounds *, MMPreviewActorDraft *,
                                    const MMPreviewActorServices *);

    typedef struct MMPreviewEventReplacement
    {
        void *context;
        /* Borrow the cache owning the requested root track for this call. */
        int (*resolve)(void *, MMI32 source, MMU32 type, MMU32 number, MMResourceCache **);
        void *decode_context;
        MMBlockTransform decode;
        int (*save_costume)(void *);
        void (*restore_costume)(void *);
    } MMPreviewEventReplacement;
    /* Replace a preview's owned track events and replay its current frame.
     * Failure restores both original tables, evaluation state and costume.
     * Cue and motion callbacks retain their replay side effects. */
    int mm_preview_actor_replace_track(MMPreviewActor *, MMI32 source, const MMPreviewEventReplacement *,
                                       const MMPreviewActorServices *);
    /* Fetch/bind a text template, adjust a body draft, then pose it. Restore
     * the entire actor and release the draft on failure, including pose errors. */
    int mm_preview_actor_replace_template(MMPreviewActor *, MMI32 source, MMU32 number,
                                          const MMPreviewActorServices *);
    int mm_preview_actor_seek(MMPreviewActor *, MMI32 frame, const MMPreviewActorServices *);
    int mm_preview_actor_replay_track(MMPreviewActor *, MMI32 frame, const MMPreviewActorServices *);
    int mm_preview_actor_position(MMPreviewActor *, int select_cel, const MMPreviewActorServices *);
    int mm_preview_actor_translate(MMPreviewActor *, const float delta[3], const MMPreviewActorServices *);
    int mm_preview_actor_offset(MMPreviewActor *, const float offset[3], const MMPreviewActorServices *);
    int mm_preview_actor_set_shape(MMPreviewActor *, const MMPreviewActorShape *, const MMPreviewActorServices *);
    void mm_preview_actor_shape(const MMPreviewActor *, MMPreviewActorShape *, float left[9], float right[9],
                                const MMPreviewActorServices *);

    enum MMPreviewCostumeResult
    {
        MM_PREVIEW_COSTUME_FAILED, MM_PREVIEW_COSTUME_OK, MM_PREVIEW_COSTUME_PREPARE,
        MM_PREVIEW_COSTUME_APPLY, MM_PREVIEW_COSTUME_NO_SET
    };
    typedef struct MMPreviewCostumeServices
    {
        void *context;
        int (*save)(void *, int selected);
        void (*restore)(void *, int selected);
        void (*rest_pose)(void *);
        int (*apply)(void *);
        int (*object_bounds)(void *, float corners[24]);
        int (*variants)(void *, size_t *count);
        MMI32 (*variant_set)(void *, size_t);
        int (*apply_variant)(void *, size_t);
        void (*vertical_bounds)(void *, float range[2]);
        int (*part_bounds)(void *, MMI32 set, float low[3], float high[3]);
        int (*render)(void *, const float corners[24], int crop);
    } MMPreviewCostumeServices;
    /* Synchronous isolated render. Capture the original costume and pose,
     * frame the union of the requested set's variants, and restore the
     * original state on every path after capture. Host callbacks do not throw. */
    int mm_preview_costume_render(MMI32 set, const MMPreviewCostumeServices *);
#ifdef __cplusplus
}
#endif
#endif
