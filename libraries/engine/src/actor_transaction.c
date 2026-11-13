/* Actor edit transactions converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_transaction.h"
#include <stddef.h>
static void release(void *snapshot,const ThreeMMActorTransactionServices *s)
{ if (snapshot) s->release(s->context,snapshot); }
int three_mm_actor_transaction_apply(int kind,ThreeMMActorTransactionResult *out,const ThreeMMActorTransactionServices *s)
{
    void *snapshot=NULL; int copied=s->duplicate(s->context,&snapshot);
    if (!copied && kind!=THREE_MM_TRANSACTION_COSTUME) return 0;
    if (!s->apply(s->context))
    {
        s->restore(s->context,snapshot);
        if (kind!=THREE_MM_TRANSACTION_REMOVE) release(snapshot,s);
        return 0;
    }
    if (!s->create_undo(s->context,snapshot))
    { s->restore(s->context,snapshot); release(snapshot,s); return 0; }
    if (kind==THREE_MM_TRANSACTION_ACTION && out)
    { out->snapshot=snapshot; if (out->publish) out->publish(out->context,snapshot); }
    else release(snapshot,s);
    return 1;
}
static void alive(ThreeMMActorDeleteResult *out,int value,int trashed)
{
    if (!out) return;
    out->alive=value; out->trashed=trashed;
    if (out->publish) out->publish(out->context,value,trashed);
}
int three_mm_actor_transaction_delete(int all,ThreeMMActorDeleteResult *out,const ThreeMMActorTransactionServices *s,
    const ThreeMMActorDeleteServices *d)
{
    ThreeMMActorDeleteState state; ThreeMMI32 original; void *snapshot=NULL; int value;
    if (!s->duplicate(s->context,&snapshot)) { alive(out,0,1); return 0; }
    d->read(d->context,&state);
    if (state.add_event>=0 && !all && state.frame==d->add_frame(d->context,state.add_event))
    {
        if (!d->delete_subroute(d->context)) goto failed;
        if (out) alive(out,d->event_count(d->context)>0,0);
    }
    else
    {
        original=state.next_event;
        if (!d->seek(d->context,three_mm_fixed_sub(state.frame,1))) goto failed;
        value=d->delete_forward(d->context,all,out!=NULL,original); alive(out,value,0);
        d->read(d->context,&state);
        if (!d->seek(d->context,three_mm_fixed_add(state.frame,1))) goto failed;
        d->read(d->context,&state); if (state.scene_present) d->sound_frame(d->context);
    }
    if (!d->lifetime(d->context)) d->diagnostic(d->context,THREE_MM_TRANSACTION_BAD_LIFETIME);
    if (!s->create_undo(s->context,snapshot))
    { s->restore(s->context,snapshot); release(snapshot,s); return 0; }
    release(snapshot,s); return 1;
failed:
    d->diagnostic(d->context,THREE_MM_TRANSACTION_SEEK_FAILED); release(snapshot,s); return 0;
}
