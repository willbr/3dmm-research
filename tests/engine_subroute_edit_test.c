#include "engine_subroute_edit_fixture.h"
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
