/* Actor creation/binding converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_CREATE_H
#define THREE_MM_ENGINE_ACTOR_CREATE_H
#include "actor_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMActorCreateDiagnostic
{
    THREE_MM_CREATE_WRONG_SCENE,
    THREE_MM_CREATE_BOUND_WITHOUT_BODY,
    THREE_MM_CREATE_BODY_WITHOUT_SCENE
};
typedef struct ThreeMMActorCreateServices
{
    ThreeMMActorOwnerServices owner;
    void *(*fetch_template)(void *,const ThreeMMTag *);
    int (*create_track)(void *,void *actor);
    int (*create_sounds)(void *,void *actor);
    int (*action_flags)(void *,void *template_object,ThreeMMI32 action,ThreeMMU32 *);
    void *(*create_body)(void *,void *template_object);
    void (*bind_body)(void *,void *body,void *scene);
    void (*diagnostic)(void *,int);
} ThreeMMActorCreateServices;
/* create returns a fresh actor initialized with actor_runtime_init and empty
 * track/sound owners. Initialization publishes the fetched template even on
 * failure; after success it assigns the tag before duplicating its reference.
 * Track creation precedes sound creation. Neither groups nor initialize rolls
 * back failure: the factory releases the partial actor via owner disposal.
 * A successful rewind uses the shared C route-edit operation, including its
 * recording-mode reset. Action zero flags are queried only after publication;
 * a callback's output remains visible even when it reports failure.
 *
 * Primitive callbacks may update their table/sound storage and flags output,
 * but must not reenter or independently change unrelated runtime fields.
 */
int three_mm_actor_create_groups(void *actor,const ThreeMMActorCreateServices *);
int three_mm_actor_initialize(void *actor,const ThreeMMTag *,const ThreeMMActorCreateServices *);
void *three_mm_actor_create(const ThreeMMTag *,const ThreeMMActorCreateServices *);
/* scene is non-NULL and must match an existing binding. A bound actor requires
 * a body; an unbound actor requires none. Diagnostics retain native assertion
 * order and do not introduce recovery for broken preconditions. Scene assignment
 * precedes body creation; body assignment precedes world binding. Failed body
 * creation leaves the scene set and returns without world binding. A repeated
 * call with an existing scene returns immediately, including after that failure
 * (diagnosing its missing body); it does not retry allocation. */
void three_mm_actor_bind_scene(void *actor,void *scene,const ThreeMMActorCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
