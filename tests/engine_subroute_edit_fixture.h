#ifndef ENGINE_SUBROUTE_EDIT_FIXTURE_H
#define ENGINE_SUBROUTE_EDIT_FIXTURE_H
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
#endif
