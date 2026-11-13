/* Actor backward deletion and event propagation converted from actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_subroute_edit.h"
#include "three_mm/actor_effect.h"
#include <string.h>
#define FLAG(kind) ((ThreeMMU32)1 << (kind))
static void publish(const ThreeMMActorState *state,const ThreeMMActorSubrouteEditServices *s)
{ if (s->route.edits.publish) s->route.edits.publish(s->route.edits.context,state); }
static void diagnostic(const ThreeMMActorSubrouteEditServices *s,int fault)
{ if (s->diagnostic) s->diagnostic(s->route.edits.context,fault); }
unsigned short three_mm_actor_edit_average_angle(unsigned short a,unsigned short b,ThreeMMI32 weight)
{
    ThreeMMI32 result=0,other=three_mm_fixed_sub(65536L,weight),difference=(ThreeMMI32)a-(ThreeMMI32)b;
    if (difference<0) difference=-difference;
    if (difference>32768) result=b>a?weight:other;
    result=three_mm_fixed_add(result,three_mm_fixed_mul(a,weight));
    result=three_mm_fixed_add(result,three_mm_fixed_mul(b,other));
    if (result>65536L) result=(ThreeMMI32)((ThreeMMU32)result & 65535U);
    return (unsigned short)result;
}
void three_mm_actor_edit_add_orientation(ThreeMMActorState *state,void *events,unsigned short x,unsigned short y,
    unsigned short z,ThreeMMU32 axes,const ThreeMMI32 *distance,const ThreeMMActorMotionServices *m,
    const ThreeMMActorEditServices *s)
{
    ThreeMMActorEntrance add; ThreeMMI32 weight;
    m->payload(events,state->addEvent,&add);
    if (!distance) { add.x=x; add.y=y; add.z=z; }
    else
    {
        weight=three_mm_fixed_div(*distance,196608L);
        if (weight<6553) weight=6553;
        if (weight>=65536L) weight=65535L;
        if (axes & THREE_MM_ACTOR_ROTATE_X) add.x=three_mm_actor_edit_average_angle(x,add.x,weight);
        if (axes & THREE_MM_ACTOR_ROTATE_Y) add.y=three_mm_actor_edit_average_angle(y,add.y,weight);
        if (axes & THREE_MM_ACTOR_ROTATE_Z) add.z=three_mm_actor_edit_average_angle(z,add.z,weight);
    }
    s->write_payload(s->context,state->addEvent,&add);
    state->transform.xaPath=add.x; state->transform.yaPath=add.y; state->transform.zaPath=add.z;
    if (s->publish) s->publish(s->context,state);
}
void three_mm_actor_subroute_save_orientation(ThreeMMActorState *state,void *route,void *events,
    const ThreeMMActorMotionServices *m,const ThreeMMActorSubrouteEditServices *s)
{
    ThreeMMActorEvent event; ThreeMMActorRoutePoint point;
    if (state->addEvent<0 || s->action.text_template(s->action.context)) return;
    m->event(events,state->addEvent,&event); m->route_point(route,event.rtel.irpt,&point);
    if (point.dwr) three_mm_actor_edit_add_orientation(state,events,state->transform.xaPath,state->transform.yaPath,
        state->transform.zaPath,0,NULL,m,&s->route.edits);
}
void three_mm_actor_subroute_position_body(ThreeMMActorState *state,void *route,void *events,const ThreeMMActorPoint *position,
    const ThreeMMActorMotionServices *m,const ThreeMMActorSubrouteEditServices *s)
{
    ThreeMMActorPoseInputs inputs; ThreeMMActorMatrix matrix; ThreeMMActorPoint absolute;
    publish(state,s); s->pose_inputs(s->route.edits.context,&inputs);
    three_mm_actor_pose_update(state,route,events,&inputs,position,&matrix,m);
    absolute.dxr=three_mm_fixed_add(state->routeTranslation.dxr,position->dxr);
    absolute.dyr=three_mm_fixed_add(state->routeTranslation.dyr,position->dyr);
    absolute.dzr=three_mm_fixed_add(state->routeTranslation.dzr,position->dzr);
    publish(state,s); s->locate_body(s->route.edits.context,&absolute,&matrix);
}
int three_mm_actor_subroute_lifetime(void *route,void *events,ThreeMMI32 *output,const ThreeMMActorSubrouteEditServices *s)
{
    ThreeMMActorSubrouteFlags flags; ThreeMMActorEditRange range; ThreeMMI32 last; int ok;
    s->get_flags(s->route.edits.context,&flags); s->route.edits.get_range(s->route.edits.context,&range);
    if (flags.recording)
    { if (output) *output=range.last; s->set_dirty(s->route.edits.context,1); return 1; }
    last=range.last;
    ok=three_mm_actor_lifetime(route,events,range.first,flags.rejoining,&last,2147483647L,&s->timeline);
    range.last=last; s->route.edits.publish_range(s->route.edits.context,&range);
    s->set_dirty(s->route.edits.context,!ok);
    if (ok && output) *output=last;
    return ok;
}
void three_mm_actor_subroute_delete_back(ThreeMMActorState *state,ThreeMMActorSounds *list,void *route,void *events,
    ThreeMMI32 actor,int *alive,const ThreeMMActorMotionServices *m,const ThreeMMActorSubrouteEditServices *s)
{
    const ThreeMMActorEditServices *e=&s->route.edits;
    ThreeMMActorEvent event; ThreeMMActorRoutePoint point; ThreeMMActorEditRange range;
    ThreeMMActorSubrouteFlags flags; ThreeMMI32 i,merged,new_distance,removed,action[2];
    e->get_range(e->context,&range);
    if (state->frame<=range.first) { if (alive) *alive=m->event_count(events)>0; return; }
    s->get_flags(e->context,&flags);
    if (flags.dirty && !three_mm_actor_subroute_lifetime(route,events,NULL,s)) diagnostic(s,THREE_MM_SUBROUTE_BAD_LIFETIME);
    m->route_point(route,state->location.irpt,&point);
    new_distance=three_mm_fixed_sub(point.dwr,state->location.dwrOffset);
    three_mm_actor_subroute_save_orientation(state,route,events,m,s);
    for (i=state->nextEvent;i<m->event_count(events);++i)
    {
        m->event(events,i,&event); if (event.rtel.irpt>state->location.irpt) break;
        if (event.rtel.dwrOffset==state->location.dwrOffset)
        {
            event.rtel.dwrOffset=0;
            if (event.rtel.dnfrm<state->location.dnfrm) { diagnostic(s,THREE_MM_SUBROUTE_BAD_HOLD); event.rtel.dnfrm=0; }
            else event.rtel.dnfrm=three_mm_fixed_sub(event.rtel.dnfrm,state->location.dnfrm);
        }
        event.rtel.dwrOffset=new_distance?three_mm_fixed_sub(event.rtel.dwrOffset,state->location.dwrOffset):0;
        s->route.write_event(events,i,&event);
    }
    three_mm_actor_route_position(route,&state->location,&point.xyz,m); point.dwr=new_distance;
    s->route.write_point(route,state->location.irpt,&point); removed=state->location.irpt;
    state->location.irpt=state->location.dwrOffset=state->location.dnfrm=0; publish(state,s);
    for (i=0;i<state->nextEvent;++i)
    {
        m->event(events,i,&event);
        if (!state->onStage || i<state->addEvent) { three_mm_actor_edit_remove(state,events,i,1,m,e); --i; continue; }
        switch (event.aet)
        {
        case THREE_MM_ACTOR_ROTATE_HERE: e->write_payload(e->context,i,&state->transform.bmat34Cur); goto merge;
        case THREE_MM_ACTOR_TWEAK: case THREE_MM_ACTOR_SOUND:
            if (i<state->firstEventInFrame) { three_mm_actor_edit_remove(state,events,i,1,m,e); --i; break; }
            if (i>0) goto merge;
            break;
        case THREE_MM_ACTOR_ACTION:
            m->payload(events,i,action); action[1]=state->cel; e->write_payload(e->context,i,action); goto merge;
        case THREE_MM_ACTOR_REMOVE:
            three_mm_actor_edit_remove(state,events,i,1,m,e); --i; diagnostic(s,THREE_MM_SUBROUTE_OFFSTAGE_EVENT); break;
        default:
merge:
            event.rtel=state->location; event.nfrm=state->frame; s->route.write_event(events,i,&event);
            if (i>0) three_mm_actor_edit_merge(state,list,events,actor,0,i,&merged,m,e);
            else merged=i;
            if (merged<i) --i;
            if (event.aet==THREE_MM_ACTOR_ADD) state->addEvent=merged;
            if (event.aet==THREE_MM_ACTOR_ACTION) state->actionEvent=merged;
            publish(state,s);
            break;
        }
    }
    if (removed>0)
    {
        if (!state->onStage) ++removed;
        for (i=state->nextEvent;i<m->event_count(events);++i)
        { m->event(events,i,&event); event.rtel.irpt=three_mm_fixed_sub(event.rtel.irpt,removed); s->route.write_event(events,i,&event); }
        s->route.delete_points(route,0,removed);
    }
    if (alive) *alive=m->event_count(events)>0;
    if (!m->event_count(events)) three_mm_actor_route_edit_reset(state,route,m,&s->route);
    else
    {
        m->event(events,0,&event); e->get_range(e->context,&range); range.first=event.nfrm; e->publish_range(e->context,&range);
        s->set_dirty(e->context,1); state->firstEventInFrame=0; publish(state,s);
    }
    if (!three_mm_actor_subroute_lifetime(route,events,NULL,s)) diagnostic(s,THREE_MM_SUBROUTE_BAD_LIFETIME);
    e->invalidate_range(e->context);
}
static int copy_apply(ThreeMMActorState *state,ThreeMMActorSounds *list,void *events,ThreeMMI32 actor,ThreeMMI32 source,
    ThreeMMActorEvent *event,int merge,const ThreeMMActorMotionServices *m,const ThreeMMActorSubrouteEditServices *s)
{
    const ThreeMMActorEditServices *e=&s->route.edits; ThreeMMI32 result=state->nextEvent;
    ThreeMMI32 bytes=m->event_size(events,source);
    if (!three_mm_actor_edit_insert(state,state->nextEvent,(ThreeMMU32)bytes,NULL,event,1,e)) return 0;
    if (!merge) ++source;
    e->copy_payload(e->context,state->nextEvent,source);
    e->duplicate_event_tag(e->context,state->nextEvent);
    if (merge) three_mm_actor_edit_merge(state,list,events,actor,state->firstEventInFrame,state->nextEvent,&result,m,e);
    publish(state,s);
    if (!e->apply_event(e->context,state,result)) { publish(state,s); return 0; }
    if (result==state->nextEvent) ++state->nextEvent;
    publish(state,s); return 1;
}
int three_mm_actor_subroute_from_previous(ThreeMMActorState *state,ThreeMMActorSounds *list,void *route,void *events,
    ThreeMMI32 actor,ThreeMMI32 limit,ThreeMMU32 mask,const ThreeMMActorMotionServices *m,const ThreeMMActorSubrouteEditServices *s)
{
    const ThreeMMActorEditServices *e=&s->route.edits;
    ThreeMMActorEvent event,current; ThreeMMI32 first,last,i,saved; int pruned=0;
    first=three_mm_actor_route_edit_find(events,THREE_MM_ACTOR_ADD,limit,1,m);
    last=three_mm_actor_route_edit_last(events,first,limit,m); m->event(events,state->nextEvent-1,&current);
    for (i=last;i>first;--i)
    {
        if (!mask) break;
        m->event(events,i,&event);
        if (!(mask & FLAG(event.aet)) || event.aet==THREE_MM_ACTOR_COSTUME) continue;
        mask^=FLAG(event.aet); event.rtel=current.rtel;
        if (!copy_apply(state,list,events,actor,i,&event,1,m,s)) return 0;
    }
    if (!(mask & FLAG(THREE_MM_ACTOR_COSTUME))) return 1;
    for (i=first+1;i<=last;++i)
    {
        m->event(events,i,&event); if (event.aet!=THREE_MM_ACTOR_COSTUME) continue;
        event.rtel=current.rtel; if (!copy_apply(state,list,events,actor,i,&event,1,m,s)) return 0;
    }
    if (first<0) return 1;
    m->event(events,first,&event);
    if (state->frame==event.nfrm)
    { three_mm_actor_route_edit_delete_add(state,route,events,first,state->addEvent,m,&s->route); return 1; }
    for (i=state->addEvent-1;i>=first;--i)
    { m->event(events,i,&event); if (state->frame==event.nfrm) { pruned=1; three_mm_actor_edit_remove(state,events,i,1,m,e); } }
    if (!pruned) return 1;
    saved=state->frame;
    if (!s->seek(e->context,state,three_mm_fixed_sub(saved,1))) return 0;
    three_mm_actor_route_edit_delete_forward(state,list,route,events,actor,0,NULL,state->nextEvent,m,&s->route);
    return s->seek(e->context,state,saved);
}
int three_mm_actor_subroute_from_later(ThreeMMActorState *state,ThreeMMActorSounds *list,void *route,void *events,
    ThreeMMI32 actor,ThreeMMU32 external_child,const ThreeMMActorMotionServices *m,const ThreeMMActorSubrouteEditServices *s)
{
    ThreeMMActorEvent event; ThreeMMActorLocation location; ThreeMMI32 i,start=-1; int position=0;
    start=three_mm_actor_route_edit_find(events,THREE_MM_ACTOR_ADD,state->nextEvent,0,m);
    if (start>=0)
    {
        m->event(events,start,&event); location=event.rtel;
        for (i=start;i<m->event_count(events);++i)
        {
            m->event(events,i,&event);
            if (event.rtel.irpt!=location.irpt || event.rtel.dwrOffset!=location.dwrOffset || event.rtel.dnfrm!=location.dnfrm) break;
            if (event.aet!=THREE_MM_ACTOR_COSTUME && event.aet!=THREE_MM_ACTOR_STRETCH &&
                event.aet!=THREE_MM_ACTOR_SIZE && event.aet!=THREE_MM_ACTOR_ROTATE_FORWARD) continue;
            event.rtel=state->location;
            if (!copy_apply(state,list,events,actor,i,&event,0,m,s)) return 0;
            ++i; if (event.aet!=THREE_MM_ACTOR_COSTUME) position=1;
        }
        if (position) three_mm_actor_subroute_position_body(state,route,events,&state->position,m,s);
    }
    return three_mm_actor_edit_set_action(state,list,events,actor,0,0,0,external_child,m,&s->action);
}
