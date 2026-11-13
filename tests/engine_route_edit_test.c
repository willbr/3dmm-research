#include "engine_route_edit_fixture.h"
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
