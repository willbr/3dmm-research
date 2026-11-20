/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_transaction.h"
#include <stddef.h>
static void release(void *snapshot,const MMActorTransactionServices *s)
{ if (snapshot) s->release(s->context,snapshot); }
int mm_actor_transaction_apply(int kind,MMActorTransactionResult *out,const MMActorTransactionServices *s)
{
    void *snapshot=NULL; int copied=s->duplicate(s->context,&snapshot);
    if (!copied && kind!=MM_TRANSACTION_COSTUME) return 0;
    if (!s->apply(s->context))
    {
        s->restore(s->context,snapshot);
        if (kind!=MM_TRANSACTION_REMOVE) release(snapshot,s);
        return 0;
    }
    if (!s->create_undo(s->context,snapshot,kind==MM_TRANSACTION_SOUND))
    { s->restore(s->context,snapshot); release(snapshot,s); return 0; }
    if (kind==MM_TRANSACTION_ACTION && out)
    { out->snapshot=snapshot; if (out->publish) out->publish(out->context,snapshot); }
    else release(snapshot,s);
    return 1;
}
static void alive(MMActorDeleteResult *out,int value,int trashed)
{
    if (!out) return;
    out->alive=value; out->trashed=trashed;
    if (out->publish) out->publish(out->context,value,trashed);
}
int mm_actor_transaction_delete(MMActorOwner *actor,int all,MMActorDeleteResult *out,const MMActorTransactionServices *s,
    const MMActorDeleteServices *d)
{
    MMActorRuntime *runtime=&actor->runtime;
    MMActorLifetime lifetime=mm_actor_owner_lifetime(actor);
    MMActorEvent entrance;
    MMI32 original;
    void *snapshot=NULL;
    int value;
    if (!s->duplicate(s->context,&snapshot)) { alive(out,0,1); return 0; }
    if (runtime->state.addEvent>=0 && !all &&
        mm_actor_events_get_fixed(actor->track.events,runtime->state.addEvent,&entrance) &&
        runtime->state.frame==entrance.nfrm)
    {
        if (!d->delete_subroute(d->context)) goto failed;
        if (out) alive(out,mm_actor_events_count(actor->track.events)>0,0);
    }
    else
    {
        original=runtime->state.nextEvent;
        if (!d->seek(d->context,mm_fixed_sub(runtime->state.frame,1))) goto failed;
        value=d->delete_forward(d->context,all,out!=NULL,original); alive(out,value,0);
        if (!d->seek(d->context,mm_fixed_add(runtime->state.frame,1))) goto failed;
        if (runtime->scene) d->sound_frame(d->context);
    }
    if (!mm_actor_refresh_lifetime(actor->track.route,actor->track.events,NULL,&lifetime,&d->timeline))
        d->diagnostic(d->context,MM_TRANSACTION_BAD_LIFETIME);
    if (!s->create_undo(s->context,snapshot,0))
    { s->restore(s->context,snapshot); release(snapshot,s); return 0; }
    release(snapshot,s); return 1;
failed:
    d->diagnostic(d->context,MM_TRANSACTION_SEEK_FAILED); release(snapshot,s); return 0;
}
