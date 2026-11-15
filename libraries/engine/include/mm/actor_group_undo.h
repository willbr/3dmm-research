/* Composite actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_GROUP_UNDO_H
#define MM_ENGINE_ACTOR_GROUP_UNDO_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorGroupState
{
    void *entries,*movie;
    MMI32 scene,frame;
} MMActorGroupState;
typedef struct MMActorGroupServices
{
    void *context;
    MMI32 (*count)(void *,void *entries);
    void *(*child)(void *,void *entries,MMI32);
    void (*retain)(void *,void *child);
    void (*release)(void *,void *child);
    int (*append_child)(void *,void *entries,void *child);
    void (*child_context)(void *,void *child,void *movie,MMI32 scene,MMI32 frame);
    int (*undo_child)(void *,void *child,void *document);
    void (*release_entries)(void *,void *entries);
    void (*publish_entries)(void *,void *entries);
    MMI32 (*read_name)(void *,void *entries,MMI32 index);
    int (*capture_name)(void *,void *movie,MMI32 actor);
    int (*rename)(void *,void *movie,MMI32 actor);
    int (*store_name)(void *,void *entries,MMI32 index);
    int (*append_name)(void *,void *entries,MMI32 actor,const void *name);
} MMActorGroupServices;
typedef struct MMActorGroupFactory
{
    void *context;
    void *(*create_owner)(void *);
    void *(*create_entries)(void *);
    void (*publish_entries)(void *,void *owner,void *entries);
    void (*release_owner)(void *,void *owner);
} MMActorGroupFactory;
void *mm_actor_group_create(const MMActorGroupFactory *);
MMI32 mm_actor_group_count(const MMActorGroupState *,const MMActorGroupServices *);
int mm_actor_group_add_child(const MMActorGroupState *,void *child,const MMActorGroupServices *);
int mm_actor_group_add_name(const MMActorGroupState *,MMI32,const void *,const MMActorGroupServices *);
/* Tables are C-backed storage supplied by adapters. Move groups own child refs;
 * rename groups own only entries. Release callbacks run before pointer clearing.
 * Names use caller-owned stored/current scratch values; read/capture/rename/store
 * are primitive transfers, not a second implementation of toggle policy. */
void mm_actor_group_dispose(MMActorGroupState *,int children,const MMActorGroupServices *);
/* Move counts are reread each iteration; name count is captured once. NULL move
 * rows are skipped. Child undo may clear/destroy the composite on failure: return
 * immediately, without further reads. Other callbacks cannot reenter/mutate the
 * group. Names skip absent actors, stop on rename failure and intentionally
 * ignore failed storage of the replaced name, preserving native partial changes. */
int mm_actor_group_move_toggle(const MMActorGroupState *,void *document,const MMActorGroupServices *);
int mm_actor_group_name_toggle(const MMActorGroupState *,const MMActorGroupServices *);
#ifdef __cplusplus
}
#endif
#endif
