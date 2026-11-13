/* Actor costume editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_costume_edit.h"
#include <string.h>
static void release(const ThreeMMActorCostumeEditServices *s, void **p)
{
    if (*p) s->release(s->context,*p);
    *p=NULL;
}
void three_mm_actor_edit_costume_fill(ThreeMMActorState *state, void *events, ThreeMMI32 first,
    const ThreeMMActorCostume *incoming, const ThreeMMActorMotionServices *m, const ThreeMMActorCostumeEditServices *s)
{
    ThreeMMI32 i;
    ThreeMMActorEvent event;
    ThreeMMActorCostume costume;
    void *material=NULL, *custom=NULL, *compare_material=NULL, *compare_custom=NULL, *current;
    int was_custom, replace=1, equal;
    current=s->current(s->context,incoming->body_set,&was_custom);
    if (was_custom) compare_custom=current;
    else compare_material=current;
    for (i=first; i<m->event_count(events); ++i)
    {
        m->event(events,i,&event);
        if (event.aet==THREE_MM_ACTOR_ADD) break;
        if (event.aet!=THREE_MM_ACTOR_COSTUME) continue;
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
        three_mm_actor_edit_remove(state,events,i,1,m,&s->edits); --i;
    }
    if (!replace) { release(s,&compare_material); release(s,&compare_custom); }
    release(s,&custom); release(s,&material);
}
int three_mm_actor_edit_set_costume(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events, ThreeMMI32 actor,
    ThreeMMI32 clicked_set, const ThreeMMTag *tag, ThreeMMI32 costume, ThreeMMI32 custom,
    const ThreeMMActorMotionServices *m, const ThreeMMActorCostumeEditServices *s)
{
    ThreeMMActorCostume value;
    void *resource;
    ThreeMMI32 set=clicked_set;
    if (custom)
    {
        resource=s->fetch_custom(s->context,costume);
        if (!resource) return 0;
        set=s->body_set(s->context,resource);
        release(s,&resource);
    }
    memset(&value,0,sizeof(value));
    value.body_set=set; value.tag=*tag; value.costume=costume; value.custom=custom;
    if (!s->reserve_events(s->context,1,sizeof(value))) return 0;
    three_mm_actor_edit_costume_fill(state,events,state->nextEvent,&value,m,s);
    return three_mm_actor_edit_add_apply(state,list,events,actor,THREE_MM_ACTOR_COSTUME,sizeof(value),&value,m,&s->edits);
}
