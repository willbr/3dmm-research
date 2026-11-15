/* Composite actor undo converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_group_undo.h"
#include <stddef.h>
void *mm_actor_group_create(const MMActorGroupFactory *s)
{
    void *entries,*owner=s->create_owner(s->context);
    if (!owner) return NULL;
    entries=s->create_entries(s->context); s->publish_entries(s->context,owner,entries);
    if (!entries) { s->release_owner(s->context,owner); return NULL; }
    return owner;
}
MMI32 mm_actor_group_count(const MMActorGroupState *state,const MMActorGroupServices *s)
{ return state->entries?s->count(s->context,state->entries):0; }
int mm_actor_group_add_child(const MMActorGroupState *state,void *child,const MMActorGroupServices *s)
{
    s->retain(s->context,child);
    if (!s->append_child(s->context,state->entries,child)) { s->release(s->context,child); return 0; }
    return 1;
}
int mm_actor_group_add_name(const MMActorGroupState *state,MMI32 actor,const void *name,const MMActorGroupServices *s)
{ return s->append_name(s->context,state->entries,actor,name); }
void mm_actor_group_dispose(MMActorGroupState *state,int children,const MMActorGroupServices *s)
{
    MMI32 i; void *child;
    if (!state->entries) return;
    if (children) for (i=0;i<s->count(s->context,state->entries);++i)
    { child=s->child(s->context,state->entries,i); if (child) s->release(s->context,child); }
    s->release_entries(s->context,state->entries); state->entries=NULL; s->publish_entries(s->context,NULL);
}
int mm_actor_group_move_toggle(const MMActorGroupState *state,void *document,const MMActorGroupServices *s)
{
    MMI32 i; void *child;
    for (i=0;i<s->count(s->context,state->entries);++i)
    {
        child=s->child(s->context,state->entries,i); if (!child) continue;
        s->child_context(s->context,child,state->movie,state->scene,state->frame);
        if (!s->undo_child(s->context,child,document)) return 0;
    }
    return 1;
}
int mm_actor_group_name_toggle(const MMActorGroupState *state,const MMActorGroupServices *s)
{
    MMI32 i,actor,count=s->count(s->context,state->entries);
    for (i=0;i<count;++i)
    {
        actor=s->read_name(s->context,state->entries,i);
        if (!s->capture_name(s->context,state->movie,actor)) continue;
        if (!s->rename(s->context,state->movie,actor)) return 0;
        s->store_name(s->context,state->entries,i);
    }
    return 1;
}
