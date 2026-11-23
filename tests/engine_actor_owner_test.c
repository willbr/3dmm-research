#include "mm/actor_owner.h"
#include "mm/actor_track.h"
#include "mm/actor_group_undo.h"
#include "mm/actor_transform_edit.h"
#include "mm/actor_undo.h"
#include "mm/body_create.h"
#include "mm/text_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"owner check failed at %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count,actor; } Ref;
typedef struct Actor
{
    Ref ref;
    MMActorOwner owner;
} Actor;
typedef struct Fixture
{
    Actor live,copy;
    Ref body,template_object;
    MMActorOwnerServices services;
    MMActorOwnerResult result;
    int fail,created,tracks,sounds,tags,disposed,published,seeks,views,track_blocks,sound_blocks;
    MMI32 seek_frame;
} Fixture;
static MMActorOwner *owner(void *p, void *actor) { (void)p; return &((Actor *)actor)->owner; }

static void *track_resize(void *p, void *ptr, MMU32 old, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)p;
    void *data;
    (void)priority;
    if (f->published)
    {
        CHECK(f->published == 1 && f->result.actor == &f->copy && f->tags == 1);
        f->tracks = 1;
        if (f->fail == 2)
            return NULL;
    }
    data = realloc(ptr, size);
    if (data && !old)
        ++f->track_blocks;
    return data;
}

static void track_release(void *p, void *ptr, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    (void)bytes;
    CHECK(--f->track_blocks >= 0);
    free(ptr);
}

static void *sound_resize(void *p, void *ptr, MMU32 old, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)p;
    void *data;
    (void)priority;
    if (f->published)
    {
        CHECK(f->tracks == 1 && f->copy.owner.track.route && f->copy.owner.track.events);
        f->sounds = 1;
        if (f->fail == 3)
            return NULL;
    }
    data = realloc(ptr, size);
    if (data && !old)
        ++f->sound_blocks;
    return data;
}

static void sound_release(void *p, void *ptr, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    (void)bytes;
    CHECK(--f->sound_blocks >= 0);
    free(ptr);
}

static void init_owner(Actor *actor, Fixture *f)
{
    MMStorageAllocator sounds = {NULL, sound_resize, sound_release};
    sounds.context = f;
    mm_actor_owner_init(&actor->owner);
    CHECK(mm_actor_sounds_init(&actor->owner.sounds, &sounds));
}

static void *create_actor(void *p)
{
    Fixture *f=(Fixture *)p; ++f->created;
    if (f->fail==1) return NULL;
    CHECK(!f->copy.ref.count); f->copy.ref.count=1; f->copy.ref.actor=1;
    init_owner(&f->copy, f);
    return &f->copy;
}

static void retain(void *p,void *a) { (void)p; CHECK(a!=NULL); ++((Ref *)a)->count; }
static void release(void *p,void *a)
{
    Fixture *f=(Fixture *)p; Ref *ref=(Ref *)a; CHECK(ref->count>0);
    if (!--ref->count && ref->actor)
    {
        Actor *actor = (Actor *)a;
        /* The partial duplicate remains visible until disposal has run. */
        if ((f->fail==2 || f->fail==3) && actor == &f->copy)
            CHECK(f->result.actor==a);
        mm_actor_owner_dispose(a,&f->services);
        CHECK(!actor->owner.runtime.body && !actor->owner.runtime.template_object &&
              !actor->owner.track.route && !actor->owner.track.events && !actor->owner.sounds.count);
        ++f->disposed;
    }
}

