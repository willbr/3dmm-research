/* Actor event editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_EDIT_H
#define THREE_MM_ENGINE_ACTOR_EDIT_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorEditServices
{
    void *context;
    void (*close_event_tag)(void *, ThreeMMI32 index);
    void (*delete_event)(void *, ThreeMMI32 index);
    int (*scene_frame)(void *, ThreeMMI32 *);
    void (*update_sound_frame)(void *);
    void (*publish)(void *, const ThreeMMActorState *);
} ThreeMMActorEditServices;
/* Borrow valid runtime tables/state. Close tags before deletion; adjust only
 * cursors strictly after the deleted entry, then notify the current-scene sound
 * indicator. Callbacks may observe published state, but cannot mutate/re-enter it. */
void three_mm_actor_edit_remove(ThreeMMActorState *, void *events, ThreeMMI32 index, int update_state,
                                const ThreeMMActorMotionServices *, const ThreeMMActorEditServices *);
#ifdef __cplusplus
}
#endif
#endif
