/* Actor backward deletion and event propagation converted from actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_subroute_edit.h"
#include "mm/actor_effect.h"
#include <string.h>
#define FLAG(kind) ((MMU32)1 << (kind))
static void publish(const MMActorState *state,const MMActorSubrouteEditServices *s)
{ if (s->route.edits.publish) s->route.edits.publish(s->route.edits.context,state); }
static void diagnostic(const MMActorSubrouteEditServices *s,int fault)
{ if (s->diagnostic) s->diagnostic(s->route.edits.context,fault); }
unsigned short mm_actor_edit_average_angle(unsigned short a,unsigned short b,MMI32 weight)
{
    MMI32 result=0,other=mm_fixed_sub(65536L,weight),difference=(MMI32)a-(MMI32)b;
    if (difference<0) difference=-difference;
    if (difference>32768) result=b>a?weight:other;
    result=mm_fixed_add(result,mm_fixed_mul(a,weight));
    result=mm_fixed_add(result,mm_fixed_mul(b,other));
    if (result>65536L) result=(MMI32)((MMU32)result & 65535U);
    return (unsigned short)result;
}
void mm_actor_edit_add_orientation(MMActorState *state,void *events,unsigned short x,unsigned short y,
    unsigned short z,MMU32 axes,const MMI32 *distance,const MMActorMotionServices *m,
    const MMActorEditServices *s)
{
    MMActorEntrance add; MMI32 weight;
    m->payload(events,state->addEvent,&add);
    if (!distance) { add.x=x; add.y=y; add.z=z; }
    else
    {
        weight=mm_fixed_div(*distance,196608L);
        if (weight<6553) weight=6553;
        if (weight>=65536L) weight=65535L;
        if (axes & MM_ACTOR_ROTATE_X) add.x=mm_actor_edit_average_angle(x,add.x,weight);
        if (axes & MM_ACTOR_ROTATE_Y) add.y=mm_actor_edit_average_angle(y,add.y,weight);
        if (axes & MM_ACTOR_ROTATE_Z) add.z=mm_actor_edit_average_angle(z,add.z,weight);
    }
    s->tables.write_payload(s->tables.context,state->addEvent,&add);
    state->transform.xaPath=add.x; state->transform.yaPath=add.y; state->transform.zaPath=add.z;
    if (s->publish) s->publish(s->context,state);
}
void mm_actor_subroute_save_orientation(MMActorState *state,void *route,void *events,
    const MMActorMotionServices *m,const MMActorSubrouteEditServices *s)
{
    MMActorEvent event; MMActorRoutePoint point;
    if (state->addEvent<0 || s->action.text_template(s->action.context)) return;
    m->event(events,state->addEvent,&event); m->route_point(route,event.rtel.irpt,&point);
    if (point.dwr) mm_actor_edit_add_orientation(state,events,state->transform.xaPath,state->transform.yaPath,
        state->transform.zaPath,0,NULL,m,&s->route.edits);
}
void mm_actor_subroute_position_body(MMActorState *state,void *route,void *events,const MMActorPoint *position,
    const MMActorMotionServices *m,const MMActorSubrouteEditServices *s)
{
    MMActorPoseInputs inputs;
    MMActorBodyPose pose;
    publish(state,s); s->pose_inputs(s->route.edits.context,&inputs);
    mm_actor_pose_evaluate(state,route,events,&inputs,position,&pose,m);
    publish(state,s); s->locate_body(s->route.edits.context,&pose);
}
int mm_actor_subroute_lifetime(void *route,void *events,MMI32 *output,const MMActorSubrouteEditServices *s)
{
    MMActorSubrouteFlags flags; MMActorEditRange range; MMI32 last; int ok;
    s->get_flags(s->route.edits.context,&flags); s->route.edits.get_range(s->route.edits.context,&range);
    if (flags.recording)
    { if (output) *output=range.last; s->set_dirty(s->route.edits.context,1); return 1; }
    last=range.last;
    ok=mm_actor_lifetime(route,events,range.first,flags.rejoining,&last,2147483647L,&s->timeline);
    range.last=last; s->route.edits.publish_range(s->route.edits.context,&range);
    s->set_dirty(s->route.edits.context,!ok);
    if (ok && output) *output=last;
    return ok;
}
void mm_actor_subroute_delete_back(MMActorState *state,MMActorSounds *list,void *route,void *events,
    MMI32 actor,int *alive,const MMActorMotionServices *m,const MMActorSubrouteEditServices *s)
{
    const MMActorEditServices *e=&s->route.edits;
    MMActorEvent event; MMActorRoutePoint point; MMActorEditRange range;
    MMActorSubrouteFlags flags; MMI32 i,merged,new_distance,removed,action[2];
    e->get_range(e->context,&range);
    if (state->frame<=range.first) { if (alive) *alive=m->event_count(events)>0; return; }
    s->get_flags(e->context,&flags);
    if (flags.dirty && !mm_actor_subroute_lifetime(route,events,NULL,s)) diagnostic(s,MM_SUBROUTE_BAD_LIFETIME);
    m->route_point(route,state->location.irpt,&point);
    new_distance=mm_fixed_sub(point.dwr,state->location.dwrOffset);
    mm_actor_subroute_save_orientation(state,route,events,m,s);
    for (i=state->nextEvent;i<m->event_count(events);++i)
    {
        m->event(events,i,&event); if (event.rtel.irpt>state->location.irpt) break;
        if (event.rtel.dwrOffset==state->location.dwrOffset)
        {
            event.rtel.dwrOffset=0;
            if (event.rtel.dnfrm<state->location.dnfrm) { diagnostic(s,MM_SUBROUTE_BAD_HOLD); event.rtel.dnfrm=0; }
            else event.rtel.dnfrm=mm_fixed_sub(event.rtel.dnfrm,state->location.dnfrm);
        }
        event.rtel.dwrOffset=new_distance?mm_fixed_sub(event.rtel.dwrOffset,state->location.dwrOffset):0;
        s->route.write_event(events,i,&event);
    }
    mm_actor_route_position(route,&state->location,&point.xyz,m); point.dwr=new_distance;
    s->route.write_point(route,state->location.irpt,&point); removed=state->location.irpt;
    state->location.irpt=state->location.dwrOffset=state->location.dnfrm=0; publish(state,s);
    for (i=0;i<state->nextEvent;++i)
    {
        m->event(events,i,&event);
        if (!state->onStage || i<state->addEvent) { mm_actor_edit_remove(state,events,i,1,m,e); --i; continue; }
        switch (event.aet)
        {
        case MM_ACTOR_ROTATE_HERE: e->tables.write_payload(e->tables.context,i,&state->transform.bmat34Cur); goto merge;
        case MM_ACTOR_TWEAK: case MM_ACTOR_SOUND:
            if (i<state->firstEventInFrame) { mm_actor_edit_remove(state,events,i,1,m,e); --i; break; }
            if (i>0) goto merge;
            break;
        case MM_ACTOR_ACTION:
            m->payload(events,i,action); action[1]=state->cel; e->tables.write_payload(e->tables.context,i,action); goto merge;
        case MM_ACTOR_REMOVE:
            mm_actor_edit_remove(state,events,i,1,m,e); --i; diagnostic(s,MM_SUBROUTE_OFFSTAGE_EVENT); break;
        default:
merge:
            event.rtel=state->location; event.nfrm=state->frame; s->route.write_event(events,i,&event);
            if (i>0) mm_actor_edit_merge(state,list,events,actor,0,i,&merged,m,e);
            else merged=i;
            if (merged<i) --i;
            if (event.aet==MM_ACTOR_ADD) state->addEvent=merged;
            if (event.aet==MM_ACTOR_ACTION) state->actionEvent=merged;
            publish(state,s);
            break;
        }
    }
    if (removed>0)
    {
        if (!state->onStage) ++removed;
        for (i=state->nextEvent;i<m->event_count(events);++i)
        { m->event(events,i,&event); event.rtel.irpt=mm_fixed_sub(event.rtel.irpt,removed); s->route.write_event(events,i,&event); }
        s->route.delete_points(route,0,removed);
    }
    if (alive) *alive=m->event_count(events)>0;
    if (!m->event_count(events)) mm_actor_route_edit_reset(state,route,m,&s->route);
    else
    {
        m->event(events,0,&event); e->get_range(e->context,&range); range.first=event.nfrm; e->publish_range(e->context,&range);
        s->set_dirty(e->context,1); state->firstEventInFrame=0; publish(state,s);
    }
    if (!mm_actor_subroute_lifetime(route,events,NULL,s)) diagnostic(s,MM_SUBROUTE_BAD_LIFETIME);
    e->invalidate_range(e->context);
}
static int copy_apply(MMActorState *state,MMActorSounds *list,void *events,MMI32 actor,MMI32 source,
    MMActorEvent *event,int merge,const MMActorMotionServices *m,const MMActorSubrouteEditServices *s)
{
    const MMActorEditServices *e=&s->route.edits; MMI32 result=state->nextEvent;
    MMI32 bytes=m->event_size(events,source);
    if (!mm_actor_edit_insert(state,state->nextEvent,(MMU32)bytes,NULL,event,1,e)) return 0;
    if (!merge) ++source;
    e->tables.copy_payload(e->tables.context,state->nextEvent,source);
    e->tables.duplicate_event_tag(e->tables.context,state->nextEvent,e->tables.tags);
    if (merge) mm_actor_edit_merge(state,list,events,actor,state->firstEventInFrame,state->nextEvent,&result,m,e);
    publish(state,s);
    if (!e->apply_event(e->context,state,result)) { publish(state,s); return 0; }
    if (result==state->nextEvent) ++state->nextEvent;
    publish(state,s); return 1;
}
int mm_actor_subroute_from_previous(MMActorState *state,MMActorSounds *list,void *route,void *events,
    MMI32 actor,MMI32 limit,MMU32 mask,const MMActorMotionServices *m,const MMActorSubrouteEditServices *s)
{
    const MMActorEditServices *e=&s->route.edits;
    MMActorEvent event,current; MMI32 first,last,i,saved; int pruned=0;
    first=mm_actor_route_edit_find(events,MM_ACTOR_ADD,limit,1,m);
    last=mm_actor_route_edit_last(events,first,limit,m); m->event(events,state->nextEvent-1,&current);
    for (i=last;i>first;--i)
    {
        if (!mask) break;
        m->event(events,i,&event);
        if (!(mask & FLAG(event.aet)) || event.aet==MM_ACTOR_COSTUME) continue;
        mask^=FLAG(event.aet); event.rtel=current.rtel;
        if (!copy_apply(state,list,events,actor,i,&event,1,m,s)) return 0;
    }
    if (!(mask & FLAG(MM_ACTOR_COSTUME))) return 1;
    for (i=first+1;i<=last;++i)
    {
        m->event(events,i,&event); if (event.aet!=MM_ACTOR_COSTUME) continue;
        event.rtel=current.rtel; if (!copy_apply(state,list,events,actor,i,&event,1,m,s)) return 0;
    }
    if (first<0) return 1;
    m->event(events,first,&event);
    if (state->frame==event.nfrm)
    { mm_actor_route_edit_delete_add(state,route,events,first,state->addEvent,m,&s->route); return 1; }
    for (i=state->addEvent-1;i>=first;--i)
    { m->event(events,i,&event); if (state->frame==event.nfrm) { pruned=1; mm_actor_edit_remove(state,events,i,1,m,e); } }
    if (!pruned) return 1;
    saved=state->frame;
    if (!s->seek(e->context,state,mm_fixed_sub(saved,1))) return 0;
    mm_actor_route_edit_delete_forward(state,list,route,events,actor,0,NULL,state->nextEvent,m,&s->route);
    return s->seek(e->context,state,saved);
}
int mm_actor_subroute_from_later(MMActorState *state,MMActorSounds *list,void *route,void *events,
    MMI32 actor,MMU32 external_child,const MMActorMotionServices *m,const MMActorSubrouteEditServices *s)
{
    MMActorEvent event; MMActorLocation location; MMI32 i,start=-1; int position=0;
    start=mm_actor_route_edit_find(events,MM_ACTOR_ADD,state->nextEvent,0,m);
    if (start>=0)
    {
        m->event(events,start,&event); location=event.rtel;
        for (i=start;i<m->event_count(events);++i)
        {
            m->event(events,i,&event);
            if (event.rtel.irpt!=location.irpt || event.rtel.dwrOffset!=location.dwrOffset || event.rtel.dnfrm!=location.dnfrm) break;
            if (event.aet!=MM_ACTOR_COSTUME && event.aet!=MM_ACTOR_STRETCH &&
                event.aet!=MM_ACTOR_SIZE && event.aet!=MM_ACTOR_ROTATE_FORWARD) continue;
            event.rtel=state->location;
            if (!copy_apply(state,list,events,actor,i,&event,0,m,s)) return 0;
            ++i; if (event.aet!=MM_ACTOR_COSTUME) position=1;
        }
        if (position) mm_actor_subroute_position_body(state,route,events,&state->position,m,s);
    }
    return mm_actor_edit_set_action(state,list,events,actor,0,0,0,external_child,m,&s->action);
}
