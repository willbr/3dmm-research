/* Actor route paste converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_route_paste.h"
static void publish(const ThreeMMActorState *state,const ThreeMMActorEditServices *e)
{ if (e->publish) e->publish(e->context,state); }
int three_mm_actor_route_paste(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,void *clipboard,int legacy_static,const ThreeMMActorMotionServices *m,const ThreeMMActorRoutePasteServices *s)
{
    const ThreeMMActorRouteEditServices *r=&s->subroute.route;
    const ThreeMMActorEditServices *e=&r->edits;
    ThreeMMActorRoutePoint point,current; ThreeMMActorPoint delta,position;
    ThreeMMActorEvent event; ThreeMMI32 removed=0,added=m->route_count(clipboard)-1,i,prior,value;
    ThreeMMU32 flags=0;
    if (added<=0) { s->diagnostic(e->context,THREE_MM_ROUTE_PASTE_EMPTY); return 0; }
    if (!s->reserve_points(route,(ThreeMMU32)added) || !r->reserve_events(e->context,1,4)) return 0;
    three_mm_actor_route_position(route,&state->location,&current.xyz,m); current.dwr=1;
    if (state->location.dwrOffset)
    {
        prior=state->location.dwrOffset; ++state->location.irpt;
        state->location.dwrOffset=state->location.dnfrm=0; publish(state,e);
        if (!three_mm_actor_route_edit_insert(route,state->location.irpt,&current,prior,m,r)) return 0;
        three_mm_actor_route_position(route,&state->location,&state->position,m); publish(state,e);
        three_mm_actor_route_edit_adjust_insert(route,events,state->location.irpt,0,m,r);
    }
    for (i=state->location.irpt+1;i<m->route_count(route);++i)
    { m->route_point(route,i,&point); ++removed; if (!point.dwr) break; }
    if (removed) r->delete_points(route,state->location.irpt,removed);
    for (i=state->nextEvent;i<m->event_count(events);++i)
    {
        m->event(events,i,&event);
        if (event.rtel.irpt>state->location.irpt+removed)
        { event.rtel.irpt+=added-removed; r->write_event(events,i,&event); }
        else { three_mm_actor_edit_remove(state,events,i,0,m,e); --i; }
    }
    m->route_point(clipboard,0,&point);
    delta.dxr=three_mm_fixed_sub(current.xyz.dxr,point.xyz.dxr);
    delta.dyr=three_mm_fixed_sub(current.xyz.dyr,point.xyz.dyr);
    delta.dzr=three_mm_fixed_sub(current.xyz.dzr,point.xyz.dzr);
    for (i=1;i<=added;++i)
    {
        m->route_point(clipboard,i,&point);
        point.xyz.dxr=three_mm_fixed_add(point.xyz.dxr,delta.dxr);
        point.xyz.dyr=three_mm_fixed_add(point.xyz.dyr,delta.dyr);
        point.xyz.dzr=three_mm_fixed_add(point.xyz.dzr,delta.dzr);
        if (!r->insert_point(route,state->location.irpt+i,&point)) s->diagnostic(e->context,THREE_MM_ROUTE_PASTE_INSERT_FAILED);
    }
    m->route_point(route,state->location.irpt+1,&point);
    delta.dxr=three_mm_fixed_sub(point.xyz.dxr,current.xyz.dxr);
    delta.dyr=three_mm_fixed_sub(point.xyz.dyr,current.xyz.dyr);
    delta.dzr=three_mm_fixed_sub(point.xyz.dzr,current.xyz.dzr);
    current.dwr=s->distance(e->context,&delta); if (!current.dwr) current.dwr=1;
    r->write_point(route,state->location.irpt,&current);
    if (legacy_static)
    {
        if (state->actionEvent<0) s->diagnostic(e->context,THREE_MM_ROUTE_PASTE_NO_ACTION);
        if (!s->subroute.action.action_flags(s->subroute.action.context,state->action,&flags)) return 0;
    }
    if (!legacy_static || (flags&8))
        if (!three_mm_actor_edit_set_step(state,sounds,events,actor,-65536L,m,&s->subroute.action.sounds)) return 0;
    event.aet=THREE_MM_ACTOR_FREEZE; event.nfrm=state->frame;
    event.rtel.irpt=state->location.irpt+added; event.rtel.dwrOffset=event.rtel.dnfrm=0; value=1;
    if (!three_mm_actor_edit_insert(state,state->nextEvent,sizeof(value),&value,&event,1,e)) return 0;
    event.aet=THREE_MM_ACTOR_STEP; value=0;
    if (!three_mm_actor_edit_insert(state,state->nextEvent,sizeof(value),&value,&event,1,e)) return 0;
    publish(state,e); r->life_dirty(e->context); e->invalidate_range(e->context); e->dirty(e->context);
    position=state->position; three_mm_actor_subroute_position_body(state,route,events,&position,m,&s->subroute);
    return 1;
}
