#include "engine_route_edit_fixture.h"
#include "three_mm/actor_subroute_edit.h"
#include "three_mm/actor_effect.h"
typedef struct SubFixture
{
    Fixture base;
    int dirty_state,rejoining,text,fail_resolve,resolves,fail_seek,seeks,seek_frames[2],locates,diagnostic_count,refs;
} SubFixture;
static void flags(void *p,ThreeMMActorSubrouteFlags *out)
{ SubFixture *f=(SubFixture *)p; out->dirty=f->dirty_state; out->recording=f->base.recording; out->rejoining=f->rejoining; }
static void set_dirty(void *p,int dirty) { ((SubFixture *)p)->dirty_state=dirty; }
static void sub_life(void *p) { SubFixture *f=(SubFixture *)p; ++f->base.lives; f->dirty_state=1; }
static int text(void *p) { return ((SubFixture *)p)->text; }
static int resolve(void *p,ThreeMMI32 action,ThreeMMI32 cel,ThreeMMI32 *step)
{ SubFixture *f=(SubFixture *)p; (void)action; (void)cel; ++f->resolves; *step=ONE; return !f->fail_resolve; }
static void last_frame(void *p,ThreeMMI32 frame) { ((SubFixture *)p)->base.range.last=frame; }
static void inputs(void *p,ThreeMMActorPoseInputs *out) { (void)p; memset(out,0,sizeof(*out)); }
static void locate(void *p,const ThreeMMActorPoint *point,const ThreeMMActorMatrix *matrix)
{ SubFixture *f=(SubFixture *)p; (void)point; (void)matrix; ++f->locates; }
static void sub_diagnostic(void *p,int fault)
{ SubFixture *f=(SubFixture *)p; CHECK(fault==THREE_MM_SUBROUTE_BAD_LIFETIME || fault==THREE_MM_SUBROUTE_BAD_HOLD); ++f->diagnostic_count; }
static int seek(void *p,ThreeMMActorState *state,ThreeMMI32 frame)
{ SubFixture *f=(SubFixture *)p; CHECK(f->seeks<2); f->seek_frames[f->seeks++]=frame; state->frame=frame; f->base.view=*state; return f->seeks!=f->fail_seek; }
static int sub_reserve(void *p,ThreeMMU32 count,ThreeMMU32 bytes)
{ SubFixture *f=(SubFixture *)p; CHECK(count>0 && bytes>0); return ++f->base.reserves!=f->base.fail_reserve; }
static int action_flags(void *p,ThreeMMI32 action,ThreeMMU32 *out) { (void)p; CHECK(action==0); *out=8; return 1; }
static int sub_apply(void *p,ThreeMMActorState *state,ThreeMMI32 i)
{
    SubFixture *f=(SubFixture *)p; ThreeMMActorPoint origin={0,0,0};
    f->base.event[i].nfrm=state->frame;
    if (++f->base.applied==f->base.fail_apply) return 0;
    if (f->base.event[i].aet==THREE_MM_ACTOR_COSTUME) return 1;
    if (f->base.event[i].aet==THREE_MM_ACTOR_ACTION)
    { state->action=f->base.payload[i][0]; state->cel=f->base.payload[i][1]; state->actionEvent=i; return 1; }
    return three_mm_actor_state_apply(state,p,p,i,&origin,&tables)==THREE_MM_ACTOR_APPLIED;
}
static void retain_tag(void *p,ThreeMMI32 i)
{
    SubFixture *f=(SubFixture *)p; ThreeMMActorCostume c;
    if (f->base.event[i].aet!=THREE_MM_ACTOR_COSTUME) return;
    memcpy(&c,f->base.payload[i],sizeof(c)); if (!c.custom) ++f->refs;
}
static void close_owned_tag(void *p,ThreeMMI32 i)
{
    SubFixture *f=(SubFixture *)p; ThreeMMActorCostume c;
    if (f->base.event[i].aet!=THREE_MM_ACTOR_COSTUME) return;
    memcpy(&c,f->base.payload[i],sizeof(c)); if (!c.custom) { CHECK(f->refs>0); --f->refs; }
}
static ThreeMMActorSubrouteEditServices sub_init(SubFixture *f)
{
    ThreeMMActorSubrouteEditServices s; int i;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); s.route=init(&f->base);
    for (i=0;i<f->base.events;++i) if (f->base.event[i].aet==THREE_MM_ACTOR_ADD) f->base.bytes[i]=20;
    s.route.life_dirty=sub_life; s.route.reserve_events=sub_reserve;
    s.route.edits.duplicate_event_tag=retain_tag; s.route.edits.close_event_tag=close_owned_tag; s.route.edits.apply_event=sub_apply;
    s.action.context=f; s.action.sounds.context=f; s.action.sounds.edits=s.route.edits;
    s.action.sounds.reserve_events=sub_reserve; s.action.action_flags=action_flags;
    s.action.text_template=text; s.action.mark_life_dirty=sub_life;
    s.timeline.motion=tables; s.timeline.context=f; s.timeline.resolve=resolve;
    s.timeline.put_event=write_event; s.timeline.publish_frame=last_frame;
    s.get_flags=flags; s.set_dirty=set_dirty; s.pose_inputs=inputs; s.locate_body=locate; s.seek=seek; s.diagnostic=sub_diagnostic;
    return s;
}
static void add(SubFixture *f,int node,int frame)
{ append(&f->base,THREE_MM_ACTOR_ADD,node,0,frame); f->base.bytes[f->base.events-1]=20; }
static void owned(SubFixture *f,int node,int frame)
{
    ThreeMMActorCostume c; int i=f->base.events;
    append(&f->base,THREE_MM_ACTOR_COSTUME,node,0,frame); memset(&c,0,sizeof(c)); c.tag.cache=&f->refs;
    f->base.bytes[i]=sizeof(c); memcpy(f->base.payload[i],&c,sizeof(c)); ++f->refs;
}
static void done(SubFixture *f)
{ int i; for (i=0;i<f->base.events;++i) close_owned_tag(f,i); CHECK(!f->refs); three_mm_actor_sounds_dispose(&f->base.sounds); }
static void angle_test(void)
{
    SubFixture f; ThreeMMActorSubrouteEditServices s=sub_init(&f); ThreeMMActorEntrance value={{123,456,789},0,0,0,0xBEEF};
    ThreeMMI32 distance=0; memcpy(f.base.payload[0],&value,sizeof(value));
    CHECK(three_mm_actor_edit_average_angle(0xFFF0,0x0010,32768)==0);
    three_mm_actor_edit_add_orientation(&f.base.state,&f,100,200,300,1,&distance,&tables,&s.route.edits);
    CHECK(f.base.state.transform.xaPath==9 && !f.base.state.transform.yaPath);
    memcpy(f.base.payload[0],&value,sizeof(value)); distance=3*ONE;
    three_mm_actor_edit_add_orientation(&f.base.state,&f,100,200,300,1,&distance,&tables,&s.route.edits);
    CHECK(f.base.state.transform.xaPath==99);
    three_mm_actor_edit_add_orientation(&f.base.state,&f,10,20,30,0,NULL,&tables,&s.route.edits);
    payload(&f,0,&value); CHECK(value.x==10 && value.y==20 && value.z==30 && value.reserved==0xBEEF && value.translation.dxr==123);
    done(&f);
}
static void lifetime_back_test(void)
{
    SubFixture f; ThreeMMActorSubrouteEditServices s=sub_init(&f); ThreeMMI32 last=-99; int alive;
    f.base.recording=0; f.base.events=0; add(&f,0,1); append(&f.base,THREE_MM_ACTOR_STEP,0,0,1);
    f.base.payload[1][0]=-ONE; add(&f,2,8); f.fail_resolve=1;
    CHECK(!three_mm_actor_subroute_lifetime(&f,&f,&last,&s));
    CHECK(last==-99 && f.base.range.last==2 && f.dirty_state && f.resolves==1);
    f.base.recording=1; f.base.range.last=55;
    CHECK(three_mm_actor_subroute_lifetime(&f,&f,&last,&s) && last==55 && f.resolves==1 && f.dirty_state); done(&f);
    s=sub_init(&f); f.base.event[2].rtel.irpt=0; f.base.event[2].rtel.dwrOffset=2*ONE; f.base.event[2].rtel.dnfrm=2;
    f.base.state.location.dnfrm=1;
    three_mm_actor_subroute_delete_back(&f.base.state,&f.base.sounds,&f,&f,7,&alive,&tables,&s);
    CHECK(alive && f.base.event[2].rtel.dwrOffset==-2*ONE && f.base.event[2].rtel.dnfrm==1 && f.base.invalidated==1);
    CHECK(f.base.range.first==2 && f.dirty_state && f.base.recording); done(&f);
    s=sub_init(&f); f.base.recording=0; f.dirty_state=1; f.fail_resolve=1; f.base.events=0;
    add(&f,0,1); append(&f.base,THREE_MM_ACTOR_STEP,0,0,1); f.base.payload[1][0]=-ONE; add(&f,2,8);
    three_mm_actor_subroute_delete_back(&f.base.state,&f.base.sounds,&f,&f,7,&alive,&tables,&s);
    CHECK(alive && f.diagnostic_count==2 && f.base.range.first==2 && f.base.range.last==3);
    CHECK(f.dirty_state && f.base.invalidated==1 && f.base.state.firstEventInFrame==0); done(&f);
    s=sub_init(&f); f.base.state.onStage=0; f.base.state.location.irpt=1; f.base.state.location.dwrOffset=0;
    f.base.events=2; alive=1; f.base.alive_observer=&alive;
    three_mm_actor_subroute_delete_back(&f.base.state,&f.base.sounds,&f,&f,7,&alive,&tables,&s);
    CHECK(!alive && f.base.reset_seen==1 && !f.base.recording && !f.dirty_state && f.base.invalidated==1); done(&f);
}
static void propagation_test(void)
{
    SubFixture f; ThreeMMActorSubrouteEditServices s; int direction,failure,ok;
    for (direction=0;direction<2;++direction) for (failure=0;failure<2;++failure)
    {
        s=sub_init(&f); f.base.events=0; f.base.point[0].dwr=0;
        add(&f,0,1);
        if (direction) { owned(&f,0,1); add(&f,2,4); f.base.state.nextEvent=3; f.base.state.addEvent=2; f.base.state.firstEventInFrame=2; f.base.state.location.irpt=2; }
        else { add(&f,2,8); owned(&f,2,8); f.base.state.nextEvent=1; f.base.state.firstEventInFrame=0; f.base.state.addEvent=0; f.base.state.location.irpt=0; }
        f.base.state.location.dwrOffset=0; f.base.state.frame=direction?4:1; f.base.fail_apply=failure?1:0;
        if (direction) ok=three_mm_actor_subroute_from_previous(&f.base.state,&f.base.sounds,&f,&f,7,2,(ThreeMMU32)1<<THREE_MM_ACTOR_COSTUME,&tables,&s);
        else ok=three_mm_actor_subroute_from_later(&f.base.state,&f.base.sounds,&f,&f,7,0,&tables,&s);
        CHECK(ok==!failure && f.refs==2); done(&f);
    }
    for (failure=1;failure<=2;++failure)
    {
        s=sub_init(&f); f.base.events=0; f.base.point[0].dwr=0;
        add(&f,0,1); owned(&f,0,4); add(&f,2,4);
        f.base.state.frame=4; f.base.state.nextEvent=3; f.base.state.addEvent=2; f.base.state.firstEventInFrame=2;
        f.base.state.location.irpt=2; f.base.state.location.dwrOffset=0; f.fail_seek=failure;
        CHECK(!three_mm_actor_subroute_from_previous(&f.base.state,&f.base.sounds,&f,&f,7,2,(ThreeMMU32)1<<THREE_MM_ACTOR_COSTUME,&tables,&s));
        CHECK(f.seeks==failure && f.seek_frames[0]==3 && f.refs==1);
        if (failure==2) CHECK(f.seek_frames[1]==4);
        done(&f);
    }
    s=sub_init(&f); f.base.events=0; add(&f,0,1); owned(&f,0,1); add(&f,2,1);
    f.base.state.frame=1; f.base.state.nextEvent=3; f.base.state.addEvent=2; f.base.state.firstEventInFrame=2;
    CHECK(three_mm_actor_subroute_from_previous(&f.base.state,&f.base.sounds,&f,&f,7,2,0,&tables,&s));
    CHECK(f.base.events==3 && f.base.nodes==3 && f.refs==1 && !f.base.applied); done(&f);
}
int main(void) { angle_test(); lifetime_back_test(); propagation_test(); puts("actor subroute editing passed"); return 0; }
