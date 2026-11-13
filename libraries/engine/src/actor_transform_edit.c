/* Actor transform/time editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_transform_edit.h"
#include "three_mm/actor_effect.h"
#define FLAG(kind) ((ThreeMMU32)1 << (kind))
static void publish(ThreeMMActorState *state,const ThreeMMActorTransformEditServices *s)
{ if (s->subroute.route.edits.publish) s->subroute.route.edits.publish(s->subroute.route.edits.context,state); }
static void diagnostic(const ThreeMMActorTransformEditServices *s,int fault)
{ if (s->diagnostic) s->diagnostic(s->subroute.route.edits.context,fault); }
static ThreeMMI32 bound(ThreeMMI32 value,ThreeMMI32 minimum,ThreeMMI32 maximum)
{ return value<minimum?minimum:value>=maximum?maximum-1:value; }
static ThreeMMI32 factor(ThreeMMI32 value)
{ return bound(value,6553,three_mm_fixed_div(65536L,6553)); }
static int reserve(const ThreeMMActorTransformEditServices *s,ThreeMMU32 count,ThreeMMU32 bytes)
{ return s->subroute.route.reserve_events(s->subroute.route.edits.context,count,bytes); }
static void fill(ThreeMMActorState *state,void *events,ThreeMMI32 kind,const void *value,ThreeMMU32 bytes,
    ThreeMMU32 flags,const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    publish(state,s);
    three_mm_actor_edit_transform_fill(state,events,kind,value,bytes,state->nextEvent,-1,flags,m,&s->fill);
}
static int add(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *events,ThreeMMI32 actor,ThreeMMI32 kind,
    const void *value,ThreeMMU32 bytes,const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{ return three_mm_actor_edit_add_apply(state,sounds,events,actor,kind,bytes,value,m,&s->subroute.route.edits); }
static void position(ThreeMMActorState *state,void *route,void *events,const ThreeMMActorPoint *point,
    const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{ three_mm_actor_subroute_position_body(state,route,events,point,m,&s->subroute); }
static void undo_path(ThreeMMActorState *state,ThreeMMActorMatrix *matrix)
{
    if (state->transform.zaPath) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_Z,(unsigned short)(0U-state->transform.zaPath),0);
    if (state->transform.yaPath) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_Y,(unsigned short)(0U-state->transform.yaPath),0);
    if (state->transform.xaPath) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_X,(unsigned short)(0U-state->transform.xaPath),0);
}
int three_mm_actor_transform_rotate(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,unsigned short x,unsigned short y,unsigned short z,int forward,int world,
    const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    ThreeMMActorMatrix *matrix; ThreeMMI32 kind;
    if (world && !x && !y && !z) return 1;
    if (!reserve(s,1,48)) return 0;
    kind=forward?THREE_MM_ACTOR_ROTATE_FORWARD:THREE_MM_ACTOR_ROTATE_HERE;
    matrix=forward?&state->transform.bmat34Fwd:&state->transform.bmat34Cur;
    if (forward && state->useCurrentRotation) { *matrix=state->transform.bmat34Cur; undo_path(state,matrix); }
    if (world)
    {
        if (z) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_X,z,0);
        if (y) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_Y,y,0);
        if (x) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_Z,x,0);
        publish(state,s); s->normalise(s->subroute.route.edits.context,matrix);
    }
    else
    {
        if (x) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_X,x,1);
        if (y) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_Y,y,1);
        if (z) three_mm_matrix_rotate(matrix,THREE_MM_MATRIX_Z,z,1);
    }
    if (forward) fill(state,events,THREE_MM_ACTOR_ROTATE_FORWARD,matrix,48,0,m,s);
    if (!add(state,sounds,events,actor,kind,matrix,48,m,s)) diagnostic(s,THREE_MM_TRANSFORM_ADD_FAILED);
    position(state,route,events,&state->position,m,s); return 1;
}
int three_mm_actor_transform_normalize(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,ThreeMMU32 flags,const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    ThreeMMI32 old=state->transform.rScaleStep; ThreeMMU32 bytes=0,mask;
    if (flags & THREE_MM_NORMAL_ROTATE) bytes+=48;
    if (flags & THREE_MM_NORMAL_SIZE) bytes+=16;
    if (!reserve(s,3,bytes)) return 0;
    if (flags & THREE_MM_NORMAL_ROTATE)
    {
        three_mm_matrix_identity(&state->transform.bmat34Fwd); three_mm_matrix_identity(&state->transform.bmat34Cur);
        fill(state,events,THREE_MM_ACTOR_ROTATE_FORWARD,&state->transform.bmat34Fwd,48,0,m,s);
        if (!add(state,sounds,events,actor,THREE_MM_ACTOR_ROTATE_FORWARD,&state->transform.bmat34Fwd,48,m,s)) diagnostic(s,THREE_MM_TRANSFORM_ADD_FAILED);
    }
    if (flags & THREE_MM_NORMAL_SIZE)
    {
        state->transform.aevpull.rScaleX=state->transform.aevpull.rScaleY=state->transform.aevpull.rScaleZ=65536L;
        mask=state->transform.rScaleStep!=65536L?FLAG(THREE_MM_ACTOR_TWEAK):0; state->transform.rScaleStep=65536L;
        fill(state,events,THREE_MM_ACTOR_STRETCH,&state->transform.aevpull,12,0,m,s);
        fill(state,events,THREE_MM_ACTOR_SIZE,&state->transform.rScaleStep,4,mask,m,s);
        if (!add(state,sounds,events,actor,THREE_MM_ACTOR_STRETCH,&state->transform.aevpull,12,m,s)) diagnostic(s,THREE_MM_TRANSFORM_ADD_FAILED);
        if (!add(state,sounds,events,actor,THREE_MM_ACTOR_SIZE,&state->transform.rScaleStep,4,m,s)) diagnostic(s,THREE_MM_TRANSFORM_ADD_FAILED);
        if (old>65536L && s->scene_present(s->subroute.route.edits.context) && state->location.irpt<m->route_count(route)-1)
        { s->subroute.set_dirty(s->subroute.route.edits.context,1); s->subroute.route.edits.invalidate_range(s->subroute.route.edits.context); }
    }
    position(state,route,events,&state->position,m,s); return 1;
}
int three_mm_actor_transform_scale(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,ThreeMMI32 value,const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    ThreeMMI32 old=state->transform.rScaleStep; ThreeMMU32 mask;
    if (!reserve(s,1,4)) return 0;
    value=factor(value); state->transform.rScaleStep=bound(three_mm_fixed_mul(old,value),16384,655360L);
    mask=state->transform.rScaleStep!=value?FLAG(THREE_MM_ACTOR_TWEAK):0;
    fill(state,events,THREE_MM_ACTOR_SIZE,&state->transform.rScaleStep,4,mask,m,s);
    if (!add(state,sounds,events,actor,THREE_MM_ACTOR_SIZE,&state->transform.rScaleStep,4,m,s)) return 0;
    if (state->transform.rScaleStep!=value) three_mm_actor_route_position(route,&state->location,&state->position,m);
    position(state,route,events,&state->position,m,s);
    if (old!=state->transform.rScaleStep && s->scene_present(s->subroute.route.edits.context) && state->location.irpt<m->route_count(route)-1)
    { s->subroute.set_dirty(s->subroute.route.edits.context,1); s->subroute.route.edits.invalidate_range(s->subroute.route.edits.context); }
    return 1;
}
int three_mm_actor_transform_pull(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,const ThreeMMActorStretch *value,const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    if (!reserve(s,1,12)) return 0;
    state->transform.aevpull.rScaleX=bound(three_mm_fixed_mul(state->transform.aevpull.rScaleX,factor(value->rScaleX)),16384,262144L);
    state->transform.aevpull.rScaleY=bound(three_mm_fixed_mul(state->transform.aevpull.rScaleY,factor(value->rScaleY)),16384,262144L);
    state->transform.aevpull.rScaleZ=bound(three_mm_fixed_mul(state->transform.aevpull.rScaleZ,factor(value->rScaleZ)),16384,262144L);
    fill(state,events,THREE_MM_ACTOR_STRETCH,&state->transform.aevpull,12,0,m,s);
    if (!add(state,sounds,events,actor,THREE_MM_ACTOR_STRETCH,&state->transform.aevpull,12,m,s)) return 0;
    position(state,route,events,&state->position,m,s); return 1;
}
int three_mm_actor_transform_tweak(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,const ThreeMMActorPoint *delta,ThreeMMU32 flags,const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    ThreeMMActorPoint point;
    if (!reserve(s,1,12)) return 0;
    point.dxr=three_mm_fixed_add(state->position.dxr,delta->dxr);
    point.dyr=three_mm_fixed_add(state->position.dyr,delta->dyr);
    point.dzr=three_mm_fixed_add(state->position.dzr,delta->dzr);
    if ((flags & THREE_MM_MOVE_GROUND) && three_mm_fixed_add(state->position.dyr,state->routeTranslation.dyr)>=0 &&
        three_mm_fixed_add(point.dyr,state->routeTranslation.dyr)<0) point.dyr=three_mm_fixed_sub(0,state->routeTranslation.dyr);
    if (!add(state,sounds,events,actor,THREE_MM_ACTOR_TWEAK,&point,12,m,s)) return 0;
    position(state,route,events,&point,m,s); return 1;
}
static ThreeMMI32 angle_distance(unsigned short a,unsigned short b)
{ ThreeMMI32 distance=(ThreeMMI32)a-(ThreeMMI32)b; if (distance<0) distance=-distance; return distance>32768?65536L-distance:distance; }
int three_mm_actor_transform_move(ThreeMMActorState *state,ThreeMMActorSounds *sounds,void *route,void *events,
    ThreeMMI32 actor,ThreeMMActorPoint *origin,const ThreeMMActorPoint *delta,ThreeMMU32 flags,ThreeMMActorMoveResult *out,
    const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    const ThreeMMActorEditServices *e=&s->subroute.route.edits;
    ThreeMMActorEntrance entrance; ThreeMMActorEvent event; ThreeMMActorPoint movement=*delta,angles;
    ThreeMMI32 old_y,distance,angular,i; ThreeMMU32 axes; unsigned short x,y,z; int moved=delta->dxr || delta->dyr || delta->dzr;
    if (out) { out->written=1; out->moved=moved; if (out->publish) out->publish(out->context,moved); }
    if (!moved) return 1;
    if (!reserve(s,2,60)) return 0;
    old_y=three_mm_fixed_add(state->position.dyr,state->routeTranslation.dyr);
    if ((flags & THREE_MM_MOVE_GROUND) && old_y>=0 && three_mm_fixed_add(movement.dyr,old_y)<0)
        movement.dyr=three_mm_fixed_sub(0,old_y);
    if ((flags & THREE_MM_MOVE_ORIENT) && !s->subroute.action.text_template(s->subroute.action.context))
    {
        undo_path(state,&state->transform.bmat34Cur);
        three_mm_actor_pose_vector(state->actionFlags,&movement,NULL,&x,&y,&z,&axes); publish(state,s);
        distance=s->distance(e->context,&movement);
        three_mm_actor_edit_add_orientation(state,events,x,y,z,axes,&distance,m,e);
        m->payload(events,state->addEvent,&entrance);
        angles.dxr=angle_distance(x,entrance.x); angles.dyr=angle_distance(y,entrance.y); angles.dzr=angle_distance(z,entrance.z);
        angular=bound(s->distance(e->context,&angles),6553,26214);
        angular=three_mm_fixed_div(three_mm_fixed_div(65536L,angular),three_mm_fixed_div(65536L,6553));
        movement.dxr=three_mm_fixed_mul(movement.dxr,angular);
        movement.dyr=three_mm_fixed_mul(movement.dyr,angular);
        movement.dzr=three_mm_fixed_mul(movement.dzr,angular);
        three_mm_actor_add_orientation(state,&entrance,1); publish(state,s);
        for (i=state->addEvent+1;i<state->nextEvent;++i)
        {
            m->event(events,i,&event);
            if (event.aet==THREE_MM_ACTOR_ROTATE_HERE) e->write_payload(e->context,i,&state->transform.bmat34Cur);
            if (event.aet==THREE_MM_ACTOR_ROTATE_FORWARD && !add(state,sounds,events,actor,THREE_MM_ACTOR_ROTATE_HERE,&state->transform.bmat34Cur,48,m,s))
            { diagnostic(s,THREE_MM_TRANSFORM_MOVE_ROTATE_FAILED); return 0; }
        }
    }
    if (flags & THREE_MM_MOVE_SCENE)
    {
        origin->dxr=three_mm_fixed_add(origin->dxr,movement.dxr); origin->dyr=three_mm_fixed_add(origin->dyr,movement.dyr);
        origin->dzr=three_mm_fixed_add(origin->dzr,movement.dzr); s->publish_origin(e->context,origin);
    }
    else
    {
        if (state->addEvent==-1) return 0;
        if (!(flags & THREE_MM_MOVE_SUBROUTE))
        { if (!add(state,sounds,events,actor,THREE_MM_ACTOR_MOVE,&movement,12,m,s)) return 0; }
        else
        {
            state->subrouteTranslation.dxr=three_mm_fixed_add(state->subrouteTranslation.dxr,movement.dxr);
            state->subrouteTranslation.dyr=three_mm_fixed_add(state->subrouteTranslation.dyr,movement.dyr);
            state->subrouteTranslation.dzr=three_mm_fixed_add(state->subrouteTranslation.dzr,movement.dzr);
            m->payload(events,state->addEvent,&entrance);
            entrance.translation.dxr=three_mm_fixed_add(entrance.translation.dxr,movement.dxr);
            entrance.translation.dyr=three_mm_fixed_add(entrance.translation.dyr,movement.dyr);
            entrance.translation.dzr=three_mm_fixed_add(entrance.translation.dzr,movement.dzr);
            e->write_payload(e->context,state->addEvent,&entrance);
        }
    }
    three_mm_actor_state_origin(state,origin); publish(state,s); e->dirty(e->context);
    if (movement.dxr || movement.dyr || movement.dzr) position(state,route,events,&state->position,m,s);
    return 1;
}
int three_mm_actor_transform_time(ThreeMMActorState *state,void *route,void *events,ThreeMMI32 shift,
    const ThreeMMActorMotionServices *m,const ThreeMMActorTransformEditServices *s)
{
    const ThreeMMActorEditServices *e=&s->subroute.route.edits;
    ThreeMMActorSubrouteFlags flags; ThreeMMActorEvent event,previous; ThreeMMActorEditRange range;
    ThreeMMI32 i,remaining,gap,saved=0,frame=0; int ok;
    if (!shift) return 1;
    s->subroute.get_flags(e->context,&flags);
    if (flags.dirty && !three_mm_actor_subroute_lifetime(route,events,NULL,&s->subroute)) diagnostic(s,THREE_MM_TRANSFORM_BAD_LIFETIME);
    if (shift<0)
    {
        remaining=three_mm_fixed_sub(0,shift);
        for (i=state->nextEvent-1;remaining>0 && i>=0;--i)
        {
            m->event(events,i,&event);
            if (event.aet!=THREE_MM_ACTOR_ADD || i==0)
            { event.nfrm=three_mm_fixed_sub(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event); continue; }
            m->event(events,i-1,&previous); gap=three_mm_fixed_sub(event.nfrm,previous.nfrm);
            event.nfrm=three_mm_fixed_sub(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event);
            remaining=three_mm_fixed_sub(remaining,three_mm_fixed_sub(gap,1));
        }
    }
    else
    {
        remaining=shift;
        for (i=state->addEvent;remaining>0 && i<m->event_count(events);++i)
        {
            m->event(events,i,&event);
            if (event.aet!=THREE_MM_ACTOR_ADD || i==state->addEvent)
            { saved=event.nfrm; event.nfrm=three_mm_fixed_add(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event); continue; }
            gap=three_mm_fixed_sub(event.nfrm,saved); remaining=three_mm_fixed_sub(remaining,three_mm_fixed_sub(gap,1));
            if (remaining<=0) break;
            event.nfrm=three_mm_fixed_add(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event);
        }
    }
    e->get_range(e->context,&range); m->event(events,0,&event); range.first=event.nfrm;
    m->event(events,m->event_count(events)-1,&event); range.last=event.nfrm; e->publish_range(e->context,&range);
    state->frame=three_mm_fixed_add(state->frame,shift); publish(state,s); e->invalidate_range(e->context);
    e->scene_frame(e->context,&frame); ok=s->seek_scene(e->context,state,three_mm_fixed_add(frame,shift));
    if (ok) s->scrollbars(e->context); return ok;
}
