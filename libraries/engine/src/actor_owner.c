/* Actor ownership/undo restoration converted from actor.cpp and actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_owner.h"
#include <string.h>
void three_mm_actor_runtime_init(ThreeMMActorRuntime *runtime)
{
    memset(runtime,0,sizeof(*runtime)); three_mm_actor_state_init(&runtime->state);
    runtime->template_tag.sid=runtime->sound_tag.sid=-1;
    runtime->first=2147483647L; runtime->last=(-2147483647L-1); runtime->actor=-1;
}
void three_mm_actor_owner_copy(void *destination,void *source,const ThreeMMActorOwnerServices *s)
{ ThreeMMActorRuntime runtime; s->read(s->context,source,&runtime); s->write(s->context,destination,&runtime); }
static void publish_result(ThreeMMActorOwnerResult *out,void *actor)
{ out->actor=actor; if (out->publish) out->publish(out->context,actor); }
int three_mm_actor_owner_duplicate(void *source,int reset,ThreeMMActorOwnerResult *out,const ThreeMMActorOwnerServices *s)
{
    ThreeMMActorRuntime runtime; void *actor=s->create(s->context);
    publish_result(out,actor); if (!actor) return 0;
    if (!s->clone_track(s->context,actor,source) || !s->clone_sounds(s->context,actor,source))
    { s->release(s->context,actor); publish_result(out,NULL); return 0; }
    s->read(s->context,source,&runtime); s->write(s->context,actor,&runtime);
    if (runtime.body) s->retain(s->context,runtime.body);
    s->retain(s->context,runtime.template_object); s->duplicate_tag(s->context,&runtime.template_tag);
    runtime.time_frozen=0; s->write(s->context,actor,&runtime);
    if (reset) three_mm_actor_owner_reset(actor,s);
    return 1;
}
void three_mm_actor_owner_reset(void *actor,const ThreeMMActorOwnerServices *s)
{
    ThreeMMActorRuntime runtime; ThreeMMActorState state; ThreeMMActorRouteEditServices route;
    s->read(s->context,actor,&runtime); runtime.scene=NULL; s->write(s->context,actor,&runtime);
    if (runtime.body) s->release(s->context,runtime.body);
    runtime.body=NULL; runtime.state.frame=2147483647L; s->write(s->context,actor,&runtime);
    state=runtime.state; s->route_services(s->context,actor,&route);
    three_mm_actor_route_edit_reset(&state,s->route(s->context,actor),&s->motion,&route);
    s->read(s->context,actor,&runtime); runtime.state=state; s->write(s->context,actor,&runtime);
}
void three_mm_actor_owner_restore(void *actor,void *snapshot,const ThreeMMActorOwnerServices *s)
{
    ThreeMMActorRuntime runtime;
    three_mm_actor_owner_copy(actor,snapshot,s); s->swap_track(s->context,actor,snapshot); s->swap_sounds(s->context,actor,snapshot);
    s->read(s->context,snapshot,&runtime); s->invalidate_views(s->context,runtime.scene);
}
void three_mm_actor_owner_restore_undo(void *actor,void *snapshot,const ThreeMMActorOwnerServices *s)
{
    ThreeMMActorRuntime live,stored; ThreeMMActorRouteEditServices route; ThreeMMI32 frame; void *scene;
    s->read(s->context,actor,&live); s->read(s->context,snapshot,&stored);
    frame=live.state.frame; scene=stored.scene; stored.body=live.body; stored.scene=live.scene;
    s->write(s->context,snapshot,&stored); s->route_services(s->context,actor,&route);
    three_mm_actor_route_edit_hide(&live.state,&route);
    three_mm_actor_owner_restore(actor,snapshot,s);
    s->read(s->context,snapshot,&stored); stored.body=NULL; stored.scene=scene; s->write(s->context,snapshot,&stored);
    s->seek(s->context,actor,frame); s->read(s->context,actor,&live); s->invalidate_views(s->context,live.scene);
}
void three_mm_actor_owner_dispose(void *actor,const ThreeMMActorOwnerServices *s)
{
    ThreeMMActorRuntime runtime; s->read(s->context,actor,&runtime);
    s->close_tag(s->context,&runtime.template_tag); s->write(s->context,actor,&runtime);
    s->close_track_tags(s->context,actor);
    if (runtime.body) s->release(s->context,runtime.body);
    runtime.body=NULL; s->write(s->context,actor,&runtime); s->clear_track(s->context,actor);
    if (runtime.template_object) s->release(s->context,runtime.template_object);
    runtime.template_object=NULL; s->write(s->context,actor,&runtime); s->dispose_sounds(s->context,actor);
}
