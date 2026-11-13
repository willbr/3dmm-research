/* Composite actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_GROUP_UNDO_H
#define THREE_MM_ENGINE_ACTOR_GROUP_UNDO_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorGroupState
{
    void *entries,*movie;
    ThreeMMI32 scene,frame;
} ThreeMMActorGroupState;
typedef struct ThreeMMActorGroupServices
{
    void *context;
    ThreeMMI32 (*count)(void *,void *entries);
    void *(*child)(void *,void *entries,ThreeMMI32);
    void (*retain)(void *,void *child);
    void (*release)(void *,void *child);
    int (*append_child)(void *,void *entries,void *child);
    void (*child_context)(void *,void *child,void *movie,ThreeMMI32 scene,ThreeMMI32 frame);
    int (*undo_child)(void *,void *child,void *document);
    void (*release_entries)(void *,void *entries);
    void (*publish_entries)(void *,void *entries);
    ThreeMMI32 (*read_name)(void *,void *entries,ThreeMMI32 index);
    int (*capture_name)(void *,void *movie,ThreeMMI32 actor);
    int (*rename)(void *,void *movie,ThreeMMI32 actor);
    int (*store_name)(void *,void *entries,ThreeMMI32 index);
    int (*append_name)(void *,void *entries,ThreeMMI32 actor,const void *name);
} ThreeMMActorGroupServices;
typedef struct ThreeMMActorGroupFactory
{
    void *context;
    void *(*create_owner)(void *);
    void *(*create_entries)(void *);
    void (*publish_entries)(void *,void *owner,void *entries);
    void (*release_owner)(void *,void *owner);
} ThreeMMActorGroupFactory;
void *three_mm_actor_group_create(const ThreeMMActorGroupFactory *);
ThreeMMI32 three_mm_actor_group_count(const ThreeMMActorGroupState *,const ThreeMMActorGroupServices *);
int three_mm_actor_group_add_child(const ThreeMMActorGroupState *,void *child,const ThreeMMActorGroupServices *);
int three_mm_actor_group_add_name(const ThreeMMActorGroupState *,ThreeMMI32,const void *,const ThreeMMActorGroupServices *);
/* Tables are C-backed storage supplied by adapters. Move groups own child refs;
 * rename groups own only entries. Release callbacks run before pointer clearing.
 * Names use caller-owned stored/current scratch values; read/capture/rename/store
 * are primitive transfers, not a second implementation of toggle policy. */
void three_mm_actor_group_dispose(ThreeMMActorGroupState *,int children,const ThreeMMActorGroupServices *);
/* Move counts are reread each iteration; name count is captured once. NULL move
 * rows are skipped. Child undo may clear/destroy the composite on failure: return
 * immediately, without further reads. Other callbacks cannot reenter/mutate the
 * group. Names skip absent actors, stop on rename failure and intentionally
 * ignore failed storage of the replaced name, preserving native partial changes. */
int three_mm_actor_group_move_toggle(const ThreeMMActorGroupState *,void *document,const ThreeMMActorGroupServices *);
int three_mm_actor_group_name_toggle(const ThreeMMActorGroupState *,const ThreeMMActorGroupServices *);
#ifdef __cplusplus
}
#endif
#endif
