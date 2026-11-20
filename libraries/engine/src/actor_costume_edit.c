/* Actor costume editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_costume_edit.h"
#include "mm/actor_owner.h"
#include <string.h>
int mm_actor_owner_set_costume(MMActorOwner *owner, MMI32 clicked_set, const MMTag *tag,
    MMI32 costume, MMI32 custom, const MMActorCostumeEditServices *s)
{
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorState state = owner->runtime.state;
    int ok = mm_actor_edit_set_costume(&state, &owner->sounds, owner->track.events, owner->runtime.actor,
                                       clicked_set, tag, costume, custom, &motion, s);
    owner->runtime.state = state;
    return ok;
}
static void release(const MMActorCostumeEditServices *s, void **p)
{
    if (*p) s->release(s->context,*p);
    *p=NULL;
}
void mm_actor_edit_costume_fill(MMActorState *state, void *events, MMI32 first,
    const MMActorCostume *incoming, const MMActorMotionServices *m, const MMActorCostumeEditServices *s)
{
    MMI32 i;
    MMActorEvent event;
    MMActorCostume costume;
    void *material=NULL, *custom=NULL, *compare_material=NULL, *compare_custom=NULL, *current;
    int was_custom, replace=1, equal;
    current=s->current(s->context,incoming->body_set,&was_custom);
    if (was_custom) compare_custom=current;
    else compare_material=current;
    for (i=first; i<m->event_count(events); ++i)
    {
        m->event(events,i,&event);
        if (event.aet==MM_ACTOR_ADD) break;
        if (event.aet!=MM_ACTOR_COSTUME) continue;
        m->payload(events,i,&costume);
        if (costume.body_set!=incoming->body_set) continue;
        if (replace)
        {
            equal=0;
            if (was_custom==costume.custom)
            {
                if (was_custom) { custom=s->fetch_custom(s->context,costume.costume); equal=compare_custom==custom; }
                else { material=s->fetch_material(s->context,&costume.tag); equal=compare_material==material; }
            }
            if (equal) goto remove;
            replace=0;
            if (!incoming->custom)
            { compare_material=s->fetch_material(s->context,&incoming->tag); compare_custom=NULL; }
            else
            { compare_custom=s->fetch_custom(s->context,incoming->costume); compare_material=NULL; }
        }
        release(s,&custom); release(s,&material);
        if (incoming->custom!=costume.custom) break;
        if (!costume.custom)
        { material=s->fetch_material(s->context,&costume.tag); if (compare_material!=material) break; }
        else
        { custom=s->fetch_custom(s->context,costume.costume); if (compare_custom!=custom) break; }
remove:
        release(s,&custom); release(s,&material);
        mm_actor_edit_remove(state,events,i,1,m,&s->edits); --i;
    }
    if (!replace) { release(s,&compare_material); release(s,&compare_custom); }
    release(s,&custom); release(s,&material);
}
int mm_actor_edit_set_costume(MMActorState *state, MMActorSounds *list, void *events, MMI32 actor,
    MMI32 clicked_set, const MMTag *tag, MMI32 costume, MMI32 custom,
    const MMActorMotionServices *m, const MMActorCostumeEditServices *s)
{
    MMActorCostume value;
    void *resource;
    MMI32 set=clicked_set;
    if (custom)
    {
        resource=s->fetch_custom(s->context,costume);
        if (!resource) return 0;
        set=s->body_set(s->context,resource);
        release(s,&resource);
    }
    memset(&value,0,sizeof(value));
    value.body_set=set; value.tag=*tag; value.costume=costume; value.custom=custom;
    if (!mm_actor_edit_reserve(&s->edits,1,sizeof(value))) return 0;
    mm_actor_edit_costume_fill(state,events,state->nextEvent,&value,m,s);
    return mm_actor_edit_add_apply(state,list,events,actor,MM_ACTOR_COSTUME,sizeof(value),&value,m,&s->edits);
}
