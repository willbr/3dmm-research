/* Actor event effects converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_EFFECT_H
#define MM_ENGINE_ACTOR_EFFECT_H
#include "actor_math.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMActorEntrance
    {
        MMActorPoint translation;
        unsigned short x, y, z, reserved;
    } MMActorEntrance;
    typedef struct MMActorStationaryTrack
    {
        MMActorRoutePoint route;
        MMActorEvent events[3];
        MMActorEntrance entrance;
        MMI32 action[2], step;
    } MMActorStationaryTrack;
    /* One zero-distance node and Add, Action and Step at the insertion frame. */
    void mm_actor_stationary_track(MMActorStationaryTrack *, MMI32 frame, const unsigned short angles[3]);
    typedef struct MMActorEffectServices
    {
        void *context;
        void (*write_event)(void *events, MMI32, const MMActorEvent *);
        /* Optional borrowed native observer view, published before host operations
         * and table reads after state changes. Callbacks must not mutate/re-enter
         * actor state or structurally change its tables. */
        void (*publish)(void *, const MMActorState *);
        /* Optional range query. Without it, the template checks action IDs. */
        MMI32 (*action_count)(void *);
        int (*select_cel)(void *, MMI32 action, MMI32 cel);
        int (*cel_count)(void *, MMI32 action, MMI32 *count);
        int (*action_flags)(void *, MMI32 action, MMU32 *flags);
        int (*save_costume)(void *), (*default_costume)(void *);
        void (*restore_costume)(void *), (*visibility)(void *, int shown);
        void *(*fetch_custom)(void *, MMI32 costume);
        void *(*fetch_material)(void *, const MMTag *);
        void (*apply_custom)(void *, void *);
        void (*apply_material)(void *, MMI32 set, void *);
        void (*release_resource)(void *, void *, int custom);
        void (*clear_sounds)(void *);
        int (*enqueue_sound)(void *, MMI32 event);
    } MMActorEffectServices;
    /* C owns event policy, cache writes, partial state, resource release order and
     * costume rollback decisions. The host supplies primitive resource/body/audio
     * operations. Metadata-only services need only table writes and action queries.
     * Snapshot/restore, visibility, sound and resource release are optional when
     * their effects are absent or use borrowed data. A costume snapshot is scoped
     * to this call by the host. Owned resources are released after application.
     * Failure is not transactional:
     * a failed cel-count query publishes its returned count, while failed action
     * flags leave the prior flags. Frame cache writes precede payload validation.
     * metadata_only evaluates the same state policy without body, costume or
     * sound effects; action metadata queries still run and must validate IDs.
     * Runtime tables and payloads must have their documented actor layouts. */
    int mm_actor_effect_apply(MMActorState *, void *route, void *events, MMI32 index,
                                    const MMActorPoint *origin, int body_present, int metadata_only,
                                    const MMActorMotionServices *, const MMActorEffectServices *);
    struct MMActorTrack;
    struct MMActorSounds;
    struct MMBodyOwner;
    struct MMBodyRuntimeServices;
    struct MMTemplateOwner;
    struct MMMaterialOwner;
    typedef struct MMActorEffectRenderer
    {
        const struct MMBodyRuntimeServices *body;
        int (*pose)(struct MMTemplateOwner *, struct MMBodyOwner *, MMI32 action, MMI32 cel, void *distance);
        int (*cel_count)(struct MMTemplateOwner *, MMI32 action, void *count);
        int (*action_flags)(struct MMTemplateOwner *, MMI32 action, void *flags);
        int (*default_costume)(struct MMTemplateOwner *, struct MMBodyOwner *);
        struct MMMaterialOwner *(*fetch_custom)(struct MMTemplateOwner *, MMI32 costume);
        void (*rest)(struct MMTemplateOwner *, unsigned short *x, unsigned short *y, unsigned short *z);
    } MMActorEffectRenderer;
    typedef struct MMActorEffectHooks
    {
        void *context;
        struct MMMaterialOwner *(*fetch_material)(void *, const MMTag *);
        void (*visibility)(void *, int shown);
        int (*enqueue_sound)(void *, MMI32 event);
        int check_actions;
    } MMActorEffectHooks;
    /* The shared renderer binding uses owned tracks and a scoped costume
     * snapshot. Hosts supply synchronous material, visibility and sound hooks.
     * Metadata replay supplies only the template and renderer queries. */
    int mm_actor_runtime_effect(MMActorState *, const struct MMActorTrack *, MMI32 index, const MMActorPoint *origin,
                                struct MMTemplateOwner *, struct MMBodyOwner *, struct MMActorSounds *,
                                int metadata_only, const MMActorEffectRenderer *, const MMActorEffectHooks *);
    /* Used by entrances and editing. no_reset composes onto the current matrix;
     * zero-angle rotations retain their original BRender rounding. */
    void mm_actor_add_orientation(MMActorState *, const MMActorEntrance *, int no_reset);
#ifdef __cplusplus
}
#endif
#endif
