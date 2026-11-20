/* Actor route editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_route_edit.h"
#include "mm/actor_action_edit.h"
#include "mm/actor_path.h"
#include "mm/actor_timeline.h"
#include <string.h>

static int reserve_points(void *route, MMU32 count)
{
    return mm_actor_path_reserve((MMActorPath *)route, count);
}

static int insert_point(void *route, MMI32 index, const MMActorRoutePoint *point)
{
    return mm_actor_path_insert((MMActorPath *)route, index, point);
}

static void write_point(void *route, MMI32 index, const MMActorRoutePoint *point)
{
    mm_actor_path_put((MMActorPath *)route, index, point);
}

static void delete_points(void *route, MMI32 index, MMI32 count)
{
    mm_array_delete(&((MMActorPath *)route)->rows, index, count);
}

static void clear_route(void *route)
{
    mm_array_set_count(&((MMActorPath *)route)->rows, 0);
}

static void write_event(void *events, MMI32 index, const MMActorEvent *event)
{
    mm_actor_events_set_fixed((MMActorEventTable *)events, index, event);
}

static void swap_events(void *events, MMI32 a, MMI32 b)
{
    mm_actor_events_swap((MMActorEventTable *)events, a, b);
}

void mm_actor_route_edit_bind_tables(MMActorRouteEditServices *s)
{
    s->reserve_points = reserve_points;
    s->insert_point = insert_point;
    s->write_point = write_point;
    s->delete_points = delete_points;
    s->clear_route = clear_route;
    s->write_event = write_event;
    s->swap_events = swap_events;
}

static void publish(const MMActorState *state,const MMActorRouteEditServices *s)
{ if (s->edits.publish) s->edits.publish(s->edits.context,state); }
static void life(const MMActorState *state,const MMActorRouteEditServices *s)
{ publish(state,s); s->life_dirty(s->edits.context); }
int mm_actor_route_edit_insert(void *route,MMI32 node,MMActorRoutePoint *point,MMI32 prior,
    const MMActorMotionServices *m,const MMActorRouteEditServices *s)
{
    MMActorRoutePoint old; MMI32 total=0;
    if (!s->insert_point(route,node,point)) return 0;
    if (node>0)
    {
        m->route_point(route,node-1,&old); total=old.dwr;
        if (old.dwr) { old.dwr=prior?prior:1; s->write_point(route,node-1,&old); }
    }
    if (node<m->route_count(route)-1)
    {
        m->route_point(route,node+1,&old);
        if (point->dwr)
        {
            point->dwr=mm_fixed_sub(total,prior);
            if (point->dwr<=0) point->dwr=1;
            s->write_point(route,node,point);
        }
    }
    return 1;
}
void mm_actor_route_edit_adjust_insert(void *route,void *events,MMI32 node,MMI32 first,
    const MMActorMotionServices *m,const MMActorRouteEditServices *s)
{
    MMActorRoutePoint point; MMActorEvent event; MMI32 i,back=0,count=m->route_count(route);
    if (node>0) { m->route_point(route,node-1,&point); back=point.dwr; }
    m->route_point(route,node,&point);
    for (i=first;i<m->event_count(events);++i)
    {
        m->event(events,i,&event);
        if (event.rtel.irpt>=node) ++event.rtel.irpt;
        else if (event.rtel.irpt<node-1) continue;
        else
        {
            if (node==count-1 || !back) continue;
            if (event.rtel.dwrOffset>=back)
            { event.rtel.dwrOffset=mm_fixed_sub(event.rtel.dwrOffset,back); ++event.rtel.irpt; }
        }
        s->write_event(events,i,&event);
    }
}
void mm_actor_route_edit_adjust_delete(MMActorState *state,void *route,void *events,MMI32 node,MMI32 first,
    const MMActorMotionServices *m,const MMActorRouteEditServices *s)
{
    MMActorRoutePoint point; MMActorEvent event; MMI32 i,back=0;
    if (node) { m->route_point(route,node-1,&point); back=point.dwr; }
    m->route_point(route,node,&point);
    for (i=first;i<m->event_count(events);++i)
    {
        m->event(events,i,&event);
        if (event.rtel.irpt>node) --event.rtel.irpt;
        else if (event.rtel.irpt==node)
        {
            if (!point.dwr) { mm_actor_edit_remove(state,events,i,1,m,&s->edits); --i; continue; }
            event.rtel.dwrOffset=mm_fixed_add(back,event.rtel.dwrOffset); --event.rtel.irpt;
        }
        else continue;
        s->write_event(events,i,&event);
    }
}
MMI32 mm_actor_route_edit_find(void *events,MMI32 kind,MMI32 start,int backwards,
    const MMActorMotionServices *m)
{
    MMActorEvent event; MMI32 i=backwards?start-1:start;
    for (;i>=0 && i<m->event_count(events);i+=backwards?-1:1)
    { m->event(events,i,&event); if (event.aet==kind) return i; }
    return -1;
}
MMI32 mm_actor_route_edit_last(void *events,MMI32 first,MMI32 limit,const MMActorMotionServices *m)
{
    MMActorTimelineServices timeline; MMActorEvent event; MMI32 last;
    if (limit==-1) limit=m->event_count(events);
    m->event(events,limit-1,&event); memset(&timeline,0,sizeof(timeline)); timeline.motion=*m;
    if (!mm_actor_stalled(events,first,&event.rtel,&last,&timeline)) last=limit-1;
    return last;
}
void mm_actor_route_edit_delete_add(MMActorState *state,void *route,void *events,MMI32 first,MMI32 limit,
    const MMActorMotionServices *m,const MMActorRouteEditServices *s)
{
    MMActorEvent event; MMI32 i,node;
    m->event(events,first,&event); node=event.rtel.irpt;
    for (i=limit-1;i>=first;--i) mm_actor_edit_remove(state,events,i,1,m,&s->edits);
    if (first>0)
    {
        m->event(events,first-1,&event);
        if (event.rtel.irpt==node || (event.rtel.irpt==node-1 && event.rtel.dwrOffset>0)) return;
    }
    mm_actor_route_edit_adjust_delete(state,route,events,node,first,m,s);
    s->delete_points(route,node,1);
    if (state->location.irpt>node) --state->location.irpt;
    publish(state,s);
}
void mm_actor_route_edit_truncate(MMActorState *state,void *route,void *events,MMI32 limit,
    const MMActorMotionServices *m,const MMActorRouteEditServices *s)
{
    MMActorRoutePoint a,b; MMActorEvent event; MMI32 node=state->location.irpt,i;
    if (!state->location.dwrOffset) return;
    m->route_point(route,node,&a); a.dwr=state->location.dwrOffset; s->write_point(route,node,&a);
    mm_actor_route_position(route,&state->location,&b.xyz,m); b.dwr=0; s->write_point(route,node+1,&b);
    state->location.dnfrm=state->location.dwrOffset=0; ++state->location.irpt; publish(state,s);
    for (i=state->nextEvent-1;i>=0;--i)
    {
        m->event(events,i,&event); if (event.rtel.irpt<node) break;
        if (event.rtel.dwrOffset>=a.dwr)
        { event.rtel.dwrOffset=0; ++event.rtel.irpt; s->write_event(events,i,&event); }
    }
    for (node=limit-1;node>state->location.irpt;--node)
    { mm_actor_route_edit_adjust_delete(state,route,events,node,0,m,s); s->delete_points(route,node,1); life(state,s); }
}
void mm_actor_route_edit_hide(MMActorState *state,const MMActorRouteEditServices *s)
{
    publish(state,s); if (state->onStage) s->hide_body(s->edits.context);
    state->onStage=0; publish(state,s);
}
void mm_actor_route_edit_rewind(MMActorState *state,void *route,const MMActorMotionServices *m,
    const MMActorRouteEditServices *s)
{
    publish(state,s); s->recording(s->edits.context,0); mm_actor_state_rewind(state,route,m); publish(state,s);
}
void mm_actor_route_edit_reset(MMActorState *state,void *route,const MMActorMotionServices *m,
    const MMActorRouteEditServices *s)
{
    mm_actor_route_edit_hide(state,s); life(state,s); s->default_costume(s->edits.context);
    mm_actor_route_edit_rewind(state,route,m,s);
}
void mm_actor_route_edit_delete_forward_core(MMActorState *state,void *route,void *events,int all,
    int *alive,MMI32 current,const MMActorMotionServices *m,const MMActorRouteEditServices *s)
{
    MMI32 i,event_limit=m->event_count(events),node_limit=m->route_count(route),first;
    MMActorEvent event; MMActorRoutePoint point; MMActorEditRange range;
    if (current==-1) current=state->nextEvent;
    if (!all)
    {
        all=1;
        for (i=current;i<m->event_count(events);++i)
        {
            m->event(events,i,&event);
            if (event.aet==MM_ACTOR_ADD) { all=0; node_limit=event.rtel.irpt; event_limit=i; break; }
        }
    }
    if (event_limit>0) for (i=event_limit-1;i>=state->nextEvent;--i)
    { mm_actor_edit_remove(state,events,i,1,m,&s->edits); life(state,s); }
    first=state->location.irpt+1;
    if (node_limit>0)
    {
        if (state->nextEvent<=0 && all)
        { s->clear_route(route); life(state,s); state->location.irpt=-1; goto done; }
        if (state->location.dwrOffset>0) mm_actor_route_edit_truncate(state,route,events,node_limit,m,s);
        else for (i=node_limit-1;i>=first;--i)
        { mm_actor_route_edit_adjust_delete(state,route,events,i,0,m,s); s->delete_points(route,i,1); life(state,s); }
        if (state->location.irpt>=0)
        { m->route_point(route,state->location.irpt,&point); point.dwr=0; s->write_point(route,state->location.irpt,&point); }
    }
done:
    publish(state,s); s->edits.dirty(s->edits.context); s->edits.get_range(s->edits.context,&range);
    if (state->frame<range.first)
    {
        if (all)
        { if (alive) *alive=0; mm_actor_route_edit_reset(state,route,m,s); return; }
        if (m->event_count(events)>0)
        { m->event(events,0,&event); range.first=event.nfrm; s->edits.publish_range(s->edits.context,&range); }
    }
    if (alive) *alive=1;
}
void mm_actor_route_edit_delete_forward(MMActorState *state,MMActorSounds *list,void *route,void *events,
    MMI32 actor,int all,int *alive,MMI32 current,const MMActorMotionServices *m,
    const MMActorRouteEditServices *s)
{
    int living; MMI32 i,frozen=1; MMActorEvent event; MMActorEditRange range;
    mm_actor_route_edit_delete_forward_core(state,route,events,all,&living,current,m,s);
    s->edits.get_range(s->edits.context,&range);
    if (living && state->frame>=range.first && mm_actor_edit_reserve(&s->edits,2,8))
    {
        if (state->addEvent<0 || !m->event_count(events))
        { if (s->diagnostic) s->diagnostic(s->edits.context,MM_ROUTE_BAD_ALIVE); goto done; }
        if (!state->onStage) goto done;
        if (!mm_actor_edit_add_apply(state,list,events,actor,MM_ACTOR_FREEZE,4,&frozen,m,&s->edits) && s->diagnostic)
            s->diagnostic(s->edits.context,MM_ROUTE_FREEZE_FAILED);
        if (!mm_actor_edit_set_step(state,list,events,actor,0,m,&s->edits) && s->diagnostic)
            s->diagnostic(s->edits.context,MM_ROUTE_STEP_FAILED);
        for (i=state->nextEvent-1;i>0;--i)
        {
            m->event(events,i,&event);
            if (event.rtel.irpt!=state->location.irpt || event.rtel.dwrOffset!=state->location.dwrOffset ||
                event.rtel.dnfrm!=state->location.dnfrm) break;
            if (event.aet==MM_ACTOR_TWEAK) { s->swap_events(events,i,state->nextEvent-1); break; }
        }
    }
done:
    if (alive) *alive=living;
}
