/* Actor ownership/undo restoration converted from actor.cpp and actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_OWNER_H
#define MM_ENGINE_ACTOR_OWNER_H
#include "actor_record.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorRuntime
{
    MMActorState state;
    MMActorRecording recording;
    void *template_object,*body,*scene;
    MMTag template_tag,sound_tag;
    MMActorPoint origin;
    MMI32 first,last,actor;
    int life_dirty,prerendered,time_frozen;
} MMActorRuntime;
typedef struct MMActorOwnerResult
{
    void *actor,*context;
    void (*publish)(void *,void *actor);
} MMActorOwnerResult;
typedef struct MMActorOwnerServices
{
    void *context;
    MMActorMotionServices motion;
    void (*read)(void *,void *actor,MMActorRuntime *);
    void (*write)(void *,void *actor,const MMActorRuntime *);
    void *(*create)(void *);
    void (*retain)(void *,void *object);
    void (*release)(void *,void *object);
    int (*clone_track)(void *,void *destination,void *source);
    int (*clone_sounds)(void *,void *destination,void *source);
    void (*swap_track)(void *,void *,void *);
    void (*swap_sounds)(void *,void *,void *);
    void (*duplicate_tag)(void *,const MMTag *);
    void (*close_tag)(void *,MMTag *);
    void (*close_track_tags)(void *,void *actor);
    void (*clear_track)(void *,void *actor);
    void (*dispose_sounds)(void *,void *actor);
    void *(*route)(void *,void *actor);
    void (*route_services)(void *,void *actor,MMActorRouteEditServices *);
    int (*seek)(void *,void *actor,MMI32 frame);
    void (*invalidate_views)(void *,void *scene);
} MMActorOwnerServices;
/* Runtime mirrors are plain values. template_tag owns its opened reference;
 * sound_tag retains the native borrowed/inactive semantics (no duplicate/close).
 * Copy/restore do not retain resources: compatible body/template owners and the
 * corresponding source references must remain live. BASE identity is untouched.
 * Callbacks provide C-backed table/sound operations and primitive resources.
 * Callbacks cannot reenter the transaction. Route services publish state and
 * update dirty/recording flags; seek can update the complete runtime. */
void mm_actor_runtime_init(MMActorRuntime *);
void mm_actor_owner_copy(void *destination,void *source,const MMActorOwnerServices *);
int mm_actor_owner_duplicate(void *source,int reset,MMActorOwnerResult *,const MMActorOwnerServices *);
void mm_actor_owner_reset(void *actor,const MMActorOwnerServices *);
void mm_actor_owner_restore(void *actor,void *snapshot,const MMActorOwnerServices *);
/* Snapshot body must be NULL. Borrow live body/scene, hide, restore/swap, restore
 * snapshot bindings, seek the saved live frame and invalidate again. Failed seek
 * is deliberately ignored, retaining partial state as in the native engine. */
void mm_actor_owner_restore_undo(void *actor,void *snapshot,const MMActorOwnerServices *);
/* Close template/event tags, release body, dispose track, release template and
 * dispose motion storage. No wrapper-field destructor is required for ownership. */
void mm_actor_owner_dispose(void *actor,const MMActorOwnerServices *);
#ifdef __cplusplus
}
#endif
#endif
