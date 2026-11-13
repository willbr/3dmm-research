#include "three_mm/actor_route_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok,int line) { if (!ok) { fprintf(stderr,"route edit failed at %d\n",line); exit(1); } }
#define CHECK(x) check((x),__LINE__)
typedef struct Fixture
{
    ThreeMMActorRoutePoint point[8]; ThreeMMActorEvent event[16]; ThreeMMI32 payload[16][12]; ThreeMMU32 bytes[16];
    ThreeMMI32 nodes,events; ThreeMMActorState state,view; ThreeMMActorSounds sounds; ThreeMMActorEditRange range;
    int fail_insert,fail_reserve,fail_apply,reserves,applied,dirty,lives,hidden,defaults,recording,invalidated,swaps,diagnostics;
    int *alive_observer,reset_seen,dirty_node;
} Fixture;
static ThreeMMU32 route_width(void *p) { (void)p; return 16; }
static ThreeMMU32 event_width(void *p) { (void)p; return 20; }
static ThreeMMI32 nodes(void *p) { return ((Fixture *)p)->nodes; }
static ThreeMMI32 events(void *p) { return ((Fixture *)p)->events; }
static void point(void *p,ThreeMMI32 i,ThreeMMActorRoutePoint *out) { Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->nodes); *out=f->point[i]; }
static void event(void *p,ThreeMMI32 i,ThreeMMActorEvent *out) { Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->events); *out=f->event[i]; }
static ThreeMMI32 size(void *p,ThreeMMI32 i) { return ((Fixture *)p)->bytes[i]; }
static void payload(void *p,ThreeMMI32 i,void *out) { Fixture *f=(Fixture *)p; memcpy(out,f->payload[i],f->bytes[i]); }
static const ThreeMMActorMotionServices tables={route_width,nodes,point,event_width,events,event,size,payload};
static int insert_point(void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *value)
{
    Fixture *f=(Fixture *)p; if (f->fail_insert) return 0;
    CHECK(f->nodes<8); memmove(f->point+i+1,f->point+i,(size_t)(f->nodes-i)*sizeof(f->point[0])); f->point[i]=*value; ++f->nodes; return 1;
}
static void write_point(void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *value) { ((Fixture *)p)->point[i]=*value; }
static void delete_points(void *p,ThreeMMI32 i,ThreeMMI32 count)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i+count<=f->nodes); memmove(f->point+i,f->point+i+count,(size_t)(f->nodes-i-count)*sizeof(f->point[0])); f->nodes-=count; }
static void clear_route(void *p) { ((Fixture *)p)->nodes=0; }
static void write_event(void *p,ThreeMMI32 i,const ThreeMMActorEvent *value) { ((Fixture *)p)->event[i]=*value; }
static void remove_event(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p;
    memmove(f->event+i,f->event+i+1,(size_t)(f->events-i-1)*sizeof(f->event[0]));
    memmove(f->payload+i,f->payload+i+1,(size_t)(f->events-i-1)*sizeof(f->payload[0]));
    memmove(f->bytes+i,f->bytes+i+1,(size_t)(f->events-i-1)*sizeof(f->bytes[0])); --f->events;
}
static int insert_event(void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *value,const ThreeMMActorEvent *e)
{
    Fixture *f=(Fixture *)p; CHECK(f->events<16 && bytes<=48);
    memmove(f->event+i+1,f->event+i,(size_t)(f->events-i)*sizeof(f->event[0]));
    memmove(f->payload+i+1,f->payload+i,(size_t)(f->events-i)*sizeof(f->payload[0]));
    memmove(f->bytes+i+1,f->bytes+i,(size_t)(f->events-i)*sizeof(f->bytes[0]));
    f->event[i]=*e; f->bytes[i]=bytes; if (value) memcpy(f->payload[i],value,bytes); ++f->events; return 1;
}
static void write_payload(void *p,ThreeMMI32 i,const void *v) { Fixture *f=(Fixture *)p; memcpy(f->payload[i],v,f->bytes[i]); }
static void copy_payload(void *p,ThreeMMI32 a,ThreeMMI32 b) { Fixture *f=(Fixture *)p; memcpy(f->payload[a],f->payload[b],f->bytes[b]); }
static void swap(void *p,ThreeMMI32 a,ThreeMMI32 b)
{
    Fixture *f=(Fixture *)p; ThreeMMActorEvent e=f->event[a]; ThreeMMI32 data[12]; ThreeMMU32 bytes=f->bytes[a];
    f->event[a]=f->event[b]; f->event[b]=e; memcpy(data,f->payload[a],sizeof(data));
    memcpy(f->payload[a],f->payload[b],sizeof(data)); memcpy(f->payload[b],data,sizeof(data));
    f->bytes[a]=f->bytes[b]; f->bytes[b]=bytes; ++f->swaps;
}
static void tag(void *p,ThreeMMI32 i) { (void)p; (void)i; }
static int scene(void *p,ThreeMMI32 *frame) { (void)p; (void)frame; return 0; }
static void publish(void *p,const ThreeMMActorState *state) { ((Fixture *)p)->view=*state; }
static void life(void *p) { ++((Fixture *)p)->lives; }
static void dirty(void *p) { Fixture *f=(Fixture *)p; ++f->dirty; f->dirty_node=f->view.location.irpt; }
static void hidden(void *p) { Fixture *f=(Fixture *)p; CHECK(f->view.onStage); ++f->hidden; }
static void defaults(void *p)
{
    Fixture *f=(Fixture *)p; CHECK(!f->view.onStage && f->lives); ++f->defaults;
    if (f->alive_observer) { CHECK(!*f->alive_observer); ++f->reset_seen; }
}
static void recording(void *p,int enabled) { Fixture *f=(Fixture *)p; CHECK(!enabled); f->recording=0; }
static void range(void *p,ThreeMMActorEditRange *out) { *out=((Fixture *)p)->range; }
static void publish_range(void *p,const ThreeMMActorEditRange *value) { ((Fixture *)p)->range=*value; }
static void invalidate(void *p) { ++((Fixture *)p)->invalidated; }
static int reserve(void *p,ThreeMMU32 count,ThreeMMU32 bytes)
{ Fixture *f=(Fixture *)p; CHECK(f->dirty && ((count==2 && bytes==8) || (count==1 && bytes==4))); return ++f->reserves!=f->fail_reserve; }
static void diagnostic(void *p,int fault) { ((Fixture *)p)->diagnostics|=1<<fault; }
static int apply(void *p,ThreeMMActorState *state,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; ThreeMMActorPoint origin={0,0,0};
    if (++f->applied==f->fail_apply) return 0;
    return three_mm_actor_state_apply(state,p,p,i,&origin,&tables)==THREE_MM_ACTOR_APPLIED;
}
static void append(Fixture *f,int kind,int node,int offset,int frame)
{
    ThreeMMActorEvent e; ThreeMMI32 value[3]={0,0,0}; memset(&e,0,sizeof(e)); e.aet=kind; e.nfrm=frame;
    e.rtel.irpt=node; e.rtel.dwrOffset=offset*ONE;
    CHECK(insert_event(f,f->events,kind==THREE_MM_ACTOR_TWEAK?12:4,value,&e));
}
static ThreeMMActorRouteEditServices init(Fixture *f)
{
    ThreeMMActorRouteEditServices s; ThreeMMActorEditServices *e;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); three_mm_actor_state_init(&f->state);
    CHECK(three_mm_actor_sounds_init(&f->sounds,NULL)); f->nodes=3;
    f->point[0].dwr=4*ONE; f->point[1].xyz.dxr=4*ONE; f->point[2].xyz.dxr=10*ONE;
    append(f,THREE_MM_ACTOR_ADD,0,0,1); append(f,THREE_MM_ACTOR_TWEAK,0,2,2);
    append(f,THREE_MM_ACTOR_FREEZE,1,0,3); append(f,THREE_MM_ACTOR_ADD,2,0,8);
    f->state.onStage=1; f->state.frame=2; f->state.location.dwrOffset=2*ONE;
    f->state.nextEvent=2; f->state.firstEventInFrame=1; f->state.addEvent=0; f->state.actionEvent=-1;
    f->view=f->state; f->range.first=1; f->range.last=8; f->recording=1;
    e=&s.edits; e->context=f; e->close_event_tag=tag; e->delete_event=remove_event; e->scene_frame=scene;
    e->publish=publish; e->insert_event=insert_event; e->duplicate_event_tag=tag; e->write_payload=write_payload;
    e->copy_payload=copy_payload; e->apply_event=apply; e->dirty=dirty; e->get_range=range;
    e->publish_range=publish_range; e->invalidate_range=invalidate;
    s.insert_point=insert_point; s.write_point=write_point; s.delete_points=delete_points; s.clear_route=clear_route;
    s.write_event=write_event; s.swap_events=swap; s.reserve_events=reserve; s.life_dirty=life;
    s.recording=recording; s.hide_body=hidden; s.default_costume=defaults; s.diagnostic=diagnostic;
    return s;
}
static void point_test(void)
{
    Fixture f; ThreeMMActorRouteEditServices s=init(&f); ThreeMMActorRoutePoint input={{9*ONE,0,0},ONE},saved=input;
    f.fail_insert=1; CHECK(!three_mm_actor_route_edit_insert(&f,1,&input,0,&tables,&s));
    CHECK(!memcmp(&input,&saved,sizeof(input)) && f.nodes==3 && f.point[0].dwr==4*ONE);
    f.fail_insert=0; CHECK(three_mm_actor_route_edit_insert(&f,1,&input,0,&tables,&s));
    CHECK(f.point[0].dwr==1 && input.dwr==4*ONE && f.point[1].dwr==4*ONE);
    three_mm_actor_sounds_dispose(&f.sounds);
    s=init(&f); input=saved; CHECK(three_mm_actor_route_edit_insert(&f,1,&input,8*ONE,&tables,&s));
    CHECK(input.dwr==1 && f.point[0].dwr==8*ONE); three_mm_actor_sounds_dispose(&f.sounds);
    s=init(&f); f.point[1].dwr=ONE; f.event[1].rtel.irpt=1; f.event[1].rtel.dwrOffset=2147483646L;
    three_mm_actor_route_edit_adjust_delete(&f.state,&f,&f,1,1,&tables,&s);
    CHECK(f.event[1].rtel.irpt==0 && f.event[1].rtel.dwrOffset==-2147221506L); three_mm_actor_sounds_dispose(&f.sounds);
}
static void forward_test(void)
{
    Fixture f; ThreeMMActorRouteEditServices s; int alive,mode;
    s=init(&f); three_mm_actor_route_edit_delete_forward_core(&f.state,&f,&f,0,&alive,-1,&tables,&s);
    CHECK(alive && f.nodes==3 && f.events==3 && f.state.location.irpt==1 && !f.state.location.dwrOffset);
    CHECK(f.point[1].xyz.dxr==4*ONE && f.point[0].dwr==2*ONE && !f.invalidated && f.lives==1 && f.dirty==1);
    CHECK(f.event[1].rtel.irpt==1 && !f.event[1].rtel.dwrOffset); three_mm_actor_sounds_dispose(&f.sounds);
    for (mode=0;mode<5;++mode)
    {
        s=init(&f); f.fail_reserve=mode==1?1:mode==2?2:0; f.fail_apply=mode==3?1:mode==4?2:0;
        three_mm_actor_route_edit_delete_forward(&f.state,&f.sounds,&f,&f,7,0,&alive,-1,&tables,&s);
        CHECK(alive && !f.invalidated && f.recording==1);
        if (mode==0) CHECK(f.applied==2 && f.reserves==2 && f.events==5 && f.state.frozen && f.swaps==1);
        if (mode==1) CHECK(f.applied==0 && f.events==3 && !f.swaps);
        if (mode==2 || mode==4) CHECK(f.diagnostics==(1<<THREE_MM_ROUTE_STEP_FAILED));
        if (mode==3) CHECK(f.diagnostics==(1<<THREE_MM_ROUTE_FREEZE_FAILED) && f.applied==2);
        if (mode!=1) CHECK(f.event[f.state.nextEvent-1].aet==THREE_MM_ACTOR_TWEAK);
        three_mm_actor_sounds_dispose(&f.sounds);
    }
    s=init(&f); f.state.frame=0; f.state.nextEvent=f.state.firstEventInFrame=0; alive=1; f.alive_observer=&alive;
    three_mm_actor_route_edit_delete_forward_core(&f.state,&f,&f,1,&alive,-1,&tables,&s);
    CHECK(!alive && !f.nodes && !f.events && f.dirty_node==-1 && f.reset_seen==1 && !f.recording);
    CHECK(f.hidden==1 && f.defaults==1 && !f.state.onStage && f.state.addEvent==-1 && f.state.location.irpt==0);
    CHECK(!f.invalidated && f.range.first==1 && f.range.last==8); three_mm_actor_sounds_dispose(&f.sounds);
}
int main(void) { point_test(); forward_test(); puts("actor route editing passed"); return 0; }
