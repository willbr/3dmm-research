#include "three_mm/actor_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok,int line) { if (!ok) { fprintf(stderr,"event edit failed at %d\n",line); exit(1); } }
#define CHECK(x) check((x),__LINE__)
typedef union Payload { ThreeMMActorSound sound; ThreeMMActorCostume costume; ThreeMMActorPoint point;
    ThreeMMActorMatrix matrix; ThreeMMI32 scalar; } Payload;
typedef struct Fixture
{
    ThreeMMActorEvent event[16]; Payload payload[16]; ThreeMMU32 bytes[16]; int count;
    ThreeMMActorState state,view; ThreeMMActorSounds sounds; ThreeMMActorEditRange range,seen[2];
    ThreeMMI32 result;
    int refs[2],closed,duplicated,published,applied,dirty,invalidated,fail_insert,fail_apply,observe_result;
} Fixture;
static ThreeMMU32 width(void *p) { (void)p; return 20; }
static ThreeMMI32 count(void *p) { return ((Fixture *)p)->count; }
static void event(void *p,ThreeMMI32 i,ThreeMMActorEvent *out)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); *out=f->event[i]; }
static void payload(void *p,ThreeMMI32 i,void *out)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); memcpy(out,&f->payload[i],f->bytes[i]); }
static const ThreeMMActorMotionServices tables={0,0,0,width,count,event,0,payload};
static ThreeMMTag *tag(Fixture *f,int i)
{
    if (f->event[i].aet==THREE_MM_ACTOR_COSTUME && !f->payload[i].costume.custom) return &f->payload[i].costume.tag;
    if (f->event[i].aet==THREE_MM_ACTOR_SOUND) return &f->payload[i].sound.tag;
    return NULL;
}
static void close_tag(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; ThreeMMTag *t=tag(f,i); ++f->closed;
    if (t && !t->sid) { CHECK(t->cache && *(int *)t->cache>0); --*(int *)t->cache; }
}
static void duplicate(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; ThreeMMTag *t=tag(f,i); ++f->duplicated;
    if (t && !t->sid) { CHECK(t->cache && *(int *)t->cache>0); ++*(int *)t->cache; }
}
static void remove_event(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count);
    if (f->observe_result) CHECK(f->result==1);
    memmove(f->event+i,f->event+i+1,(size_t)(f->count-i-1)*sizeof(f->event[0]));
    memmove(f->payload+i,f->payload+i+1,(size_t)(f->count-i-1)*sizeof(f->payload[0]));
    memmove(f->bytes+i,f->bytes+i+1,(size_t)(f->count-i-1)*sizeof(f->bytes[0])); --f->count;
}
static int scene(void *p,ThreeMMI32 *frame) { (void)p; *frame=-77; return 1; }
static void notify(void *p) { (void)p; }
static void publish(void *p,const ThreeMMActorState *state)
{ Fixture *f=(Fixture *)p; f->view=*state; ++f->published; }
static int insert(void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *data,const ThreeMMActorEvent *e)
{
    Fixture *f=(Fixture *)p; if (f->fail_insert) return 0;
    CHECK(f->count<16 && i>=0 && i<=f->count && bytes<=sizeof(Payload));
    memmove(f->event+i+1,f->event+i,(size_t)(f->count-i)*sizeof(f->event[0]));
    memmove(f->payload+i+1,f->payload+i,(size_t)(f->count-i)*sizeof(f->payload[0]));
    memmove(f->bytes+i+1,f->bytes+i,(size_t)(f->count-i)*sizeof(f->bytes[0]));
    f->event[i]=*e; f->bytes[i]=bytes; memset(&f->payload[i],0,sizeof(Payload));
    if (data) memcpy(&f->payload[i],data,bytes);
    ++f->count; return 1;
}
static void write_payload(void *p,ThreeMMI32 i,const void *data)
{ Fixture *f=(Fixture *)p; memcpy(&f->payload[i],data,f->bytes[i]); }
static void copy_payload(void *p,ThreeMMI32 dst,ThreeMMI32 src)
{ Fixture *f=(Fixture *)p; CHECK(f->bytes[dst]==f->bytes[src]); f->payload[dst]=f->payload[src]; }
static int apply(void *p,ThreeMMActorState *state,ThreeMMI32 i)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); ++f->applied; state->cel=99; return !f->fail_apply; }
static void dirty(void *p) { ++((Fixture *)p)->dirty; }
static void range(void *p,ThreeMMActorEditRange *out) { *out=((Fixture *)p)->range; }
static void invalidate(void *p)
{ Fixture *f=(Fixture *)p; CHECK(f->invalidated<2); f->seen[f->invalidated++]=f->range; }
static void publish_range(void *p,const ThreeMMActorEditRange *r) { ((Fixture *)p)->range=*r; }
static ThreeMMActorEditServices init(Fixture *f)
{
    ThreeMMActorEditServices s;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); three_mm_actor_state_init(&f->state);
    CHECK(three_mm_actor_sounds_init(&f->sounds,NULL)); f->state.onStage=1; f->state.frame=4;
    f->state.addEvent=0; f->state.firstEventInFrame=1; f->state.nextEvent=2; f->result=-99;
    f->range.first=1; f->range.last=30;
    s.context=f; s.close_event_tag=close_tag; s.delete_event=remove_event; s.scene_frame=scene;
    s.update_sound_frame=notify; s.publish=publish; s.insert_event=insert; s.duplicate_event_tag=duplicate;
    s.write_payload=write_payload; s.copy_payload=copy_payload; s.apply_event=apply; s.dirty=dirty;
    s.get_range=range; s.invalidate_range=invalidate; s.publish_range=publish_range;
    return s;
}
static void append(Fixture *f,int kind,ThreeMMU32 bytes,const void *data,int frame)
{ ThreeMMActorEvent e; memset(&e,0,sizeof(e)); e.aet=kind; e.nfrm=frame; CHECK(insert(f,f->count,bytes,data,&e)); }
static void ownership_test(void)
{
    Fixture f; ThreeMMActorEditServices s=init(&f); ThreeMMActorCostume a,b;
    append(&f,THREE_MM_ACTOR_ADD,0,NULL,1); memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
    a.body_set=b.body_set=1; a.tag.cache=&f.refs[0]; b.tag.cache=&f.refs[1];
    f.refs[0]=f.refs[1]=1;
    append(&f,THREE_MM_ACTOR_COSTUME,sizeof(a),&a,4); append(&f,THREE_MM_ACTOR_COSTUME,sizeof(b),&b,4);
    f.observe_result=1;
    three_mm_actor_edit_merge(&f.state,&f.sounds,&f,23,1,2,&f.result,&tables,&s);
    CHECK(f.count==2 && f.result==1 && f.refs[0]==0 && f.refs[1]==1 && f.duplicated==1);
    f.observe_result=0;
    three_mm_actor_edit_remove(&f.state,&f,1,1,&tables,&s);
    CHECK(f.refs[1]==0); three_mm_actor_sounds_dispose(&f.sounds);
}
static void insert_test(void)
{
    Fixture f; ThreeMMActorEditServices s=init(&f); ThreeMMActorEvent e; ThreeMMActorState before;
    ThreeMMI32 value=1;
    append(&f,THREE_MM_ACTOR_ADD,0,NULL,1); append(&f,THREE_MM_ACTOR_FREEZE,4,&value,4);
    f.state.nextEvent=2; f.state.firstEventInFrame=1; f.state.actionEvent=1;
    memset(&e,0,sizeof(e)); e.aet=THREE_MM_ACTOR_FREEZE;
    CHECK(three_mm_actor_edit_insert(&f.state,0,4,&value,&e,1,&s));
    CHECK(f.state.nextEvent==3 && f.state.firstEventInFrame==2 && f.state.actionEvent==2 && f.state.addEvent==1);
    CHECK(f.duplicated==1); before=f.state;
    CHECK(three_mm_actor_edit_insert(&f.state,1,4,NULL,&e,1,&s));
    CHECK(!memcmp(&before,&f.state,sizeof(before)) && f.duplicated==1);
    f.fail_insert=1; CHECK(!three_mm_actor_edit_insert(&f.state,0,4,&value,&e,1,&s));
    CHECK(!memcmp(&before,&f.state,sizeof(before)) && f.count==4);
    three_mm_actor_sounds_dispose(&f.sounds);
}
static void apply_test(void)
{
    Fixture f; ThreeMMActorEditServices s=init(&f); ThreeMMActorPoint old={3*ONE,0,0},fresh={2*ONE,0,0};
    ThreeMMI32 value=1;
    append(&f,THREE_MM_ACTOR_ADD,0,NULL,1); append(&f,THREE_MM_ACTOR_MOVE,sizeof(old),&old,4);
    f.state.subrouteTranslation.dxr=7*ONE; f.state.routeTranslation.dxr=42*ONE;
    f.range.first=10; f.range.last=-10;
    CHECK(three_mm_actor_edit_add_apply(&f.state,&f.sounds,&f,23,THREE_MM_ACTOR_MOVE,sizeof(fresh),&fresh,&tables,&s));
    CHECK(f.count==2 && f.payload[1].point.dxr==5*ONE && f.state.subrouteTranslation.dxr==9*ONE);
    CHECK(f.state.routeTranslation.dxr==42*ONE && f.applied==0 && f.dirty==1);
    CHECK(f.invalidated==2 && f.seen[0].first==10 && f.seen[1].first==4 && f.range.first==4 && f.range.last==4);
    three_mm_actor_sounds_dispose(&f.sounds);
    s=init(&f); append(&f,THREE_MM_ACTOR_ADD,0,NULL,1); append(&f,THREE_MM_ACTOR_FREEZE,4,&value,4);
    f.fail_apply=1;
    CHECK(!three_mm_actor_edit_add_apply(&f.state,&f.sounds,&f,23,THREE_MM_ACTOR_FREEZE,4,&value,&tables,&s));
    CHECK(f.count==2 && f.applied==1 && f.state.cel==99 && f.view.cel==99 && !f.dirty && !f.invalidated);
    three_mm_actor_sounds_dispose(&f.sounds);
}
static void rotation_test(void)
{
    Fixture f; ThreeMMActorEditServices s=init(&f); ThreeMMActorMatrix matrix;
    memset(&matrix,0,sizeof(matrix));
    append(&f,THREE_MM_ACTOR_ADD,0,NULL,1);
    append(&f,THREE_MM_ACTOR_ROTATE_FORWARD,sizeof(matrix),&matrix,4);
    append(&f,THREE_MM_ACTOR_ROTATE_HERE,sizeof(matrix),&matrix,4);
    append(&f,THREE_MM_ACTOR_ROTATE_HERE,sizeof(matrix),&matrix,3);
    append(&f,THREE_MM_ACTOR_ROTATE_FORWARD,sizeof(matrix),&matrix,4);
    f.state.nextEvent=4;
    three_mm_actor_edit_merge(&f.state,&f.sounds,&f,23,1,4,&f.result,&tables,&s);
    CHECK(f.count==3 && f.result==2 && f.event[1].aet==THREE_MM_ACTOR_ROTATE_HERE && f.event[1].nfrm==3);
    three_mm_actor_sounds_dispose(&f.sounds);
}
int main(void) { ownership_test(); insert_test(); apply_test(); rotation_test(); puts("actor event editing passed"); return 0; }
