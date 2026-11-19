#include "mm/actor_owner.h"
#include "mm/actor_track.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"owner check failed at %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count,actor; } Ref;
typedef struct Actor
{
    Ref ref;
    MMActorRuntime runtime;
    int track,sounds;
} Actor;
typedef struct Fixture
{
    Actor live,copy;
    Ref body,template_object;
    MMActorOwnerServices services;
    MMActorOwnerResult result;
    int fail,created,tracks,sounds,tags,closed,cleared,disposed,published,hides,seeks,views;
    MMI32 seek_frame;
} Fixture;
static MMActorRuntime *runtime(void *p,void *a) { (void)p; return &((Actor *)a)->runtime; }
static void *create_actor(void *p)
{
    Fixture *f=(Fixture *)p; ++f->created;
    if (f->fail==1) return NULL;
    CHECK(!f->copy.ref.count); f->copy.ref.count=1; f->copy.ref.actor=1;
    mm_actor_runtime_init(&f->copy.runtime); return &f->copy;
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
        mm_actor_owner_dispose(a,&f->services);
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
static void duplicate_tag(void *p,const MMTag *tag) { Fixture *f=(Fixture *)p; CHECK(tag->sid==7); ++f->tags; }
static void close_tag(void *p,MMTag *tag)
{ Fixture *f=(Fixture *)p; if (tag->sid==7) { CHECK(f->tags>0); --f->tags; } tag->sid=-1; ++f->closed; }
static void close_track(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(((Actor *)a)->runtime.template_tag.sid==-1); ++f->closed; }
static void clear_track(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(!((Actor *)a)->runtime.body); ((Actor *)a)->track=0; ++f->cleared; }
static void dispose_sounds(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(!((Actor *)a)->runtime.template_object); ((Actor *)a)->sounds=0; ++f->disposed; }
static void publish_result(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(f->result.actor==a); ++f->published; }
static void publish_state(void *p,const MMActorState *s) { ((Actor *)p)->runtime.state=*s; }
static void dirty(void *p) { ((Actor *)p)->runtime.life_dirty=1; }
static void recording(void *p,int enabled) { ((Actor *)p)->runtime.recording.recording=enabled; }
static void hide(void *p) { CHECK(((Actor *)p)->runtime.state.onStage); }
static void costume(void *p) { CHECK(((Actor *)p)->runtime.life_dirty); }
static void route_services(void *p,void *a,MMActorRouteEditServices *s)
{
    (void)p; memset(s,0,sizeof(*s)); s->edits.context=a; s->edits.publish=publish_state;
    s->life_dirty=dirty; s->recording=recording; s->hide_body=hide; s->default_costume=costume;
}
static void *route(void *p,void *a) { (void)p; (void)a; return NULL; }
static int seek(void *p,void *a,MMI32 frame)
{
    Fixture *f=(Fixture *)p; ++f->seeks; f->seek_frame=frame;
    CHECK(!f->copy.runtime.body && !f->copy.runtime.scene);
    ((Actor *)a)->runtime.state.frame=frame; ((Actor *)a)->runtime.state.cel=19;
    return 0; /* Failure is intentionally ignored by undo restoration. */
}
static void views(void *p,void *scene) { Fixture *f=(Fixture *)p; CHECK(scene==f); ++f->views; }
static void init(Fixture *f)
{
    MMActorOwnerServices *s;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f;
    s->runtime=runtime; s->create=create_actor; s->retain=retain; s->release=release;
    s->clone_track=clone_track; s->clone_sounds=clone_sounds; s->swap_track=swap_track; s->swap_sounds=swap_sounds;
    s->duplicate_tag=duplicate_tag; s->close_tag=close_tag; s->close_track_tags=close_track;
    s->clear_track=clear_track; s->dispose_sounds=dispose_sounds; s->route=route; s->route_services=route_services;
    s->seek=seek; s->invalidate_views=views;
    f->result.context=f; f->result.publish=publish_result;
    mm_actor_runtime_init(&f->live.runtime);
    CHECK(f->live.runtime.first==2147483647L && f->live.runtime.last==(-2147483647L-1));
    CHECK(f->live.runtime.actor==-1 && f->live.runtime.template_tag.sid==-1 && f->live.runtime.sound_tag.sid==-1);
    f->live.ref.count=f->body.count=f->template_object.count=1; f->live.ref.actor=1;
    f->live.runtime.body=&f->body; f->live.runtime.template_object=&f->template_object;
    f->live.runtime.scene=f; f->live.runtime.template_tag.sid=7; f->tags=1;
    f->live.runtime.time_frozen=1; f->live.runtime.state.frame=23; f->live.runtime.state.onStage=1;
    f->live.runtime.recording.recording=f->live.runtime.recording.rejoining=1;
    f->live.track=11; f->live.sounds=22;
}

typedef struct Replacement
{
    MMActorRuntime runtime;
    MMActorEventTable *events;
    int old, next, body, old_tag, new_tag, costume_tags;
    int failure, step, removed;
} Replacement;

static void retain_tag(void *p) { ++*(int *)p; }
static void release_tag(void *p) { CHECK(--*(int *)p >= 0); }
static const MMTagServices replacement_tags = {
    NULL, NULL, retain_tag, release_tag, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void *fetch_replacement(void *p, const MMTag *tag)
{
    Replacement *r = (Replacement *)p;
    CHECK(++r->step == 1 && tag->cache == &r->new_tag);
    if (r->failure == 1)
        return NULL;
    ++r->next;
    return &r->next;
}

static void release_replacement(void *p, void *object)
{
    Replacement *r = (Replacement *)p;
    CHECK(r->runtime.template_object == &r->old);
    CHECK((r->failure == 2 && object == &r->next) || (!r->failure && r->step == 4 && object == &r->old));
    release_tag(object);
}

static int is_text_replacement(void *p, void *object)
{
    CHECK(object == &((Replacement *)p)->next);
    return 1;
}

static int adjust_replacement(void *p, void *object, void *body)
{
    Replacement *r = (Replacement *)p;
    CHECK(++r->step == 2 && object == &r->next && body == &r->body);
    if (r->failure == 2)
        return 0;
    r->body = 2;
    return 1;
}

static int select_replacement(void *p, void *object, void *body, MMI32 action, MMI32 cel)
{
    Replacement *r = (Replacement *)p;
    CHECK(++r->step == 3 && object == &r->next && body == &r->body && r->body == 2);
    CHECK(action == 3 && cel == 4);
    return r->failure != 3;
}

static void position_replacement(void *p)
{
    Replacement *r = (Replacement *)p;
    CHECK(++r->step == 4 && r->runtime.template_object == &r->old && r->old == 1);
}

static MMI32 replacement_sets(void *p, void *body)
{
    Replacement *r = (Replacement *)p;
    CHECK(body == &r->body && r->runtime.template_object == &r->next);
    CHECK(!r->old && !r->old_tag && r->new_tag == 2);
    return r->body;
}

static void close_costume(void *p, MMI32 index)
{
    Replacement *r = (Replacement *)p;
    CHECK(index == (r->removed ? 0 : 3));
    mm_actor_track_close_tag(r->events, index, &replacement_tags);
}

static void remove_costume(void *p, MMI32 index)
{
    Replacement *r = (Replacement *)p;
    CHECK(mm_actor_events_delete(r->events, index));
    ++r->removed;
}

static void template_replacement(void)
{
    Replacement r;
    MMActorTemplateServices s = {NULL, &replacement_tags, fetch_replacement, release_replacement,
                                 is_text_replacement, adjust_replacement, select_replacement,
                                 position_replacement, replacement_sets};
    MMActorEditServices edits = {0};
    MMActorEvent event = {0};
    MMActorCostume costume = {0};
    MMTag tag = {0};
    int failure, i;
    s.context = edits.context = &r;
    edits.close_event_tag = close_costume;
    edits.delete_event = remove_costume;
    for (failure = 0; failure <= 3; ++failure)
    {
        memset(&r, 0, sizeof(r));
        mm_actor_runtime_init(&r.runtime);
        r.events = mm_actor_events_create(NULL, 0, 0);
        r.failure = failure;
        r.old = r.old_tag = r.new_tag = 1;
        r.body = 4;
        r.runtime.template_object = &r.old;
        r.runtime.template_tag.sid = 0;
        r.runtime.template_tag.cache = &r.old_tag;
        r.runtime.body = &r.body;
        r.runtime.state.action = 3;
        r.runtime.state.cel = 4;
        r.runtime.state.nextEvent = 4;
        r.runtime.state.actionEvent = 2;
        tag.cache = &r.new_tag;
        costume.tag.cache = &r.costume_tags;
        for (i = 0; i < 4; ++i)
        {
            event.aet = i == 2 ? MM_ACTOR_ACTION : MM_ACTOR_COSTUME;
            costume.body_set = i == 1 ? 1 : 2 + i;
            CHECK(mm_actor_events_insert(r.events, i, i == 2 ? 8 : sizeof(costume), &costume, &event));
            if (i != 2)
                ++r.costume_tags;
        }
        CHECK(mm_actor_replace_template(&r.runtime, r.events, &tag, &s, &edits) == !failure);
        CHECK(r.step == (failure ? failure : 4) && r.removed == (failure ? 0 : 2));
        CHECK(r.body == (!failure || failure == 3 ? 2 : 4));
        CHECK(r.next == (!failure || failure == 3));
        CHECK(r.runtime.template_object == (failure ? &r.old : &r.next));
        CHECK(r.costume_tags == (failure ? 3 : 1));
        CHECK(r.runtime.state.nextEvent == (failure ? 4 : 2));
        CHECK(r.runtime.state.actionEvent == (failure ? 2 : 1));
        mm_tag_close(&r.runtime.template_tag, &replacement_tags);
        for (i = mm_actor_events_count(r.events) - 1; i >= 0; --i)
            mm_actor_track_close_tag(r.events, i, &replacement_tags);
        mm_actor_events_release(r.events);
        CHECK(!r.costume_tags);
    }
}

int main(void)
{
    Fixture f; int fail,reset;
    for (fail=1;fail<=3;++fail)
    {
        init(&f); f.fail=fail;
        CHECK(!mm_actor_owner_duplicate(&f.live,0,&f.result,&f.services));
        CHECK(!f.result.actor && !f.copy.ref.count && f.tags==1 && f.body.count==1 && f.template_object.count==1);
        CHECK(f.tracks==(fail>1) && f.sounds==(fail>2));
        CHECK(f.published==(fail==1?1:2) && f.disposed==(fail>1));
    }
    for (reset=0;reset<=1;++reset)
    {
        init(&f); CHECK(mm_actor_owner_duplicate(&f.live,reset,&f.result,&f.services));
        CHECK(f.result.actor==&f.copy && f.template_object.count==2 && f.tags==2);
        CHECK(!f.copy.runtime.time_frozen && f.copy.runtime.recording.rejoining);
        CHECK(f.copy.track==11 && f.copy.sounds==22 && f.body.count==(reset?1:2));
        f.copy.track=33; f.copy.sounds=44; f.copy.runtime.actor=99;
        if (reset)
        {
            CHECK(!f.copy.runtime.body && !f.copy.runtime.scene && !f.copy.runtime.recording.recording);
            CHECK(!f.copy.runtime.state.onStage && f.copy.runtime.state.frame==2147483647L);
            mm_actor_owner_restore_undo(&f.live,&f.copy,&f.services);
            CHECK(f.seeks==1 && f.seek_frame==23 && f.live.runtime.state.cel==19 && f.views==2);
            CHECK(!f.copy.runtime.body && !f.copy.runtime.scene);
        }
        else
        {
            mm_actor_owner_restore(&f.live,&f.copy,&f.services);
            CHECK(f.views==1 && f.live.runtime.state.frame==23);
        }
        CHECK(f.live.runtime.actor==99 && f.live.track==33 && f.copy.track==11);
        CHECK(f.live.sounds==44 && f.copy.sounds==22);
        release(&f,&f.copy); CHECK(f.body.count==1 && f.template_object.count==1 && f.tags==1);
        release(&f,&f.live); CHECK(!f.body.count && !f.template_object.count && !f.tags && f.disposed==2);
    }
    template_replacement();
    puts("actor ownership C89 checks passed"); return 0;
}
