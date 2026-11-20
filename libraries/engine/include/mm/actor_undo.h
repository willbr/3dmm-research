/* Actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_UNDO_H
#define MM_ENGINE_ACTOR_UNDO_H
#include "actor_owner.h"
#include "movie_undo.h"
#include "movie_title.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorUndoState
{
    MMMovieUndoContext *base;
    void *actor;
    MMI32 last_frame,actor_id;
    int sooner_later,sound;
    MMMovieTitle name;
} MMActorUndoState;
typedef struct MMActorUndoServices
{
    void *context;
    int (*invoke_undo)(void *);
    int (*switch_scene)(void *,void *movie,MMI32 scene);
    void *(*current_scene)(void *,void *movie);
    int (*seek)(void *,void *scene,MMI32 frame);
    void (*clear_undo)(void *,void *movie);
    void (*flush)(void *,void *movie);
    void (*play)(void *,void *movie);
    void *(*find_actor)(void *,void *scene,MMI32 id);
    void (*retain)(void *,void *actor);
    void (*release)(void *,void *actor);
    MMI32 (*actor_id)(void *,void *actor);
    void (*remove_actor)(void *,void *scene,MMI32 id);
    int (*add_actor)(void *,void *scene,void *actor);
    int (*find_name)(void *,void *movie,MMI32 id,const void **text,MMU32 *count);
    void (*rename)(void *,void *movie,MMI32 id,const void *text,MMU32 count);
    void *(*view)(void *,void *movie);
    int (*text_mode)(void *,void *view);
    void (*select_actor)(void *,void *scene,void *actor);
    void (*invalidate_range)(void *,void *scene);
    void (*reset_actor)(void *,void *actor);
} MMActorUndoServices;
/* Initialize a unique owning actor record over its stable borrowed base context.
 * Native counted names retain one/two-byte units, including embedded zeroes.
 * Dispose releases the actor; scalar/name values remain. Do not copy owners. */
int mm_actor_undo_init(MMActorUndoState *, MMMovieUndoContext *, unsigned unit);
int mm_actor_undo_set_name(MMActorUndoState *, const void *, MMU32 count);
/* Valid initialized movie/scene/actor handles are required. Callbacks cannot
 * reenter this controller except invoke_undo (virtual dispatch for redo).
 * find_name borrows native units until the next roll-call mutation. The
 * controller copies them before renaming and saves the copy even if rename fails.
 * Failed seek may destroy the owner via clear_undo; toggle returns immediately
 * without accessing that owner again. Redo requires a retained owner through
 * invoke_undo. */
int mm_actor_undo_toggle(MMActorUndoState *,const MMActorUndoServices *);
int mm_actor_undo_redo(MMActorUndoState *,const MMActorUndoServices *);
/* Set is for an empty/new owner and retains the non-NULL actor after storing it.
 * Dispose releases before clearing the pointer. */
void mm_actor_undo_set_actor(MMActorUndoState *,void *,const MMActorUndoServices *);
void mm_actor_undo_dispose(MMActorUndoState *,const MMActorUndoServices *);
typedef struct MMActorUndoCreateServices
{
    void *context;
    MMActorUndoState *(*create)(void *,void **undo);
    int (*add)(void *,void *undo);
    void (*clear)(void *);
    void (*release)(void *,void *undo);
} MMActorUndoCreateServices;
/* create returns the initialized state and the owning handle used for register
 * and release. The snapshot must be a live actor. Successful registration
 * retains and resets it through the actor owner. The optional name is counted
 * in the state's native units. */
int mm_actor_undo_create(void *snapshot,MMI32 id,int sound,const void *name,
    MMU32 count,const MMActorOwnerServices *,const MMActorUndoCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
