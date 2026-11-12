/* Actor event effects converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_EFFECT_H
#define THREE_MM_ENGINE_ACTOR_EFFECT_H
#include "actor_math.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMActorEntrance
    {
        ThreeMMActorPoint translation;
        unsigned short x, y, z, reserved;
    } ThreeMMActorEntrance;
    enum ThreeMMActorEffectOperation
    {
        THREE_MM_EFFECT_CLEAR_SOUNDS,
        THREE_MM_EFFECT_SET_ACTION_CEL,
        THREE_MM_EFFECT_CEL_COUNT,
        THREE_MM_EFFECT_ACTION_FLAGS,
        THREE_MM_EFFECT_SAVE_COSTUME,
        THREE_MM_EFFECT_DEFAULT_COSTUME,
        THREE_MM_EFFECT_RESTORE_COSTUME,
        THREE_MM_EFFECT_SHOW,
        THREE_MM_EFFECT_HIDE,
        THREE_MM_EFFECT_FETCH_CUSTOM,
        THREE_MM_EFFECT_FETCH_MATERIAL,
        THREE_MM_EFFECT_APPLY_CUSTOM,
        THREE_MM_EFFECT_APPLY_MATERIAL,
        THREE_MM_EFFECT_RELEASE_CUSTOM,
        THREE_MM_EFFECT_RELEASE_MATERIAL,
        THREE_MM_EFFECT_ENQUEUE_SOUND
    };
    typedef struct ThreeMMActorEffectCommand
    {
        int operation;
        ThreeMMI32 first, second;
        const ThreeMMTag *tag;
        void *resource;
    } ThreeMMActorEffectCommand;
    typedef struct ThreeMMActorEffectReply
    {
        int success;
        ThreeMMI32 value;
        ThreeMMU32 flags;
        void *resource;
    } ThreeMMActorEffectReply;
    typedef struct ThreeMMActorEffectServices
    {
        void *context;
        void (*write_event)(void *events, ThreeMMI32, const ThreeMMActorEvent *);
        void (*run)(void *, const ThreeMMActorEffectCommand *, ThreeMMActorEffectReply *);
        /* Optional borrowed native observer view, published before host operations
         * and table reads after state changes. Callbacks must not mutate/re-enter
         * actor state or structurally change its tables. */
        void (*publish)(void *, const ThreeMMActorState *);
    } ThreeMMActorEffectServices;
    /* C owns event policy, cache writes, partial state, resource release order and
     * costume rollback decisions. The host supplies primitive resource/body/audio
     * operations. A costume snapshot is scoped to this call by the host. Every
     * fetched resource is released after application. Failure is not transactional:
     * a failed cel-count query publishes its returned count, while failed action
     * flags leave the prior flags. Frame cache writes precede payload validation.
     * metadata_only evaluates the same state policy without body, costume or
     * sound effects; action metadata queries still run and must validate IDs.
     * Runtime tables and payloads must have their documented actor layouts. */
    int three_mm_actor_effect_apply(ThreeMMActorState *, void *route, void *events, ThreeMMI32 index,
                                    const ThreeMMActorPoint *origin, int body_present, int metadata_only,
                                    const ThreeMMActorMotionServices *, const ThreeMMActorEffectServices *);
    /* Used by entrances and editing. no_reset composes onto the current matrix;
     * zero-angle rotations retain their original BRender rounding. */
    void three_mm_actor_add_orientation(ThreeMMActorState *, const ThreeMMActorEntrance *, int no_reset);
#ifdef __cplusplus
}
#endif
#endif
