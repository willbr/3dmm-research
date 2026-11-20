#include "mm/scene_events.h"
#include "scene_member_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene events line %d\n", __LINE__);                                                       \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
enum
{
    STYLE = 1,
    DUP,
    FREE,
    PUBLISH,
    PLAY,
    FETCH,
    CAMERA,
    TRANS,
    SET_TRANS,
    PUB_TAG,
    OBJECT,
    BIND,
    SEEK,
    REASON,
    PAUSE_TYPE,
    DELAY,
    PAUSE,
    EVENT,
    PAYLOAD,
    DIAG,
    RELEASE,
    PICK,
    PICK_COUNT,
    PICK_ACTOR,
    PICK_OWNER,
    PICK_ORPHAN
};
typedef struct Object
{
    int id, bound, seeks;
} Object;
typedef struct Payload
{
    MMI32 camera;
    int style, reason;
    MMI32 delay;
    Object *object;
    MMTag tag;
} Payload;
typedef struct Event
{
    MMI32 frame;
    int kind;
    Payload payload;
} Event;
typedef struct Group
{
    int count;
    Event events[8];
} Group;
typedef struct Fixture
{
    MMSceneState data;
    MMSceneRuntime runtime;
    MMSceneOwner owner;
    SceneMemberMemory members;
    MMSceneEventServices s;
    Group frames, starts;
    Object objects[4];
    Payload packet, *active;
    MMTag published;
    int ops[128], used, issues[5], live, dup_fail, fetch_fail, camera_fail;
    int seek_fail, transition, camera, releases, sounds, payloads, last_payload, mutate_delay;
    MMI32 published_frame;
    void *clicked;
    int hit_part, orphan_count;
} Fixture;
static void log_op(Fixture *f, int op)
{
    CHECK(f->used < 128);
    f->ops[f->used++] = op;
}
static void expect(Fixture *f, const int *ops, size_t count)
{
    CHECK(f->used == (int)count);
    CHECK(!memcmp(f->ops, ops, count * sizeof(*ops)));
    f->used = 0;
}
static MMI32 count(void *p, void *group)
{
    (void)p;
    return ((Group *)group)->count;
}
static void publish(void *p, void *scene, const MMSceneOwner *owner)
{
    Fixture *f = (Fixture *)p;
    CHECK(scene == f && owner == &f->owner);
    f->published_frame = f->runtime.background_frame;
    log_op(f, PUBLISH);
}
static void release(void *p, void *object)
{
    Fixture *f = (Fixture *)p;
    CHECK(object == &f->objects[3]);
    ++f->releases;
    log_op(f, RELEASE);
}
static MMI32 sound_count(void *p, void *packet)
{
    (void)p;
    (void)packet;
    return 0;
}
static int sound_style(void *p, void *packet)
{
    log_op((Fixture *)p, STYLE);
    return ((Payload *)packet)->style;
}
static MMI32 owned_style(void *p, void *packet)
{
    (void)p;
    return ((Payload *)packet)->style;
}
static void free_sound(void *p, void *packet)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->owner.owned[MM_SCENE_BACKGROUND_SOUND] == packet && f->live > 0);
    --f->live;
    log_op(f, FREE);
    free(packet);
}
static void *duplicate(void *p, void *packet)
{
    Fixture *f = (Fixture *)p;
    Payload *copy;
    log_op(f, DUP);
    if (f->dup_fail)
        return NULL;
    copy = (Payload *)malloc(sizeof(*copy));
    CHECK(copy);
    *copy = *(Payload *)packet;
    ++f->live;
    return copy;
}
static void play(void *p, void *packet)
{
    Fixture *f = (Fixture *)p;
    CHECK(packet);
    ++f->sounds;
    log_op(f, PLAY);
}
static void *fetch(void *p, MMTag *tag)
{
    Fixture *f = (Fixture *)p;
    log_op(f, FETCH);
    CHECK(tag->sid == 3 && tag->cache == f && tag->type == 7 && tag->number == 9);
    tag->number = 17;
    return f->fetch_fail ? NULL : &f->objects[3];
}
static int camera(void *p, void *background, MMI32 camera)
{
    Fixture *f = (Fixture *)p;
    CHECK(background == &f->objects[3]);
    f->camera = camera;
    log_op(f, CAMERA);
    return !f->camera_fail;
}
static int transition(void *p)
{
    Fixture *f = (Fixture *)p;
    log_op(f, TRANS);
    return f->transition;
}
static void set_transition(void *p, int value)
{
    Fixture *f = (Fixture *)p;
    log_op(f, SET_TRANS);
    f->transition = value;
}
static void publish_tag(void *p, const MMTag *tag)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->owner.owned[MM_SCENE_BACKGROUND] == &f->objects[3]);
    f->published = *tag;
    log_op(f, PUB_TAG);
}
static void *read_object(void *p, void *packet)
{
    log_op((Fixture *)p, OBJECT);
    return ((Payload *)packet)->object;
}
static void bind(void *p, void *object)
{
    ((Object *)object)->bound = 1;
    log_op((Fixture *)p, BIND);
}
static int seek(void *p, int text, void *object, MMI32 frame)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)object;
    CHECK(frame == f->data.currentFrame && (text || o->bound));
    ++o->seeks;
    log_op(f, SEEK);
    return f->seek_fail != o->id;
}
static int reason(void *p, void *packet)
{
    Fixture *f = (Fixture *)p;
    f->active = (Payload *)packet;
    log_op(f, REASON);
    return f->active->reason;
}
static MMI32 delay(void *p, void *packet)
{
    log_op((Fixture *)p, DELAY);
    return ((Payload *)packet)->delay;
}
static void pause_type(void *p, int reason)
{
    Fixture *f = (Fixture *)p;
    CHECK(reason == f->active->reason);
    log_op(f, PAUSE_TYPE);
    if (f->mutate_delay)
        f->active->delay = 42;
}
static void pause_movie(void *p, int reason, MMI32 delay)
{
    Fixture *f = (Fixture *)p;
    CHECK(reason == f->active->reason && delay == f->active->delay);
    log_op(f, PAUSE);
}
static MMI32 read_camera(void *p, void *packet)
{
    MMI32 value;
    log_op((Fixture *)p, OBJECT);
    memcpy(&value, packet, sizeof(value));
    return value;
}
static void diagnostic(void *p, int issue)
{
    Fixture *f = (Fixture *)p;
    CHECK(issue >= 0 && issue < 5);
    ++f->issues[issue];
    log_op(f, DIAG);
}
static void event(void *p, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    Fixture *f = (Fixture *)p;
    Group *g = (Group *)group;
    CHECK(index >= 0 && index < g->count);
    log_op(f, EVENT);
    *kind = g->events[index].kind;
    if (g != &f->starts || *kind == MM_SCENE_PLAY_SOUND)
        *frame = g->events[index].frame;
}
static void *payload(void *p, void *group, MMI32 index)
{
    Fixture *f = (Fixture *)p;
    Group *g = (Group *)group;
    CHECK(index >= 0 && index < g->count);
    ++f->payloads;
    f->last_payload = index;
    log_op(f, PAYLOAD);
    return g->events[index].kind == MM_SCENE_SET_BACKGROUND ? (void *)&g->events[index].payload.tag
                                                           : (void *)&g->events[index].payload;
}
static void init(Fixture *f)
{
    int i;
    MMSceneEventServices *s;
    memset(f, 0, sizeof(*f));
    mm_scene_runtime_init(&f->runtime);
    init_scene_members(&f->owner, &f->members);
    f->data.currentFrame = 3;
    f->transition = -1;
    f->packet.style = 4;
    f->packet.tag.sid = 3;
    f->packet.tag.cache = f;
    f->packet.tag.type = 7;
    f->packet.tag.number = 9;
    for (i = 0; i < 4; ++i)
        f->objects[i].id = i + 1;
    f->owner.owned[MM_SCENE_FRAMES] = &f->frames;
    f->owner.owned[MM_SCENE_STARTS] = &f->starts;
    s = &f->s;
    s->context = s->scene = f;
    s->data = &f->data;
    s->runtime = &f->runtime;
    s->owner = &f->owner;
    s->owners.context = f;
    s->owners.publish = publish;
    s->owners.release = release;
    s->owners.count = count;
    s->owners.sound.context = f;
    s->owners.sound.count = sound_count;
    s->owners.sound.style = owned_style;
    s->owners.sound.free = free_sound;
    s->event = event;
    s->payload = payload;
    s->sound_style = sound_style;
    s->duplicate_sound = duplicate;
    s->play_sound = play;
    s->fetch_background = fetch;
    s->set_camera = camera;
    s->transition = transition;
    s->set_transition = set_transition;
    s->publish_tag = publish_tag;
    s->read_object = read_object;
    s->bind_actor = bind;
    s->seek = seek;
    s->pause_reason = reason;
    s->pause_delay = delay;
    s->pause_type = pause_type;
    s->pause = pause_movie;
    s->camera = read_camera;
    s->diagnostic = diagnostic;
}
static int dispatch(Fixture *f, int kind, unsigned mask)
{
    void *bytes = kind == MM_SCENE_SET_BACKGROUND ? (void *)&f->packet.tag : (void *)&f->packet;
    return mm_scene_play_event(8, kind, bytes, mask, &f->s);
}
static void cleanup(Fixture *f)
{
    mm_scene_owner_drop(&f->owner, f, MM_SCENE_BACKGROUND_SOUND, &f->s.owners);
    CHECK(!f->live);
}
static void sounds(void)
{
    Fixture f;
    void *old;
    static const int first[] = {STYLE, DUP, PUBLISH}, failed[] = {STYLE, DUP};
    static const int replaced[] = {STYLE, DUP, FREE, PUBLISH, PUBLISH, PLAY}, skipped[] = {STYLE},
                     played[] = {STYLE, PLAY};
    init(&f);
    f.runtime.background_frame = 2;
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 1));
    expect(&f, first, 3);
    CHECK(f.live == 1 && f.runtime.background_frame == 8 && f.published_frame == 2 && !f.sounds);
    old = f.owner.owned[MM_SCENE_BACKGROUND_SOUND];
    f.dup_fail = 1;
    CHECK(!mm_scene_play_event(9, MM_SCENE_PLAY_SOUND, &f.packet, 0, &f.s));
    expect(&f, failed, 2);
    CHECK(f.owner.owned[MM_SCENE_BACKGROUND_SOUND] == old && f.runtime.background_frame == 8);
    f.dup_fail = 0;
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 0));
    expect(&f, replaced, 6);
    CHECK(f.live == 1 && f.sounds == 1);
    f.packet.style = 2;
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 1));
    expect(&f, skipped, 1);
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 0));
    expect(&f, played, 2);
    CHECK(f.live == 1);
    cleanup(&f);
}
static void backgrounds(void)
{
    Fixture f;
    static const int no_fetch[] = {FETCH}, no_camera[] = {FETCH, CAMERA, RELEASE};
    static const int loaded[] = {FETCH, CAMERA, TRANS, SET_TRANS, PUBLISH, PUB_TAG};
    static const int retained[] = {FETCH, CAMERA, TRANS, PUBLISH, PUB_TAG};
    init(&f);
    f.fetch_fail = 1;
    CHECK(!dispatch(&f, MM_SCENE_SET_BACKGROUND, 65535));
    expect(&f, no_fetch, sizeof(no_fetch) / sizeof(*no_fetch));
    CHECK(!f.owner.owned[MM_SCENE_BACKGROUND] && f.transition == -1);
    f.fetch_fail = 0;
    f.camera_fail = 1;
    CHECK(!dispatch(&f, MM_SCENE_SET_BACKGROUND, 0));
    expect(&f, no_camera, sizeof(no_camera) / sizeof(*no_camera));
    CHECK(f.releases == 1 && !f.owner.owned[MM_SCENE_BACKGROUND] && f.transition == -1);
    f.camera_fail = 0;
    CHECK(dispatch(&f, MM_SCENE_SET_BACKGROUND, 65535));
    expect(&f, loaded, sizeof(loaded) / sizeof(*loaded));
    CHECK(f.transition == 0 && f.camera == 0 && f.published.number == 17 && f.published.cache == &f &&
          f.published.sid == 3);
    init(&f);
    f.transition = 3;
    CHECK(dispatch(&f, MM_SCENE_SET_BACKGROUND, 0));
    expect(&f, retained, sizeof(retained) / sizeof(*retained));
    CHECK(f.transition == 3);
}
static void objects(void)
{
    Fixture f;
    int text, list;
    static const int failed[] = {OBJECT}, bound[] = {OBJECT, BIND, SEEK};
    static const int caption[] = {OBJECT, SEEK};
    for (text = 0; text < 2; ++text)
    {
        list = text ? MM_SCENE_TEXTS : MM_SCENE_ACTORS;
        init(&f);
        f.packet.object = &f.objects[0];
        f.members.fail_at = 1;
        CHECK(!dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 65535));
        expect(&f, failed, 1);
        CHECK(!mm_scene_member_count(&f.owner, list) && !f.objects[0].bound && !f.objects[0].seeks);
        f.members.fail_at = 0;
        f.seek_fail = 1;
        CHECK(!dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 0));
        expect(&f, text ? caption : bound, text ? 2 : 3);
        CHECK(!mm_scene_member_count(&f.owner, list) && f.objects[0].bound == !text);
        f.seek_fail = 0;
        CHECK(dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 65535));
        expect(&f, text ? caption : bound, text ? 2 : 3);
        CHECK(mm_scene_member_count(&f.owner, list) == 1 && mm_scene_member_at(&f.owner, list, 0) == &f.objects[0]);
        f.packet.object = &f.objects[1];
        CHECK(dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 0));
        CHECK(mm_scene_member_count(&f.owner, list) == 2 && mm_scene_member_at(&f.owner, list, 1) == &f.objects[1]);
    }
}
static void pauses_and_cameras(void)
{
    Fixture f;
    static const int suppressed[] = {REASON, PAUSE_TYPE}, paused[] = {REASON, PAUSE_TYPE, DELAY, PAUSE};
    static const int absent[] = {DIAG}, changed[] = {OBJECT, CAMERA};
    init(&f);
    f.packet.reason = 3;
    f.packet.delay = (-2147483647 - 1);
    f.mutate_delay = 1;
    CHECK(dispatch(&f, MM_SCENE_PAUSE, 2));
    expect(&f, suppressed, 2);
    CHECK(dispatch(&f, MM_SCENE_PAUSE, 0));
    expect(&f, paused, 4);
    CHECK(f.packet.delay == 42);
    CHECK(dispatch(&f, MM_SCENE_CHANGE_CAMERA, 64));
    expect(&f, absent, 1);
    CHECK(f.issues[MM_SCENE_EVENT_NO_BACKGROUND] == 1);
    f.owner.owned[MM_SCENE_BACKGROUND] = &f.objects[3];
    f.packet.camera = 2;
    CHECK(dispatch(&f, MM_SCENE_CHANGE_CAMERA, 0));
    expect(&f, changed, 2);
    CHECK(f.camera == 2);
    f.camera_fail = 1;
    CHECK(!dispatch(&f, MM_SCENE_CHANGE_CAMERA, 0));
    expect(&f, changed, 2);
    CHECK(dispatch(&f, 99, 0));
    CHECK(f.issues[MM_SCENE_EVENT_UNKNOWN] == 1);
    f.used = 0;
    CHECK(mm_scene_unplay_event(99, &f.s) && !f.used);
}
static void rewind_events(void)
{
    Fixture f;
    int i;
    init(&f);
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 1));
    f.used = 0;
    f.frames.count = 4;
    f.runtime.next_event = 4;
    for (i = 0; i < 4; ++i)
    {
        f.frames.events[i].kind = MM_SCENE_PLAY_SOUND;
        f.frames.events[i].frame = i + 1;
        f.frames.events[i].payload.style = 2;
    }
    CHECK(mm_scene_unplay_event(MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(f.last_payload == 1 && f.payloads == 1 && f.live == 1 && !f.sounds);
    f.frames.events[1].payload.style = 4;
    f.dup_fail = 1;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(f.live == 1 && f.runtime.background_frame == 8);
    f.data.currentFrame = 1;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(!f.live && f.runtime.background_frame == 8);
    for (i = 0; i < 4; ++i)
    {
        f.frames.events[i].kind = MM_SCENE_CHANGE_CAMERA;
        f.frames.events[i].payload.camera = i + 1;
    }
    f.frames.events[0].frame = 2;
    f.frames.events[3].frame = 1;
    f.runtime.next_event = 3;
    f.owner.owned[MM_SCENE_BACKGROUND] = &f.objects[3];
    f.data.currentFrame = 3;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.camera == 2 && f.last_payload == 1);
    f.camera_fail = 1;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.issues[MM_SCENE_EVENT_RESTORE_FAILED] == 1);
    f.data.currentFrame = 1;
    f.used = 0;
    CHECK(!mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.camera == 0 && f.issues[MM_SCENE_EVENT_FIRST_CAMERA_FAILED] == 1);
    f.runtime.disabled = 64;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.used == f.runtime.next_event);
    f.owner.owned[MM_SCENE_BACKGROUND] = NULL;
    f.runtime.next_event = 0;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.used == 2 && f.issues[MM_SCENE_EVENT_NO_BACKGROUND] == 2);
    cleanup(&f);
}
static void starts(void)
{
    Fixture f;
    int i;
    init(&f);
    f.starts.count = 3;
    f.runtime.disabled = 65535;
    f.starts.events[0].kind = MM_SCENE_SET_BACKGROUND;
    f.starts.events[0].payload = f.packet;
    f.starts.events[1].kind = MM_SCENE_ADD_ACTOR;
    f.starts.events[1].payload.object = &f.objects[0];
    f.starts.events[2].kind = MM_SCENE_ADD_TEXT;
    f.starts.events[2].payload.object = &f.objects[1];
    CHECK(mm_scene_start_events(1, &f.s));
    CHECK(f.payloads == 1 && f.last_payload == 1 && mm_scene_member_count(&f.owner, MM_SCENE_ACTORS) == 1 &&
          !mm_scene_member_count(&f.owner, MM_SCENE_TEXTS) && f.transition == -1);
    init(&f);
    f.starts.count = 3;
    for (i = 0; i < 3; ++i)
    {
        f.starts.events[i].kind = MM_SCENE_ADD_ACTOR;
        f.starts.events[i].payload.object = &f.objects[i];
    }
    f.seek_fail = 2;
    CHECK(!mm_scene_start_events(0, &f.s));
    CHECK(f.payloads == 2 && mm_scene_member_count(&f.owner, MM_SCENE_ACTORS) == 1 && f.objects[1].bound &&
          !f.objects[2].bound);
    init(&f);
    f.starts.count = 3;
    f.starts.events[0].kind = MM_SCENE_SET_BACKGROUND;
    f.starts.events[0].payload = f.packet;
    f.starts.events[1].kind = MM_SCENE_ADD_ACTOR;
    f.starts.events[1].payload.object = &f.objects[0];
    f.starts.events[2].kind = MM_SCENE_ADD_TEXT;
    f.starts.events[2].payload.object = &f.objects[1];
    CHECK(mm_scene_start_events(0, &f.s));
    CHECK(f.payloads == 3 && mm_scene_member_count(&f.owner, MM_SCENE_ACTORS) == 1 &&
          mm_scene_member_count(&f.owner, MM_SCENE_TEXTS) == 1 && f.transition == 0);
}
static void background_query(void)
{
    Fixture f;
    MMTag out;
    const int found[] = {EVENT, EVENT, PAYLOAD};
    init(&f);
    out.sid = 99;
    out.cache = &f.objects[3];
    out.type = 12;
    out.number = 34;
    f.owner.owned[MM_SCENE_BACKGROUND] = &f.objects[0];
    CHECK(!mm_scene_background_tag(&out, &f.s));
    CHECK(!f.used && out.sid == 99 && out.cache == &f.objects[3] && out.type == 12 && out.number == 34);
    f.starts.count = 3;
    f.starts.events[0].kind = MM_SCENE_ADD_ACTOR;
    f.starts.events[1].kind = f.starts.events[2].kind = MM_SCENE_SET_BACKGROUND;
    f.starts.events[1].payload.tag = f.packet.tag;
    f.starts.events[2].payload.tag = f.packet.tag;
    f.starts.events[2].payload.tag.number = 42;
    CHECK(mm_scene_background_tag(&out, &f.s));
    CHECK(out.sid == f.packet.tag.sid && out.cache == f.packet.tag.cache && out.type == f.packet.tag.type &&
          out.number == f.packet.tag.number);
    expect(&f, found, sizeof(found) / sizeof(*found));
    CHECK(!f.releases && !f.live && f.payloads == 1 && f.last_payload == 1);
    f.starts.count = 1;
    CHECK(!mm_scene_background_tag(&out, &f.s));
    CHECK(out.number == f.packet.tag.number && f.used == 1 && f.ops[0] == EVENT);
}
static void *pick_body(void *p, MMI32 x, MMI32 y)
{
    Fixture *f = (Fixture *)p;
    CHECK(x == -7 && y == 13);
    f->hit_part = 41;
    log_op(f, PICK);
    return f->clicked;
}
static MMI32 pick_count(void *p)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->hit_part == 41);
    log_op(f, PICK_COUNT);
    return mm_scene_member_count(&f->owner, MM_SCENE_ACTORS);
}
static void *pick_actor(void *p, MMI32 i)
{
    Fixture *f = (Fixture *)p;
    CHECK(i >= 0 && i < mm_scene_member_count(&f->owner, MM_SCENE_ACTORS));
    log_op(f, PICK_ACTOR);
    return mm_scene_member_at(&f->owner, MM_SCENE_ACTORS, i);
}
static int pick_owner(void *p, void *actor, void *body)
{
    Fixture *f = (Fixture *)p;
    CHECK(body == f->clicked);
    log_op(f, PICK_OWNER);
    return actor == body;
}
static void pick_orphan(void *p)
{
    Fixture *f = (Fixture *)p;
    ++f->orphan_count;
    log_op(f, PICK_ORPHAN);
}
static void picking(void)
{
    Fixture f;
    MMScenePickServices s;
    int i;
    init(&f);
    s.context = &f;
    s.pick = pick_body;
    s.count = pick_count;
    s.actor = pick_actor;
    s.owns_body = pick_owner;
    s.orphan_body = pick_orphan;
    CHECK(!mm_scene_pick_actor(-7, 13, &s) && f.hit_part == 41 && !f.orphan_count);
    {
        const int e[] = {PICK};
        expect(&f, e, 1);
    }
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.objects[i]));
    f.clicked = &f.objects[1];
    CHECK(mm_scene_pick_actor(-7, 13, &s) == &f.objects[1]);
    {
        const int e[] = {PICK, PICK_COUNT, PICK_ACTOR, PICK_OWNER, PICK_COUNT, PICK_ACTOR, PICK_OWNER};
        expect(&f, e, 7);
    }
    f.clicked = &f.objects[3];
    CHECK(!mm_scene_pick_actor(-7, 13, &s) && f.orphan_count == 1 && f.hit_part == 41);
    CHECK(f.used == 12 && f.ops[f.used - 1] == PICK_ORPHAN && !f.releases && !f.live);
    f.used = 0;
    mm_scene_member_clear(&f.owner, MM_SCENE_ACTORS);
    CHECK(!mm_scene_pick_actor(-7, 13, &s) && f.orphan_count == 2);
    {
        const int e[] = {PICK, PICK_COUNT, PICK_ORPHAN};
        expect(&f, e, 3);
    }
}
int main(void)
{
    sounds();
    backgrounds();
    objects();
    pauses_and_cameras();
    rewind_events();
    starts();
    background_query();
    picking();
    puts("C89 Scene event dispatch, ownership, suppression, start prefixes and rewind failures passed.");
    return 0;
}
