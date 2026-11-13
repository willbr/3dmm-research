/* Actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_UNDO_H
#define THREE_MM_ENGINE_ACTOR_UNDO_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorUndoState
{
    void *movie,*actor;
    ThreeMMI32 scene,frame,last_frame,actor_id;
    int sooner_later,sound;
} ThreeMMActorUndoState;
typedef struct ThreeMMActorUndoServices
{
    void *context;
    void (*read)(void *,ThreeMMActorUndoState *);
    void (*publish)(void *,const ThreeMMActorUndoState *);
    int (*invoke_undo)(void *);
    int (*switch_scene)(void *,void *movie,ThreeMMI32 scene);
    void *(*current_scene)(void *,void *movie);
    int (*seek)(void *,void *scene,ThreeMMI32 frame);
    void (*clear_undo)(void *,void *movie);
    void (*flush)(void *,void *movie);
    void (*play)(void *,void *movie);
    void *(*find_actor)(void *,void *scene,ThreeMMI32 id);
    void (*retain)(void *,void *actor);
    void (*release)(void *,void *actor);
    ThreeMMI32 (*actor_id)(void *,void *actor);
    void (*remove_actor)(void *,void *scene,ThreeMMI32 id);
    int (*add_actor)(void *,void *scene,void *actor);
    int (*has_name)(void *);
    int (*capture_name)(void *,void *movie,ThreeMMI32 id);
    void (*assign_name)(void *,void *movie,ThreeMMI32 id);
    void (*store_name)(void *);
    void *(*view)(void *,void *movie);
    int (*text_mode)(void *,void *view);
    void (*select_actor)(void *,void *scene,void *actor);
    void (*invalidate_range)(void *,void *scene);
    void (*reset_actor)(void *,void *actor);
} ThreeMMActorUndoServices;
/* Valid initialized movie/scene/actor handles are required. Callbacks cannot
 * reenter this controller except invoke_undo (virtual dispatch for redo).
 * Names use caller-owned scratch storage: capture, assign (failure ignored),
 * then store the captured name. No allocation is required for the scratch.
 * Failed seek may destroy the owner via clear_undo; toggle returns immediately
 * without publishing/touching that owner again. Redo requires a retained owner
 * through invoke_undo, as the original controller did. */
int three_mm_actor_undo_toggle(ThreeMMActorUndoState *,const ThreeMMActorUndoServices *);
int three_mm_actor_undo_redo(ThreeMMActorUndoState *,const ThreeMMActorUndoServices *);
/* Set is for an empty/new owner and retains the non-NULL actor after publishing.
 * Dispose releases before clearing/publishing the pointer. */
void three_mm_actor_undo_set_actor(ThreeMMActorUndoState *,void *,const ThreeMMActorUndoServices *);
void three_mm_actor_undo_dispose(ThreeMMActorUndoState *,const ThreeMMActorUndoServices *);
typedef struct ThreeMMActorUndoCreateServices
{
    void *context;
    void *(*create)(void *);
    void (*set_actor)(void *,void *undo,void *actor);
    void (*set_id)(void *,void *undo,ThreeMMI32);
    void (*set_sound)(void *,void *undo,int);
    void (*set_name)(void *,void *undo,const void *name);
    int (*add)(void *,void *undo);
    void (*clear)(void *);
    void (*release)(void *,void *undo);
    void (*reset_actor)(void *,void *actor);
} ThreeMMActorUndoCreateServices;
int three_mm_actor_undo_create(void *snapshot,ThreeMMI32 id,int sound,const void *name,
    const ThreeMMActorUndoCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
