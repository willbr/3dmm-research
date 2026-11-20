/* Actor transform/time editing converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_transform_edit.h"
#include "mm/actor_effect.h"
#define FLAG(kind) ((MMU32)1 << (kind))
static void publish(MMActorState *state,const MMActorTransformEditServices *s)
{ if (s->subroute.route.edits.publish) s->subroute.route.edits.publish(s->subroute.route.edits.context,state); }
static void diagnostic(const MMActorTransformEditServices *s,int fault)
{ if (s->diagnostic) s->diagnostic(s->subroute.route.edits.context,fault); }
static MMI32 bound(MMI32 value,MMI32 minimum,MMI32 maximum)
{ return value<minimum?minimum:value>=maximum?maximum-1:value; }
static MMI32 factor(MMI32 value)
{ return bound(value,6553,mm_fixed_div(65536L,6553)); }
static int reserve(const MMActorTransformEditServices *s,MMU32 count,MMU32 bytes)
{ return s->subroute.route.reserve_events(s->subroute.route.edits.context,count,bytes); }
static void fill(MMActorState *state,void *events,MMI32 kind,const void *value,MMU32 bytes,
    MMU32 flags,const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    publish(state,s);
    mm_actor_edit_transform_fill(state,events,kind,value,bytes,state->nextEvent,-1,flags,m,&s->fill);
}
static int add(MMActorState *state,MMActorSounds *sounds,void *events,MMI32 actor,MMI32 kind,
    const void *value,MMU32 bytes,const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{ return mm_actor_edit_add_apply(state,sounds,events,actor,kind,bytes,value,m,&s->subroute.route.edits); }
static void position(MMActorState *state,void *route,void *events,const MMActorPoint *point,
    const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{ mm_actor_subroute_position_body(state,route,events,point,m,&s->subroute); }
static void undo_path(MMActorState *state,MMActorMatrix *matrix)
{
    if (state->transform.zaPath) mm_matrix_rotate(matrix,MM_MATRIX_Z,(unsigned short)(0U-state->transform.zaPath),0);
    if (state->transform.yaPath) mm_matrix_rotate(matrix,MM_MATRIX_Y,(unsigned short)(0U-state->transform.yaPath),0);
    if (state->transform.xaPath) mm_matrix_rotate(matrix,MM_MATRIX_X,(unsigned short)(0U-state->transform.xaPath),0);
}
int mm_actor_transform_rotate(MMActorState *state,MMActorSounds *sounds,void *route,void *events,
    MMI32 actor,unsigned short x,unsigned short y,unsigned short z,int forward,int world,
    const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    MMActorMatrix *matrix; MMI32 kind;
    if (world && !x && !y && !z) return 1;
    if (!reserve(s,1,48)) return 0;
    kind=forward?MM_ACTOR_ROTATE_FORWARD:MM_ACTOR_ROTATE_HERE;
    matrix=forward?&state->transform.bmat34Fwd:&state->transform.bmat34Cur;
    if (forward && state->useCurrentRotation) { *matrix=state->transform.bmat34Cur; undo_path(state,matrix); }
    if (world)
    {
        if (z) mm_matrix_rotate(matrix,MM_MATRIX_X,z,0);
        if (y) mm_matrix_rotate(matrix,MM_MATRIX_Y,y,0);
        if (x) mm_matrix_rotate(matrix,MM_MATRIX_Z,x,0);
        publish(state,s); s->normalise(s->subroute.route.edits.context,matrix);
    }
    else
    {
        if (x) mm_matrix_rotate(matrix,MM_MATRIX_X,x,1);
        if (y) mm_matrix_rotate(matrix,MM_MATRIX_Y,y,1);
        if (z) mm_matrix_rotate(matrix,MM_MATRIX_Z,z,1);
    }
    if (forward) fill(state,events,MM_ACTOR_ROTATE_FORWARD,matrix,48,0,m,s);
    if (!add(state,sounds,events,actor,kind,matrix,48,m,s)) diagnostic(s,MM_TRANSFORM_ADD_FAILED);
    position(state,route,events,&state->position,m,s); return 1;
}
int mm_actor_transform_normalize(MMActorState *state,MMActorSounds *sounds,void *route,void *events,
    MMI32 actor,MMU32 flags,const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    MMI32 old=state->transform.rScaleStep; MMU32 bytes=0,mask;
    if (flags & MM_NORMAL_ROTATE) bytes+=48;
    if (flags & MM_NORMAL_SIZE) bytes+=16;
    if (!reserve(s,3,bytes)) return 0;
    if (flags & MM_NORMAL_ROTATE)
    {
        mm_matrix_identity(&state->transform.bmat34Fwd); mm_matrix_identity(&state->transform.bmat34Cur);
        fill(state,events,MM_ACTOR_ROTATE_FORWARD,&state->transform.bmat34Fwd,48,0,m,s);
        if (!add(state,sounds,events,actor,MM_ACTOR_ROTATE_FORWARD,&state->transform.bmat34Fwd,48,m,s)) diagnostic(s,MM_TRANSFORM_ADD_FAILED);
    }
    if (flags & MM_NORMAL_SIZE)
    {
        state->transform.aevpull.rScaleX=state->transform.aevpull.rScaleY=state->transform.aevpull.rScaleZ=65536L;
        mask=state->transform.rScaleStep!=65536L?FLAG(MM_ACTOR_TWEAK):0; state->transform.rScaleStep=65536L;
        fill(state,events,MM_ACTOR_STRETCH,&state->transform.aevpull,12,0,m,s);
        fill(state,events,MM_ACTOR_SIZE,&state->transform.rScaleStep,4,mask,m,s);
        if (!add(state,sounds,events,actor,MM_ACTOR_STRETCH,&state->transform.aevpull,12,m,s)) diagnostic(s,MM_TRANSFORM_ADD_FAILED);
        if (!add(state,sounds,events,actor,MM_ACTOR_SIZE,&state->transform.rScaleStep,4,m,s)) diagnostic(s,MM_TRANSFORM_ADD_FAILED);
        if (old>65536L && s->scene_present(s->subroute.route.edits.context) && state->location.irpt<m->route_count(route)-1)
        { s->subroute.set_dirty(s->subroute.route.edits.context,1); s->subroute.route.edits.invalidate_range(s->subroute.route.edits.context); }
    }
    position(state,route,events,&state->position,m,s); return 1;
}
int mm_actor_transform_scale(MMActorState *state,MMActorSounds *sounds,void *route,void *events,
    MMI32 actor,MMI32 value,const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    MMI32 old=state->transform.rScaleStep; MMU32 mask;
    if (!reserve(s,1,4)) return 0;
    value=factor(value); state->transform.rScaleStep=bound(mm_fixed_mul(old,value),16384,655360L);
    mask=state->transform.rScaleStep!=value?FLAG(MM_ACTOR_TWEAK):0;
    fill(state,events,MM_ACTOR_SIZE,&state->transform.rScaleStep,4,mask,m,s);
    if (!add(state,sounds,events,actor,MM_ACTOR_SIZE,&state->transform.rScaleStep,4,m,s)) return 0;
    if (state->transform.rScaleStep!=value) mm_actor_route_position(route,&state->location,&state->position,m);
    position(state,route,events,&state->position,m,s);
    if (old!=state->transform.rScaleStep && s->scene_present(s->subroute.route.edits.context) && state->location.irpt<m->route_count(route)-1)
    { s->subroute.set_dirty(s->subroute.route.edits.context,1); s->subroute.route.edits.invalidate_range(s->subroute.route.edits.context); }
    return 1;
}
int mm_actor_transform_pull(MMActorState *state,MMActorSounds *sounds,void *route,void *events,
    MMI32 actor,const MMActorStretch *value,const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    if (!reserve(s,1,12)) return 0;
    state->transform.aevpull.rScaleX=bound(mm_fixed_mul(state->transform.aevpull.rScaleX,factor(value->rScaleX)),16384,262144L);
    state->transform.aevpull.rScaleY=bound(mm_fixed_mul(state->transform.aevpull.rScaleY,factor(value->rScaleY)),16384,262144L);
    state->transform.aevpull.rScaleZ=bound(mm_fixed_mul(state->transform.aevpull.rScaleZ,factor(value->rScaleZ)),16384,262144L);
    fill(state,events,MM_ACTOR_STRETCH,&state->transform.aevpull,12,0,m,s);
    if (!add(state,sounds,events,actor,MM_ACTOR_STRETCH,&state->transform.aevpull,12,m,s)) return 0;
    position(state,route,events,&state->position,m,s); return 1;
}
int mm_actor_transform_tweak(MMActorState *state,MMActorSounds *sounds,void *route,void *events,
    MMI32 actor,const MMActorPoint *delta,MMU32 flags,const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    MMActorPoint point;
    if (!reserve(s,1,12)) return 0;
    point.dxr=mm_fixed_add(state->position.dxr,delta->dxr);
    point.dyr=mm_fixed_add(state->position.dyr,delta->dyr);
    point.dzr=mm_fixed_add(state->position.dzr,delta->dzr);
    if ((flags & MM_MOVE_GROUND) && mm_fixed_add(state->position.dyr,state->routeTranslation.dyr)>=0 &&
        mm_fixed_add(point.dyr,state->routeTranslation.dyr)<0) point.dyr=mm_fixed_sub(0,state->routeTranslation.dyr);
    if (!add(state,sounds,events,actor,MM_ACTOR_TWEAK,&point,12,m,s)) return 0;
    position(state,route,events,&point,m,s); return 1;
}
static MMI32 angle_distance(unsigned short a,unsigned short b)
{ MMI32 distance=(MMI32)a-(MMI32)b; if (distance<0) distance=-distance; return distance>32768?65536L-distance:distance; }
int mm_actor_transform_move(MMActorState *state,MMActorSounds *sounds,void *route,void *events,
    MMI32 actor,MMActorPoint *origin,const MMActorPoint *delta,MMU32 flags,MMActorMoveResult *out,
    const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    const MMActorEditServices *e=&s->subroute.route.edits;
    MMActorEntrance entrance; MMActorEvent event; MMActorPoint movement=*delta,angles;
    MMI32 old_y,distance,angular,i; MMU32 axes; unsigned short x,y,z; int moved=delta->dxr || delta->dyr || delta->dzr;
    if (out) { out->written=1; out->moved=moved; if (out->publish) out->publish(out->context,moved); }
    if (!moved) return 1;
    if (!reserve(s,2,60)) return 0;
    old_y=mm_fixed_add(state->position.dyr,state->routeTranslation.dyr);
    if ((flags & MM_MOVE_GROUND) && old_y>=0 && mm_fixed_add(movement.dyr,old_y)<0)
        movement.dyr=mm_fixed_sub(0,old_y);
    if ((flags & MM_MOVE_ORIENT) && !s->subroute.action.text_template(s->subroute.action.context))
    {
        undo_path(state,&state->transform.bmat34Cur);
        mm_actor_pose_vector(state->actionFlags,&movement,NULL,&x,&y,&z,&axes); publish(state,s);
        distance=s->distance(e->context,&movement);
        mm_actor_edit_add_orientation(state,events,x,y,z,axes,&distance,m,e);
        m->payload(events,state->addEvent,&entrance);
        angles.dxr=angle_distance(x,entrance.x); angles.dyr=angle_distance(y,entrance.y); angles.dzr=angle_distance(z,entrance.z);
        angular=bound(s->distance(e->context,&angles),6553,26214);
        angular=mm_fixed_div(mm_fixed_div(65536L,angular),mm_fixed_div(65536L,6553));
        movement.dxr=mm_fixed_mul(movement.dxr,angular);
        movement.dyr=mm_fixed_mul(movement.dyr,angular);
        movement.dzr=mm_fixed_mul(movement.dzr,angular);
        mm_actor_add_orientation(state,&entrance,1); publish(state,s);
        for (i=state->addEvent+1;i<state->nextEvent;++i)
        {
            m->event(events,i,&event);
            if (event.aet==MM_ACTOR_ROTATE_HERE) e->tables.write_payload(e->tables.context,i,&state->transform.bmat34Cur);
            if (event.aet==MM_ACTOR_ROTATE_FORWARD && !add(state,sounds,events,actor,MM_ACTOR_ROTATE_HERE,&state->transform.bmat34Cur,48,m,s))
            { diagnostic(s,MM_TRANSFORM_MOVE_ROTATE_FAILED); return 0; }
        }
    }
    if (flags & MM_MOVE_SCENE)
    {
        origin->dxr=mm_fixed_add(origin->dxr,movement.dxr); origin->dyr=mm_fixed_add(origin->dyr,movement.dyr);
        origin->dzr=mm_fixed_add(origin->dzr,movement.dzr); s->publish_origin(e->context,origin);
    }
    else
    {
        if (state->addEvent==-1) return 0;
        if (!(flags & MM_MOVE_SUBROUTE))
        { if (!add(state,sounds,events,actor,MM_ACTOR_MOVE,&movement,12,m,s)) return 0; }
        else
        {
            state->subrouteTranslation.dxr=mm_fixed_add(state->subrouteTranslation.dxr,movement.dxr);
            state->subrouteTranslation.dyr=mm_fixed_add(state->subrouteTranslation.dyr,movement.dyr);
            state->subrouteTranslation.dzr=mm_fixed_add(state->subrouteTranslation.dzr,movement.dzr);
            m->payload(events,state->addEvent,&entrance);
            entrance.translation.dxr=mm_fixed_add(entrance.translation.dxr,movement.dxr);
            entrance.translation.dyr=mm_fixed_add(entrance.translation.dyr,movement.dyr);
            entrance.translation.dzr=mm_fixed_add(entrance.translation.dzr,movement.dzr);
            e->tables.write_payload(e->tables.context,state->addEvent,&entrance);
        }
    }
    mm_actor_state_origin(state,origin); publish(state,s); e->dirty(e->context);
    if (movement.dxr || movement.dyr || movement.dzr) position(state,route,events,&state->position,m,s);
    return 1;
}
int mm_actor_transform_time(MMActorState *state,void *route,void *events,MMI32 shift,
    const MMActorMotionServices *m,const MMActorTransformEditServices *s)
{
    const MMActorEditServices *e=&s->subroute.route.edits;
    MMActorSubrouteFlags flags; MMActorEvent event,previous; MMActorEditRange range;
    MMI32 i,remaining,gap,saved=0,frame=0; int ok;
    if (!shift) return 1;
    s->subroute.get_flags(e->context,&flags);
    if (flags.dirty && !mm_actor_subroute_lifetime(route,events,NULL,&s->subroute)) diagnostic(s,MM_TRANSFORM_BAD_LIFETIME);
    if (shift<0)
    {
        remaining=mm_fixed_sub(0,shift);
        for (i=state->nextEvent-1;remaining>0 && i>=0;--i)
        {
            m->event(events,i,&event);
            if (event.aet!=MM_ACTOR_ADD || i==0)
            { event.nfrm=mm_fixed_sub(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event); continue; }
            m->event(events,i-1,&previous); gap=mm_fixed_sub(event.nfrm,previous.nfrm);
            event.nfrm=mm_fixed_sub(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event);
            remaining=mm_fixed_sub(remaining,mm_fixed_sub(gap,1));
        }
    }
    else
    {
        remaining=shift;
        for (i=state->addEvent;remaining>0 && i<m->event_count(events);++i)
        {
            m->event(events,i,&event);
            if (event.aet!=MM_ACTOR_ADD || i==state->addEvent)
            { saved=event.nfrm; event.nfrm=mm_fixed_add(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event); continue; }
            gap=mm_fixed_sub(event.nfrm,saved); remaining=mm_fixed_sub(remaining,mm_fixed_sub(gap,1));
            if (remaining<=0) break;
            event.nfrm=mm_fixed_add(event.nfrm,remaining); s->subroute.route.write_event(events,i,&event);
        }
    }
    e->get_range(e->context,&range); m->event(events,0,&event); range.first=event.nfrm;
    m->event(events,m->event_count(events)-1,&event); range.last=event.nfrm; e->publish_range(e->context,&range);
    state->frame=mm_fixed_add(state->frame,shift); publish(state,s); e->invalidate_range(e->context);
    e->scene_frame(e->context,&frame); ok=s->seek_scene(e->context,state,mm_fixed_add(frame,shift));
    if (ok) s->scrollbars(e->context); return ok;
}
