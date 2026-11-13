#include "three_mm/actor_costume_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok,int line) { if (!ok) { fprintf(stderr,"costume edit failed at %d\n",line); exit(1); } }
#define CHECK(x) check((x),__LINE__)
typedef struct Resource { int id,custom,refs; } Resource;
typedef struct Fixture
{
    ThreeMMActorEvent event[8]; ThreeMMActorCostume value[8]; int count;
    ThreeMMActorState state; ThreeMMActorSounds sounds;
    Resource resource[2][3]; void *current; int custom;
    int trace[100],traces,fetches,fail_fetch,reserves,fail_reserve,fail_insert,fail_apply,applied,dirty;
} Fixture;
static void record(Fixture *f,int code) { CHECK(f->traces<100); f->trace[f->traces++]=code; }
static ThreeMMI32 count(void *p) { return ((Fixture *)p)->count; }
static void event(void *p,ThreeMMI32 i,ThreeMMActorEvent *out) { *out=((Fixture *)p)->event[i]; }
static void payload(void *p,ThreeMMI32 i,void *out) { *(ThreeMMActorCostume *)out=((Fixture *)p)->value[i]; }
static const ThreeMMActorMotionServices tables={0,0,0,0,count,event,0,payload};
static void close_tag(void *p,ThreeMMI32 i) { record((Fixture *)p,700+i); }
static void remove_event(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; record(f,800+i);
    memmove(f->event+i,f->event+i+1,(size_t)(f->count-i-1)*sizeof(f->event[0]));
    memmove(f->value+i,f->value+i+1,(size_t)(f->count-i-1)*sizeof(f->value[0])); --f->count;
}
static int scene(void *p,ThreeMMI32 *frame) { (void)p; (void)frame; return 0; }
static void *current(void *p,ThreeMMI32 set,int *custom)
{ Fixture *f=(Fixture *)p; CHECK(set>=0); record(f,100); *custom=f->custom; return f->current; }
static void *fetch(Fixture *f,int custom,int id)
{
    Resource *r; record(f,200+custom*100+id); ++f->fetches;
    if (id>=3 || f->fetches==f->fail_fetch) return NULL;
    r=&f->resource[custom][id]; ++r->refs; return r;
}
static void *material(void *p,const ThreeMMTag *tag) { return fetch((Fixture *)p,0,(int)tag->number); }
static void *custom(void *p,ThreeMMI32 id) { return fetch((Fixture *)p,1,id); }
static void release(void *p,void *value)
{ Fixture *f=(Fixture *)p; Resource *r=(Resource *)value; CHECK(r->refs>1); record(f,400+r->custom*10+r->id); --r->refs; }
static ThreeMMI32 body_set(void *p,void *value)
{ Resource *r=(Resource *)value; CHECK(r->custom); record((Fixture *)p,500+r->id); return r->id; }
static int reserve(void *p,ThreeMMU32 entries,ThreeMMU32 bytes)
{ Fixture *f=(Fixture *)p; CHECK(entries==1 && bytes==sizeof(ThreeMMActorCostume)); record(f,600); ++f->reserves; return !f->fail_reserve; }
static int insert(void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *value,const ThreeMMActorEvent *e)
{
    Fixture *f=(Fixture *)p; CHECK(bytes==sizeof(ThreeMMActorCostume) && f->count<8); record(f,900);
    if (f->fail_insert) return 0;
    memmove(f->event+i+1,f->event+i,(size_t)(f->count-i)*sizeof(f->event[0]));
    memmove(f->value+i+1,f->value+i,(size_t)(f->count-i)*sizeof(f->value[0]));
    f->event[i]=*e; f->value[i]=*(const ThreeMMActorCostume *)value; ++f->count; return 1;
}
static void duplicate(void *p,ThreeMMI32 i) { (void)p; (void)i; }
static int apply(void *p,ThreeMMActorState *state,ThreeMMI32 i)
{ Fixture *f=(Fixture *)p; CHECK(f->event[i].aet==THREE_MM_ACTOR_COSTUME); record(f,1000); ++f->applied; state->cel=99; return !f->fail_apply; }
static void dirty(void *p) { ++((Fixture *)p)->dirty; }
static void range(void *p,ThreeMMActorEditRange *out) { (void)p; out->first=1; out->last=20; }
static ThreeMMActorCostumeEditServices init(Fixture *f)
{
    ThreeMMActorCostumeEditServices s; int type,id;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); three_mm_actor_state_init(&f->state);
    CHECK(three_mm_actor_sounds_init(&f->sounds,NULL)); f->state.onStage=1; f->state.frame=2;
    f->state.nextEvent=f->state.firstEventInFrame=1; f->state.actionEvent=-1; f->state.addEvent=0;
    f->count=1; f->event[0].aet=THREE_MM_ACTOR_ADD;
    for (type=0;type<2;++type) for (id=0;id<3;++id)
    { Resource *r=&f->resource[type][id]; r->id=id; r->custom=type; r->refs=1; }
    f->current=&f->resource[0][0];
    s.context=f; s.current=current; s.fetch_material=material; s.fetch_custom=custom; s.release=release;
    s.body_set=body_set; s.reserve_events=reserve; s.edits.context=f; s.edits.close_event_tag=close_tag;
    s.edits.delete_event=remove_event; s.edits.scene_frame=scene; s.edits.insert_event=insert;
    s.edits.duplicate_event_tag=duplicate; s.edits.apply_event=apply; s.edits.dirty=dirty; s.edits.get_range=range;
    return s;
}
static ThreeMMActorCostume costume(int custom,int id,int set)
{ ThreeMMActorCostume c; memset(&c,0,sizeof(c)); c.custom=custom; c.costume=id; c.tag.number=id; c.body_set=set; return c; }
static void append(Fixture *f,int custom,int id,int set)
{ int i=f->count++; f->event[i].aet=THREE_MM_ACTOR_COSTUME; f->value[i]=costume(custom,id,set); }
static void done(Fixture *f)
{ int type,id; for (type=0;type<2;++type) for (id=0;id<3;++id) CHECK(f->resource[type][id].refs==1); three_mm_actor_sounds_dispose(&f->sounds); }
static void fill_tests(void)
{
    Fixture f; ThreeMMActorCostumeEditServices s=init(&f); ThreeMMActorCostume incoming=costume(0,1,0);
    static const int expected[]={100,201,201,401,201,401,701,801,202,401,402};
    int current_type,incoming_type,failure;
    append(&f,0,1,0); append(&f,0,2,0);
    three_mm_actor_edit_costume_fill(&f.state,&f,1,&incoming,&tables,&s);
    CHECK(f.count==2 && f.value[1].tag.number==2 && f.traces==sizeof(expected)/sizeof(expected[0]));
    CHECK(!memcmp(f.trace,expected,sizeof(expected))); done(&f);
    s=init(&f); f.current=NULL; append(&f,0,3,0);
    incoming=costume(0,3,0); three_mm_actor_edit_costume_fill(&f.state,&f,1,&incoming,&tables,&s);
    CHECK(f.count==1 && f.fetches==1); done(&f);
    s=init(&f); append(&f,0,3,0); incoming=costume(0,3,0);
    three_mm_actor_edit_costume_fill(&f.state,&f,1,&incoming,&tables,&s);
    CHECK(f.count==1 && f.fetches==3); done(&f);
    for (current_type=0;current_type<2;++current_type) for (incoming_type=0;incoming_type<3;++incoming_type)
        for (failure=0;failure<7;++failure)
    {
        s=init(&f); f.custom=current_type; f.current=&f.resource[current_type][0]; f.fail_fetch=failure;
        append(&f,current_type,0,0); append(&f,1,0,1); append(&f,incoming_type,1,0); append(&f,incoming_type,1,0);
        f.event[f.count++].aet=THREE_MM_ACTOR_ADD; append(&f,incoming_type,1,0);
        incoming=costume(incoming_type,1,0);
        three_mm_actor_edit_costume_fill(&f.state,&f,1,&incoming,&tables,&s);
        CHECK(f.event[f.count-2].aet==THREE_MM_ACTOR_ADD && f.value[f.count-1].body_set==0);
        done(&f);
    }
}
static void setter_tests(void)
{
    Fixture f; ThreeMMActorCostumeEditServices s; ThreeMMActorCostume incoming=costume(2,1,9); int mode,ok;
    static const int failed_reserve[]={301,501,411,600};
    for (mode=0;mode<5;++mode)
    {
        s=init(&f); f.fail_reserve=mode==1; f.fail_insert=mode==2; f.fail_apply=mode==3;
        if (mode==4) f.fail_fetch=1;
        ok=three_mm_actor_edit_set_costume(&f.state,&f.sounds,&f,7,9,&incoming.tag,1,2,&tables,&s);
        CHECK(ok==(mode==0));
        if (mode==1) CHECK(f.traces==4 && !memcmp(f.trace,failed_reserve,sizeof(failed_reserve)));
        if (mode==0 || mode==3) CHECK(f.value[1].custom==2 && f.value[1].body_set==1 && f.state.cel==99);
        CHECK(f.dirty==(mode==0)); done(&f);
    }
    s=init(&f); append(&f,0,0,0); incoming=costume(0,1,0); f.fail_insert=1;
    CHECK(!three_mm_actor_edit_set_costume(&f.state,&f.sounds,&f,7,0,&incoming.tag,1,0,&tables,&s));
    CHECK(f.count==1 && f.applied==0 && !f.dirty); done(&f);
}
int main(void) { fill_tests(); setter_tests(); puts("actor costume editing passed"); return 0; }
