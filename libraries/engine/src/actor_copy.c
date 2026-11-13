/* Actor copying converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_copy.h"
#include <stddef.h>
static void publish(ThreeMMActorCopyResult *out,void *actor)
{ out->actor=actor; if (out->publish) out->publish(out->context,actor); }
static int fail(ThreeMMActorCopyResult *out,const ThreeMMActorCopyServices *s)
{ s->release_actor(s->context,out->actor); publish(out,NULL); return 0; }
static int insert(void *actor,void *source,void *target,ThreeMMI32 i,const void *replacement,
    const ThreeMMActorEvent *event,int merge,const ThreeMMActorCopyServices *s)
{
    ThreeMMI32 added=s->motion.event_count(target); int ok; const void *payload;
    s->lock_events(s->context,source);
    payload=replacement?replacement:s->payload_view(s->context,source,i);
    ok=s->insert_event(s->context,actor,added,(ThreeMMU32)s->motion.event_size(source,i),payload,event);
    s->unlock_events(s->context,source);
    if (!ok) return 0;
    if (merge) s->merge_event(s->context,actor,0,added);
    return 1;
}
int three_mm_actor_copy(const ThreeMMActorState *state,void *route,void *events,int entire,int postponed,
    ThreeMMActorCopyResult *out,const ThreeMMActorCopyServices *s)
{
    ThreeMMActorEvent event; ThreeMMActorSound sound; ThreeMMActorRoutePoint point;
    ThreeMMI32 i,limit,node,action[2]; void *actor,*target_events,*target_route; const void *replacement;
    actor=s->create_actor(s->context); publish(out,actor); if (!actor) return 0;
    target_events=s->events(s->context,actor); target_route=s->route(s->context,actor);
    s->motion.route_point(route,state->location.irpt,&point);
    point.dwr=three_mm_fixed_sub(point.dwr,state->location.dwrOffset);
    for (i=state->addEvent;i<state->nextEvent;++i)
    {
        s->motion.event(events,i,&event); replacement=NULL;
        switch (event.aet)
        {
        case THREE_MM_ACTOR_ACTION:
            s->motion.payload(events,i,action); action[1]=state->cel; replacement=action; break;
        case THREE_MM_ACTOR_ADD: case THREE_MM_ACTOR_COSTUME: case THREE_MM_ACTOR_STRETCH:
        case THREE_MM_ACTOR_SIZE: case THREE_MM_ACTOR_ROTATE_FORWARD: case THREE_MM_ACTOR_FREEZE:
        case THREE_MM_ACTOR_STEP: case THREE_MM_ACTOR_MOVE: break;
        case THREE_MM_ACTOR_SOUND:
            s->motion.payload(events,i,&sound);
            if (!s->resolve_sound(s->context,&sound)) return fail(out,s);
            s->write_payload(s->context,events,i,&sound);
            if (sound.cel==-1 && i>=state->firstEventInFrame) break;
            if (i>state->actionEvent && sound.cel!=-1) break;
            continue;
        case THREE_MM_ACTOR_TWEAK: case THREE_MM_ACTOR_ROTATE_HERE:
            if (i>=(postponed?state->nextEvent:state->firstEventInFrame)) break;
            continue;
        case THREE_MM_ACTOR_REMOVE: continue;
        default: s->unknown_event(s->context); break;
        }
        event.rtel.irpt=event.rtel.dwrOffset=event.rtel.dnfrm=0;
        if (!insert(actor,events,target_events,i,replacement,&event,1,s)) return fail(out,s);
    }
    for (i=state->nextEvent;i<s->motion.event_count(events);++i)
    {
        s->motion.event(events,i,&event);
        if (!entire && event.aet==THREE_MM_ACTOR_ADD && s->motion.event_count(target_events)>0) break;
        event.rtel.irpt-=state->location.irpt;
        if (!event.rtel.irpt)
        {
            if (event.rtel.dwrOffset==state->location.dwrOffset)
            {
                event.rtel.dwrOffset=0;
                event.rtel.dnfrm=event.rtel.dnfrm<state->location.dnfrm?0:event.rtel.dnfrm-state->location.dnfrm;
            }
            else event.rtel.dwrOffset=point.dwr?three_mm_fixed_sub(event.rtel.dwrOffset,state->location.dwrOffset):0;
        }
        if (!insert(actor,events,target_events,i,NULL,&event,0,s)) return fail(out,s);
    }
    if (!s->insert_point(s->context,target_route,0,&point)) return fail(out,s);
    if (state->location.irpt+1<s->motion.route_count(route))
    {
        limit=s->motion.route_count(route);
        if (!entire) for (node=state->location.irpt;node<limit;++node)
        { s->motion.route_point(route,node,&point); if (!point.dwr) { limit=node+1; break; } }
        i=limit-(state->location.irpt+1);
        if (!s->resize_route(s->context,target_route,s->motion.route_count(target_route)+i)) return fail(out,s);
        s->copy_points(s->context,route,state->location.irpt+1,target_route,1,i);
    }
    else { point.dwr=0; s->write_point(s->context,target_route,0,&point); }
    s->rewind_actor(s->context,actor); return 1;
}
int three_mm_actor_copy_route(const ThreeMMActorState *state,void *route,int entire,
    ThreeMMActorCopyResult *out,const ThreeMMActorCopyServices *s)
{
    ThreeMMActorRoutePoint point,node,next; ThreeMMActorPoint delta;
    ThreeMMI32 i,limit,count; void *actor,*target;
    actor=s->create_actor(s->context); publish(out,actor); if (!actor) return 0;
    target=s->route(s->context,actor); three_mm_actor_route_position(route,&state->location,&point.xyz,&s->motion);
    s->motion.route_point(route,state->location.irpt,&node); point.dwr=0;
    if (!state->location.dwrOffset) point.dwr=node.dwr;
    else if (node.dwr>0 && state->location.irpt<s->motion.route_count(route)-1)
    {
        s->motion.route_point(route,state->location.irpt+1,&next);
        delta.dxr=three_mm_fixed_sub(next.xyz.dxr,point.xyz.dxr);
        delta.dyr=three_mm_fixed_sub(next.xyz.dyr,point.xyz.dyr);
        delta.dzr=three_mm_fixed_sub(next.xyz.dzr,point.xyz.dzr);
        point.dwr=s->distance(s->context,&delta); if (!point.dwr) point.dwr=1;
    }
    if (!s->insert_point(s->context,target,0,&point)) return fail(out,s);
    if (!entire && !point.dwr) { s->rewind_actor(s->context,actor); return 1; }
    limit=s->motion.route_count(route);
    if (!entire) for (i=state->location.irpt+1;i<limit;++i)
    { s->motion.route_point(route,i,&point); if (!point.dwr) { limit=i+1; break; } }
    count=limit-(state->location.irpt+1);
    if (count>0 && !s->reserve_route(s->context,target,count)) return fail(out,s);
    for (i=state->location.irpt+1;i<limit;++i)
    {
        s->motion.route_point(route,i,&point);
        if (!s->insert_point(s->context,target,i-state->location.irpt,&point)) return fail(out,s);
        if (!entire && !point.dwr) break;
    }
    s->rewind_actor(s->context,actor); return 1;
}
