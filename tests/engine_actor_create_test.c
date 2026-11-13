#include "three_mm/actor_create.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"creation check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count,kind; } Ref;
typedef struct Actor { Ref ref; ThreeMMActorRuntime r; int track,sounds; } Actor;
typedef struct Fixture
{
    Actor actor; Ref template_object,body; ThreeMMTag tag; ThreeMMActorCreateServices s;
    int cache,scene,other_scene,fail,created,fetched,duplicated,tracks,sounds,rewound,flags,bodies,bound,releases;
    int closed,cleared,disposed,diagnostics[3];
} Fixture;
static void read_actor(void *p,void *a,ThreeMMActorRuntime *r) { (void)p; *r=((Actor *)a)->r; }
static void write_actor(void *p,void *a,const ThreeMMActorRuntime *r) { (void)p; ((Actor *)a)->r=*r; }
static void *create(void *p)
{
    Fixture *f=(Fixture *)p; ++f->created; if (f->fail==1) return NULL;
    three_mm_actor_runtime_init(&f->actor.r); f->actor.r.recording.recording=1;
    f->actor.ref.count=1; f->actor.ref.kind=1; return &f->actor;
}
static void release(void *p,void *object)
{
    Fixture *f=(Fixture *)p; Ref *ref=(Ref *)object; CHECK(ref->count>0); --ref->count;
    if (ref->kind==1)
    {
        ++f->releases; CHECK(!ref->count); three_mm_actor_owner_dispose(object,&f->s.owner);
    }
    else if (ref->kind==2) CHECK(f->actor.r.template_object==object && !f->actor.track);
    else CHECK(ref->kind==3 && f->actor.r.body==object);
}
static void *fetch_template(void *p,const ThreeMMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(tag==&f->tag && f->actor.r.template_tag.sid==-1);
    ++f->fetched; if (f->fail==2) return NULL;
    ++f->template_object.count; return &f->template_object;
}
static void duplicate_tag(void *p,const ThreeMMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(tag->cache==&f->cache && f->actor.r.template_tag.cache==tag->cache);
    CHECK(f->actor.r.template_object==&f->template_object && f->template_object.count==1);
    ++f->cache; ++f->duplicated;
}
static void close_tag(void *p,ThreeMMTag *tag)
{ Fixture *f=(Fixture *)p; if (tag->cache) { CHECK(f->cache==2); --f->cache; ++f->closed; tag->cache=NULL; } }
static void close_track(void *p,void *a) { (void)p; CHECK(!((Actor *)a)->r.template_tag.cache); }
static void clear_track(void *p,void *a)
{ Fixture *f=(Fixture *)p; CHECK(!((Actor *)a)->r.body); ((Actor *)a)->track=0; ++f->cleared; }
static void dispose_sounds(void *p,void *a)
{ Fixture *f=(Fixture *)p; CHECK(!((Actor *)a)->r.template_object); ((Actor *)a)->sounds=0; ++f->disposed; }
static int create_track(void *p,void *a)
{
    Fixture *f=(Fixture *)p; ++f->tracks;
    if (f->fetched) CHECK(f->duplicated==1 && ((Actor *)a)->r.template_object);
    if (f->fail==3) return 0; ((Actor *)a)->track=1; return 1;
}
static int create_sounds(void *p,void *a)
{
    Fixture *f=(Fixture *)p; CHECK(((Actor *)a)->track==1); ++f->sounds;
    if (f->fail==4) return 0; ((Actor *)a)->sounds=1; return 1;
}
static void *route(void *p,void *a) { (void)p; return a; }
static ThreeMMI32 route_count(void *a) { CHECK(((Actor *)a)->track==1); return 0; }
static void publish(void *p,const ThreeMMActorState *state) { ((Actor *)p)->r.state=*state; }
static void recording(void *p,int enabled)
{ CHECK(!enabled); ((Actor *)p)->r.recording.recording=enabled; }
static void route_services(void *p,void *a,ThreeMMActorRouteEditServices *s)
{
    Fixture *f=(Fixture *)p; CHECK(((Actor *)a)->sounds==1); ++f->rewound;
    memset(s,0,sizeof(*s)); s->edits.context=a; s->edits.publish=publish; s->recording=recording;
}
static int action_flags(void *p,void *object,ThreeMMI32 action,ThreeMMU32 *flags)
{
    Fixture *f=(Fixture *)p; CHECK(object==&f->template_object && !action && f->rewound==1);
    CHECK(!f->actor.r.recording.recording && !f->actor.r.state.nextEvent && f->actor.r.state.actionEvent==-1);
    CHECK(f->actor.r.state.location.dnfrm==-1 && f->actor.r.state.transform.rScaleStep==65536L);
    ++f->flags; *flags=0x55; return f->fail!=5;
}
static void *create_body(void *p,void *object)
{
    Fixture *f=(Fixture *)p; CHECK(object==&f->template_object && f->actor.r.scene==&f->scene && !f->actor.r.body);
    ++f->bodies; if (f->fail==6) return NULL; ++f->body.count; return &f->body;
}
static void bind_body(void *p,void *body,void *scene)
{
    Fixture *f=(Fixture *)p; CHECK(body==&f->body && scene==&f->scene);
    CHECK(f->actor.r.body==body && f->actor.r.scene==scene); ++f->bound;
}
static void diagnostic(void *p,int which)
{ Fixture *f=(Fixture *)p; CHECK(which>=0 && which<3); ++f->diagnostics[which]; }
static void init(Fixture *f)
{
    ThreeMMActorOwnerServices *o;
    memset(f,0,sizeof(*f)); f->cache=1; f->template_object.kind=2; f->body.kind=3;
    f->tag.sid=0; f->tag.cache=&f->cache; f->tag.type=17; f->tag.number=23;
    o=&f->s.owner; o->context=f; o->read=read_actor; o->write=write_actor; o->create=create; o->release=release;
    o->duplicate_tag=duplicate_tag; o->close_tag=close_tag; o->close_track_tags=close_track;
    o->clear_track=clear_track; o->dispose_sounds=dispose_sounds; o->route=route;
    o->route_services=route_services; o->motion.route_count=route_count;
    f->s.fetch_template=fetch_template; f->s.create_track=create_track; f->s.create_sounds=create_sounds;
    f->s.action_flags=action_flags; f->s.create_body=create_body; f->s.bind_body=bind_body; f->s.diagnostic=diagnostic;
}
static void clean(Fixture *f)
{ CHECK(!f->actor.ref.count && !f->template_object.count && !f->body.count && f->cache==1 && !f->actor.track && !f->actor.sounds); }
int main(void)
{
    Fixture f; int fail; void *actor;
    for (fail=0;fail<=5;++fail)
    {
        init(&f); f.fail=fail; actor=three_mm_actor_create(&f.tag,&f.s);
        CHECK((actor!=NULL)==!fail && f.created==1);
        CHECK(f.fetched==(fail!=1) && f.tracks==(fail!=1 && fail!=2));
        CHECK(f.sounds==(fail==0 || fail>=4) && f.flags==(fail==0 || fail==5));
        CHECK(f.actor.r.state.actionFlags==(f.flags?0x55:0));
        if (actor)
        {
            CHECK(f.actor.r.first==2147483647L && f.actor.r.last==(-2147483647L-1L) && f.actor.r.actor==-1);
            CHECK(f.cache==2 && f.actor.r.template_tag.number==23 && !f.actor.r.scene && !f.actor.r.body);
            three_mm_actor_bind_scene(actor,&f.scene,&f.s); CHECK(f.bodies==1 && f.bound==1);
            three_mm_actor_bind_scene(actor,&f.scene,&f.s); CHECK(f.bodies==1 && f.bound==1);
            three_mm_actor_bind_scene(actor,&f.other_scene,&f.s); CHECK(f.diagnostics[THREE_MM_CREATE_WRONG_SCENE]==1 && f.actor.r.scene==&f.scene);
            release(&f,actor);
        }
        CHECK(f.releases==(fail!=1)); clean(&f);
    }
    init(&f); actor=three_mm_actor_create(&f.tag,&f.s); CHECK(actor); f.fail=6;
    three_mm_actor_bind_scene(actor,&f.scene,&f.s); CHECK(f.actor.r.scene==&f.scene && !f.actor.r.body && !f.bound && f.bodies==1);
    three_mm_actor_bind_scene(actor,&f.scene,&f.s); CHECK(f.bodies==1 && f.diagnostics[THREE_MM_CREATE_BOUND_WITHOUT_BODY]==1);
    release(&f,actor); clean(&f);
    /* Direct group creation leaves successful track storage for caller cleanup. */
    init(&f); actor=create(&f); f.fail=4; CHECK(!three_mm_actor_create_groups(actor,&f.s));
    CHECK(f.actor.track==1 && !f.actor.sounds && !f.flags && !f.rewound); release(&f,actor); clean(&f);
    /* Direct initialization retains the failed query's partial state/ownership. */
    init(&f); actor=create(&f); f.fail=5; CHECK(!three_mm_actor_initialize(actor,&f.tag,&f.s));
    CHECK(f.actor.ref.count==1 && f.template_object.count==1 && f.cache==2 && f.actor.r.state.actionFlags==0x55);
    release(&f,actor); clean(&f);
    puts("C89 actor creation and binding checks passed"); return 0;
}
