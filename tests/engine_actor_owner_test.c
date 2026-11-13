#include "three_mm/actor_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"owner check failed at %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count,actor; } Ref;
typedef struct Actor
{
    Ref ref;
    ThreeMMActorRuntime runtime;
    int track,sounds;
} Actor;
typedef struct Fixture
{
    Actor live,copy;
    Ref body,template_object;
    ThreeMMActorOwnerServices services;
    ThreeMMActorOwnerResult result;
    int fail,created,tracks,sounds,tags,closed,cleared,disposed,published,hides,seeks,views;
    ThreeMMI32 seek_frame;
} Fixture;
static void read_actor(void *p,void *a,ThreeMMActorRuntime *r) { (void)p; *r=((Actor *)a)->runtime; }
static void write_actor(void *p,void *a,const ThreeMMActorRuntime *r) { (void)p; ((Actor *)a)->runtime=*r; }
static void *create_actor(void *p)
{
    Fixture *f=(Fixture *)p; ++f->created;
    if (f->fail==1) return NULL;
    CHECK(!f->copy.ref.count); f->copy.ref.count=1; f->copy.ref.actor=1;
    three_mm_actor_runtime_init(&f->copy.runtime); return &f->copy;
}
static void retain(void *p,void *a) { (void)p; CHECK(a!=NULL); ++((Ref *)a)->count; }
static void release(void *p,void *a)
{
    Fixture *f=(Fixture *)p; Ref *ref=(Ref *)a; CHECK(ref->count>0);
    if (!--ref->count && ref->actor)
    {
        /* On failed duplication the native output still identifies the object
         * while its destructor releases partially cloned storage. */
        if (f->fail==2 || f->fail==3) CHECK(f->result.actor==a);
        three_mm_actor_owner_dispose(a,&f->services);
    }
}
static int clone_track(void *p,void *d,void *a)
{
    Fixture *f=(Fixture *)p; ++f->tracks; CHECK(f->published==1);
    if (f->fail==2) return 0;
    ((Actor *)d)->track=((Actor *)a)->track; return 1;
}
static int clone_sounds(void *p,void *d,void *a)
{
    Fixture *f=(Fixture *)p; ++f->sounds; CHECK(f->tracks==1);
    if (f->fail==3) return 0;
    ((Actor *)d)->sounds=((Actor *)a)->sounds; return 1;
}
static void swap_track(void *p,void *a,void *b)
{ int t=((Actor *)a)->track; (void)p; ((Actor *)a)->track=((Actor *)b)->track; ((Actor *)b)->track=t; }
static void swap_sounds(void *p,void *a,void *b)
{ int t=((Actor *)a)->sounds; (void)p; ((Actor *)a)->sounds=((Actor *)b)->sounds; ((Actor *)b)->sounds=t; }
static void duplicate_tag(void *p,const ThreeMMTag *tag) { Fixture *f=(Fixture *)p; CHECK(tag->sid==7); ++f->tags; }
static void close_tag(void *p,ThreeMMTag *tag)
{ Fixture *f=(Fixture *)p; if (tag->sid==7) { CHECK(f->tags>0); --f->tags; } tag->sid=-1; ++f->closed; }
static void close_track(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(((Actor *)a)->runtime.template_tag.sid==-1); ++f->closed; }
static void clear_track(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(!((Actor *)a)->runtime.body); ((Actor *)a)->track=0; ++f->cleared; }
static void dispose_sounds(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(!((Actor *)a)->runtime.template_object); ((Actor *)a)->sounds=0; ++f->disposed; }
static void publish_result(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(f->result.actor==a); ++f->published; }
static void publish_state(void *p,const ThreeMMActorState *s) { ((Actor *)p)->runtime.state=*s; }
static void dirty(void *p) { ((Actor *)p)->runtime.life_dirty=1; }
static void recording(void *p,int enabled) { ((Actor *)p)->runtime.recording.recording=enabled; }
static void hide(void *p) { CHECK(((Actor *)p)->runtime.state.onStage); }
static void costume(void *p) { CHECK(((Actor *)p)->runtime.life_dirty); }
static void route_services(void *p,void *a,ThreeMMActorRouteEditServices *s)
{
    (void)p; memset(s,0,sizeof(*s)); s->edits.context=a; s->edits.publish=publish_state;
    s->life_dirty=dirty; s->recording=recording; s->hide_body=hide; s->default_costume=costume;
}
static void *route(void *p,void *a) { (void)p; (void)a; return NULL; }
static int seek(void *p,void *a,ThreeMMI32 frame)
{
    Fixture *f=(Fixture *)p; ++f->seeks; f->seek_frame=frame;
    CHECK(!f->copy.runtime.body && !f->copy.runtime.scene);
    ((Actor *)a)->runtime.state.frame=frame; ((Actor *)a)->runtime.state.cel=19;
    return 0; /* Failure is intentionally ignored by undo restoration. */
}
static void views(void *p,void *scene) { Fixture *f=(Fixture *)p; CHECK(scene==f); ++f->views; }
static void init(Fixture *f)
{
    ThreeMMActorOwnerServices *s;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f;
    s->read=read_actor; s->write=write_actor; s->create=create_actor; s->retain=retain; s->release=release;
    s->clone_track=clone_track; s->clone_sounds=clone_sounds; s->swap_track=swap_track; s->swap_sounds=swap_sounds;
    s->duplicate_tag=duplicate_tag; s->close_tag=close_tag; s->close_track_tags=close_track;
    s->clear_track=clear_track; s->dispose_sounds=dispose_sounds; s->route=route; s->route_services=route_services;
    s->seek=seek; s->invalidate_views=views;
    f->result.context=f; f->result.publish=publish_result;
    three_mm_actor_runtime_init(&f->live.runtime);
    CHECK(f->live.runtime.first==2147483647L && f->live.runtime.last==(-2147483647L-1));
    CHECK(f->live.runtime.actor==-1 && f->live.runtime.template_tag.sid==-1 && f->live.runtime.sound_tag.sid==-1);
    f->live.ref.count=f->body.count=f->template_object.count=1; f->live.ref.actor=1;
    f->live.runtime.body=&f->body; f->live.runtime.template_object=&f->template_object;
    f->live.runtime.scene=f; f->live.runtime.template_tag.sid=7; f->tags=1;
    f->live.runtime.time_frozen=1; f->live.runtime.state.frame=23; f->live.runtime.state.onStage=1;
    f->live.runtime.recording.recording=f->live.runtime.recording.rejoining=1;
    f->live.track=11; f->live.sounds=22;
}
int main(void)
{
    Fixture f; int fail,reset;
    for (fail=1;fail<=3;++fail)
    {
        init(&f); f.fail=fail;
        CHECK(!three_mm_actor_owner_duplicate(&f.live,0,&f.result,&f.services));
        CHECK(!f.result.actor && !f.copy.ref.count && f.tags==1 && f.body.count==1 && f.template_object.count==1);
        CHECK(f.tracks==(fail>1) && f.sounds==(fail>2));
        CHECK(f.published==(fail==1?1:2) && f.disposed==(fail>1));
    }
    for (reset=0;reset<=1;++reset)
    {
        init(&f); CHECK(three_mm_actor_owner_duplicate(&f.live,reset,&f.result,&f.services));
        CHECK(f.result.actor==&f.copy && f.template_object.count==2 && f.tags==2);
        CHECK(!f.copy.runtime.time_frozen && f.copy.runtime.recording.rejoining);
        CHECK(f.copy.track==11 && f.copy.sounds==22 && f.body.count==(reset?1:2));
        f.copy.track=33; f.copy.sounds=44; f.copy.runtime.actor=99;
        if (reset)
        {
            CHECK(!f.copy.runtime.body && !f.copy.runtime.scene && !f.copy.runtime.recording.recording);
            CHECK(!f.copy.runtime.state.onStage && f.copy.runtime.state.frame==2147483647L);
            three_mm_actor_owner_restore_undo(&f.live,&f.copy,&f.services);
            CHECK(f.seeks==1 && f.seek_frame==23 && f.live.runtime.state.cel==19 && f.views==2);
            CHECK(!f.copy.runtime.body && !f.copy.runtime.scene);
        }
        else
        {
            three_mm_actor_owner_restore(&f.live,&f.copy,&f.services);
            CHECK(f.views==1 && f.live.runtime.state.frame==23);
        }
        CHECK(f.live.runtime.actor==99 && f.live.track==33 && f.copy.track==11);
        CHECK(f.live.sounds==44 && f.copy.sounds==22);
        release(&f,&f.copy); CHECK(f.body.count==1 && f.template_object.count==1 && f.tags==1);
        release(&f,&f.live); CHECK(!f.body.count && !f.template_object.count && !f.tags && f.disposed==2);
    }
    puts("actor ownership C89 checks passed"); return 0;
}
