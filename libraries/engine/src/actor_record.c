/* Actor recording converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_record.h"
#include "mm/wide.h"
#define FLAG(kind) ((MMU32)1 << (kind))
static void publish(MMActorState *state, const MMActorRecording *record, const MMActorRecordServices *s)
{
    const MMActorEditServices *e=&s->subroute.route.edits;
    if (e->publish) e->publish(e->context,state);
    s->publish_record(e->context,record);
}
static void diagnostic(const MMActorRecordServices *s,int fault)
{ if (s->diagnostic) s->diagnostic(s->subroute.route.edits.context,fault); }
static int restore(MMActorState *state,MMActorRecording *record,void *undo,const MMActorRecordServices *s)
{
    publish(state,record,s);
    s->restore(s->subroute.route.edits.context,undo,state,record);
    return 0;
}
static int after(const MMActorLocation *a,const MMActorLocation *b)
{
    return a->irpt>b->irpt || (a->irpt==b->irpt && (a->dwrOffset>b->dwrOffset ||
        (a->dwrOffset==b->dwrOffset && a->dnfrm>b->dnfrm)));
}
static MMI32 smooth(MMI32 raw,MMI32 delta)
{
    MMI32 value=mm_fixed_add(raw,mm_fixed_add(delta,delta));
    if (value<0) return mm_signed_word(0U-(0U-(MMU32)value)/3U);
    return value/3;
}
static void result(MMActorRecordResult *out,int frame,int route,unsigned changed)
{
    if (!out) return;
    out->written=1; out->frame=frame; out->route=route;
    if (out->publish) out->publish(out->context,frame,route,changed);
}
static int freeze(MMActorState *state,MMActorSounds *sounds,void *events,MMI32 actor,
    MMI32 enabled,const MMActorMotionServices *m,const MMActorRecordServices *s)
{ return mm_actor_edit_add_apply(state,sounds,events,actor,MM_ACTOR_FREEZE,4,&enabled,m,&s->subroute.route.edits); }
static int step(MMActorState *state,MMActorSounds *sounds,void *events,MMI32 actor,
    MMI32 distance,const MMActorMotionServices *m,const MMActorRecordServices *s)
{ return mm_actor_edit_set_step(state,sounds,events,actor,distance,m,&s->subroute.action.sounds); }
static int advance_scene(MMActorState *state,MMActorRecording *record,const MMActorRecordServices *s)
{
    const MMActorEditServices *e=&s->subroute.route.edits;
    MMI32 frame=0;
    publish(state,record,s); e->scene_frame(e->context,&frame);
    return s->seek_scene(e->context,state,record,mm_fixed_add(frame,1));
}
int mm_actor_record_valid(const MMActorRecording *record,const MMActorPoint *delta,MMU32 now,
    const MMActorRecordServices *s)
{
    if (record->recording) return 1;
    return !(s->distance(s->subroute.route.edits.context,delta)<131072L && (MMU32)(now-record->timestamp)<750U);
}
int mm_actor_record_step(const MMActorState *state,MMI32 *distance,const MMActorTimelineServices *s)
{
    if (!mm_actor_step(state->action,state->cel,distance,s)) return 0;
    *distance=mm_fixed_mul(*distance,state->transform.rScaleStep);
    return 1;
}
int mm_actor_record_begin(MMActorState *state,MMActorRecording *record,void *route,void *events,
    int replace,void *undo,const MMActorMotionServices *m,const MMActorRecordServices *s)
{
    const MMActorRouteEditServices *r=&s->subroute.route;
    const MMActorEditServices *e=&r->edits;
    MMActorRoutePoint point;
    MMActorEvent event;
    MMActorSubrouteFlags flags;
    MMI32 i,next,offset,frame,previous;
    int closest=1;
    m->route_point(route,state->location.irpt,&point);
    record->inserted=0; record->rejoining=!replace && point.dwr!=0;
    publish(state,record,s);
    if (record->rejoining)
    {
        next=mm_fixed_add(state->location.irpt,1);
        for (i=state->nextEvent;i<m->event_count(events);++i)
        {
            m->event(events,i,&event); if (next<event.rtel.irpt) break;
            if (event.aet==MM_ACTOR_REMOVE)
            {
                record->rejoining=0; publish(state,record,s);
                mm_actor_edit_remove(state,events,i,1,m,e); --i;
                mm_actor_route_edit_adjust_delete(state,route,events,next,state->nextEvent,m,r);
                r->delete_points(route,next,1);
            }
        }
    }
    if (state->location.dwrOffset)
    {
        offset=state->location.dwrOffset;
        mm_actor_route_position(route,&state->location,&point.xyz,m);
        state->location.irpt=mm_fixed_add(state->location.irpt,1);
        state->location.dwrOffset=state->location.dnfrm=0; publish(state,record,s);
        if (!mm_actor_route_edit_insert(route,state->location.irpt,&point,offset,m,r)) return restore(state,record,undo,s);
        mm_actor_route_position(route,&state->location,&state->position,m);
        mm_actor_route_edit_adjust_insert(route,events,state->location.irpt,0,m,r);
    }
    record->gap=0; publish(state,record,s);
    s->subroute.get_flags(e->context,&flags);
    if (flags.dirty && !mm_actor_subroute_lifetime(route,events,NULL,&s->subroute))
    { diagnostic(s,MM_RECORD_BAD_LIFETIME); return restore(state,record,undo,s); }
    for (i=state->nextEvent;i<m->event_count(events);++i)
    {
        m->event(events,i,&event); if (event.aet!=MM_ACTOR_ADD) continue;
        frame=event.nfrm;
        if (closest) { previous=state->frame; closest=0; }
        else { m->event(events,i-1,&event); previous=event.nfrm; }
        record->gap=mm_fixed_sub(record->gap,mm_fixed_sub(frame,previous));
    }
    record->insertion=state->location; record->recording=1;
    record->raw.dxr=record->raw.dyr=record->raw.dzr=0; publish(state,record,s);
    return 1;
}
int mm_actor_record_move(MMActorState *state,MMActorRecording *record,MMActorSounds *sounds,
    void *route,void *events,MMI32 actor,const MMActorPoint *delta,MMU32 flags,MMU32 now,
    MMActorRecordResult *output,void *undo,const MMActorMotionServices *m,const MMActorRecordServices *s)
{
    const MMActorRouteEditServices *r=&s->subroute.route;
    const MMActorEditServices *e=&r->edits;
    MMActorPoint movement,mouse;
    MMActorRoutePoint current,next;
    MMActorEditRange range;
    MMI32 distance,mouse_distance,fraction;
    if (!record->recording) return 1;
    if (!r->reserve_events(e->context,2,8) || !s->reserve_points(route,1)) return restore(state,record,undo,s);
    movement.dxr=smooth(record->raw.dxr,delta->dxr);
    movement.dyr=smooth(record->raw.dyr,delta->dyr);
    movement.dzr=smooth(record->raw.dzr,delta->dzr);
    if (state->frozen && !(flags & MM_RECORD_FREEZE) && !freeze(state,sounds,events,actor,0,m,s))
        diagnostic(s,MM_RECORD_UNFREEZE_FAILED);
    if (!state->frozen && (flags & MM_RECORD_FREEZE) && !freeze(state,sounds,events,actor,1,m,s))
        diagnostic(s,MM_RECORD_FREEZE_FAILED);
    result(output,0,0,3);
    m->route_point(route,state->location.irpt,&current);
    mouse.dxr=mm_fixed_add(movement.dxr,current.xyz.dxr);
    mouse.dyr=mm_fixed_add(movement.dyr,current.xyz.dyr);
    mouse.dzr=mm_fixed_add(movement.dzr,current.xyz.dzr);
    mouse_distance=s->distance(e->context,&movement); publish(state,record,s);
    if (!mm_actor_record_step(state,&distance,&s->subroute.timeline)) return restore(state,record,undo,s);
    if (mouse_distance<distance)
    {
        if ((MMU32)(now-record->timestamp)<750U) return 1;
        if (state->step && !step(state,sounds,events,actor,0,m,s)) diagnostic(s,MM_RECORD_STEP_FAILED);
        record->inserted=1; publish(state,record,s); result(output,1,0,1);
        if (!advance_scene(state,record,s)) return restore(state,record,undo,s);
        return 1;
    }
    record->timestamp=now; record->raw=*delta; publish(state,record,s);
    fraction=mm_fixed_div(distance,mouse_distance);
    mm_actor_interpolate(&current.xyz,&mouse,fraction,&next.xyz); next.dwr=0;
    if ((flags & MM_RECORD_GROUND) && mm_fixed_add(current.xyz.dyr,state->routeTranslation.dyr)>=0 &&
        mm_fixed_add(next.xyz.dyr,state->routeTranslation.dyr)<0)
        next.xyz.dyr=mm_fixed_sub(0,state->routeTranslation.dyr);
    if (!r->insert_point(route,mm_fixed_add(state->location.irpt,1),&next)) return restore(state,record,undo,s);
    if (state->step!=-65536L && !step(state,sounds,events,actor,-65536L,m,s)) diagnostic(s,MM_RECORD_STEP_FAILED);
    current.dwr=distance; r->write_point(route,state->location.irpt,&current);
    mm_actor_route_edit_adjust_insert(route,events,mm_fixed_add(state->location.irpt,1),state->nextEvent,m,r);
    record->inserted=1; publish(state,record,s); result(output,1,1,3);
    record->gap=mm_fixed_add(record->gap,1);
    if (record->gap>0)
    { e->get_range(e->context,&range); range.last=mm_fixed_add(range.last,1); e->publish_range(e->context,&range); }
    if (!advance_scene(state,record,s)) return restore(state,record,undo,s);
    return 1;
}
int mm_actor_record_end(MMActorState *state,MMActorRecording *record,MMActorSounds *sounds,
    void *route,void *events,MMI32 actor,int replace,void *undo,
    const MMActorMotionServices *m,const MMActorRecordServices *s)
{
    const MMActorRouteEditServices *r=&s->subroute.route;
    const MMActorEditServices *e=&r->edits;
    MMActorEvent event;
    MMActorRoutePoint point,current,joined;
    MMActorPoint delta;
    MMActorLocation join=state->location;
    MMI32 distance,minimum=2147418112L,i,node,limit=m->route_count(route),first=state->nextEvent,merged;
    if (record->recording && (!state->onStage || replace))
    {
        mm_actor_route_edit_delete_forward_core(state,route,events,0,NULL,state->nextEvent,m,r);
        if (!freeze(state,sounds,events,actor,1,m,s) || !step(state,sounds,events,actor,0,m,s)) goto fail;
        goto finished;
    }
    if (!record->recording || !record->inserted || !state->onStage)
    {
        if (mm_actor_subroute_events_done(events,state->nextEvent,&state->location,m) &&
            !freeze(state,sounds,events,actor,1,m,s)) goto fail;
        goto finished;
    }
    if (!state->step && !step(state,sounds,events,actor,-65536L,m,s)) goto fail;
    if (!record->rejoining) join.dnfrm=1;
    else
    {
        for (i=state->nextEvent;i<m->event_count(events);++i)
        { m->event(events,i,&event); if (event.aet==MM_ACTOR_ADD) { limit=event.rtel.irpt; break; } }
        join.irpt=mm_fixed_add(state->location.irpt,1); join.dwrOffset=join.dnfrm=0;
        for (node=state->location.irpt+1;node<limit;++node)
        {
            m->route_point(route,node,&point);
            delta.dxr=mm_fixed_sub(state->position.dxr,point.xyz.dxr);
            delta.dyr=mm_fixed_sub(state->position.dyr,point.xyz.dyr);
            delta.dzr=mm_fixed_sub(state->position.dzr,point.xyz.dzr);
            distance=s->distance(e->context,&delta);
            if (distance<minimum) { minimum=distance; join.irpt=node; }
            if (!point.dwr) break;
        }
        m->route_point(route,state->location.irpt,&current); m->route_point(route,join.irpt,&joined);
        delta.dxr=mm_fixed_sub(state->position.dxr,joined.xyz.dxr);
        delta.dyr=mm_fixed_sub(state->position.dyr,joined.xyz.dyr);
        delta.dzr=mm_fixed_sub(state->position.dzr,joined.xyz.dzr);
        current.dwr=s->distance(e->context,&delta); if (!current.dwr) current.dwr=1;
        r->write_point(route,state->location.irpt,&current);
    }
    for (i=state->nextEvent;i<m->event_count(events);++i)
    {
        m->event(events,i,&event);
        if (event.aet==MM_ACTOR_ADD || (record->rejoining && !after(&join,&event.rtel))) break;
        if (after(&event.rtel,&record->insertion))
        {
            if (event.aet==MM_ACTOR_TWEAK || event.aet==MM_ACTOR_STEP || event.aet==MM_ACTOR_REMOVE ||
                event.aet==MM_ACTOR_ACTION || event.aet==MM_ACTOR_FREEZE)
            { mm_actor_edit_remove(state,events,i,1,m,e); --i; continue; }
            event.rtel=join; r->write_event(events,i,&event);
            mm_actor_edit_merge(state,sounds,events,actor,first,i,&merged,m,e);
            if (merged<i) --i;
        }
    }
    if (record->rejoining)
    {
        for (node=join.irpt-1;node>state->location.irpt;--node)
        { mm_actor_route_edit_adjust_delete(state,route,events,node,state->nextEvent,m,r); r->delete_points(route,node,1); }
        mm_actor_edit_action_fill(state,events,first,state->action,state->action,
            FLAG(MM_ACTOR_TWEAK)|FLAG(MM_ACTOR_FREEZE)|FLAG(MM_ACTOR_ACTION),m,e);
    }
    if (!replace && !record->rejoining && (!freeze(state,sounds,events,actor,1,m,s) || !step(state,sounds,events,actor,0,m,s))) goto fail;
finished:
    s->subroute.set_dirty(e->context,1); record->rejoining=record->recording=0;
    publish(state,record,s); e->dirty(e->context); e->invalidate_range(e->context); return 1;
fail:
    restore(state,record,undo,s); record->rejoining=record->recording=0; publish(state,record,s); return 0;
}
