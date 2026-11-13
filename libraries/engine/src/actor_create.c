/* Actor creation/binding converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_create.h"
#include <stddef.h>
int three_mm_actor_create_groups(void *actor,const ThreeMMActorCreateServices *s)
{
    return s->create_track(s->owner.context,actor) && s->create_sounds(s->owner.context,actor);
}
int three_mm_actor_initialize(void *actor,const ThreeMMTag *tag,const ThreeMMActorCreateServices *s)
{
    const ThreeMMActorOwnerServices *o=&s->owner;
    ThreeMMActorRuntime r; ThreeMMActorState state; ThreeMMActorRouteEditServices route;
    int ok;
    o->read(o->context,actor,&r); r.template_object=s->fetch_template(o->context,tag); o->write(o->context,actor,&r);
    if (!r.template_object) return 0;
    r.template_tag=*tag; o->write(o->context,actor,&r); o->duplicate_tag(o->context,&r.template_tag);
    if (!three_mm_actor_create_groups(actor,s)) return 0;
    state=r.state; o->route_services(o->context,actor,&route);
    three_mm_actor_route_edit_rewind(&state,o->route(o->context,actor),&o->motion,&route);
    o->read(o->context,actor,&r); r.state=state; o->write(o->context,actor,&r);
    ok=s->action_flags(o->context,r.template_object,0,&r.state.actionFlags);
    o->write(o->context,actor,&r); return ok;
}
void *three_mm_actor_create(const ThreeMMTag *tag,const ThreeMMActorCreateServices *s)
{
    void *actor=s->owner.create(s->owner.context);
    if (!actor) return NULL;
    if (!three_mm_actor_initialize(actor,tag,s)) { s->owner.release(s->owner.context,actor); return NULL; }
    return actor;
}
void three_mm_actor_bind_scene(void *actor,void *scene,const ThreeMMActorCreateServices *s)
{
    const ThreeMMActorOwnerServices *o=&s->owner; ThreeMMActorRuntime r;
    o->read(o->context,actor,&r);
    if (r.scene && r.scene!=scene) s->diagnostic(o->context,THREE_MM_CREATE_WRONG_SCENE);
    if (r.scene)
    {
        if (!r.body) s->diagnostic(o->context,THREE_MM_CREATE_BOUND_WITHOUT_BODY);
        return;
    }
    if (r.body) s->diagnostic(o->context,THREE_MM_CREATE_BODY_WITHOUT_SCENE);
    r.scene=scene; o->write(o->context,actor,&r);
    r.body=s->create_body(o->context,r.template_object); o->write(o->context,actor,&r);
    if (r.body) s->bind_body(o->context,r.body,scene);
}
