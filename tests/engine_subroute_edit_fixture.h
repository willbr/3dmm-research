#ifndef ENGINE_SUBROUTE_EDIT_FIXTURE_H
#define ENGINE_SUBROUTE_EDIT_FIXTURE_H
#include "engine_route_edit_fixture.h"
#include "mm/actor_subroute_edit.h"
#include "mm/actor_effect.h"
typedef struct SubFixture
{
    Fixture base;
    int dirty_state,rejoining,text,fail_resolve,resolves,fail_seek,seeks,seek_frames[2],locates,diagnostic_count,refs;
    MMI32 last_at_resolve;
} SubFixture;
static void sub_life(void *p) { SubFixture *f=(SubFixture *)p; ++f->base.lives; f->dirty_state=1; }
static int text(void *p) { return ((SubFixture *)p)->text; }
static int resolve(void *p,MMI32 action,MMI32 cel,MMI32 *step)
{
    SubFixture *f=(SubFixture *)p;
    (void)action; (void)cel;
    ++f->resolves; f->last_at_resolve=f->base.range.last;
    *step=ONE; return !f->fail_resolve;
}
static void inputs(void *p,MMActorPoseInputs *out) { (void)p; memset(out,0,sizeof(*out)); }
static void locate(void *p,const MMActorBodyPose *pose)
{ SubFixture *f=(SubFixture *)p; (void)pose; ++f->locates; }
static void sub_diagnostic(void *p,int fault)
{ SubFixture *f=(SubFixture *)p; CHECK(fault==MM_SUBROUTE_BAD_LIFETIME || fault==MM_SUBROUTE_BAD_HOLD); ++f->diagnostic_count; }
static int seek(void *p,MMActorState *state,MMI32 frame)
{ SubFixture *f=(SubFixture *)p; CHECK(f->seeks<2); f->seek_frames[f->seeks++]=frame; state->frame=frame; f->base.view=*state; return f->seeks!=f->fail_seek; }
static int sub_reserve(void *p,MMU32 count,MMU32 bytes)
{ SubFixture *f=(SubFixture *)p; CHECK(count>0 && bytes>0); return ++f->base.reserves!=f->base.fail_reserve; }
static int action_flags(void *p,MMI32 action,MMU32 *out) { (void)p; CHECK(action==0); *out=8; return 1; }
static int sub_apply(void *p,MMActorState *state,MMI32 i)
{
    SubFixture *f=(SubFixture *)p; MMActorPoint origin={0,0,0};
    f->base.event[i].nfrm=state->frame;
    if (++f->base.applied==f->base.fail_apply) return 0;
    if (f->base.event[i].aet==MM_ACTOR_COSTUME) return 1;
    if (f->base.event[i].aet==MM_ACTOR_ACTION)
    { state->action=f->base.payload[i][0]; state->cel=f->base.payload[i][1]; state->actionEvent=i; return 1; }
    return mm_actor_state_apply(state,p,p,i,&origin,&tables)==MM_ACTOR_APPLIED;
}
static void retain_tag(void *p,MMI32 i, const MMTagServices *tags)
{
    SubFixture *f=(SubFixture *)p; MMActorCostume c;
    (void)tags;
    if (f->base.event[i].aet!=MM_ACTOR_COSTUME) return;
    memcpy(&c,f->base.payload[i],sizeof(c)); if (!c.custom) ++f->refs;
}
static void close_owned_tag(void *p,MMI32 i, const MMTagServices *tags)
{
    SubFixture *f=(SubFixture *)p; MMActorCostume c;
    (void)tags;
    if (f->base.event[i].aet!=MM_ACTOR_COSTUME) return;
    memcpy(&c,f->base.payload[i],sizeof(c)); if (!c.custom) { CHECK(f->refs>0); --f->refs; }
}
static MMActorSubrouteEditServices sub_init(SubFixture *f)
{
    MMActorSubrouteEditServices s; int i;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); s.route=init(&f->base);
    for (i=0;i<f->base.events;++i) if (f->base.event[i].aet==MM_ACTOR_ADD) f->base.bytes[i]=20;
    s.route.life_dirty=sub_life; s.route.edits.tables.reserve_events=sub_reserve;
    s.route.edits.tables.duplicate_event_tag=retain_tag; s.route.edits.tables.close_event_tag=close_owned_tag; s.route.edits.apply_event=sub_apply;
    s.action.context=f; s.action.sounds.context=f; s.action.sounds.edits=s.route.edits;
    s.action.action_flags=action_flags;
    s.action.text_template=text; s.action.mark_life_dirty=sub_life;
    s.timeline.motion=tables; s.timeline.context=f; s.timeline.resolve=resolve;
    s.timeline.put_event=write_event;
    s.lifetime.first=&f->base.range.first; s.lifetime.last=&f->base.range.last; s.lifetime.dirty=&f->dirty_state;
    s.lifetime.recording=&f->base.recording; s.lifetime.rejoining=&f->rejoining; s.lifetime.publish_attempts=1;
    s.pose_inputs=inputs; s.locate_body=locate; s.seek=seek; s.diagnostic=sub_diagnostic;
    return s;
}
static void add(SubFixture *f,int node,int frame)
{ append(&f->base,MM_ACTOR_ADD,node,0,frame); f->base.bytes[f->base.events-1]=20; }
static void owned(SubFixture *f,int node,int frame)
{
    MMActorCostume c; int i=f->base.events;
    append(&f->base,MM_ACTOR_COSTUME,node,0,frame); memset(&c,0,sizeof(c)); c.tag.cache=&f->refs;
    f->base.bytes[i]=sizeof(c); memcpy(f->base.payload[i],&c,sizeof(c)); ++f->refs;
}
static void done(SubFixture *f)
{ int i; for (i=0;i<f->base.events;++i) close_owned_tag(f,i,NULL); CHECK(!f->refs); mm_actor_sounds_dispose(&f->base.sounds); }
#endif
