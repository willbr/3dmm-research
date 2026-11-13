#include "engine_subroute_edit_fixture.h"
#include "three_mm/actor_stage_edit.h"
typedef struct StageFixture
{
    SubFixture sub;
    int point_reserves, fail_points, placements, inserted, fail_event, reveal, fail_seek;
    ThreeMMU32 counts[8], sizes[8];
} StageFixture;
static int stage_reserve(void *p, ThreeMMU32 count, ThreeMMU32 bytes)
{
    StageFixture *f=(StageFixture *)p;
    int i=f->sub.base.reserves++;
    CHECK(i<8); f->counts[i]=count; f->sizes[i]=bytes;
    return i+1!=f->sub.base.fail_reserve;
}
static int reserve_points(void *p, ThreeMMU32 count)
{
    StageFixture *f=(StageFixture *)p;
    CHECK(count==1 && f->sub.base.reserves==1 && !f->placements);
    ++f->point_reserves; return !f->fail_points;
}
static void placement(void *p, ThreeMMActorPoint *point, ThreeMMActorPoint *origin, unsigned short *yaw)
{
    StageFixture *f=(StageFixture *)p;
    CHECK(f->point_reserves==1 && f->sub.base.reserves==1);
    ++f->placements;
    point->dxr=100*ONE; point->dyr=2*ONE; point->dzr=-30*ONE;
    origin->dxr=11*ONE; origin->dyr=12*ONE; origin->dzr=13*ONE; *yaw=0x1234;
}
static int stage_insert(void *p, ThreeMMI32 i, ThreeMMU32 bytes, const void *value, const ThreeMMActorEvent *e)
{
    StageFixture *f=(StageFixture *)p;
    if (++f->inserted==f->fail_event) return 0;
    return insert_event(p,i,bytes,value,e);
}
static void stage_default(void *p)
{
    StageFixture *f=(StageFixture *)p;
    CHECK(!f->sub.base.view.onStage && f->sub.base.recording);
    ++f->sub.base.defaults;
}
static int stage_seek(void *p, ThreeMMActorState *state, ThreeMMI32 frame)
{
    StageFixture *f=(StageFixture *)p;
    CHECK(!state->onStage && state->frame==2147483647L && !f->sub.base.recording && f->sub.base.defaults==1);
    ++f->sub.seeks; state->frame=frame; state->onStage=f->reveal;
    state->addEvent=0; state->nextEvent=1; state->firstEventInFrame=0;
    return !f->fail_seek;
}
static void stage_diagnostic(void *p, int fault) { (void)p; (void)fault; CHECK(0); }
static int stage_apply(void *p, ThreeMMActorState *state, ThreeMMI32 i)
{
    StageFixture *f=(StageFixture *)p;
    ThreeMMActorEffectServices effect;
    ThreeMMActorPoint origin={11*ONE,12*ONE,13*ONE};
    if (f->sub.base.event[i].aet!=THREE_MM_ACTOR_ADD && f->sub.base.event[i].aet!=THREE_MM_ACTOR_REMOVE)
        return sub_apply(p,state,i);
    if (++f->sub.base.applied==f->sub.base.fail_apply) return 0;
    memset(&effect,0,sizeof(effect)); effect.context=p; effect.write_event=write_event; effect.publish=publish;
    return three_mm_actor_effect_apply(state,p,p,i,&origin,0,1,&tables,&effect);
}
static ThreeMMActorStageEditServices stage_init(StageFixture *f)
{
    ThreeMMActorStageEditServices s;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); s.subroute=sub_init(&f->sub);
    s.subroute.route.edits.insert_event=stage_insert;
    s.subroute.route.edits.apply_event=stage_apply;
    s.subroute.action.sounds.edits=s.subroute.route.edits;
    s.subroute.route.reserve_events=s.subroute.action.sounds.reserve_events=stage_reserve;
    s.subroute.route.default_costume=stage_default; s.subroute.seek=stage_seek;
    s.reserve_points=reserve_points; s.placement=placement; s.diagnostic=stage_diagnostic;
    return s;
}
static void empty(StageFixture *f)
{
    f->sub.base.nodes=f->sub.base.events=0;
    three_mm_actor_state_init(&f->sub.base.state);
    f->sub.base.state.frame=5; f->sub.base.state.celCount=4;
    f->sub.base.range.first=10; f->sub.base.range.last=0;
    f->sub.base.view=f->sub.base.state;
}
static int enter(StageFixture *f, const ThreeMMActorStageEditServices *s)
{ return three_mm_actor_stage_add(&f->sub.base.state,&f->sub.base.sounds,f,f,7,0,&tables,s); }
static int leave(StageFixture *f, const ThreeMMActorStageEditServices *s)
{ return three_mm_actor_stage_remove(&f->sub.base.state,&f->sub.base.sounds,f,f,7,&tables,s); }
static void reservation_test(void)
{
    StageFixture f; ThreeMMActorStageEditServices s;
    ThreeMMActorState before;
    int nonempty, failure;
    for (nonempty=0;nonempty<2;++nonempty) for (failure=0;failure<2;++failure)
    {
        s=stage_init(&f); if (!nonempty) empty(&f);
        before=f.sub.base.state;
        if (failure) f.fail_points=1; else f.sub.base.fail_reserve=1;
        CHECK(!enter(&f,&s));
        CHECK(f.counts[0]==(nonempty?8U:4U));
        CHECK(f.sizes[0]==(nonempty?36+sizeof(ThreeMMActorCostume)+48+4+12:36));
        CHECK(f.point_reserves==failure && !f.placements && !f.sub.base.dirty && !f.sub.base.invalidated);
        CHECK(!memcmp(&before,&f.sub.base.state,sizeof(before)));
        done(&f.sub);
    }
    s=stage_init(&f); f.sub.base.state.onStage=0; f.sub.base.fail_reserve=1;
    CHECK(leave(&f,&s) && !f.sub.base.reserves); done(&f.sub);
    s=stage_init(&f); f.sub.base.fail_reserve=1;
    CHECK(!leave(&f,&s) && !f.sub.base.applied && !f.sub.base.hidden); done(&f.sub);
}
static void entrance_test(void)
{
    StageFixture f; ThreeMMActorStageEditServices s;
    ThreeMMActorEntrance entrance;
    int failure;
    s=stage_init(&f); empty(&f);
    CHECK(enter(&f,&s));
    CHECK(f.sub.base.state.onStage && f.sub.base.state.frozen && !f.sub.base.state.step);
    CHECK(f.sub.base.nodes==1 && f.sub.base.state.addEvent==0 && f.sub.locates==1);
    CHECK(f.sub.base.invalidated==1 && f.sub.base.range.first==5 && f.sub.base.range.last==5);
    memcpy(&entrance,f.sub.base.payload[0],sizeof(entrance));
    CHECK(entrance.translation.dxr==89*ONE && entrance.translation.dyr==-10*ONE && entrance.translation.dzr==-43*ONE);
    CHECK(entrance.x==0 && entrance.y==0x1234 && entrance.z==0);
    CHECK(f.sub.base.events==4 && f.sub.base.state.nextEvent==4);
    done(&f.sub);
    for (failure=1;failure<=5;++failure)
    {
        s=stage_init(&f); empty(&f); f.sub.base.fail_apply=failure;
        CHECK(!enter(&f,&s));
        CHECK(f.sub.base.nodes==1 && f.sub.base.events>0 && f.sub.base.range.first==5 && f.sub.base.range.last==5);
        CHECK(!f.sub.base.invalidated && f.sub.base.applied==failure);
        done(&f.sub);
    }
    s=stage_init(&f); empty(&f); f.fail_event=1;
    CHECK(!enter(&f,&s) && f.sub.base.nodes==1 && f.sub.base.events==0 && !f.sub.base.invalidated); done(&f.sub);
    for (failure=2;failure<=3;++failure)
    {
        s=stage_init(&f); empty(&f); f.sub.base.fail_reserve=failure;
        CHECK(!enter(&f,&s) && f.sub.base.nodes==1 && f.sub.base.events>0 && !f.sub.base.invalidated); done(&f.sub);
    }
}
static void ownership_test(void)
{
    StageFixture f; ThreeMMActorStageEditServices s;
    int earlier;
    for (earlier=0;earlier<2;++earlier)
    {
        s=stage_init(&f); f.sub.base.events=0; f.sub.base.nodes=1; f.sub.base.point[0].dwr=0;
        add(&f.sub,0,earlier?1:10); owned(&f.sub,0,earlier?1:10);
        f.sub.base.state.frame=5; f.sub.base.state.onStage=0; f.sub.base.state.addEvent=earlier?0:-1;
        f.sub.base.state.nextEvent=earlier?2:0; f.sub.base.state.firstEventInFrame=0;
        f.sub.base.state.location.irpt=0; f.sub.base.state.location.dwrOffset=0; f.sub.base.state.action=0;
        CHECK(enter(&f,&s) && f.sub.refs==2); done(&f.sub);
    }
}
static void removal_test(void)
{
    StageFixture f; ThreeMMActorStageEditServices s;
    int failure;
    s=stage_init(&f); f.sub.base.events=0; f.sub.base.nodes=1; f.sub.base.point[0].dwr=0;
    add(&f.sub,0,5); f.sub.base.state.frame=5; f.sub.base.state.nextEvent=1; f.sub.base.state.location.dwrOffset=0;
    CHECK(leave(&f,&s));
    CHECK(!f.sub.base.nodes && !f.sub.base.events && f.sub.base.state.frame==2147483647L && f.sub.base.range.first==2147483647L);
    CHECK(f.sub.base.range.last==8 && !f.sub.base.recording && !f.sub.dirty_state && f.sub.base.hidden==1 && f.sub.base.defaults==1 && !f.sub.seeks);
    done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=stage_init(&f); f.sub.base.events=0; f.sub.base.nodes=2; f.sub.base.point[0].dwr=0;
        add(&f.sub,0,1); add(&f.sub,1,10);
        f.sub.base.state.frame=10; f.sub.base.state.addEvent=1; f.sub.base.state.nextEvent=2;
        f.sub.base.state.location.irpt=1; f.sub.base.state.location.dwrOffset=0;
        f.fail_seek=failure; f.reveal=1;
        CHECK(leave(&f,&s)==!failure);
        CHECK(f.sub.seeks==1 && f.sub.base.nodes==1 && f.sub.base.range.first==1 && !f.sub.base.recording);
        CHECK(f.sub.base.events==(failure?1:2) && f.sub.base.state.frame==10);
        CHECK(failure?f.sub.base.state.onStage:!f.sub.base.state.onStage);
        if (!failure) CHECK(f.sub.base.event[1].aet==THREE_MM_ACTOR_REMOVE);
        done(&f.sub);
    }
    s=stage_init(&f); f.sub.base.state.frame=3;
    CHECK(leave(&f,&s) && !f.sub.base.state.onStage && f.sub.base.events==5 && !f.sub.base.defaults && !f.sub.seeks);
    done(&f.sub);
}
int main(void)
{
    reservation_test(); entrance_test(); ownership_test(); removal_test();
    puts("actor stage editing checks passed"); return 0;
}
