/* Actor ownership/undo restoration converted from actor.cpp and actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_OWNER_H
#define MM_ENGINE_ACTOR_OWNER_H
#include "actor_record.h"
#include "actor_track.h"
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
/* Copy runtime values separately from the uniquely owned tables. Motion
 * sounds borrow the track's opened tag references. */
typedef struct MMActorOwner
{
    MMActorRuntime runtime;
    MMActorTrack track;
    MMActorSounds sounds;
} MMActorOwner;
typedef struct MMActorOwnerResult
{
    void *actor,*context;
    void (*publish)(void *,void *actor);
} MMActorOwnerResult;
typedef struct MMActorOwnerServices
{
    void *context;
    const MMTagServices *tags;
    MMActorOwner *(*owner)(void *,void *actor);
    void *(*create)(void *);
    void (*retain)(void *,void *object);
    void (*release)(void *,void *object);
    void (*retain_body)(void *,void *body);
    void (*release_body)(void *,void *body);
    void (*retain_template)(void *,void *object);
    void (*release_template)(void *,void *object);
    void (*route_services)(void *,void *actor,MMActorRouteEditServices *);
    void (*invalidate_views)(void *,void *scene);
} MMActorOwnerServices;
/* Runtime is the actor's live record. template_tag owns its opened reference;
 * sound_tag retains the native borrowed/inactive semantics (no duplicate/close).
 * Copy/restore do not retain resources: compatible body/template owners and the
 * corresponding source references must remain live. BASE identity is untouched.
 * Callbacks provide native actor identity and primitive resources. They
 * cannot reenter the transaction. Route services publish state and
 * update dirty/recording flags. */
void mm_actor_runtime_init(MMActorRuntime *);
void mm_actor_owner_init(MMActorOwner *);
MMActorLifetime mm_actor_owner_lifetime(MMActorOwner *);
enum { MM_ACTOR_FRAME_UNPLACED = 2147483647L };
enum MMActorRangeResult
{
    MM_ACTOR_RANGE_UNPLACED,
    MM_ACTOR_RANGE_READY,
    MM_ACTOR_RANGE_PARTIAL
};
/* Requesting the last frame refreshes dirty lifetime caches. A failed refresh
 * retains the partial caches and publishes only the first frame. An unplaced
 * actor leaves both optional outputs alone. */
int mm_actor_owner_frame_range(MMActorOwner *, MMI32 *first, MMI32 *last, const MMActorTimelineServices *);
/* Seek the live Studio actor. Frame and entrance policy reads from the owner.
 * The host provides template metadata, animation, sound and renderer effects. */
int mm_actor_owner_seek(MMActorOwner *, MMI32 target, int *sound, const MMActorTimelineServices *,
                        const MMActorPlaybackEffects *);
int mm_actor_owner_set_action(MMActorOwner *, MMI32 action, MMI32 cel, int frozen, MMU32 external_child,
                              const MMActorMotionServices *, const MMActorActionEditServices *);
void mm_actor_owner_delete_forward(MMActorOwner *,int all,int *alive,MMI32 current,
                                  const MMActorRouteEditServices *);
int mm_actor_owner_duplicate(void *source,int reset,MMActorOwnerResult *,const MMActorOwnerServices *);
void mm_actor_owner_rewind(void *actor,const MMActorOwnerServices *);
void mm_actor_owner_reset(void *actor,const MMActorOwnerServices *);
void mm_actor_owner_restore(void *actor,void *snapshot,const MMActorOwnerServices *);
/* Snapshot body must be NULL. Borrow live body/scene, hide, restore/swap, restore
 * snapshot bindings, seek the saved live frame and invalidate again. Failed seek
 * is deliberately ignored, retaining partial state as in the native engine. */
void mm_actor_owner_restore_undo(void *actor,void *snapshot,const MMActorOwnerServices *,
                                const MMActorTimelineServices *,const MMActorPlaybackEffects *);
/* Close template/event tags, release body, dispose track, release template and
 * dispose motion storage. No wrapper-field destructor is required for ownership. */
void mm_actor_owner_dispose(void *actor,const MMActorOwnerServices *);

typedef struct MMActorTemplateServices
{
    void *context;
    const MMTagServices *tags;
    void *(*fetch)(void *, const MMTag *);
    void (*release)(void *, void *object);
    int (*text)(void *, void *object);
    int (*adjust_body)(void *, void *object, void *body);
    int (*select_cel)(void *, void *object, void *body, MMI32 action, MMI32 cel);
    void (*position)(void *);
    MMI32 (*body_sets)(void *, void *body);
} MMActorTemplateServices;

/* Replace the Studio template and discard costume rows whose body sets no
 * longer exist. Reposition with the old template before publishing the new
 * one. Adjustment failure releases the candidate. A failed cel selection
 * retains the adjusted body and the legacy candidate reference. */
int mm_actor_replace_template(MMActorRuntime *, MMActorEventTable *, const MMTag *,
                              const MMActorTemplateServices *, const MMActorEditServices *);
#ifdef __cplusplus
}
#endif
#endif
