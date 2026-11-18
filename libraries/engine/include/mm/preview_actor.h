/* Preview actor ownership and playback control over the shared actor evaluators. */
#ifndef MM_ENGINE_PREVIEW_ACTOR_H
#define MM_ENGINE_PREVIEW_ACTOR_H
#include "actor_timeline.h"
#include "actor_pose.h"
#include "actor_effect.h"
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
        int (*track_create)(void *, void **route, void **events);
        int (*route_insert)(void *, void *, MMI32, const MMActorRoutePoint *);
        int (*event_insert)(void *, void *, MMI32, MMU32 bytes, const void *, const MMActorEvent *);
        void (*rest)(void *, void *, unsigned short angles[3]);
        MMI32 (*scalar)(float);
        float (*scalar_float)(MMI32);
        void (*locate)(void *, void *, const MMActorBodyPose *);
        int (*pose)(void *, void *, void *, MMI32 action, MMI32 cel);
        int (*apply)(void *, MMI32 index);
        void (*clear_cues)(void *), (*clear_motion)(void *);
        int (*default_costume)(void *, void *, void *), (*motion_cues)(void *);
    } MMPreviewActorServices;
    /* Unique owning template/body handles, with borrowed validated track tables.
     * Initialize once, dispose before the typed track owner. Effects are synchronous
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
    int mm_preview_actor_create(MMPreviewActor *, const MMPreviewActorDefinition *, const MMPreviewActorServices *);
    int mm_preview_actor_seek(MMPreviewActor *, MMI32 frame, const MMPreviewActorServices *);
    int mm_preview_actor_replace_events(MMPreviewActor *, void *events, MMI32 frame, const MMPreviewActorServices *);
    int mm_preview_actor_position(MMPreviewActor *, int select_cel, const MMPreviewActorServices *);
    int mm_preview_actor_translate(MMPreviewActor *, const float delta[3], const MMPreviewActorServices *);
    int mm_preview_actor_offset(MMPreviewActor *, const float offset[3], const MMPreviewActorServices *);
    int mm_preview_actor_set_shape(MMPreviewActor *, const MMPreviewActorShape *, const MMPreviewActorServices *);
    void mm_preview_actor_shape(const MMPreviewActor *, MMPreviewActorShape *, float left[9], float right[9],
                                const MMPreviewActorServices *);
#ifdef __cplusplus
}
#endif
#endif
