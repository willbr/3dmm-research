#ifndef ENGINE_ROUTE_EDIT_FIXTURE_H
#define ENGINE_ROUTE_EDIT_FIXTURE_H
#include "mm/actor_route_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok,int line) { if (!ok) { fprintf(stderr,"route edit failed at %d\n",line); exit(1); } }
#define CHECK(x) check((x),__LINE__)
typedef struct Fixture
{
    MMActorRoutePoint point[8]; MMActorEvent event[16]; MMI32 payload[16][12]; MMU32 bytes[16];
    MMI32 nodes,events; MMActorState state,view; MMActorSounds sounds; MMActorEditRange range;
    int fail_insert,fail_reserve,fail_apply,reserves,applied,dirty,lives,hidden,defaults,recording,invalidated,swaps,diagnostics;
    int *alive_observer,reset_seen,dirty_node;
} Fixture;
static MMU32 route_width(void *p) { (void)p; return 16; }
static MMU32 event_width(void *p) { (void)p; return 20; }
static MMI32 nodes(void *p) { return ((Fixture *)p)->nodes; }
static MMI32 events(void *p) { return ((Fixture *)p)->events; }
static void point(void *p,MMI32 i,MMActorRoutePoint *out) { Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->nodes); *out=f->point[i]; }
static void event(void *p,MMI32 i,MMActorEvent *out) { Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->events); *out=f->event[i]; }
static MMI32 size(void *p,MMI32 i) { return ((Fixture *)p)->bytes[i]; }
static void payload(void *p,MMI32 i,void *out) { Fixture *f=(Fixture *)p; memcpy(out,f->payload[i],f->bytes[i]); }
static const MMActorMotionServices tables={route_width,nodes,point,event_width,events,event,size,payload};
static int insert_point(void *p,MMI32 i,const MMActorRoutePoint *value)
{
    Fixture *f=(Fixture *)p; if (f->fail_insert) return 0;
    CHECK(f->nodes<8); memmove(f->point+i+1,f->point+i,(size_t)(f->nodes-i)*sizeof(f->point[0])); f->point[i]=*value; ++f->nodes; return 1;
}
static void write_point(void *p,MMI32 i,const MMActorRoutePoint *value) { ((Fixture *)p)->point[i]=*value; }
static void delete_points(void *p,MMI32 i,MMI32 count)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i+count<=f->nodes); memmove(f->point+i,f->point+i+count,(size_t)(f->nodes-i-count)*sizeof(f->point[0])); f->nodes-=count; }
static void clear_route(void *p) { ((Fixture *)p)->nodes=0; }
static void write_event(void *p,MMI32 i,const MMActorEvent *value) { ((Fixture *)p)->event[i]=*value; }
static void remove_event(void *p,MMI32 i)
{
    Fixture *f=(Fixture *)p;
    memmove(f->event+i,f->event+i+1,(size_t)(f->events-i-1)*sizeof(f->event[0]));
    memmove(f->payload+i,f->payload+i+1,(size_t)(f->events-i-1)*sizeof(f->payload[0]));
    memmove(f->bytes+i,f->bytes+i+1,(size_t)(f->events-i-1)*sizeof(f->bytes[0])); --f->events;
}
static int insert_event(void *p,MMI32 i,MMU32 bytes,const void *value,const MMActorEvent *e)
{
    Fixture *f=(Fixture *)p; CHECK(f->events<16 && bytes<=48);
    memmove(f->event+i+1,f->event+i,(size_t)(f->events-i)*sizeof(f->event[0]));
    memmove(f->payload+i+1,f->payload+i,(size_t)(f->events-i)*sizeof(f->payload[0]));
    memmove(f->bytes+i+1,f->bytes+i,(size_t)(f->events-i)*sizeof(f->bytes[0]));
    f->event[i]=*e; f->bytes[i]=bytes; if (value) memcpy(f->payload[i],value,bytes); ++f->events; return 1;
}
static void write_payload(void *p,MMI32 i,const void *v) { Fixture *f=(Fixture *)p; memcpy(f->payload[i],v,f->bytes[i]); }
static void copy_payload(void *p,MMI32 a,MMI32 b) { Fixture *f=(Fixture *)p; memcpy(f->payload[a],f->payload[b],f->bytes[b]); }
static void swap(void *p,MMI32 a,MMI32 b)
{
    Fixture *f=(Fixture *)p; MMActorEvent e=f->event[a]; MMI32 data[12]; MMU32 bytes=f->bytes[a];
    f->event[a]=f->event[b]; f->event[b]=e; memcpy(data,f->payload[a],sizeof(data));
    memcpy(f->payload[a],f->payload[b],sizeof(data)); memcpy(f->payload[b],data,sizeof(data));
    f->bytes[a]=f->bytes[b]; f->bytes[b]=bytes; ++f->swaps;
}
static void tag(void *p,MMI32 i, const MMTagServices *tags) { (void)tags; (void)p; (void)i; }
static int scene(void *p,MMI32 *frame) { (void)p; (void)frame; return 0; }
static void publish(void *p,const MMActorState *state) { ((Fixture *)p)->view=*state; }
static void life(void *p) { ++((Fixture *)p)->lives; }
static void dirty(void *p) { Fixture *f=(Fixture *)p; ++f->dirty; f->dirty_node=f->view.location.irpt; }
static void hidden(void *p) { Fixture *f=(Fixture *)p; CHECK(f->view.onStage); ++f->hidden; }
static void defaults(void *p)
{
    Fixture *f=(Fixture *)p; CHECK(!f->view.onStage && f->lives); ++f->defaults;
    if (f->alive_observer) { CHECK(!*f->alive_observer); ++f->reset_seen; }
}
static void recording(void *p,int enabled) { Fixture *f=(Fixture *)p; CHECK(!enabled); f->recording=0; }
static void range(void *p,MMActorEditRange *out) { *out=((Fixture *)p)->range; }
static void publish_range(void *p,const MMActorEditRange *value) { ((Fixture *)p)->range=*value; }
static void invalidate(void *p) { ++((Fixture *)p)->invalidated; }
static int reserve(void *p,MMU32 count,MMU32 bytes)
{ Fixture *f=(Fixture *)p; CHECK(f->dirty && ((count==2 && bytes==8) || (count==1 && bytes==4))); return ++f->reserves!=f->fail_reserve; }
static void diagnostic(void *p,int fault) { ((Fixture *)p)->diagnostics|=1<<fault; }
static int apply(void *p,MMActorState *state,MMI32 i)
{
    Fixture *f=(Fixture *)p; MMActorPoint origin={0,0,0};
    if (++f->applied==f->fail_apply) return 0;
    return mm_actor_state_apply(state,p,p,i,&origin,&tables)==MM_ACTOR_APPLIED;
}
static void append(Fixture *f,int kind,int node,int offset,int frame)
{
    MMActorEvent e; MMI32 value[3]={0,0,0}; memset(&e,0,sizeof(e)); e.aet=kind; e.nfrm=frame;
    e.rtel.irpt=node; e.rtel.dwrOffset=offset*ONE;
    CHECK(insert_event(f,f->events,kind==MM_ACTOR_TWEAK?12:4,value,&e));
}
static MMActorRouteEditServices init(Fixture *f)
{
    MMActorRouteEditServices s; MMActorEditServices *e;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); mm_actor_state_init(&f->state);
    CHECK(mm_actor_sounds_init(&f->sounds,NULL)); f->nodes=3;
    f->point[0].dwr=4*ONE; f->point[1].xyz.dxr=4*ONE; f->point[2].xyz.dxr=10*ONE;
    append(f,MM_ACTOR_ADD,0,0,1); append(f,MM_ACTOR_TWEAK,0,2,2);
    append(f,MM_ACTOR_FREEZE,1,0,3); append(f,MM_ACTOR_ADD,2,0,8);
    f->state.onStage=1; f->state.frame=2; f->state.location.dwrOffset=2*ONE;
    f->state.nextEvent=2; f->state.firstEventInFrame=1; f->state.addEvent=0; f->state.actionEvent=-1;
    f->view=f->state; f->range.first=1; f->range.last=8; f->recording=1;
    e=&s.edits; e->context=f; e->tables.context=f; e->tables.close_event_tag=tag; e->tables.delete_event=remove_event; e->scene_frame=scene;
    e->publish=publish; e->tables.insert_event=insert_event; e->tables.duplicate_event_tag=tag; e->tables.write_payload=write_payload;
    e->tables.copy_payload=copy_payload; e->apply_event=apply; e->dirty=dirty; e->get_range=range;
    e->publish_range=publish_range; e->invalidate_range=invalidate;
    s.insert_point=insert_point; s.write_point=write_point; s.delete_points=delete_points; s.clear_route=clear_route;
    s.write_event=write_event; s.swap_events=swap; e->tables.reserve_events=reserve; s.life_dirty=life;
    s.recording=recording; s.hide_body=hidden; s.default_costume=defaults; s.diagnostic=diagnostic;
    return s;
}
#endif
