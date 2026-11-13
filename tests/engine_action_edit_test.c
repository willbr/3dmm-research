#include "three_mm/actor_action_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FLAG(x) ((ThreeMMU32)1 << (x))
static void check(int ok,int line) { if (!ok) { fprintf(stderr,"action edit failed at %d\n",line); exit(1); } }
#define CHECK(x) check((x),__LINE__)
typedef struct Fixture
{
    ThreeMMActorEvent event[24]; ThreeMMI32 data[24][12]; ThreeMMU32 bytes[24]; int count;
    ThreeMMActorState state; ThreeMMActorSounds sounds; ThreeMMActorEditRange range;
    int locked,locks,unlocks,removed,applied,dirty,life,invalidated,reserves,metadata,defaults,text_queries;
    int fail_reserve,fail_metadata,fail_apply,fail_defaults,text; ThreeMMU32 flags;
} Fixture;
static ThreeMMI32 count(void *p) { return ((Fixture *)p)->count; }
static void event(void *p,ThreeMMI32 i,ThreeMMActorEvent *out) { Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); *out=f->event[i]; }
static void payload(void *p,ThreeMMI32 i,void *out) { Fixture *f=(Fixture *)p; memcpy(out,f->data[i],f->bytes[i]); }
static const ThreeMMActorMotionServices tables={0,0,0,0,count,event,0,payload};
static int insert(void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *value,const ThreeMMActorEvent *e)
{
    Fixture *f=(Fixture *)p; CHECK(f->count<24 && bytes<=48 && i>=0 && i<=f->count);
    memmove(f->event+i+1,f->event+i,(size_t)(f->count-i)*sizeof(f->event[0]));
    memmove(f->data+i+1,f->data+i,(size_t)(f->count-i)*sizeof(f->data[0]));
    memmove(f->bytes+i+1,f->bytes+i,(size_t)(f->count-i)*sizeof(f->bytes[0]));
    f->event[i]=*e; f->bytes[i]=bytes; if (value) memcpy(f->data[i],value,bytes); ++f->count; return 1;
}
static void remove_event(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; ++f->removed;
    memmove(f->event+i,f->event+i+1,(size_t)(f->count-i-1)*sizeof(f->event[0]));
    memmove(f->data+i,f->data+i+1,(size_t)(f->count-i-1)*sizeof(f->data[0]));
    memmove(f->bytes+i,f->bytes+i+1,(size_t)(f->count-i-1)*sizeof(f->bytes[0])); --f->count;
}
static void tag(void *p,ThreeMMI32 i) { (void)p; (void)i; }
static int scene(void *p,ThreeMMI32 *frame) { (void)p; (void)frame; return 0; }
static void copy(void *p,ThreeMMI32 dst,ThreeMMI32 src) { Fixture *f=(Fixture *)p; memcpy(f->data[dst],f->data[src],f->bytes[src]); }
static void write(void *p,ThreeMMI32 i,const void *value) { Fixture *f=(Fixture *)p; memcpy(f->data[i],value,f->bytes[i]); }
static int apply(void *p,ThreeMMActorState *s,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; ++f->applied;
    switch (f->event[i].aet)
    {
    case THREE_MM_ACTOR_ACTION: s->action=f->data[i][0]; s->cel=f->data[i][1]; s->actionEvent=i; break;
    case THREE_MM_ACTOR_FREEZE: s->frozen=f->data[i][0]; break;
    case THREE_MM_ACTOR_STEP: s->step=f->data[i][0]; break;
    default: CHECK(0); break;
    }
    return f->applied!=f->fail_apply;
}
static int reserve(void *p,ThreeMMU32 count,ThreeMMU32 bytes)
{ Fixture *f=(Fixture *)p; ++f->reserves; CHECK((count==4 && bytes==20) || (count==1 && bytes==4)); return f->reserves!=f->fail_reserve; }
static int metadata(void *p,ThreeMMI32 action,ThreeMMU32 *flags)
{ Fixture *f=(Fixture *)p; CHECK(action==2); ++f->metadata; *flags=f->flags; return !f->fail_metadata; }
static int defaults(void *p,ThreeMMI32 action,ThreeMMI32 *count)
{ Fixture *f=(Fixture *)p; CHECK(action==2); ++f->defaults; *count=0; return !f->fail_defaults; }
static int text(void *p) { Fixture *f=(Fixture *)p; ++f->text_queries; return f->text; }
static void dirty(void *p) { ++((Fixture *)p)->dirty; }
static void life(void *p) { ++((Fixture *)p)->life; }
static void invalidate(void *p) { ++((Fixture *)p)->invalidated; }
static void range(void *p,ThreeMMActorEditRange *out) { *out=((Fixture *)p)->range; }
static void lock(void *p) { Fixture *f=(Fixture *)p; CHECK(!f->locked); f->locked=1; ++f->locks; }
static void unlock(void *p) { Fixture *f=(Fixture *)p; CHECK(f->locked); f->locked=0; ++f->unlocks; }
static const void *view(void *p,ThreeMMI32 i) { Fixture *f=(Fixture *)p; CHECK(f->locked); return f->data[i]; }
static ThreeMMActorActionEditServices init(Fixture *f)
{
    ThreeMMActorActionEditServices s; ThreeMMActorEditServices *e;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); three_mm_actor_state_init(&f->state);
    CHECK(three_mm_actor_sounds_init(&f->sounds,NULL)); f->state.action=1; f->state.actionEvent=1;
    f->state.onStage=1; f->state.frame=3; f->state.location.dwrOffset=2*65536L;
    f->state.nextEvent=f->state.firstEventInFrame=2; f->state.addEvent=0; f->range.first=1; f->range.last=20;
    s.context=f; s.action_flags=metadata; s.text_template=text; s.mark_life_dirty=life;
    s.sounds.context=f; s.sounds.reserve_events=reserve; s.sounds.runtime.context=f; s.sounds.runtime.cel_count=defaults;
    e=&s.sounds.edits; e->context=f; e->close_event_tag=tag; e->delete_event=remove_event; e->scene_frame=scene;
    e->insert_event=insert; e->duplicate_event_tag=tag; e->write_payload=write; e->copy_payload=copy;
    e->apply_event=apply; e->dirty=dirty; e->get_range=range; e->invalidate_range=invalidate;
    return s;
}
static void append(Fixture *f,int kind,ThreeMMI32 value,int offset)
{
    ThreeMMActorEvent e; ThreeMMI32 data[2]; memset(&e,0,sizeof(e)); e.aet=kind; e.nfrm=offset+1;
    e.rtel.dwrOffset=offset*65536L; data[0]=value; data[1]=0;
    CHECK(insert(f,f->count,kind==THREE_MM_ACTOR_ACTION?8:4,data,&e));
}
static void action_test(void)
{
    int mode,ok,want_step; Fixture f; ThreeMMActorActionEditServices s;
    for (mode=0; mode<64; ++mode)
    {
        s=init(&f); f.flags=(mode&1)?8:0; f.text=(mode&2)!=0;
        if (mode&4) f.state.actionEvent=-1;
        append(&f,THREE_MM_ACTOR_ADD,0,0); append(&f,THREE_MM_ACTOR_ACTION,1,0);
        append(&f,THREE_MM_ACTOR_STEP,0,(mode&8)?2:10);
        if (mode&16) f.state.action=2;
        ok=three_mm_actor_edit_set_action(&f.state,&f.sounds,&f,7,2,3,(mode&32)!=0,0,&tables,&s);
        CHECK(ok && f.state.action==2 && f.state.cel==3 && f.state.frozen==((mode&40)!=0));
        want_step=!(mode&7) && !(mode&8);
        CHECK(f.applied==2+want_step && f.reserves==1+want_step && f.life==1 && f.invalidated==1);
        CHECK(f.defaults==((mode&16)?0:1) && f.metadata==1 && f.dirty==2+want_step);
        if (want_step) CHECK(f.state.step==-65536L);
        three_mm_actor_sounds_dispose(&f.sounds);
    }
    for (mode=0; mode<7; ++mode)
    {
        s=init(&f); append(&f,THREE_MM_ACTOR_ADD,0,0); append(&f,THREE_MM_ACTOR_ACTION,1,0); append(&f,THREE_MM_ACTOR_STEP,0,10);
        if (mode==0) f.fail_reserve=1;
        if (mode==1) f.fail_metadata=1;
        if (mode==2) f.fail_apply=1;
        if (mode==3) f.fail_defaults=1;
        if (mode==4) f.fail_apply=2;
        if (mode==5) f.fail_reserve=2;
        if (mode==6) f.fail_apply=3;
        CHECK(!three_mm_actor_edit_set_action(&f.state,&f.sounds,&f,7,2,3,0,0,&tables,&s));
        CHECK(f.life==(mode>=4) && f.dirty==(mode<3?0:mode<5?1:2));
        if (mode>=2) CHECK(f.state.action==2 && f.state.cel==3);
        if (mode>=4) CHECK(f.state.frozen==0);
        three_mm_actor_sounds_dispose(&f.sounds);
    }
}
static void fill_test(void)
{
    Fixture f; ThreeMMActorActionEditServices s=init(&f); ThreeMMActorFillServices fill;
    ThreeMMI32 fresh=2;
    append(&f,THREE_MM_ACTOR_ADD,0,0); append(&f,THREE_MM_ACTOR_SIZE,1,0);
    append(&f,THREE_MM_ACTOR_SIZE,1,3); append(&f,THREE_MM_ACTOR_SIZE,2,4);
    append(&f,THREE_MM_ACTOR_SIZE,3,5); append(&f,THREE_MM_ACTOR_TWEAK,3,6);
    memset(&fill,0,sizeof(fill)); fill.edits=s.sounds.edits; fill.lock_events=lock; fill.unlock_events=unlock; fill.payload_view=view;
    three_mm_actor_edit_transform_fill(&f.state,&f,THREE_MM_ACTOR_SIZE,&fresh,4,2,1,~(ThreeMMU32)0,&tables,&fill);
    CHECK(f.count==4 && f.removed==2 && f.data[2][0]==3 && f.event[3].aet==THREE_MM_ACTOR_TWEAK && f.locks==1 && f.unlocks==1);
    three_mm_actor_sounds_dispose(&f.sounds);
    s=init(&f); append(&f,THREE_MM_ACTOR_ADD,0,0); append(&f,THREE_MM_ACTOR_ACTION,1,0);
    append(&f,THREE_MM_ACTOR_ACTION,1,3); append(&f,THREE_MM_ACTOR_ACTION,2,4);
    append(&f,THREE_MM_ACTOR_FREEZE,1,10); append(&f,THREE_MM_ACTOR_ADD,0,10);
    append(&f,THREE_MM_ACTOR_TWEAK,3,11);
    three_mm_actor_edit_action_fill(&f.state,&f,2,1,2,FLAG(THREE_MM_ACTOR_ACTION)|FLAG(THREE_MM_ACTOR_FREEZE)|FLAG(THREE_MM_ACTOR_TWEAK),&tables,&s.sounds.edits);
    CHECK(f.count==5 && f.event[2].aet==THREE_MM_ACTOR_FREEZE && f.event[4].aet==THREE_MM_ACTOR_TWEAK);
    three_mm_actor_sounds_dispose(&f.sounds);
}
int main(void) { action_test(); fill_test(); puts("actor action and fill editing passed"); return 0; }
