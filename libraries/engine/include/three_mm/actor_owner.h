/* Actor ownership/undo restoration converted from actor.cpp and actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_OWNER_H
#define THREE_MM_ENGINE_ACTOR_OWNER_H
#include "actor_record.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorRuntime
{
    ThreeMMActorState state;
    ThreeMMActorRecording recording;
    void *template_object,*body,*scene;
    ThreeMMTag template_tag,sound_tag;
    ThreeMMActorPoint origin;
    ThreeMMI32 first,last,actor;
    int life_dirty,prerendered,time_frozen;
} ThreeMMActorRuntime;
typedef struct ThreeMMActorOwnerResult
{
    void *actor,*context;
    void (*publish)(void *,void *actor);
} ThreeMMActorOwnerResult;
typedef struct ThreeMMActorOwnerServices
{
    void *context;
    ThreeMMActorMotionServices motion;
    void (*read)(void *,void *actor,ThreeMMActorRuntime *);
    void (*write)(void *,void *actor,const ThreeMMActorRuntime *);
    void *(*create)(void *);
    void (*retain)(void *,void *object);
    void (*release)(void *,void *object);
    int (*clone_track)(void *,void *destination,void *source);
    int (*clone_sounds)(void *,void *destination,void *source);
    void (*swap_track)(void *,void *,void *);
    void (*swap_sounds)(void *,void *,void *);
    void (*duplicate_tag)(void *,const ThreeMMTag *);
    void (*close_tag)(void *,ThreeMMTag *);
    void (*close_track_tags)(void *,void *actor);
    void (*clear_track)(void *,void *actor);
    void (*dispose_sounds)(void *,void *actor);
    void *(*route)(void *,void *actor);
    void (*route_services)(void *,void *actor,ThreeMMActorRouteEditServices *);
    int (*seek)(void *,void *actor,ThreeMMI32 frame);
    void (*invalidate_views)(void *,void *scene);
} ThreeMMActorOwnerServices;
/* Runtime mirrors are plain values. template_tag owns its opened reference;
 * sound_tag retains the native borrowed/inactive semantics (no duplicate/close).
 * Copy/restore do not retain resources: compatible body/template owners and the
 * corresponding source references must remain live. BASE identity is untouched.
 * Callbacks provide C-backed table/sound operations and primitive resources.
 * Callbacks cannot reenter the transaction. Route services publish state and
 * update dirty/recording flags; seek can update the complete runtime. */
void three_mm_actor_runtime_init(ThreeMMActorRuntime *);
void three_mm_actor_owner_copy(void *destination,void *source,const ThreeMMActorOwnerServices *);
int three_mm_actor_owner_duplicate(void *source,int reset,ThreeMMActorOwnerResult *,const ThreeMMActorOwnerServices *);
void three_mm_actor_owner_reset(void *actor,const ThreeMMActorOwnerServices *);
void three_mm_actor_owner_restore(void *actor,void *snapshot,const ThreeMMActorOwnerServices *);
/* Snapshot body must be NULL. Borrow live body/scene, hide, restore/swap, restore
 * snapshot bindings, seek the saved live frame and invalidate again. Failed seek
 * is deliberately ignored, retaining partial state as in the native engine. */
void three_mm_actor_owner_restore_undo(void *actor,void *snapshot,const ThreeMMActorOwnerServices *);
/* Close template/event tags, release body, dispose track, release template and
 * dispose motion storage. No wrapper-field destructor is required for ownership. */
void three_mm_actor_owner_dispose(void *actor,const ThreeMMActorOwnerServices *);
#ifdef __cplusplus
}
#endif
#endif
