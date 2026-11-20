/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_transaction.h"
#include "mm/actor_stage_edit.h"
#include <stddef.h>
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
int mm_actor_transaction_apply(void *actor,int sound,const MMActorTransactionServices *s)
{
    MMActorOwnerResult copy={NULL,NULL,NULL};
    if (!mm_actor_owner_duplicate(actor,0,&copy,&s->actors))
        return 0;
    return finish_edit(actor,copy.actor,s->apply(s->context),sound,NULL,s);
}
int mm_actor_transaction_action(void *actor,MMI32 action,MMI32 cel,int frozen,MMU32 external_child,
    void **snapshot,const MMActorMotionServices *motion,const MMActorActionEditServices *edit,
    const MMActorTransactionServices *s)
{
    MMActorOwnerResult copy={NULL,NULL,NULL};
    MMActorOwner *owner;
    int applied;
    if (!mm_actor_owner_duplicate(actor,0,&copy,&s->actors))
        return 0;
    owner=s->actors.owner(s->actors.context,actor);
    applied=mm_actor_owner_set_action(owner,action,cel,frozen,external_child,motion,edit);
    return finish_edit(actor,copy.actor,applied,0,snapshot,s);
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
    MMActorOwnerResult copy={NULL,NULL,NULL};
    int value;
    if (!mm_actor_owner_duplicate(actor,0,&copy,&s->actors))
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
    if (!register_snapshot(actor,copy.actor,0,s))
    {
        mm_actor_owner_restore(actor,copy.actor,&s->actors);
        s->actors.release(s->actors.context,copy.actor);
        return 0;
    }
    s->actors.release(s->actors.context,copy.actor);
    return 1;
failed:
    d->diagnostic(d->context,MM_TRANSACTION_SEEK_FAILED);
    s->actors.release(s->actors.context,copy.actor);
    return 0;
}
