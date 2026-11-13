#include "engine_subroute_edit_fixture.h"
#include "three_mm/actor_record.h"
typedef struct RecordFixture
{
    SubFixture sub;
    ThreeMMActorRecording record,view_record,saved_record;
    ThreeMMActorState saved_state;
    ThreeMMActorPoint measured;
    int restores,point_reserves,fail_points,distances,seeks,fail_scene,invalid_handles,outputs;
    int output_frame,output_route;
    int aliased_output;
    unsigned output_mask;
    ThreeMMI32 forced_distance,scene_frame;
} RecordFixture;
static void record_publish(void *p,const ThreeMMActorRecording *record)
{
    RecordFixture *f=(RecordFixture *)p; f->view_record=*record;
    f->sub.base.recording=record->recording; f->sub.rejoining=record->rejoining;
}
static void record_restore(void *p,void *undo,ThreeMMActorState *state,ThreeMMActorRecording *record)
{
    RecordFixture *f=(RecordFixture *)p; CHECK(undo==&f->saved_state);
    ++f->restores; *state=f->saved_state; *record=f->saved_record; f->invalid_handles=1;
}
static ThreeMMI32 checked_events(void *p) { RecordFixture *f=(RecordFixture *)p; CHECK(!f->invalid_handles); return events(p); }
static ThreeMMI32 checked_nodes(void *p) { RecordFixture *f=(RecordFixture *)p; CHECK(!f->invalid_handles); return nodes(p); }
static ThreeMMI32 metric(void *p,const ThreeMMActorPoint *delta)
{
    RecordFixture *f=(RecordFixture *)p; f->measured=*delta; ++f->distances;
    if (f->forced_distance>=0) return f->forced_distance;
    return (delta->dxr<0?-delta->dxr:delta->dxr)+(delta->dyr<0?-delta->dyr:delta->dyr)+(delta->dzr<0?-delta->dzr:delta->dzr);
}
static int record_points(void *p,ThreeMMU32 count)
{ RecordFixture *f=(RecordFixture *)p; CHECK(count==1 && f->sub.base.reserves==1); ++f->point_reserves; return !f->fail_points; }
static int current_frame(void *p,ThreeMMI32 *frame) { *frame=((RecordFixture *)p)->scene_frame; return 1; }
static void output(void *p,int frame,int route,unsigned changed)
{
    RecordFixture *f=(RecordFixture *)p; ++f->outputs; f->output_frame=frame; f->output_route=route;
    f->output_mask=changed;
    if (changed&1) f->aliased_output=frame;
    if (changed&2) f->aliased_output=route;
    if (frame) CHECK(f->view_record.inserted && !f->restores);
}
static int scene_seek(void *p,ThreeMMActorState *state,ThreeMMActorRecording *record,ThreeMMI32 frame)
{
    RecordFixture *f=(RecordFixture *)p; CHECK(frame==f->scene_frame+1);
    CHECK(f->outputs==2 && f->output_frame && f->view_record.inserted);
    ++f->seeks; state->frame=frame;
    if (f->fail_scene) { state->cel=99; record->timestamp=99; return 0; }
    return 1;
}
static void record_diagnostic(void *p,int fault)
{ RecordFixture *f=(RecordFixture *)p; CHECK(fault>=0 && fault<=THREE_MM_RECORD_STEP_FAILED); ++f->sub.diagnostic_count; }
static ThreeMMActorRecordServices record_init(RecordFixture *f)
{
    ThreeMMActorRecordServices s;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); s.subroute=sub_init(&f->sub);
    f->sub.base.events=0; add(&f->sub,0,1); append(&f->sub.base,THREE_MM_ACTOR_ACTION,0,0,1); f->sub.base.bytes[1]=8;
    f->sub.base.point[1].dwr=4*ONE; f->sub.base.point[2].xyz.dxr=8*ONE;
    f->sub.base.state.location.irpt=0; f->sub.base.state.location.dwrOffset=f->sub.base.state.location.dnfrm=0;
    f->sub.base.state.firstEventInFrame=0; f->sub.base.state.actionEvent=1; f->sub.base.state.celCount=4; f->sub.base.state.step=-ONE;
    f->record.recording=f->record.rejoining=f->record.inserted=1; f->record.timestamp=1000;
    f->scene_frame=2; f->forced_distance=-1; f->saved_state=f->sub.base.state; f->saved_state.frame=44;
    f->saved_record=f->record; f->saved_record.timestamp=123; f->saved_record.gap=77;
    s.subroute.route.edits.scene_frame=current_frame; s.subroute.action.sounds.edits=s.subroute.route.edits;
    s.distance=metric; s.publish_record=record_publish; s.restore=record_restore; s.seek_scene=scene_seek;
    s.reserve_points=record_points; s.diagnostic=record_diagnostic; record_publish(f,&f->record);
    return s;
}
static ThreeMMActorMotionServices checked_tables(void)
{ ThreeMMActorMotionServices m=tables; m.event_count=checked_events; m.route_count=checked_nodes; return m; }
static int begin(RecordFixture *f,int replace,const ThreeMMActorRecordServices *s)
{ ThreeMMActorMotionServices m=checked_tables(); return three_mm_actor_record_begin(&f->sub.base.state,&f->record,f,f,replace,&f->saved_state,&m,s); }
static int move(RecordFixture *f,const ThreeMMActorPoint *delta,ThreeMMU32 flags,ThreeMMU32 now,ThreeMMActorRecordResult *out,const ThreeMMActorRecordServices *s)
{ ThreeMMActorMotionServices m=checked_tables(); return three_mm_actor_record_move(&f->sub.base.state,&f->record,&f->sub.base.sounds,f,f,7,delta,flags,now,out,&f->saved_state,&m,s); }
static int finish(RecordFixture *f,int replace,const ThreeMMActorRecordServices *s)
{ ThreeMMActorMotionServices m=checked_tables(); return three_mm_actor_record_end(&f->sub.base.state,&f->record,&f->sub.base.sounds,f,f,7,replace,&f->saved_state,&m,s); }
static ThreeMMActorRecordResult result(RecordFixture *f)
{ ThreeMMActorRecordResult out; memset(&out,0,sizeof(out)); out.frame=41; out.route=42; out.context=f; out.publish=output; return out; }
static void validity_test(void)
{
    RecordFixture f; ThreeMMActorRecordServices s=record_init(&f); ThreeMMActorPoint delta={0,0,0}; ThreeMMI32 value;
    CHECK(three_mm_actor_record_valid(&f.record,&delta,0,&s) && !f.distances);
    f.record.recording=0; f.record.timestamp=0xffffff00U; f.forced_distance=2*ONE-1;
    CHECK(!three_mm_actor_record_valid(&f.record,&delta,493,&s));
    CHECK(three_mm_actor_record_valid(&f.record,&delta,494,&s));
    f.forced_distance=2*ONE; CHECK(three_mm_actor_record_valid(&f.record,&delta,f.record.timestamp,&s));
    f.forced_distance=0; CHECK(three_mm_actor_record_valid(&f.record,&delta,f.record.timestamp-1,&s));
    f.sub.base.state.transform.rScaleStep=2*ONE; f.sub.base.state.step=0;
    CHECK(three_mm_actor_record_step(&f.sub.base.state,&value,&s.subroute.timeline) && value==2*ONE);
    f.sub.fail_resolve=1; value=-99;
    CHECK(!three_mm_actor_record_step(&f.sub.base.state,&value,&s.subroute.timeline) && value==ONE);
    done(&f.sub);
}
static void begin_test(void)
{
    RecordFixture f; ThreeMMActorRecordServices s;
    s=record_init(&f); f.record.recording=0; f.sub.base.state.location.dwrOffset=2*ONE; add(&f.sub,2,8);
    CHECK(begin(&f,0,&s));
    CHECK(f.record.recording && f.record.rejoining && !f.record.inserted && f.record.gap==-6 && f.record.timestamp==1000);
    CHECK(f.record.insertion.irpt==1 && !f.record.raw.dxr && f.sub.base.state.position.dxr==2*ONE && f.sub.base.nodes==4);
    CHECK(f.sub.base.point[0].dwr==2*ONE && f.sub.base.point[1].dwr==2*ONE && f.sub.base.event[2].rtel.irpt==3); done(&f.sub);
    s=record_init(&f); f.record.recording=0; append(&f.sub.base,THREE_MM_ACTOR_REMOVE,1,0,3); f.sub.base.bytes[2]=0; add(&f.sub,2,8);
    CHECK(begin(&f,0,&s) && !f.record.rejoining && f.sub.base.nodes==2 && f.sub.base.events==3);
    CHECK(f.sub.base.event[2].aet==THREE_MM_ACTOR_ADD && f.sub.base.event[2].rtel.irpt==1); done(&f.sub);
    s=record_init(&f); f.sub.base.point[0].dwr=0;
    CHECK(begin(&f,0,&s) && !f.record.rejoining); done(&f.sub);
    s=record_init(&f); f.sub.base.state.location.dwrOffset=ONE; f.sub.base.fail_insert=1;
    CHECK(!begin(&f,0,&s) && f.restores==1 && f.sub.base.state.frame==44 && f.record.timestamp==123); done(&f.sub);
    s=record_init(&f); f.record.recording=0; f.sub.dirty_state=f.sub.fail_resolve=1;
    append(&f.sub.base,THREE_MM_ACTOR_STEP,0,0,1); f.sub.base.payload[2][0]=-ONE; add(&f.sub,2,8);
    CHECK(!begin(&f,0,&s) && f.restores==1 && f.sub.diagnostic_count==1 && f.record.gap==77); done(&f.sub);
}
static void move_test(void)
{
    RecordFixture f; ThreeMMActorRecordServices s; ThreeMMActorRecordResult out;
    ThreeMMActorPoint delta={0,0,0}; int failure;
    s=record_init(&f); f.record.recording=0; out=result(&f);
    CHECK(move(&f,&delta,0,1000,&out,&s) && !out.written && out.frame==41 && !f.sub.base.reserves); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=record_init(&f); out=result(&f);
        if (failure) f.fail_points=1; else f.sub.base.fail_reserve=1;
        CHECK(!move(&f,&delta,0,1000,&out,&s) && f.restores==1 && !out.written && f.point_reserves==failure && !f.outputs); done(&f.sub);
    }
    s=record_init(&f); out=result(&f);
    CHECK(move(&f,&delta,0,1749,&out,&s) && out.written && !out.frame && !out.route && !f.seeks && !f.restores);
    CHECK(f.record.timestamp==1000 && f.sub.base.state.step==-ONE); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=record_init(&f); out=result(&f); f.fail_scene=failure;
        CHECK(move(&f,&delta,0,1750,&out,&s)==!failure);
        CHECK(out.frame && !out.route && f.seeks==1 && f.outputs==2 && f.restores==failure);
        CHECK(f.output_mask==1 && f.aliased_output==1);
        CHECK(failure ? f.sub.base.state.frame==44 : (f.sub.base.state.step==0 && f.record.timestamp==1000 && !f.record.gap)); done(&f.sub);
    }
    for (failure=0;failure<4;++failure)
    {
        s=record_init(&f); out=result(&f); delta.dxr=3*ONE; delta.dyr=delta.dzr=0;
        if (failure==1) f.sub.fail_resolve=1;
        if (failure==2) f.sub.base.fail_insert=1;
        if (failure==3) f.fail_scene=1;
        CHECK(move(&f,&delta,0,1100,&out,&s)==!failure);
        CHECK(out.written && out.frame==(failure==0 || failure==3) && out.route==out.frame);
        if (failure) CHECK(f.restores==1 && f.sub.base.state.frame==44 && f.record.timestamp==123);
        else CHECK(f.sub.base.nodes==4 && f.sub.base.point[0].dwr==ONE && f.sub.base.point[1].xyz.dxr==ONE &&
                   f.record.raw.dxr==3*ONE && f.record.timestamp==1100 && f.record.gap==1 && f.sub.base.range.last==9);
        CHECK(f.output_mask==3);
        done(&f.sub);
    }
    s=record_init(&f); out=result(&f); delta.dxr=0; delta.dyr=-3*ONE;
    f.sub.base.state.routeTranslation.dyr=ONE/2; f.record.gap=-1;
    CHECK(move(&f,&delta,THREE_MM_RECORD_GROUND,1100,&out,&s));
    CHECK(f.sub.base.point[1].xyz.dyr==-ONE/2 && f.sub.base.point[0].dwr==ONE && !f.record.gap && f.sub.base.range.last==8); done(&f.sub);
    s=record_init(&f); out=result(&f); f.forced_distance=0; f.record.raw.dxr=3; f.record.raw.dyr=-1; f.record.raw.dzr=3;
    delta.dxr=2147483647L; delta.dyr=(-2147483647L-1); delta.dzr=-5;
    CHECK(move(&f,&delta,0,1001,&out,&s));
    CHECK(!f.measured.dxr && !f.measured.dyr && f.measured.dzr==-2 && f.record.raw.dxr==3); done(&f.sub);
    s=record_init(&f); out=result(&f); f.forced_distance=0; delta.dxr=-1073741824L; delta.dyr=delta.dzr=0;
    CHECK(move(&f,&delta,0,1001,&out,&s) && f.measured.dxr==-715827882L); done(&f.sub);
    s=record_init(&f); out=result(&f); f.sub.base.state.frozen=1; f.sub.base.fail_apply=1; delta.dxr=0;
    CHECK(move(&f,&delta,0,1001,&out,&s) && f.sub.diagnostic_count==1 && !f.restores && out.written); done(&f.sub);
}
static void end_test(void)
{
    RecordFixture f; ThreeMMActorRecordServices s; ThreeMMActorRoutePoint point; int failure,i,found;
    s=record_init(&f); f.record.recording=0;
    CHECK(finish(&f,0,&s) && f.sub.base.state.frozen && !f.record.recording && !f.record.rejoining && f.sub.dirty_state && f.sub.base.invalidated==1); done(&f.sub);
    s=record_init(&f); owned(&f.sub,1,3); add(&f.sub,2,8);
    CHECK(finish(&f,1,&s) && f.sub.refs==0 && f.sub.base.nodes==2 && f.sub.base.state.frozen && !f.sub.base.state.step); done(&f.sub);
    s=record_init(&f); f.sub.base.state.position.dxr=8*ONE; f.sub.base.point[0].dwr=0;
    owned(&f.sub,1,3); owned(&f.sub,1,4); append(&f.sub.base,THREE_MM_ACTOR_TWEAK,1,0,4); append(&f.sub.base,THREE_MM_ACTOR_FREEZE,1,0,5);
    CHECK(finish(&f,0,&s) && f.sub.base.nodes==2 && f.sub.base.point[0].dwr==1 && f.sub.refs==1 && f.sub.base.events==3);
    CHECK(f.sub.base.event[2].aet==THREE_MM_ACTOR_COSTUME && f.sub.base.event[2].rtel.irpt==1); done(&f.sub);
    s=record_init(&f); f.sub.base.state.position.dxr=8*ONE; owned(&f.sub,1,3); add(&f.sub,2,8); owned(&f.sub,2,8);
    CHECK(finish(&f,0,&s) && f.sub.base.nodes==3 && f.sub.base.point[0].dwr==4*ONE && f.sub.base.event[3].rtel.irpt==2 && f.sub.refs==2); done(&f.sub);
    s=record_init(&f); f.record.rejoining=0; owned(&f.sub,1,3); add(&f.sub,2,8); owned(&f.sub,2,8);
    CHECK(finish(&f,0,&s) && f.sub.refs==2);
    found=0;
    for (i=0;i<f.sub.base.events;++i) if (f.sub.base.event[i].aet==THREE_MM_ACTOR_COSTUME && f.sub.base.event[i].rtel.irpt==0)
    { CHECK(f.sub.base.event[i].rtel.dnfrm==1); ++found; }
    CHECK(found==1);
    point=f.sub.base.point[2]; CHECK(point.xyz.dxr==8*ONE && f.sub.base.state.frozen && !f.sub.base.state.step); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=record_init(&f);
        if (failure) f.sub.base.fail_reserve=1; else f.sub.base.fail_apply=1;
        CHECK(!finish(&f,1,&s) && f.restores==1 && !f.record.recording && !f.record.rejoining && f.record.timestamp==123 && f.record.gap==77); done(&f.sub);
    }
}
int main(void)
{ validity_test(); begin_test(); move_test(); end_test(); puts("actor recording checks passed"); return 0; }
