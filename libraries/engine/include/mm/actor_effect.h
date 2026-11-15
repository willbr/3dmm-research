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
    enum MMActorEffectOperation
    {
        MM_EFFECT_CLEAR_SOUNDS,
        MM_EFFECT_SET_ACTION_CEL,
        MM_EFFECT_CEL_COUNT,
        MM_EFFECT_ACTION_FLAGS,
        MM_EFFECT_SAVE_COSTUME,
        MM_EFFECT_DEFAULT_COSTUME,
        MM_EFFECT_RESTORE_COSTUME,
        MM_EFFECT_SHOW,
        MM_EFFECT_HIDE,
        MM_EFFECT_FETCH_CUSTOM,
        MM_EFFECT_FETCH_MATERIAL,
        MM_EFFECT_APPLY_CUSTOM,
        MM_EFFECT_APPLY_MATERIAL,
        MM_EFFECT_RELEASE_CUSTOM,
        MM_EFFECT_RELEASE_MATERIAL,
        MM_EFFECT_ENQUEUE_SOUND
    };
    typedef struct MMActorEffectCommand
    {
        int operation;
        MMI32 first, second;
        const MMTag *tag;
        void *resource;
    } MMActorEffectCommand;
    typedef struct MMActorEffectReply
    {
        int success;
        MMI32 value;
        MMU32 flags;
        void *resource;
    } MMActorEffectReply;
    typedef struct MMActorEffectServices
    {
        void *context;
        void (*write_event)(void *events, MMI32, const MMActorEvent *);
        void (*run)(void *, const MMActorEffectCommand *, MMActorEffectReply *);
        /* Optional borrowed native observer view, published before host operations
         * and table reads after state changes. Callbacks must not mutate/re-enter
         * actor state or structurally change its tables. */
        void (*publish)(void *, const MMActorState *);
    } MMActorEffectServices;
    /* C owns event policy, cache writes, partial state, resource release order and
     * costume rollback decisions. The host supplies primitive resource/body/audio
     * operations. A costume snapshot is scoped to this call by the host. Every
     * fetched resource is released after application. Failure is not transactional:
     * a failed cel-count query publishes its returned count, while failed action
     * flags leave the prior flags. Frame cache writes precede payload validation.
     * metadata_only evaluates the same state policy without body, costume or
     * sound effects; action metadata queries still run and must validate IDs.
     * Runtime tables and payloads must have their documented actor layouts. */
    int mm_actor_effect_apply(MMActorState *, void *route, void *events, MMI32 index,
                                    const MMActorPoint *origin, int body_present, int metadata_only,
                                    const MMActorMotionServices *, const MMActorEffectServices *);
    /* Used by entrances and editing. no_reset composes onto the current matrix;
     * zero-angle rotations retain their original BRender rounding. */
    void mm_actor_add_orientation(MMActorState *, const MMActorEntrance *, int no_reset);
#ifdef __cplusplus
}
#endif
#endif
