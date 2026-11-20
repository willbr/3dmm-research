/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_transaction.h"
#include <stddef.h>
static int register_snapshot(void *actor,void *snapshot,int sound,const MMActorTransactionServices *s)
{
    const MMActorOwner *owner=s->actors.owner(s->actors.context,actor);
    return mm_actor_undo_create(snapshot,owner->runtime.actor,sound,NULL,0,&s->actors,&s->undo);
}
int mm_actor_transaction_apply(void *actor,int kind,MMActorTransactionResult *out,const MMActorTransactionServices *s)
{
    MMActorOwnerResult copy={NULL,NULL,NULL};
    if (!mm_actor_owner_duplicate(actor,0,&copy,&s->actors))
        return 0;
    if (!s->apply(s->context) || !register_snapshot(actor,copy.actor,kind==MM_TRANSACTION_SOUND,s))
    {
        mm_actor_owner_restore(actor,copy.actor,&s->actors);
        s->actors.release(s->actors.context,copy.actor);
        return 0;
    }
    if (kind==MM_TRANSACTION_ACTION && out)
    {
        out->snapshot=copy.actor;
        if (out->publish) out->publish(out->context,copy.actor);
    }
    else s->actors.release(s->actors.context,copy.actor);
    return 1;
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
        if (!d->delete_subroute(d->context)) goto failed;
        if (out) alive(out,mm_actor_events_count(owner->track.events)>0,0);
    }
    else
    {
        original=runtime->state.nextEvent;
        if (!d->seek(d->context,mm_fixed_sub(runtime->state.frame,1))) goto failed;
        value=d->delete_forward(d->context,all,out!=NULL,original); alive(out,value,0);
        if (!d->seek(d->context,mm_fixed_add(runtime->state.frame,1))) goto failed;
        if (runtime->scene) d->sound_frame(d->context);
    }
    lifetime=mm_actor_owner_lifetime(owner);
    if (!mm_actor_refresh_lifetime(owner->track.route,owner->track.events,NULL,&lifetime,&d->timeline))
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
