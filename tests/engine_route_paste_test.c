#include "engine_subroute_edit_fixture.h"
#include "three_mm/actor_route_paste.h"
typedef struct PasteFixture
{
    SubFixture sub;
    int point_reserves,fail_points,point_inserts,fail_point_at,event_inserts,fail_event_at;
    int flag_queries,fail_flags,flags,distances,zero_distance,diagnostics;
} PasteFixture;
static int reserve_points(void *p,ThreeMMU32 count)
{ PasteFixture *f=(PasteFixture *)p; CHECK(count>0); ++f->point_reserves; return !f->fail_points; }
static int paste_point(void *p,ThreeMMI32 i,const ThreeMMActorRoutePoint *v)
{ PasteFixture *f=(PasteFixture *)p; if (++f->point_inserts==f->fail_point_at) return 0; return insert_point(p,i,v); }
static int paste_event(void *p,ThreeMMI32 i,ThreeMMU32 bytes,const void *v,const ThreeMMActorEvent *e)
{ PasteFixture *f=(PasteFixture *)p; if (++f->event_inserts==f->fail_event_at) return 0; return insert_event(p,i,bytes,v,e); }
static ThreeMMI32 distance(void *p,const ThreeMMActorPoint *v)
{ PasteFixture *f=(PasteFixture *)p; ++f->distances; return f->zero_distance?0:(v->dxr<0?-v->dxr:v->dxr); }
static int paste_flags(void *p,ThreeMMI32 action,ThreeMMU32 *flags)
{ PasteFixture *f=(PasteFixture *)p; CHECK(action==0); ++f->flag_queries; *flags=(ThreeMMU32)f->flags; return !f->fail_flags; }
static void paste_diagnostic(void *p,int fault) { PasteFixture *f=(PasteFixture *)p; CHECK(fault>=0 && fault<=2); f->diagnostics|=1<<fault; }
static ThreeMMActorRoutePasteServices setup(PasteFixture *f,Fixture *clipboard)
{
    ThreeMMActorRoutePasteServices s; int i; const int x[]={0,4,10,20,30},d[]={4,6,0,10,0};
    memset(f,0,sizeof(*f)); memset(clipboard,0,sizeof(*clipboard)); memset(&s,0,sizeof(s));
    s.subroute=sub_init(&f->sub); f->sub.base.events=0; f->sub.base.nodes=5; f->flags=8;
    for (i=0;i<5;++i) { f->sub.base.point[i].xyz.dxr=x[i]*ONE; f->sub.base.point[i].dwr=d[i]*ONE; }
    add(&f->sub,0,1); append(&f->sub.base,THREE_MM_ACTOR_ACTION,0,0,1); f->sub.base.bytes[1]=8;
    append(&f->sub.base,THREE_MM_ACTOR_STEP,0,0,2); owned(&f->sub,1,3); append(&f->sub.base,THREE_MM_ACTOR_STEP,2,0,4);
    add(&f->sub,3,8); append(&f->sub.base,THREE_MM_ACTOR_ACTION,3,0,8); f->sub.base.bytes[6]=8;
    owned(&f->sub,3,8); append(&f->sub.base,THREE_MM_ACTOR_STEP,4,0,10);
    f->sub.base.state.frame=3; f->sub.base.state.firstEventInFrame=f->sub.base.state.nextEvent=3;
    f->sub.base.state.addEvent=0; f->sub.base.state.actionEvent=1; f->sub.base.state.celCount=4;
    f->sub.base.state.location.irpt=f->sub.base.state.location.dwrOffset=f->sub.base.state.location.dnfrm=0;
    f->sub.base.state.position.dxr=f->sub.base.state.position.dyr=f->sub.base.state.position.dzr=0;
    f->sub.base.state.step=0; f->sub.base.view=f->sub.base.state; f->sub.base.range.last=10;
    s.subroute.route.insert_point=paste_point; s.subroute.route.edits.insert_event=paste_event;
    s.subroute.action.sounds.edits=s.subroute.route.edits; s.subroute.action.action_flags=paste_flags;
    s.reserve_points=reserve_points; s.distance=distance; s.diagnostic=paste_diagnostic;
    clipboard->nodes=3;
    for (i=0;i<3;++i) { clipboard->point[i].xyz.dxr=(100+i*4)*ONE; clipboard->point[i].dwr=i==2?0:4*ONE; }
    return s;
}
static int paste(PasteFixture *f,Fixture *clipboard,int legacy,const ThreeMMActorRoutePasteServices *s)
{ return three_mm_actor_route_paste(&f->sub.base.state,&f->sub.base.sounds,f,f,23,clipboard,legacy,&tables,s); }
static int later_node(PasteFixture *f)
{
    int i; for (i=0;i<f->sub.base.events;++i) if (f->sub.base.event[i].aet==THREE_MM_ACTOR_ADD && f->sub.base.event[i].nfrm==8) return f->sub.base.event[i].rtel.irpt;
    return -1;
}
int main(void)
{
    PasteFixture f; Fixture clip; ThreeMMActorRoutePasteServices s; ThreeMMActorRoutePoint original[3]; int fail,i;
    s=setup(&f,&clip); memcpy(original,clip.point,sizeof(original)); CHECK(paste(&f,&clip,0,&s));
    CHECK(!memcmp(original,clip.point,sizeof(original)) && f.sub.base.nodes==5 && f.sub.refs==1 && later_node(&f)==3);
    CHECK(f.sub.base.point[0].xyz.dxr==0 && f.sub.base.point[0].dwr==4*ONE && f.sub.base.point[1].xyz.dxr==4*ONE && f.sub.base.point[2].xyz.dxr==8*ONE);
    CHECK(f.sub.base.state.step==-ONE && f.sub.dirty_state && f.sub.base.dirty==2 && f.sub.base.invalidated==1 && f.sub.locates==1);
    i=f.sub.base.state.nextEvent; CHECK(f.sub.base.event[i].aet==THREE_MM_ACTOR_STEP && f.sub.base.payload[i][0]==0);
    CHECK(f.sub.base.event[i+1].aet==THREE_MM_ACTOR_FREEZE && f.sub.base.payload[i+1][0]==1);
    CHECK(f.sub.base.event[i].rtel.irpt==2 && !f.sub.base.event[i].rtel.dwrOffset && !f.sub.base.event[i].rtel.dnfrm && f.sub.base.event[i].nfrm==3);
    done(&f.sub);
    s=setup(&f,&clip); f.sub.base.state.location.dwrOffset=2*ONE; f.sub.base.state.position.dxr=2*ONE;
    CHECK(paste(&f,&clip,0,&s) && f.sub.base.nodes==6 && f.sub.base.state.location.irpt==1 && !f.sub.base.state.location.dwrOffset);
    CHECK(f.sub.base.point[0].dwr==2*ONE && f.sub.base.point[1].xyz.dxr==2*ONE && f.sub.base.point[2].xyz.dxr==6*ONE && later_node(&f)==4);
    done(&f.sub);
    s=setup(&f,&clip); clip.nodes=2; clip.point[1].dwr=0;
    CHECK(paste(&f,&clip,0,&s) && f.sub.base.nodes==4 && later_node(&f)==2 && f.sub.refs==1); done(&f.sub);
    s=setup(&f,&clip); f.zero_distance=1; CHECK(paste(&f,&clip,0,&s) && f.sub.base.point[0].dwr==1); done(&f.sub);
    for (i=0;i<2;++i)
    {
        s=setup(&f,&clip); clip.nodes=i; CHECK(!paste(&f,&clip,0,&s));
        CHECK(f.diagnostics==(1<<THREE_MM_ROUTE_PASTE_EMPTY) && !f.point_reserves && !f.sub.base.reserves && f.sub.refs==2 && f.sub.base.nodes==5); done(&f.sub);
    }
    s=setup(&f,&clip); f.fail_points=1; CHECK(!paste(&f,&clip,0,&s) && !f.sub.base.reserves && f.sub.refs==2 && !f.point_inserts); done(&f.sub);
    s=setup(&f,&clip); f.sub.base.fail_reserve=1; CHECK(!paste(&f,&clip,0,&s) && !f.point_inserts && f.sub.refs==2); done(&f.sub);
    s=setup(&f,&clip); f.fail_point_at=1; f.sub.base.state.location.dwrOffset=2*ONE;
    CHECK(!paste(&f,&clip,0,&s) && f.sub.base.state.location.irpt==1 && !f.sub.base.state.location.dwrOffset && f.sub.base.nodes==5 && f.sub.refs==2); done(&f.sub);
    s=setup(&f,&clip); f.fail_point_at=1;
    CHECK(paste(&f,&clip,0,&s) && f.diagnostics==(1<<THREE_MM_ROUTE_PASTE_INSERT_FAILED) && f.sub.locates==1); done(&f.sub);
    for (fail=1;fail<=3;++fail)
    {
        s=setup(&f,&clip); f.fail_event_at=fail; CHECK(!paste(&f,&clip,0,&s));
        CHECK(f.sub.refs==1 && !f.sub.locates && !f.sub.dirty_state && !f.sub.base.invalidated && f.sub.base.nodes==5);
        CHECK(f.sub.base.dirty==(fail>1)); done(&f.sub);
    }
    s=setup(&f,&clip); f.sub.base.fail_reserve=2; CHECK(!paste(&f,&clip,0,&s) && f.sub.refs==1 && !f.event_inserts); done(&f.sub);
    s=setup(&f,&clip); f.sub.base.fail_apply=1; CHECK(!paste(&f,&clip,0,&s) && f.sub.refs==1 && f.event_inserts==1 && !f.sub.locates); done(&f.sub);
    s=setup(&f,&clip); CHECK(paste(&f,&clip,1,&s) && f.flag_queries==1 && f.sub.base.state.step==-ONE && f.event_inserts==3); done(&f.sub);
    s=setup(&f,&clip); f.flags=0; CHECK(paste(&f,&clip,1,&s) && f.flag_queries==1 && f.sub.base.state.step==0 && f.event_inserts==2 && f.sub.base.dirty==1); done(&f.sub);
    s=setup(&f,&clip); f.fail_flags=1; CHECK(!paste(&f,&clip,1,&s) && f.sub.refs==1 && !f.event_inserts && !f.sub.locates); done(&f.sub);
    s=setup(&f,&clip); f.sub.base.state.actionEvent=-1; f.fail_flags=1;
    CHECK(!paste(&f,&clip,1,&s) && f.diagnostics==(1<<THREE_MM_ROUTE_PASTE_NO_ACTION) && f.flag_queries==1); done(&f.sub);
    puts("actor route paste C89 checks passed"); return 0;
}
