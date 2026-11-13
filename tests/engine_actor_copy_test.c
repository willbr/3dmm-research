#include "three_mm/actor_copy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"copy check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define ONE 65536L
typedef struct Table
{
    int count,locks;
    ThreeMMActorRoutePoint points[8];
    ThreeMMActorEvent events[24];
    unsigned char data[24][64]; ThreeMMI32 sizes[24];
} Table;
typedef struct Actor { Table route,events; int rewound; } Actor;
typedef struct Fixture
{
    Actor source,target;
    ThreeMMActorState state;
    ThreeMMActorCopyResult out;
    ThreeMMActorCopyServices services;
    int created,released,publications,template_refs,tag_refs,inserts,merges,resolves,writes,points,resizes,reserves,copies,copied,distances,unknown;
    int fail_create,fail_event,fail_resolve,fail_point,fail_resize,fail_reserve,zero_distance;
} Fixture;
static ThreeMMI32 count(void *p) { return ((Table *)p)->count; }
static void point(void *p,ThreeMMI32 i,ThreeMMActorRoutePoint *v)
{ Table *t=(Table *)p; CHECK(i>=0 && i<t->count); *v=t->points[i]; }
static void event(void *p,ThreeMMI32 i,ThreeMMActorEvent *v)
{ Table *t=(Table *)p; CHECK(i>=0 && i<t->count); *v=t->events[i]; }
static ThreeMMI32 size(void *p,ThreeMMI32 i)
{ Table *t=(Table *)p; CHECK(t->locks==1 && i>=0 && i<t->count); return t->sizes[i]; }
static void payload(void *p,ThreeMMI32 i,void *v)
{ Table *t=(Table *)p; CHECK(i>=0 && i<t->count); memcpy(v,t->data[i],(size_t)t->sizes[i]); }
static void *create(void *p) { Fixture *f=(Fixture *)p; ++f->created; if (f->fail_create) return NULL; ++f->template_refs; return &f->target; }
static void release(void *p,void *actor)
{
    Fixture *f=(Fixture *)p; int i;
    CHECK(actor==&f->target && f->out.actor==actor && !f->source.events.locks && !f->target.rewound);
    for (i=0;i<f->target.events.count;++i) if (f->target.events.events[i].aet==THREE_MM_ACTOR_SOUND) --f->tag_refs;
    --f->template_refs; ++f->released;
}
static void publish(void *p,void *actor) { Fixture *f=(Fixture *)p; CHECK(f->out.actor==actor); ++f->publications; }
static void *route(void *p,void *actor) { (void)p; return &((Actor *)actor)->route; }
static void *events(void *p,void *actor) { (void)p; return &((Actor *)actor)->events; }
static void lock(void *p,void *t) { (void)p; CHECK(!((Table *)t)->locks); ++((Table *)t)->locks; }
static void unlock(void *p,void *t) { (void)p; CHECK(((Table *)t)->locks==1); --((Table *)t)->locks; }
static const void *view(void *p,void *t,ThreeMMI32 i) { (void)p; CHECK(((Table *)t)->locks==1); return ((Table *)t)->data[i]; }
static void write_payload(void *p,void *t,ThreeMMI32 i,const void *v)
{ Fixture *f=(Fixture *)p; CHECK(t==&f->source.events); ++f->writes; memcpy(((Table *)t)->data[i],v,(size_t)((Table *)t)->sizes[i]); }
static int resolve(void *p,ThreeMMActorSound *sound)
{ Fixture *f=(Fixture *)p; if (++f->resolves==f->fail_resolve) return 0; sound->tag.number=99; return 1; }
static int insert_event(void *p,void *actor,ThreeMMI32 i,ThreeMMU32 bytes,const void *v,const ThreeMMActorEvent *e)
{
    Fixture *f=(Fixture *)p; Table *t=&f->target.events; CHECK(actor==&f->target && f->source.events.locks==1 && i==t->count);
    if (++f->inserts==f->fail_event) return 0;
    CHECK(i<24 && bytes<=64); t->events[i]=*e; t->sizes[i]=(ThreeMMI32)bytes; memcpy(t->data[i],v,bytes); ++t->count;
    if (e->aet==THREE_MM_ACTOR_SOUND) ++f->tag_refs;
    return 1;
}
static void merge(void *p,void *actor,ThreeMMI32 first,ThreeMMI32 added)
{ Fixture *f=(Fixture *)p; CHECK(actor==&f->target && !f->source.events.locks && first==0 && added==f->target.events.count-1); ++f->merges; }
static int insert_point(void *p,void *t,ThreeMMI32 i,const ThreeMMActorRoutePoint *v)
{
    Fixture *f=(Fixture *)p; Table *table=(Table *)t; CHECK(t==&f->target.route && i==table->count);
    if (++f->points==f->fail_point) return 0;
    CHECK(i<8); table->points[i]=*v; ++table->count; return 1;
}
static void write_point(void *p,void *t,ThreeMMI32 i,const ThreeMMActorRoutePoint *v)
{ (void)p; CHECK(i>=0 && i<((Table *)t)->count); ((Table *)t)->points[i]=*v; }
static int resize(void *p,void *t,ThreeMMI32 n)
{ Fixture *f=(Fixture *)p; ++f->resizes; CHECK(t==&f->target.route && n>0 && n<=8); if (f->fail_resize) return 0; ((Table *)t)->count=n; return 1; }
static int reserve(void *p,void *t,ThreeMMI32 n)
{ Fixture *f=(Fixture *)p; ++f->reserves; CHECK(t==&f->target.route && n>0); return !f->fail_reserve; }
static void copy_points(void *p,void *source,ThreeMMI32 first,void *target,ThreeMMI32 to,ThreeMMI32 n)
{
    Fixture *f=(Fixture *)p; ++f->copies; f->copied=n;
    CHECK(source==&f->source.route && target==&f->target.route && first+n<=f->source.route.count && to+n<=f->target.route.count);
    memcpy(((Table *)target)->points+to,((Table *)source)->points+first,(size_t)n*sizeof(ThreeMMActorRoutePoint));
}
static ThreeMMI32 distance(void *p,const ThreeMMActorPoint *v)
{ Fixture *f=(Fixture *)p; ++f->distances; CHECK(!v->dyr && !v->dzr); return f->zero_distance?0:(v->dxr<0?-v->dxr:v->dxr); }
static void rewind_actor(void *p,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(actor==&f->target && !f->source.events.locks && f->target.route.count>0); ++f->target.rewound; }
static void unknown(void *p) { ++((Fixture *)p)->unknown; }
static void append(Fixture *f,int kind,int node,int frame,const void *v,int bytes)
{
    Table *t=&f->source.events; int i=t->count++; t->events[i].aet=kind; t->events[i].nfrm=frame; t->events[i].rtel.irpt=node;
    t->sizes[i]=bytes; if (bytes) memcpy(t->data[i],v,(size_t)bytes);
}
static void init(Fixture *f)
{
    ThreeMMActorCopyServices *s; ThreeMMActorSound sound; ThreeMMI32 action[2]={0,1},scalar=ONE; unsigned char blank[48]; int i;
    memset(f,0,sizeof(*f)); memset(&sound,0,sizeof(sound)); memset(blank,0,sizeof(blank));
    s=&f->services; s->context=f; s->motion.route_count=count; s->motion.route_point=point; s->motion.event_count=count;
    s->motion.event=event; s->motion.event_size=size; s->motion.payload=payload;
    s->create_actor=create; s->release_actor=release; s->route=route; s->events=events; s->lock_events=lock; s->unlock_events=unlock;
    s->payload_view=view; s->write_payload=write_payload; s->resolve_sound=resolve; s->insert_event=insert_event; s->merge_event=merge;
    s->insert_point=insert_point; s->write_point=write_point; s->resize_route=resize; s->reserve_route=reserve; s->copy_points=copy_points;
    s->distance=distance; s->rewind_actor=rewind_actor; s->unknown_event=unknown;
    f->out.context=f; f->out.publish=publish; f->template_refs=1; f->tag_refs=2;
    f->source.route.count=4;
    for (i=0;i<4;++i) { f->source.route.points[i].xyz.dxr=i*10*ONE; f->source.route.points[i].dwr=(i==1 || i==3)?0:10*ONE; }
    append(f,THREE_MM_ACTOR_ADD,0,1,blank,20); append(f,THREE_MM_ACTOR_ACTION,0,1,action,8);
    sound.cel=-1; sound.tag.number=10; append(f,THREE_MM_ACTOR_SOUND,0,2,&sound,sizeof(sound));
    append(f,THREE_MM_ACTOR_TWEAK,0,2,blank,12); append(f,THREE_MM_ACTOR_ROTATE_HERE,0,3,blank,48);
    sound.cel=2; append(f,THREE_MM_ACTOR_SOUND,0,3,&sound,sizeof(sound)); append(f,THREE_MM_ACTOR_REMOVE,0,3,blank,0);
    append(f,THREE_MM_ACTOR_STEP,0,4,&scalar,4); f->source.events.events[7].rtel.dwrOffset=7*ONE; f->source.events.events[7].rtel.dnfrm=1;
    append(f,THREE_MM_ACTOR_ADD,2,8,blank,20); append(f,THREE_MM_ACTOR_ACTION,2,8,action,8); append(f,THREE_MM_ACTOR_STEP,3,9,&scalar,4);
    three_mm_actor_state_init(&f->state); f->state.cel=3; f->state.addEvent=0; f->state.nextEvent=7;
    f->state.firstEventInFrame=4; f->state.actionEvent=1; f->state.location.dwrOffset=5*ONE; f->state.location.dnfrm=2;
}
static int full(Fixture *f,int entire,int postponed)
{ return three_mm_actor_copy(&f->state,&f->source.route,&f->source.events,entire,postponed,&f->out,&f->services); }
static int path(Fixture *f,int entire)
{ return three_mm_actor_copy_route(&f->state,&f->source.route,entire,&f->out,&f->services); }
static ThreeMMActorSound sound_at(Table *t,int i) { ThreeMMActorSound sound; memcpy(&sound,t->data[i],sizeof(sound)); return sound; }
static void failed(Fixture *f)
{ CHECK(!f->out.actor && f->released==1 && f->publications==2 && f->template_refs==1 && f->tag_refs==2 && !f->source.events.locks && !f->target.rewound); }
int main(void)
{
    Fixture f; ThreeMMI32 action[2]; int fail;
    init(&f); CHECK(full(&f,0,0)); CHECK(f.out.actor==&f.target && f.target.rewound==1 && f.target.events.count==5 && f.merges==4);
    CHECK(f.target.route.count==2 && !f.target.route.points[0].xyz.dxr && f.target.route.points[0].dwr==5*ONE);
    CHECK(f.resolves==2 && f.writes==2 && sound_at(&f.source.events,2).tag.number==99 && sound_at(&f.source.events,5).tag.number==99);
    CHECK(f.tag_refs==3 && f.template_refs==2 && sound_at(&f.target.events,3).cel==2);
    memcpy(action,f.target.events.data[1],8); CHECK(action[1]==3); memcpy(action,f.source.events.data[1],8); CHECK(action[1]==1);
    CHECK(!f.target.events.events[0].rtel.irpt && !f.target.events.events[0].rtel.dnfrm && f.target.events.events[0].nfrm==1);
    CHECK(f.target.events.events[4].rtel.dwrOffset==2*ONE && f.target.events.events[4].rtel.dnfrm==1);
    init(&f); CHECK(full(&f,1,0) && f.target.route.count==4 && f.target.events.count==8);
    memcpy(action,f.target.events.data[6],8); CHECK(action[1]==1 && f.target.events.events[5].rtel.irpt==2);
    init(&f); CHECK(full(&f,0,1) && f.target.events.count==4 && f.merges==3);
    init(&f); f.source.events.events[7].rtel.dwrOffset=5*ONE; CHECK(full(&f,0,0) && !f.target.events.events[4].rtel.dnfrm && !f.target.events.events[4].rtel.dwrOffset);
    init(&f); f.source.events.events[7].rtel.dwrOffset=5*ONE; f.source.events.events[7].rtel.dnfrm=4;
    CHECK(full(&f,0,0) && f.target.events.events[4].rtel.dnfrm==2);
    init(&f); f.state.location.dwrOffset=10*ONE; CHECK(full(&f,0,0) && !f.target.events.events[4].rtel.dwrOffset);
    init(&f); f.state.nextEvent=f.state.addEvent=0; CHECK(full(&f,0,0) && f.target.events.count==8 && !f.merges);
    init(&f); f.source.events.events[3].aet=99; CHECK(full(&f,0,0) && f.unknown==1);
    init(&f); f.state.location.irpt=1; f.state.location.dwrOffset=0; f.source.events.events[7].rtel.irpt=1; f.source.events.events[7].rtel.dwrOffset=0;
    CHECK(full(&f,0,0) && f.target.route.count==1 && f.copies==1 && !f.copied);
    init(&f); f.state.location.irpt=3; f.state.location.dwrOffset=0; f.state.addEvent=8; f.state.nextEvent=11;
    CHECK(full(&f,0,0) && f.target.route.count==1 && !f.target.route.points[0].dwr && !f.resolves);
    for (fail=1;fail<=5;++fail) { init(&f); f.fail_event=fail; CHECK(!full(&f,0,0)); failed(&f); }
    for (fail=1;fail<=2;++fail)
    {
        init(&f); f.fail_resolve=fail; CHECK(!full(&f,0,0)); failed(&f); CHECK(f.writes==fail-1);
        CHECK(sound_at(&f.source.events,2).tag.number==(fail==1?10U:99U));
    }
    init(&f); f.fail_point=1; CHECK(!full(&f,0,0)); failed(&f);
    init(&f); f.fail_resize=1; CHECK(!full(&f,0,0)); failed(&f); CHECK(!f.copies);
    init(&f); f.fail_create=1; CHECK(!full(&f,0,0) && !f.out.actor && f.publications==1 && !f.released && f.template_refs==1);
    init(&f); CHECK(path(&f,0) && f.target.route.count==2 && !f.target.events.count && f.target.route.points[0].xyz.dxr==5*ONE && f.target.route.points[0].dwr==5*ONE);
    CHECK(!f.resolves && !f.writes && f.reserves==1 && f.distances==1);
    init(&f); CHECK(path(&f,1) && f.target.route.count==4);
    init(&f); f.state.location.dwrOffset=0; CHECK(path(&f,0) && !f.distances && f.target.route.points[0].dwr==10*ONE);
    init(&f); f.state.location.irpt=1; f.state.location.dwrOffset=0; CHECK(path(&f,0) && f.target.route.count==1 && !f.reserves);
    init(&f); f.state.location.dwrOffset=10*ONE; CHECK(path(&f,0) && f.target.route.points[0].dwr==1);
    for (fail=1;fail<=2;++fail) { init(&f); f.fail_point=fail; CHECK(!path(&f,0)); failed(&f); }
    init(&f); f.fail_reserve=1; CHECK(!path(&f,0)); failed(&f);
    init(&f); f.fail_create=1; CHECK(!path(&f,0) && !f.out.actor && !f.released);
    puts("actor copy C89 checks passed"); return 0;
}
