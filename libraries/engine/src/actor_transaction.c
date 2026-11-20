/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_transaction.h"
#include <stddef.h>
static void *capture(void *actor,const MMActorTransactionServices *s)
{
    MMActorOwnerResult copy={NULL,NULL,NULL};
    return mm_actor_owner_duplicate(actor,0,&copy,&s->actors)?copy.actor:NULL;
}
static int register_snapshot(void *actor,void *snapshot,int sound,const MMActorTransactionServices *s)
{
    const MMActorOwner *owner=s->actors.owner(s->actors.context,actor);
    return mm_actor_undo_create(snapshot,owner->runtime.actor,sound,NULL,0,&s->actors,&s->undo);
}
static int finish_edit(void *actor,void *snapshot,int applied,int sound,void **out,
                       const MMActorTransactionServices *s)
{
    if (!applied || !register_snapshot(actor,snapshot,sound,s))
    {
        mm_actor_owner_restore(actor,snapshot,&s->actors);
        s->actors.release(s->actors.context,snapshot);
        return 0;
    }
    if (out) *out=snapshot;
    else s->actors.release(s->actors.context,snapshot);
    return 1;
}
int mm_actor_transaction_stage(void *actor,int entrance,MMU32 external_child,
    const MMActorStageEditServices *edit,const MMActorTransactionServices *s)
{
    void *copy=capture(actor,s);
    MMActorOwner *owner;
    int applied;
    if (!copy) return 0;
    owner=s->actors.owner(s->actors.context,actor);
    applied=mm_actor_owner_stage_edit(owner,entrance,external_child,edit);
    return finish_edit(actor,copy,applied,0,NULL,s);
}
int mm_actor_transaction_normalize(void *actor,MMU32 flags,
    const MMActorTransformEditServices *edit,const MMActorTransactionServices *s)
{
    void *copy=capture(actor,s);
    MMActorOwner *owner;
    int applied;
    if (!copy) return 0;
    owner=s->actors.owner(s->actors.context,actor);
    applied=mm_actor_owner_normalize(owner,flags,edit);
    return finish_edit(actor,copy,applied,0,NULL,s);
}
int mm_actor_transaction_costume(void *actor,MMI32 set,const MMTag *tag,MMI32 costume,MMI32 custom,
    const MMActorCostumeEditServices *edit,const MMActorTransactionServices *s)
{
    void *copy=capture(actor,s);
    MMActorOwner *owner;
    int applied;
    if (!copy) return 0;
    owner=s->actors.owner(s->actors.context,actor);
    applied=mm_actor_owner_set_costume(owner,set,tag,costume,custom,edit);
    return finish_edit(actor,copy,applied,0,NULL,s);
}
int mm_actor_transaction_sound(void *actor,const MMTag *tag,MMI32 loop,MMI32 queue,int motion_match,
    MMI32 volume,MMI32 style,MMU32 external_child,const MMActorSoundEditServices *edit,
    const MMActorTransactionServices *s)
{
    const MMActorMotionServices motion=mm_actor_track_motion_services();
    void *copy=capture(actor,s);
    MMActorOwner *owner;
    MMActorState state;
    int applied;
    if (!copy) return 0;
    owner=s->actors.owner(s->actors.context,actor);
    state=owner->runtime.state;
    applied=mm_actor_sound_set(&state,&owner->sounds,owner->track.events,owner->runtime.actor,tag,
                                loop,queue,motion_match,volume,style,external_child,&motion,edit);
    owner->runtime.state=state;
    return finish_edit(actor,copy,applied,1,NULL,s);
}
int mm_actor_transaction_action(void *actor,MMI32 action,MMI32 cel,int frozen,MMU32 external_child,
    void **snapshot,const MMActorMotionServices *motion,const MMActorActionEditServices *edit,
    const MMActorTransactionServices *s)
{
    void *copy=capture(actor,s);
    MMActorOwner *owner;
    int applied;
    if (!copy) return 0;
    owner=s->actors.owner(s->actors.context,actor);
    applied=mm_actor_owner_set_action(owner,action,cel,frozen,external_child,motion,edit);
    return finish_edit(actor,copy,applied,0,snapshot,s);
}
static void alive(MMActorDeleteResult *out,int value,int trashed)
{
    if (!out) return;
    out->alive=value; out->trashed=trashed;
    if (out->publish) out->publish(out->context,value,trashed);
}
int mm_actor_transaction_delete(void *actor,int all,MMActorDeleteResult *out,const MMActorTransactionServices *s,
    const MMActorDeleteServices *d)
{
    const MMActorSubrouteEditServices *edit=&d->subroute;
    MMActorOwner *owner;
    MMActorRuntime *runtime;
    MMActorLifetime lifetime;
    MMActorEvent entrance;
    MMI32 original;
    void *copy=capture(actor,s);
    int value;
    if (!copy)
    {
        alive(out,0,1);
        return 0;
    }
    owner=s->actors.owner(s->actors.context,actor);
    runtime=&owner->runtime;
    if (runtime->state.addEvent>=0 && !all &&
        mm_actor_events_get_fixed(owner->track.events,runtime->state.addEvent,&entrance) &&
        runtime->state.frame==entrance.nfrm)
    {
        MMActorState state=runtime->state;
        value=mm_actor_stage_delete_subroute(&state,owner->track.route,owner->track.events,&edit->timeline.motion,edit);
        runtime->state=state;
        if (!value) goto failed;
        if (out) alive(out,mm_actor_events_count(owner->track.events)>0,0);
    }
    else
    {
        original=runtime->state.nextEvent;
        if (!mm_actor_subroute_seek(&runtime->state,mm_fixed_sub(runtime->state.frame,1),edit)) goto failed;
        mm_actor_owner_delete_forward(owner,all,out?&value:NULL,original,&edit->route);
        if (out) alive(out,value,0);
        if (!mm_actor_subroute_seek(&runtime->state,mm_fixed_add(runtime->state.frame,1),edit)) goto failed;
        if (runtime->scene) d->sound_frame(d->context);
    }
    lifetime=mm_actor_owner_lifetime(owner);
    if (!mm_actor_refresh_lifetime(owner->track.route,owner->track.events,NULL,&lifetime,&edit->timeline))
        d->diagnostic(d->context,MM_TRANSACTION_BAD_LIFETIME);
    return finish_edit(actor,copy,1,0,NULL,s);
failed:
    d->diagnostic(d->context,MM_TRANSACTION_SEEK_FAILED);
    s->actors.release(s->actors.context,copy);
    return 0;
}
