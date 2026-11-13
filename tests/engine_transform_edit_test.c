#include "engine_subroute_edit_fixture.h"
#include "three_mm/actor_transform_edit.h"
#include <limits.h>
typedef struct TransformFixture
{
    SubFixture sub;
    ThreeMMActorPoint origin,published_origin;
    ThreeMMU32 reserve_count,reserve_bytes;
    int locks,present,normalises,distances,origins,seeks,bars,fail_scene,event_inserts,fail_event,outputs,output_value,require_output;
    ThreeMMI32 scene_frame,target;
} TransformFixture;
static int trans_reserve(void *p,ThreeMMU32 count,ThreeMMU32 bytes)
{
    TransformFixture *f=(TransformFixture *)p; f->reserve_count=count; f->reserve_bytes=bytes;
    if (f->require_output) CHECK(f->outputs==1 && f->output_value);
    return ++f->sub.base.reserves!=f->sub.base.fail_reserve;
}
static int trans_insert(void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *value,const ThreeMMActorEvent *e)
{
    TransformFixture *f=(TransformFixture *)p;
    if (++f->event_inserts==f->fail_event) return 0;
    return insert_event(p,i,bytes,value,e);
}
static void lock(void *p) { ++((TransformFixture *)p)->locks; }
static void unlock(void *p) { TransformFixture *f=(TransformFixture *)p; CHECK(f->locks==1); --f->locks; }
static const void *view(void *p,ThreeMMI32 i) { TransformFixture *f=(TransformFixture *)p; CHECK(f->locks==1); return f->sub.base.payload[i]; }
static int present(void *p) { return ((TransformFixture *)p)->present; }
static void normalise(void *p,ThreeMMActorMatrix *matrix) { ++((TransformFixture *)p)->normalises; matrix->m[3][1]=1234; }
static ThreeMMI32 distance(void *p,const ThreeMMActorPoint *v)
{
    TransformFixture *f=(TransformFixture *)p; ++f->distances;
    return (v->dxr<0?-v->dxr:v->dxr)+(v->dyr<0?-v->dyr:v->dyr)+(v->dzr<0?-v->dzr:v->dzr);
}
static void origin(void *p,const ThreeMMActorPoint *value) { TransformFixture *f=(TransformFixture *)p; ++f->origins; f->published_origin=*value; }
static int scene_frame(void *p,ThreeMMI32 *frame) { *frame=((TransformFixture *)p)->scene_frame; return 1; }
static int seek_scene(void *p,ThreeMMActorState *state,ThreeMMI32 frame)
{
    TransformFixture *f=(TransformFixture *)p; ++f->seeks; f->target=frame;
    CHECK(f->sub.base.invalidated==1 && f->sub.base.view.frame==frame);
    state->frame=frame; if (f->fail_scene) { state->cel=99; return 0; } return 1;
}
static void bars(void *p) { TransformFixture *f=(TransformFixture *)p; CHECK(f->seeks==1 && !f->fail_scene); ++f->bars; }
static void trans_diagnostic(void *p,int fault)
{ TransformFixture *f=(TransformFixture *)p; CHECK(fault>=0 && fault<=THREE_MM_TRANSFORM_BAD_LIFETIME); ++f->sub.diagnostic_count; }
static void moved(void *p,int value)
{ TransformFixture *f=(TransformFixture *)p; CHECK(!f->sub.base.reserves); ++f->outputs; f->output_value=value; }
static ThreeMMActorTransformEditServices init_transform(TransformFixture *f)
{
    ThreeMMActorTransformEditServices s;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); s.subroute=sub_init(&f->sub);
    f->sub.base.events=0; add(&f->sub,0,1); append(&f->sub.base,THREE_MM_ACTOR_ACTION,0,0,1); f->sub.base.bytes[1]=8;
    f->sub.base.state.location.irpt=0; f->sub.base.state.location.dwrOffset=f->sub.base.state.location.dnfrm=0;
    f->sub.base.state.firstEventInFrame=0; f->sub.base.state.actionEvent=1; f->sub.base.state.celCount=4;
    f->sub.base.recording=0; f->present=1; f->scene_frame=2;
    s.subroute.route.edits.insert_event=trans_insert; s.subroute.route.edits.scene_frame=scene_frame;
    s.subroute.route.reserve_events=trans_reserve;
    s.subroute.action.sounds.edits=s.subroute.route.edits; s.subroute.action.sounds.reserve_events=trans_reserve;
    s.fill.edits=s.subroute.route.edits; s.fill.lock_events=lock; s.fill.unlock_events=unlock; s.fill.payload_view=view;
    s.scene_present=present; s.normalise=normalise; s.distance=distance; s.publish_origin=origin;
    s.seek_scene=seek_scene; s.scrollbars=bars; s.diagnostic=trans_diagnostic;
    return s;
}
static int scale(TransformFixture *f,ThreeMMI32 value,const ThreeMMActorTransformEditServices *s)
{ return three_mm_actor_transform_scale(&f->sub.base.state,&f->sub.base.sounds,f,f,7,value,&tables,s); }
static int rotate(TransformFixture *f,int world,unsigned short angle,const ThreeMMActorTransformEditServices *s)
{ return three_mm_actor_transform_rotate(&f->sub.base.state,&f->sub.base.sounds,f,f,7,angle,0,0,1,world,&tables,s); }
static int move_route(TransformFixture *f,const ThreeMMActorPoint *delta,ThreeMMU32 flags,const ThreeMMActorTransformEditServices *s)
{
    ThreeMMActorMoveResult result; memset(&result,0,sizeof(result)); result.context=f; result.publish=moved;
    return three_mm_actor_transform_move(&f->sub.base.state,&f->sub.base.sounds,f,f,7,&f->origin,delta,flags,&result,&tables,s);
}
static void scale_test(void)
{
    TransformFixture f; ThreeMMActorTransformEditServices s; ThreeMMActorState before;
    ThreeMMActorStretch stretch={INT_MAX,0,ONE}; int failure;
    s=init_transform(&f); CHECK(scale(&f,INT_MAX,&s) && f.sub.base.state.transform.rScaleStep==655359L);
    CHECK(f.sub.dirty_state && f.sub.base.invalidated==1 && f.sub.locates==1 && !f.locks); done(&f.sub);
    s=init_transform(&f); CHECK(scale(&f,-ONE,&s) && f.sub.base.state.transform.rScaleStep==16384); done(&f.sub);
    s=init_transform(&f); f.sub.base.state.position.dxr=1234; append(&f.sub.base,THREE_MM_ACTOR_TWEAK,1,0,3);
    CHECK(scale(&f,ONE/2,&s) && f.sub.base.events==4 && f.sub.base.state.position.dxr==1234); done(&f.sub);
    s=init_transform(&f); f.sub.base.state.transform.rScaleStep=2*ONE; f.sub.base.state.position.dxr=1234;
    append(&f.sub.base,THREE_MM_ACTOR_TWEAK,1,0,3); owned(&f.sub,1,3);
    CHECK(scale(&f,ONE/2,&s) && f.sub.base.events==4 && f.sub.base.state.position.dxr==0 && f.sub.refs==1); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=init_transform(&f); before=f.sub.base.state;
        if (failure) f.sub.base.fail_apply=1; else f.sub.base.fail_reserve=1;
        CHECK(!scale(&f,ONE/2,&s) && !f.sub.locates && !f.sub.base.invalidated);
        if (failure) CHECK(f.sub.base.state.transform.rScaleStep==ONE/2 && f.sub.base.events==3);
        else CHECK(!memcmp(&before,&f.sub.base.state,sizeof(before)) && f.sub.base.events==2);
        done(&f.sub);
    }
    s=init_transform(&f); f.present=0;
    CHECK(scale(&f,ONE/2,&s) && !f.sub.dirty_state && !f.sub.base.invalidated); done(&f.sub);
    s=init_transform(&f);
    CHECK(three_mm_actor_transform_pull(&f.sub.base.state,&f.sub.base.sounds,&f,&f,7,&stretch,&tables,&s));
    CHECK(f.sub.base.state.transform.aevpull.rScaleX==262143L && f.sub.base.state.transform.aevpull.rScaleY==16384 &&
          f.sub.base.state.transform.aevpull.rScaleZ==ONE && f.reserve_count==1 && f.reserve_bytes==12); done(&f.sub);
}
static void rotate_normalize_test(void)
{
    TransformFixture f; ThreeMMActorTransformEditServices s; ThreeMMActorState before; ThreeMMActorMatrix matrix;
    s=init_transform(&f); before=f.sub.base.state;
    CHECK(rotate(&f,1,0,&s) && !f.sub.base.reserves && !f.sub.locates && !memcmp(&before,&f.sub.base.state,sizeof(before))); done(&f.sub);
    s=init_transform(&f); CHECK(rotate(&f,0,0,&s) && f.sub.base.reserves==1 && f.sub.locates==1 && f.sub.base.events==3); done(&f.sub);
    s=init_transform(&f); CHECK(rotate(&f,1,0x4000,&s) && f.normalises==1 && f.reserve_count==1 && f.reserve_bytes==48);
    memcpy(&matrix,f.sub.base.payload[2],sizeof(matrix)); CHECK(matrix.m[3][1]==1234); done(&f.sub);
    s=init_transform(&f); f.fail_event=1;
    CHECK(rotate(&f,0,0x4000,&s) && f.sub.diagnostic_count==1 && f.sub.locates==1 && !f.sub.base.dirty && f.sub.base.events==2); done(&f.sub);
    s=init_transform(&f); f.sub.base.fail_reserve=1; CHECK(!rotate(&f,0,1,&s) && !f.sub.locates); done(&f.sub);
    s=init_transform(&f); f.sub.base.state.transform.rScaleStep=2*ONE;
    CHECK(three_mm_actor_transform_normalize(&f.sub.base.state,&f.sub.base.sounds,&f,&f,7,3,&tables,&s));
    CHECK(f.reserve_count==3 && f.reserve_bytes==64 && f.sub.base.state.transform.rScaleStep==ONE && f.sub.base.invalidated==1);
    CHECK(f.sub.base.state.transform.aevpull.rScaleX==ONE && f.sub.base.state.transform.aevpull.rScaleY==ONE && !f.locks); done(&f.sub);
    s=init_transform(&f);
    CHECK(three_mm_actor_transform_normalize(&f.sub.base.state,&f.sub.base.sounds,&f,&f,7,0,&tables,&s));
    CHECK(f.reserve_count==3 && !f.reserve_bytes && f.sub.locates==1 && !f.sub.base.dirty); done(&f.sub);
}
static void movement_test(void)
{
    TransformFixture f; ThreeMMActorTransformEditServices s; ThreeMMActorPoint delta={0,0,0};
    ThreeMMActorEntrance entrance; ThreeMMActorMatrix matrix; int scope;
    s=init_transform(&f); CHECK(move_route(&f,&delta,0,&s) && f.outputs==1 && !f.output_value && !f.sub.base.reserves); done(&f.sub);
    s=init_transform(&f); delta.dxr=ONE; f.sub.base.fail_reserve=1; f.require_output=1;
    CHECK(!move_route(&f,&delta,0,&s) && f.output_value && !f.sub.base.dirty && !f.sub.locates); done(&f.sub);
    s=init_transform(&f); delta.dxr=0; delta.dyr=-ONE;
    CHECK(move_route(&f,&delta,THREE_MM_MOVE_GROUND,&s) && f.output_value && !f.sub.locates && f.sub.base.dirty==2);
    CHECK(!f.sub.base.state.subrouteTranslation.dyr && f.reserve_count==2 && f.reserve_bytes==60); done(&f.sub);
    delta.dxr=ONE; delta.dyr=0;
    for (scope=0;scope<3;++scope)
    {
        s=init_transform(&f); owned(&f.sub,0,1); f.sub.base.state.nextEvent=3;
        CHECK(move_route(&f,&delta,scope==0?0:scope==1?THREE_MM_MOVE_SUBROUTE:THREE_MM_MOVE_SCENE|THREE_MM_MOVE_SUBROUTE,&s));
        CHECK(f.sub.refs==1 && f.sub.base.state.routeTranslation.dxr==ONE && f.sub.locates==1);
        memcpy(&entrance,f.sub.base.payload[0],sizeof(entrance));
        CHECK(entrance.translation.dxr==(scope==1?ONE:0) && f.origins==(scope==2) && f.sub.base.events==(scope==0?4:3));
        done(&f.sub);
    }
    s=init_transform(&f); f.sub.base.state.addEvent=-1;
    CHECK(!move_route(&f,&delta,0,&s) && f.output_value && !f.sub.base.dirty && !f.sub.locates); done(&f.sub);
    s=init_transform(&f); f.sub.text=1;
    CHECK(move_route(&f,&delta,THREE_MM_MOVE_ORIENT|THREE_MM_MOVE_SUBROUTE,&s) && !f.distances); done(&f.sub);
    s=init_transform(&f); f.sub.base.state.actionFlags=THREE_MM_ACTOR_ROTATE_Y;
    three_mm_matrix_identity(&matrix); append(&f.sub.base,THREE_MM_ACTOR_ROTATE_FORWARD,0,0,1);
    f.sub.base.bytes[2]=48; memcpy(f.sub.base.payload[2],&matrix,48); f.sub.base.state.nextEvent=3;
    CHECK(move_route(&f,&delta,THREE_MM_MOVE_ORIENT|THREE_MM_MOVE_SUBROUTE,&s) && f.distances==2 && f.sub.base.events==4);
    CHECK(f.sub.base.state.subrouteTranslation.dxr>0 && f.sub.base.state.subrouteTranslation.dxr<ONE);
    CHECK(f.sub.base.event[3].aet==THREE_MM_ACTOR_ROTATE_HERE); done(&f.sub);
    s=init_transform(&f); f.sub.base.state.actionFlags=THREE_MM_ACTOR_ROTATE_Y;
    append(&f.sub.base,THREE_MM_ACTOR_ROTATE_FORWARD,0,0,1); f.sub.base.bytes[2]=48; memcpy(f.sub.base.payload[2],&matrix,48);
    f.sub.base.state.nextEvent=3; f.fail_event=1;
    CHECK(!move_route(&f,&delta,THREE_MM_MOVE_ORIENT,&s) && f.sub.diagnostic_count==1 && !f.sub.locates && f.sub.base.events==3);
    memcpy(&entrance,f.sub.base.payload[0],sizeof(entrance)); CHECK(entrance.y>0 && !f.origin.dxr); done(&f.sub);
    s=init_transform(&f); delta.dxr=0; delta.dyr=-2*ONE; f.sub.base.state.routeTranslation.dyr=ONE;
    CHECK(three_mm_actor_transform_tweak(&f.sub.base.state,&f.sub.base.sounds,&f,&f,7,&delta,THREE_MM_MOVE_GROUND,&tables,&s));
    CHECK(f.sub.base.state.position.dyr==-ONE && f.sub.locates==1); done(&f.sub);
}
static ThreeMMActorTransformEditServices time_init(TransformFixture *f)
{
    ThreeMMActorTransformEditServices s=init_transform(f); int i;
    const int frames[]={5,6,10,11,18,19};
    f->sub.base.events=0;
    for (i=0;i<3;++i)
    {
        add(&f->sub,i,frames[i*2]); append(&f->sub.base,THREE_MM_ACTOR_FREEZE,i,0,frames[i*2+1]);
        f->sub.base.event[i*2+1].rtel.dnfrm=1; f->sub.base.payload[i*2+1][0]=1; f->sub.base.point[i].dwr=0;
    }
    f->sub.base.state.addEvent=2; f->sub.base.state.nextEvent=4; f->sub.base.state.frame=f->scene_frame=11;
    f->sub.base.state.location.irpt=1; f->sub.base.state.location.dnfrm=1; f->sub.base.range.first=5; f->sub.base.range.last=19;
    return s;
}
static void time_test(void)
{
    TransformFixture f; ThreeMMActorTransformEditServices s; int failure;
    s=time_init(&f); f.sub.dirty_state=f.sub.fail_resolve=1;
    CHECK(three_mm_actor_transform_time(&f.sub.base.state,&f,&f,0,&tables,&s) && !f.sub.resolves && !f.seeks && !f.sub.base.invalidated); done(&f.sub);
    s=time_init(&f);
    CHECK(three_mm_actor_transform_time(&f.sub.base.state,&f,&f,2,&tables,&s) && f.target==13 && f.bars==1);
    CHECK(f.sub.base.event[2].nfrm==12 && f.sub.base.event[3].nfrm==13 && f.sub.base.event[4].nfrm==18 && f.sub.base.range.last==19); done(&f.sub);
    s=time_init(&f);
    CHECK(three_mm_actor_transform_time(&f.sub.base.state,&f,&f,-4,&tables,&s) && f.target==7 && f.bars==1);
    CHECK(f.sub.base.event[2].nfrm==6 && f.sub.base.event[0].nfrm==4 && f.sub.base.range.first==4 && f.sub.base.range.last==19); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=time_init(&f); f.fail_scene=failure;
        CHECK(three_mm_actor_transform_time(&f.sub.base.state,&f,&f,9,&tables,&s)==!failure);
        CHECK(f.sub.base.event[4].nfrm==21 && f.sub.base.event[5].nfrm==22 && f.sub.base.range.last==22 && f.bars==!failure);
        if (failure) CHECK(f.sub.base.state.cel==99 && f.sub.base.state.frame==20);
        done(&f.sub);
    }
    s=time_init(&f); f.sub.dirty_state=f.sub.fail_resolve=1;
    f.sub.base.event[1].aet=THREE_MM_ACTOR_STEP; f.sub.base.payload[1][0]=-ONE;
    CHECK(three_mm_actor_transform_time(&f.sub.base.state,&f,&f,1,&tables,&s) && f.sub.diagnostic_count==1 && f.sub.resolves==1 && f.seeks==1); done(&f.sub);
}
int main(void)
{ scale_test(); rotate_normalize_test(); movement_test(); time_test(); puts("actor transform/time editing checks passed"); return 0; }
