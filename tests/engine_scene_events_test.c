#include "three_mm/scene_events.h"
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
    TAG,
    FETCH,
    CAMERA,
    TRANS,
    SET_TRANS,
    PUB_TAG,
    OBJECT,
    APPEND,
    BIND,
    SEEK,
    REMOVE,
    REASON,
    PAUSE_TYPE,
    DELAY,
    PAUSE,
    EVENT,
    PAYLOAD,
    DIAG,
    RELEASE
};
typedef struct Object
{
    int id, bound, seeks;
} Object;
typedef struct Payload
{
    ThreeMMI32 camera;
    int style, reason;
    ThreeMMI32 delay;
    Object *object;
    ThreeMMTag tag;
} Payload;
typedef struct Event
{
    ThreeMMI32 frame;
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
    ThreeMMSceneState data;
    ThreeMMSceneRuntime runtime;
    ThreeMMSceneOwner owner;
    ThreeMMSceneEventServices s;
    Group frames, starts;
    Object objects[4], *lists[2][8];
    Payload packet, *active;
    ThreeMMTag published;
    int counts[2], ops[128], used, issues[5], live, dup_fail, fetch_fail, camera_fail;
    int append_fail, seek_fail, transition, camera, releases, sounds, payloads, last_payload, mutate_delay;
    ThreeMMI32 published_frame;
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
static ThreeMMI32 count(void *p, void *group)
{
    (void)p;
    return ((Group *)group)->count;
}
static void publish(void *p, void *scene, const ThreeMMSceneOwner *owner)
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
static ThreeMMI32 sound_count(void *p, void *packet)
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
static ThreeMMI32 owned_style(void *p, void *packet)
{
    (void)p;
    return ((Payload *)packet)->style;
}
static void free_sound(void *p, void *packet)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->owner.owned[THREE_MM_SCENE_BACKGROUND_SOUND] == packet && f->live > 0);
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
static void read_tag(void *p, void *packet, ThreeMMTag *tag)
{
    log_op((Fixture *)p, TAG);
    *tag = ((Payload *)packet)->tag;
}
static void *fetch(void *p, ThreeMMTag *tag)
{
    Fixture *f = (Fixture *)p;
    log_op(f, FETCH);
    CHECK(tag->sid == 3 && tag->cache == f && tag->type == 7 && tag->number == 9);
    tag->number = 17;
    return f->fetch_fail ? NULL : &f->objects[3];
}
static int camera(void *p, void *background, ThreeMMI32 camera)
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
static void publish_tag(void *p, const ThreeMMTag *tag)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->owner.owned[THREE_MM_SCENE_BACKGROUND] == &f->objects[3]);
    f->published = *tag;
    log_op(f, PUB_TAG);
}
static void *read_object(void *p, void *packet)
{
    log_op((Fixture *)p, OBJECT);
    return ((Payload *)packet)->object;
}
static int append(void *p, int text, void *object)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)object;
    log_op(f, APPEND);
    if (f->append_fail == o->id)
        return 0;
    CHECK(f->counts[text] < 8);
    f->lists[text][f->counts[text]++] = o;
    return 1;
}
static void remove_last(void *p, int text)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->counts[text] > 0);
    --f->counts[text];
    log_op(f, REMOVE);
}
static void bind(void *p, void *object)
{
    ((Object *)object)->bound = 1;
    log_op((Fixture *)p, BIND);
}
static int seek(void *p, int text, void *object, ThreeMMI32 frame)
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
static ThreeMMI32 delay(void *p, void *packet)
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
static void pause_movie(void *p, int reason, ThreeMMI32 delay)
{
    Fixture *f = (Fixture *)p;
    CHECK(reason == f->active->reason && delay == f->active->delay);
    log_op(f, PAUSE);
}
static ThreeMMI32 read_camera(void *p, void *packet)
{
    ThreeMMI32 value;
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
static void event(void *p, void *group, ThreeMMI32 index, ThreeMMI32 *frame, int *kind)
{
    Fixture *f = (Fixture *)p;
    Group *g = (Group *)group;
    CHECK(index >= 0 && index < g->count);
    log_op(f, EVENT);
    *kind = g->events[index].kind;
    if (g != &f->starts || *kind == THREE_MM_SCENE_PLAY_SOUND)
        *frame = g->events[index].frame;
}
static void *payload(void *p, void *group, ThreeMMI32 index)
{
    Fixture *f = (Fixture *)p;
    Group *g = (Group *)group;
    CHECK(index >= 0 && index < g->count);
    ++f->payloads;
    f->last_payload = index;
    log_op(f, PAYLOAD);
    return &g->events[index].payload;
}
static void init(Fixture *f)
{
    int i;
    ThreeMMSceneEventServices *s;
    memset(f, 0, sizeof(*f));
    three_mm_scene_runtime_init(&f->runtime);
    three_mm_scene_owner_init(&f->owner, NULL);
    f->data.currentFrame = 3;
    f->transition = -1;
    f->packet.style = 4;
    f->packet.tag.sid = 3;
    f->packet.tag.cache = f;
    f->packet.tag.type = 7;
    f->packet.tag.number = 9;
    for (i = 0; i < 4; ++i)
        f->objects[i].id = i + 1;
    f->owner.owned[THREE_MM_SCENE_FRAMES] = &f->frames;
    f->owner.owned[THREE_MM_SCENE_STARTS] = &f->starts;
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
    s->read_tag = read_tag;
    s->fetch_background = fetch;
    s->set_camera = camera;
    s->transition = transition;
    s->set_transition = set_transition;
    s->publish_tag = publish_tag;
    s->read_object = read_object;
    s->append = append;
    s->remove_last = remove_last;
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
    return three_mm_scene_play_event(8, kind, &f->packet, mask, &f->s);
}
static void cleanup(Fixture *f)
{
    three_mm_scene_owner_drop(&f->owner, f, THREE_MM_SCENE_BACKGROUND_SOUND, &f->s.owners);
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
    CHECK(dispatch(&f, THREE_MM_SCENE_PLAY_SOUND, 1));
    expect(&f, first, 3);
    CHECK(f.live == 1 && f.runtime.background_frame == 8 && f.published_frame == 2 && !f.sounds);
    old = f.owner.owned[THREE_MM_SCENE_BACKGROUND_SOUND];
    f.dup_fail = 1;
    CHECK(!three_mm_scene_play_event(9, THREE_MM_SCENE_PLAY_SOUND, &f.packet, 0, &f.s));
    expect(&f, failed, 2);
    CHECK(f.owner.owned[THREE_MM_SCENE_BACKGROUND_SOUND] == old && f.runtime.background_frame == 8);
    f.dup_fail = 0;
    CHECK(dispatch(&f, THREE_MM_SCENE_PLAY_SOUND, 0));
    expect(&f, replaced, 6);
    CHECK(f.live == 1 && f.sounds == 1);
    f.packet.style = 2;
    CHECK(dispatch(&f, THREE_MM_SCENE_PLAY_SOUND, 1));
    expect(&f, skipped, 1);
    CHECK(dispatch(&f, THREE_MM_SCENE_PLAY_SOUND, 0));
    expect(&f, played, 2);
    CHECK(f.live == 1);
    cleanup(&f);
}
static void backgrounds(void)
{
    Fixture f;
    static const int no_fetch[] = {TAG, FETCH}, no_camera[] = {TAG, FETCH, CAMERA, RELEASE};
    static const int loaded[] = {TAG, FETCH, CAMERA, TRANS, SET_TRANS, PUBLISH, PUB_TAG};
    static const int retained[] = {TAG, FETCH, CAMERA, TRANS, PUBLISH, PUB_TAG};
    init(&f);
    f.fetch_fail = 1;
    CHECK(!dispatch(&f, THREE_MM_SCENE_SET_BACKGROUND, 65535));
    expect(&f, no_fetch, 2);
    CHECK(!f.owner.owned[THREE_MM_SCENE_BACKGROUND] && f.transition == -1);
    f.fetch_fail = 0;
    f.camera_fail = 1;
    CHECK(!dispatch(&f, THREE_MM_SCENE_SET_BACKGROUND, 0));
    expect(&f, no_camera, 4);
    CHECK(f.releases == 1 && !f.owner.owned[THREE_MM_SCENE_BACKGROUND] && f.transition == -1);
    f.camera_fail = 0;
    CHECK(dispatch(&f, THREE_MM_SCENE_SET_BACKGROUND, 65535));
    expect(&f, loaded, 7);
    CHECK(f.transition == 0 && f.camera == 0 && f.published.number == 17 && f.published.cache == &f &&
          f.published.sid == 3);
    init(&f);
    f.transition = 3;
    CHECK(dispatch(&f, THREE_MM_SCENE_SET_BACKGROUND, 0));
    expect(&f, retained, 6);
    CHECK(f.transition == 3);
}
static void objects(void)
{
    Fixture f;
    int text;
    static const int failed[] = {OBJECT, APPEND}, bound[] = {OBJECT, APPEND, BIND, SEEK, REMOVE};
    static const int text_failed[] = {OBJECT, APPEND, SEEK, REMOVE}, actor[] = {OBJECT, APPEND, BIND, SEEK},
                     caption[] = {OBJECT, APPEND, SEEK};
    for (text = 0; text < 2; ++text)
    {
        init(&f);
        f.packet.object = &f.objects[0];
        f.append_fail = 1;
        CHECK(!dispatch(&f, text ? THREE_MM_SCENE_ADD_TEXT : THREE_MM_SCENE_ADD_ACTOR, 65535));
        expect(&f, failed, 2);
        CHECK(!f.counts[text] && !f.objects[0].bound && !f.objects[0].seeks);
        f.append_fail = 0;
        f.seek_fail = 1;
        CHECK(!dispatch(&f, text ? THREE_MM_SCENE_ADD_TEXT : THREE_MM_SCENE_ADD_ACTOR, 0));
        expect(&f, text ? text_failed : bound, text ? 4 : 5);
        CHECK(!f.counts[text] && f.objects[0].bound == !text);
        f.seek_fail = 0;
        CHECK(dispatch(&f, text ? THREE_MM_SCENE_ADD_TEXT : THREE_MM_SCENE_ADD_ACTOR, 65535));
        expect(&f, text ? caption : actor, text ? 3 : 4);
        CHECK(f.counts[text] == 1 && f.lists[text][0] == &f.objects[0]);
        f.packet.object = &f.objects[1];
        CHECK(dispatch(&f, text ? THREE_MM_SCENE_ADD_TEXT : THREE_MM_SCENE_ADD_ACTOR, 0));
        CHECK(f.counts[text] == 2 && f.lists[text][1] == &f.objects[1]);
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
    CHECK(dispatch(&f, THREE_MM_SCENE_PAUSE, 2));
    expect(&f, suppressed, 2);
    CHECK(dispatch(&f, THREE_MM_SCENE_PAUSE, 0));
    expect(&f, paused, 4);
    CHECK(f.packet.delay == 42);
    CHECK(dispatch(&f, THREE_MM_SCENE_CHANGE_CAMERA, 64));
    expect(&f, absent, 1);
    CHECK(f.issues[THREE_MM_SCENE_EVENT_NO_BACKGROUND] == 1);
    f.owner.owned[THREE_MM_SCENE_BACKGROUND] = &f.objects[3];
    f.packet.camera = 2;
    CHECK(dispatch(&f, THREE_MM_SCENE_CHANGE_CAMERA, 0));
    expect(&f, changed, 2);
    CHECK(f.camera == 2);
    f.camera_fail = 1;
    CHECK(!dispatch(&f, THREE_MM_SCENE_CHANGE_CAMERA, 0));
    expect(&f, changed, 2);
    CHECK(dispatch(&f, 99, 0));
    CHECK(f.issues[THREE_MM_SCENE_EVENT_UNKNOWN] == 1);
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(99, &f.s) && !f.used);
}
static void rewind_events(void)
{
    Fixture f;
    int i;
    init(&f);
    CHECK(dispatch(&f, THREE_MM_SCENE_PLAY_SOUND, 1));
    f.used = 0;
    f.frames.count = 4;
    f.runtime.next_event = 4;
    for (i = 0; i < 4; ++i)
    {
        f.frames.events[i].kind = THREE_MM_SCENE_PLAY_SOUND;
        f.frames.events[i].frame = i + 1;
        f.frames.events[i].payload.style = 2;
    }
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(f.last_payload == 1 && f.payloads == 1 && f.live == 1 && !f.sounds);
    f.frames.events[1].payload.style = 4;
    f.dup_fail = 1;
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(f.live == 1 && f.runtime.background_frame == 8);
    f.data.currentFrame = 1;
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(!f.live && f.runtime.background_frame == 8);
    for (i = 0; i < 4; ++i)
    {
        f.frames.events[i].kind = THREE_MM_SCENE_CHANGE_CAMERA;
        f.frames.events[i].payload.camera = i + 1;
    }
    f.owner.owned[THREE_MM_SCENE_BACKGROUND] = &f.objects[3];
    f.data.currentFrame = 3;
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.camera == 2 && f.last_payload == 1);
    f.camera_fail = 1;
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.issues[THREE_MM_SCENE_EVENT_RESTORE_FAILED] == 1);
    f.data.currentFrame = 1;
    f.used = 0;
    CHECK(!three_mm_scene_unplay_event(THREE_MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.camera == 0 && f.issues[THREE_MM_SCENE_EVENT_FIRST_CAMERA_FAILED] == 1);
    f.runtime.disabled = 64;
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.used == 4);
    f.owner.owned[THREE_MM_SCENE_BACKGROUND] = NULL;
    f.runtime.next_event = 0;
    f.used = 0;
    CHECK(three_mm_scene_unplay_event(THREE_MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.used == 2 && f.issues[THREE_MM_SCENE_EVENT_NO_BACKGROUND] == 2);
    cleanup(&f);
}
static void starts(void)
{
    Fixture f;
    int i;
    init(&f);
    f.starts.count = 3;
    f.runtime.disabled = 65535;
    f.starts.events[0].kind = THREE_MM_SCENE_SET_BACKGROUND;
    f.starts.events[0].payload = f.packet;
    f.starts.events[1].kind = THREE_MM_SCENE_ADD_ACTOR;
    f.starts.events[1].payload.object = &f.objects[0];
    f.starts.events[2].kind = THREE_MM_SCENE_ADD_TEXT;
    f.starts.events[2].payload.object = &f.objects[1];
    CHECK(three_mm_scene_start_events(1, &f.s));
    CHECK(f.payloads == 1 && f.last_payload == 1 && f.counts[0] == 1 && !f.counts[1] && f.transition == -1);
    init(&f);
    f.starts.count = 3;
    for (i = 0; i < 3; ++i)
    {
        f.starts.events[i].kind = THREE_MM_SCENE_ADD_ACTOR;
        f.starts.events[i].payload.object = &f.objects[i];
    }
    f.seek_fail = 2;
    CHECK(!three_mm_scene_start_events(0, &f.s));
    CHECK(f.payloads == 2 && f.counts[0] == 1 && f.objects[1].bound && !f.objects[2].bound);
    init(&f);
    f.starts.count = 3;
    f.starts.events[0].kind = THREE_MM_SCENE_SET_BACKGROUND;
    f.starts.events[0].payload = f.packet;
    f.starts.events[1].kind = THREE_MM_SCENE_ADD_ACTOR;
    f.starts.events[1].payload.object = &f.objects[0];
    f.starts.events[2].kind = THREE_MM_SCENE_ADD_TEXT;
    f.starts.events[2].payload.object = &f.objects[1];
    CHECK(three_mm_scene_start_events(0, &f.s));
    CHECK(f.payloads == 3 && f.counts[0] == 1 && f.counts[1] == 1 && f.transition == 0);
}
int main(void)
{
    sounds();
    backgrounds();
    objects();
    pauses_and_cameras();
    rewind_events();
    starts();
    puts("C89 Scene event dispatch, ownership, suppression, start prefixes and rewind failures passed.");
    return 0;
}
