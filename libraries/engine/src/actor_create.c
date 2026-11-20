/* Actor creation/binding converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_create.h"
#include <stddef.h>
int mm_actor_create_groups(MMActorOwner *actor,const MMActorCreateServices *s)
{
    return mm_actor_track_create(&actor->track, &s->track_allocator, s->trash_unused, s->owner.tags) &&
           mm_actor_sounds_reserve(&actor->sounds, MM_ACTOR_SOUND_GROW);
}
int mm_actor_initialize(void *actor,const MMTag *tag,const MMActorCreateServices *s)
{
    const MMActorOwnerServices *o=&s->owner;
    MMActorOwner *owner = o->owner(o->context, actor);
    MMActorRuntime *r = &owner->runtime;
    r->template_object = s->fetch_template(o->context, tag);
    if (!r->template_object)
        return 0;
    r->template_tag = *tag;
    mm_tag_dup(&r->template_tag, o->tags);
    if (!mm_actor_create_groups(owner, s))
        return 0;
    mm_actor_owner_rewind(actor, o);
    return s->action_flags(o->context, r->template_object, 0, &r->state.actionFlags);
}
void *mm_actor_create(const MMTag *tag,const MMActorCreateServices *s)
{
    void *actor=s->owner.create(s->owner.context);
    if (!actor) return NULL;
    if (!mm_actor_initialize(actor,tag,s)) { s->owner.release(s->owner.context,actor); return NULL; }
    return actor;
}
void mm_actor_bind_scene(MMActorRuntime *r,void *scene,const MMActorCreateServices *s)
{
    const MMActorOwnerServices *o = &s->owner;
    if (r->scene && r->scene != scene)
        s->diagnostic(o->context, MM_CREATE_WRONG_SCENE);
    if (r->scene)
    {
        if (!r->body)
            s->diagnostic(o->context, MM_CREATE_BOUND_WITHOUT_BODY);
        return;
    }
    if (r->body)
        s->diagnostic(o->context, MM_CREATE_BODY_WITHOUT_SCENE);
    r->scene = scene;
    r->body = s->create_body(o->context, r->template_object);
    if (r->body)
        s->bind_body(o->context, r->body, scene);
}