static void retain_cache(void *p) { ++((Fixture *)p)->tags; }
static void release_cache(void *p) { CHECK(--((Fixture *)p)->tags >= 0); }
static const MMTagServices owner_tags = {
    NULL, retain_cache, release_cache, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static void publish_result(void *p,void *a) { Fixture *f=(Fixture *)p; CHECK(f->result.actor==a); ++f->published; }
static void publish_state(void *p,const MMActorState *s) { ((Actor *)p)->owner.runtime.state=*s; }
static void dirty(void *p) { ((Actor *)p)->owner.runtime.life_dirty=1; }
static void recording(void *p,int enabled) { ((Actor *)p)->owner.runtime.recording.recording=enabled; }
static void hide(void *p) { CHECK(((Actor *)p)->owner.runtime.state.onStage); }
static void costume(void *p) { CHECK(((Actor *)p)->owner.runtime.life_dirty); }
static void route_services(void *p,void *a,MMActorRouteEditServices *s)
{
    (void)p; memset(s,0,sizeof(*s)); s->edits.context=a; s->edits.publish=publish_state;
    s->life_dirty=dirty; s->recording=recording; s->hide_body=hide; s->default_costume=costume;
}
static int fail_costume_reset(void *p)
{
    Fixture *f=(Fixture *)p;
    CHECK(f->views==1 && !f->copy.owner.runtime.body && !f->copy.owner.runtime.scene);
    CHECK(f->live.owner.runtime.state.frame==23);
    return -1;
}
static void seek_failed(void *p,MMI32 frame)
{
    Fixture *f=(Fixture *)p;
    CHECK(f->views==1); ++f->seeks; f->seek_frame=frame;
}
static void views(void *p,void *scene) { Fixture *f=(Fixture *)p; CHECK(scene==f); ++f->views; }
static void init(Fixture *f)
{
    MMActorOwnerServices *s;
    MMStorageAllocator track = {NULL, track_resize, track_release};
    MMActorRoutePoint point = {0};
    MMActorEvent event = {0};
    MMActorSound sound = {0};
    MMI32 payload = 0;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f;
    s->owner=owner; s->create=create_actor; s->retain=retain; s->release=release;
    s->retain_body=retain; s->release_body=release;
    s->retain_template=retain; s->release_template=release;
    s->tags=&owner_tags; s->route_services=route_services; s->invalidate_views=views;
    f->result.context=f; f->result.publish=publish_result;
    init_owner(&f->live, f);
    CHECK(f->live.owner.runtime.first==2147483647L && f->live.owner.runtime.last==(-2147483647L-1));
    CHECK(f->live.owner.runtime.actor==-1 && f->live.owner.runtime.template_tag.sid==-1 && f->live.owner.runtime.sound_tag.sid==-1);
    f->live.ref.count=f->body.count=f->template_object.count=1; f->live.ref.actor=1;
    f->live.owner.runtime.body=&f->body; f->live.owner.runtime.template_object=&f->template_object;
    f->live.owner.runtime.scene=f; f->live.owner.runtime.template_tag.sid=0; f->live.owner.runtime.template_tag.cache=f; f->tags=1;
    f->live.owner.runtime.time_frozen=1; f->live.owner.runtime.state.frame=23; f->live.owner.runtime.state.onStage=1;
    f->live.owner.runtime.recording.recording=f->live.owner.runtime.recording.rejoining=1;
    track.context = f;
    CHECK(mm_actor_track_create(&f->live.owner.track, &track, 0, &owner_tags));
    point.xyz.dxr = 11;
    CHECK(mm_actor_path_insert(f->live.owner.track.route, 0, &point));
    event.aet = MM_ACTOR_STEP;
    CHECK(mm_actor_events_insert(f->live.owner.track.events, 0, sizeof(payload), &payload, &event));
    sound.cel = 22;
    CHECK(mm_actor_sounds_insert(&f->live.owner.sounds, &event, &sound));
}

typedef struct Replacement
{
    MMActorRuntime runtime;
    MMActorEventTable *events;
    MMTemplateOwner *old, *next;
    MMBodyOwner *body;
    int old_tag, new_tag, costume_tags;
    int failure, step, removed;
} Replacement;

static void retain_tag(void *p) { ++*(int *)p; }
static void release_tag(void *p) { CHECK(--*(int *)p >= 0); }
static const MMTagServices replacement_tags = {
    NULL, retain_tag, release_tag, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void *fetch_replacement(void *p, const MMTag *tag)
{
    Replacement *r = (Replacement *)p;
    CHECK(++r->step == 1 && tag->cache == &r->new_tag);
    if (r->failure == 1)
        return NULL;
    mm_template_owner_retain(r->next);
    return r->next;
}

static MMBodyState *replacement_body_state(void *p, void *body)
{
    (void)p;
    return mm_body_owner_state(body);
}

static void replacement_free(void *p, void **memory)
{
    (void)p;
    CHECK(!*memory);
}

static int adjust_replacement(MMTemplateOwner *next, MMBodyOwner *body)
{
    Replacement *r = mm_template_owner_provider(next)->context;
    CHECK(++r->step == 2 && next == r->next && body == r->body);
    if (r->failure == 2)
        return 0;
    mm_body_owner_state(body)->shape.set_count = 2;
    return 1;
}

static int select_replacement(MMTemplateOwner *next, MMBodyOwner *body, MMI32 action, MMI32 cel, void *distance)
{
    Replacement *r = mm_template_owner_provider(next)->context;
    CHECK(++r->step == 3 && next == r->next && body == r->body);
    CHECK(mm_body_owner_state(body)->shape.set_count == 2 && action == 3 && cel == 4 && !distance);
    return r->failure != 3;
}

static void position_replacement(void *p)
{
    Replacement *r = (Replacement *)p;
    CHECK(++r->step == 4 && r->runtime.template_object == r->old);
    CHECK(mm_template_owner_references(r->old) == 2 && r->old_tag == 1 && r->new_tag == 1);
}

static void close_costume(void *p, MMI32 index, const MMTagServices *tags)
{
    Replacement *r = (Replacement *)p;
    (void)tags;
    CHECK(r->runtime.template_object == r->next && mm_template_owner_references(r->old) == 1);
    CHECK(!r->old_tag && r->new_tag == 2);
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
    const MMTemplateOwnerHost host = {NULL, 1, NULL};
    Replacement r;
    MMBodyCreateServices body_services = {0};
    MMActorTemplateServices s = {NULL, &replacement_tags, fetch_replacement, adjust_replacement,
                                 select_replacement, position_replacement};
    MMActorEditServices edits = {0};
    MMActorEvent event = {0};
    MMActorCostume costume = {0};
    MMTag tag = {0};
    int failure, i;
    s.context = edits.context = edits.tables.context = &r;
    body_services.state = replacement_body_state;
    body_services.free_memory = replacement_free;
    edits.tables.close_event_tag = close_costume;
    edits.tables.delete_event = remove_costume;
    for (failure = 0; failure <= 3; ++failure)
    {
        memset(&r, 0, sizeof(r));
        mm_actor_runtime_init(&r.runtime);
        r.events = mm_actor_events_create(NULL, 0, 0);
        r.failure = failure;
        r.old = mm_template_owner_create(&host, 0);
        r.next = mm_template_owner_create(&host, 1);
        r.body = mm_body_owner_create(NULL, &body_services);
        CHECK(r.old && r.next && r.body);
        mm_template_owner_state(r.next)->flags = MM_TEMPLATE_TEXT;
        mm_template_owner_provider(r.next)->context = &r;
        mm_body_owner_state(r.body)->shape.set_count = 4;
        mm_body_owner_state(r.body)->shape.hidden = 1;
        r.old_tag = r.new_tag = 1;
        mm_template_owner_retain(r.old);
        r.runtime.template_object = r.old;
        r.runtime.template_tag.sid = 0;
        r.runtime.template_tag.cache = &r.old_tag;
        r.runtime.body = r.body;
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
        CHECK(mm_body_owner_state(r.body)->shape.set_count == (!failure || failure == 3 ? 2 : 4));
        CHECK(mm_template_owner_references(r.old) == (failure ? 2 : 1));
        CHECK(mm_template_owner_references(r.next) == (!failure || failure == 3 ? 2 : 1));
        CHECK(r.runtime.template_object == (failure ? r.old : r.next));
        CHECK(r.costume_tags == (failure ? 3 : 1));
        CHECK(r.runtime.state.nextEvent == (failure ? 4 : 2));
        CHECK(r.runtime.state.actionEvent == (failure ? 2 : 1));
        mm_tag_close(&r.runtime.template_tag, &replacement_tags);
        mm_template_owner_release(r.runtime.template_object);
        if (failure == 3)
            mm_template_owner_release(r.next);
        mm_template_owner_release(r.next);
        mm_template_owner_release(r.old);
        mm_body_owner_release(r.body);
        for (i = mm_actor_events_count(r.events) - 1; i >= 0; --i)
            mm_actor_track_close_tag(r.events, i, &replacement_tags);
        mm_actor_events_release(r.events);
        CHECK(!r.costume_tags);
    }
}

static void check_storage(const Actor *actor, MMI32 point, MMI32 cel)
{
    MMActorRoutePoint row;
    MMActorMotionSound sound;
    CHECK(mm_actor_path_get(actor->owner.track.route, 0, &row) && row.xyz.dxr == point);
    CHECK(mm_actor_events_count(actor->owner.track.events) == 1);
    CHECK(mm_actor_sounds_get(&actor->owner.sounds, 0, &sound) && sound.sound.cel == cel);
}

typedef struct TimeUndoFixture
{
    Fixture actors;
    MMMovieUndoContext base;
    MMActorUndoState undo;
    char trace[8];
    int count, fault, refs;
} TimeUndoFixture;

static void time_mark(TimeUndoFixture *f, char call)
{
    CHECK(f->count < 7);
    f->trace[f->count++] = call;
    f->trace[f->count] = 0;
}

static MMActorUndoState *time_create(void *p, void **handle)
{
    TimeUndoFixture *f = p;
    time_mark(f, 'u');
    if (f->fault == 1)
        return NULL;
    CHECK(mm_actor_undo_init(&f->undo, &f->base, 1));
    *handle = &f->undo;
    f->refs = 1;
    return &f->undo;
}

static int time_commit(void *p, void *handle)
{
    TimeUndoFixture *f = p;
    Actor *copy = &f->actors.copy;
    CHECK(handle == &f->undo && f->undo.actor == copy && f->undo.actor_id == 25);
    CHECK(f->undo.sooner_later && f->undo.last_frame == -2 && !f->undo.sound && !f->undo.name.count);
    CHECK(copy->ref.count == 1 && !copy->owner.runtime.time_frozen && !copy->owner.runtime.body);
    check_storage(copy, 11, 22);
    time_mark(f, 'a');
    if (f->fault == 3)
        return 0;
    ++f->refs;
    return 1;
}

static void time_release(void *p, void *handle)
{
    TimeUndoFixture *f = p;
    CHECK(handle == &f->undo && f->refs > 0);
    time_mark(f, 'r');
    if (!--f->refs && f->undo.actor)
        release(&f->actors, f->undo.actor);
}

static void time_clear(void *p) { time_mark(p, 'c'); }
static void time_diagnostic(void *p) { time_mark(p, 'd'); }

static void check_time_undo(void)
{
    static const char *const traces[] = {"uar", "ucd", "ucdr", "uacdr"};
    TimeUndoFixture f;
    MMActorDragUndoServices s;
    int fault;
    memset(&s, 0, sizeof(s));
    s.context = &f;
    s.create_actor = time_create;
    s.commit = time_commit;
    s.release = time_release;
    s.clear = time_clear;
    s.not_undoable = time_diagnostic;
    for (fault = 0; fault < 4; ++fault)
    {
        memset(&f, 0, sizeof(f));
        init(&f.actors);
        mm_movie_undo_context_init(&f.base);
        f.actors.live.owner.runtime.actor = 25;
        f.fault = fault;
        f.actors.fail = fault == 2;
        CHECK(mm_actor_owner_freeze_time(&f.actors.live.owner, 0));
        CHECK(!mm_actor_owner_freeze_time(&f.actors.live.owner, 0));
        CHECK(mm_actor_owner_freeze_time(&f.actors.live.owner, 1));
        mm_actor_time_undo_begin(&f.actors.live, -2, &f.actors.services, &s);
        CHECK(!strcmp(traces[fault], f.trace) && f.actors.created == (fault != 1));
        CHECK(f.actors.live.owner.runtime.time_frozen && f.refs == !fault);
        if (!fault)
            time_release(&f, &f.undo);
        CHECK(!f.actors.copy.ref.count);
        release(&f.actors, &f.actors.live);
        CHECK(!f.actors.tags && !f.actors.track_blocks && !f.actors.sound_blocks);
    }
}

int main(void)
{
    Fixture f;
    MMActorRoutePoint row;
    MMActorMotionSound sound;
    int fail,reset;
    for (fail=1;fail<=3;++fail)
    {
        init(&f); f.fail=fail;
        CHECK(!mm_actor_owner_duplicate(&f.live,0,&f.result,&f.services));
        CHECK(!f.result.actor && !f.copy.ref.count && f.tags==1 && f.body.count==1 && f.template_object.count==1);
        CHECK(f.tracks==(fail>1) && f.sounds==(fail==3));
        CHECK(f.published==(fail==1?1:2) && f.disposed==(fail>1));
        check_storage(&f.live, 11, 22);
        release(&f,&f.live);
        CHECK(!f.tags && !f.track_blocks && !f.sound_blocks);
    }
    for (reset=0;reset<=1;++reset)
    {
        init(&f); CHECK(mm_actor_owner_duplicate(&f.live,reset,&f.result,&f.services));
        CHECK(f.result.actor==&f.copy && f.template_object.count==2 && f.tags==2);
        CHECK(!f.copy.owner.runtime.time_frozen && f.copy.owner.runtime.recording.rejoining);
        CHECK(f.body.count==(reset?1:2));
        CHECK(f.copy.owner.track.route != f.live.owner.track.route && f.copy.owner.track.events != f.live.owner.track.events);
        check_storage(&f.copy, 11, 22);
        CHECK(mm_actor_path_get(f.copy.owner.track.route, 0, &row));
        row.xyz.dxr = 33;
        CHECK(mm_actor_path_put(f.copy.owner.track.route, 0, &row));
        CHECK(mm_actor_sounds_get(&f.copy.owner.sounds, 0, &sound));
        sound.sound.cel = 44;
        CHECK(mm_actor_sounds_put(&f.copy.owner.sounds, 0, &sound));
        f.copy.owner.runtime.actor=99;
        if (reset)
        {
            const MMActorTimelineServices timeline=mm_actor_track_timeline_services();
            MMActorPlaybackEffects playback={0};
            playback.context=&f; playback.reset_costume=fail_costume_reset; playback.seek_failed=seek_failed;
            CHECK(!f.copy.owner.runtime.body && !f.copy.owner.runtime.scene && !f.copy.owner.runtime.recording.recording);
            CHECK(!f.copy.owner.runtime.state.onStage && f.copy.owner.runtime.state.frame==2147483647L);
            mm_actor_owner_restore_undo(&f.live,&f.copy,&f.services,&timeline,&playback);
            CHECK(f.seeks==1 && f.seek_frame==23 && f.live.owner.runtime.state.frame==23 && f.views==2);
            CHECK(!f.copy.owner.runtime.body && !f.copy.owner.runtime.scene);
        }
        else
        {
            mm_actor_owner_restore(&f.live,&f.copy,&f.services);
            CHECK(f.views==1 && f.live.owner.runtime.state.frame==23);
        }
        CHECK(f.live.owner.runtime.actor==99);
        check_storage(&f.live, 33, 44);
        check_storage(&f.copy, 11, 22);
        release(&f,&f.copy); CHECK(f.body.count==1 && f.template_object.count==1 && f.tags==1);
        release(&f,&f.live); CHECK(!f.body.count && !f.template_object.count && !f.tags && f.disposed==2);
        CHECK(!f.track_blocks && !f.sound_blocks);
    }
    template_replacement();
    check_time_undo();
    puts("actor ownership C89 checks passed"); return 0;
}
