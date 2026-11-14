#include "three_mm/scene_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene runtime line %d\n", __LINE__);                                                      \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Object
{
    int id, fail, has_lifetime, rendered, visible, queries, must[2], shows, hides;
    ThreeMMI32 first, last;
} Object;
typedef struct Event
{
    ThreeMMI32 frame;
    int kind;
} Event;
typedef struct Fixture
{
    ThreeMMSceneState data;
    ThreeMMSceneRuntime runtime;
    ThreeMMSceneRuntimeServices services;
    Event events[8];
    Object actors[3], texts[2];
    int count, actors_count, texts_count, playing, recording, fail_play, fail_unplay, fail_replay;
    int notices[7], sound, snapshots, ends, until, grow;
    int ops[128], used, seen_cursor[16], calls;
    ThreeMMU32 masks[16];
} Fixture;
static void log_op(Fixture *f, int op)
{
    CHECK(f->used < 128);
    f->ops[f->used++] = op;
}
static ThreeMMI32 event_count(void *p)
{
    return ((Fixture *)p)->count;
}
static void event(void *p, ThreeMMI32 i, ThreeMMI32 *frame, int *kind)
{
    Fixture *f = (Fixture *)p;
    CHECK(i >= 0 && i < f->count);
    *frame = f->events[i].frame;
    *kind = f->events[i].kind;
}
static int play(void *p, ThreeMMI32 i, ThreeMMU32 mask)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->calls < 16);
    f->seen_cursor[f->calls] = f->runtime.next_event;
    f->masks[f->calls++] = mask;
    log_op(f, 10 + i);
    return f->fail_play != i + 1;
}
static int unplay(void *p, ThreeMMI32 i)
{
    Fixture *f = (Fixture *)p;
    log_op(f, 20 + i);
    CHECK(f->runtime.next_event == i + 1);
    return f->fail_unplay != i + 1;
}
static void move_camera(void *p, ThreeMMI32 i, ThreeMMI32 frame)
{
    Fixture *f = (Fixture *)p;
    Event e = f->events[i];
    e.frame = frame;
    memmove(f->events + 1, f->events, (size_t)i * sizeof(e));
    f->events[0] = e;
    log_op(f, 30 + i);
}
static ThreeMMI32 object_count(void *p, int text)
{
    Fixture *f = (Fixture *)p;
    return text ? f->texts_count : f->actors_count;
}
static void *object(void *p, int text, ThreeMMI32 i)
{
    Fixture *f = (Fixture *)p;
    CHECK(i >= 0 && i < (text ? f->texts_count : f->actors_count));
    return text ? f->texts + i : f->actors + i;
}
static int seek(void *p, int text, void *o, ThreeMMI32 frame, int *sound)
{
    Fixture *f = (Fixture *)p;
    Object *a = (Object *)o;
    CHECK(frame == f->data.currentFrame);
    log_op(f, 40 + a->id);
    if (text)
        CHECK(!sound);
    else if (sound)
    {
        if (a->id == 0)
            CHECK(*sound == f->sound);
        else
            CHECK(*sound);
        *sound = 1;
    }
    if (f->grow && a->id == 0)
        f->actors_count = 3;
    return !a->fail;
}
static int lifetime(void *p, int text, void *o, ThreeMMI32 *first, ThreeMMI32 *last)
{
    Object *a = (Object *)o;
    (void)text;
    log_op((Fixture *)p, 50 + a->id);
    *first = a->first;
    *last = a->last;
    return a->has_lifetime;
}
static int replay_actor(void *p, void *o, ThreeMMU32 enabled)
{
    Fixture *f = (Fixture *)p;
    Object *a = (Object *)o;
    CHECK(enabled & THREE_MM_SCENE_EFFECT_ACTORS);
    log_op(f, 60 + a->id);
    return f->fail_replay != a->id + 1;
}
static int playing(void *p)
{
    return ((Fixture *)p)->playing;
}
static int recording(void *p)
{
    return ((Fixture *)p)->recording;
}
static int must_render(void *p, void *o, ThreeMMI32 until)
{
    Fixture *f = (Fixture *)p;
    Object *a = (Object *)o;
    CHECK(a->queries < 2);
    f->until = until;
    log_op(f, 70 + a->id);
    return a->must[a->queries++];
}
static int prerendered(void *p, void *o)
{
    (void)p;
    return ((Object *)o)->rendered;
}
static void show(void *p, void *o, int visible)
{
    Object *a = (Object *)o;
    a->visible = visible;
    if (visible)
        ++a->shows;
    else
        ++a->hides;
    log_op((Fixture *)p, (visible ? 80 : 90) + a->id);
}
static void set_prerendered(void *p, void *o, int value)
{
    (void)p;
    ((Object *)o)->rendered = value;
}
static void snapshot(void *p, int begin)
{
    Fixture *f = (Fixture *)p;
    if (begin)
        ++f->snapshots;
    else
        ++f->ends;
    log_op(f, begin ? 100 : 101);
}
static void notice(void *p, int kind, int value)
{
    Fixture *f = (Fixture *)p;
    CHECK(kind >= 0 && kind < 7);
    ++f->notices[kind];
    if (kind == THREE_MM_SCENE_SOUND_INDICATOR)
        f->sound = value;
    log_op(f, 110 + kind);
}
static void init(Fixture *f)
{
    int i;
    ThreeMMSceneRuntimeServices *s;
    memset(f, 0, sizeof(*f));
    three_mm_scene_runtime_init(&f->runtime);
    f->data.firstFrame = f->data.currentFrame = 1;
    f->data.lastFrame = 10;
    for (i = 0; i < 3; ++i)
        f->actors[i].id = i;
    for (i = 0; i < 2; ++i)
        f->texts[i].id = i + 3;
    s = &f->services;
    s->context = f;
    s->event_count = event_count;
    s->event = event;
    s->play = play;
    s->unplay = unplay;
    s->move_camera = move_camera;
    s->object_count = object_count;
    s->object = object;
    s->seek = seek;
    s->lifetime = lifetime;
    s->replay_actor = replay_actor;
    s->playing = playing;
    s->recording = recording;
    s->must_render = must_render;
    s->prerendered = prerendered;
    s->show = show;
    s->set_prerendered = set_prerendered;
    s->snapshot = snapshot;
    s->notice = notice;
}
static void events(Fixture *f)
{
    f->count = 4;
    f->events[0].frame = 1;
    f->events[0].kind = THREE_MM_SCENE_PAUSE;
    f->events[1].frame = 3;
    f->events[1].kind = THREE_MM_SCENE_PLAY_SOUND;
    f->events[2].frame = 3;
    f->events[2].kind = THREE_MM_SCENE_PAUSE;
    f->events[3].frame = 7;
    f->events[3].kind = THREE_MM_SCENE_CHANGE_CAMERA;
}
static void seeks(void)
{
    Fixture f;
    int i, sound;
    init(&f);
    events(&f);
    f.recording = 1;
    f.actors_count = 1;
    f.texts_count = 1;
    f.sound = 1;
    CHECK(three_mm_scene_goto(&f.runtime, &f.data, 3, &f.services));
    CHECK(f.runtime.next_event == 3 && f.data.currentFrame == 3);
    CHECK(f.calls == 3 && f.masks[0] == 3 && f.masks[1] == 2 && f.masks[2] == 2);
    CHECK(f.seen_cursor[0] == 0 && f.seen_cursor[2] == 2);
    CHECK(f.ops[0] == 111 && f.ops[1] == 110 && f.sound == 1);
    f.used = 0;
    f.calls = 0;
    f.sound = 0;
    CHECK(three_mm_scene_goto(&f.runtime, &f.data, 3, &f.services));
    CHECK(!f.calls && f.used == 3 && f.ops[0] == 40 && f.ops[1] == 43 && f.ops[2] == 113);
    f.used = 0;
    f.calls = 0;
    CHECK(three_mm_scene_replay(&f.runtime, &f.data, 0, &f.services));
    CHECK(f.used == 2 && f.ops[0] == 12 && f.ops[1] == 11);
    CHECK(f.masks[0] == (ThreeMMU32) ~(ThreeMMU32)0);
    f.used = 0;
    f.calls = 0;
    CHECK(three_mm_scene_replay(&f.runtime, &f.data, (ThreeMMU32) ~(ThreeMMU32)0, &f.services));
    CHECK(f.masks[0] == 64 && f.ops[2] == 60);
    f.used = 0;
    f.calls = 0;
    f.sound = 0;
    CHECK(three_mm_scene_goto(&f.runtime, &f.data, 1, &f.services));
    CHECK(f.ops[0] == 110 && f.ops[1] == 22 && f.ops[2] == 21 && f.ops[3] == 10);
    CHECK(f.runtime.next_event == 1 && f.sound == 1);
    for (i = 1; i <= 3; ++i)
    {
        init(&f);
        events(&f);
        f.fail_play = i;
        CHECK(!three_mm_scene_goto(&f.runtime, &f.data, 11, &f.services));
        CHECK(f.runtime.next_event == i - 1 && f.data.currentFrame == 11 && f.data.lastFrame == 11);
        CHECK(f.notices[THREE_MM_SCENE_GOTO_FAILURE] == 1 && !f.notices[THREE_MM_SCENE_INVALIDATE]);
        CHECK(f.notices[THREE_MM_SCENE_DIRTY] == 1);
    }
    for (i = 2; i <= 4; ++i)
    {
        init(&f);
        events(&f);
        f.runtime.next_event = 4;
        f.data.currentFrame = 8;
        f.fail_unplay = i;
        CHECK(!three_mm_scene_goto(&f.runtime, &f.data, 1, &f.services));
        CHECK(f.runtime.next_event == i && f.data.currentFrame == 1);
        CHECK(f.notices[THREE_MM_SCENE_GOTO_FAILURE] == 1);
    }
    init(&f);
    events(&f);
    f.runtime.next_event = 3;
    f.fail_play = 2;
    f.data.currentFrame = 3;
    CHECK(!three_mm_scene_replay(&f.runtime, &f.data, 8, &f.services));
    CHECK(f.runtime.next_event == 3 && f.notices[THREE_MM_SCENE_GOTO_FAILURE] == 1);
    init(&f);
    f.actors_count = 2;
    f.actors[1].fail = 1;
    CHECK(!three_mm_scene_goto(&f.runtime, &f.data, 2, &f.services));
    CHECK(!f.notices[THREE_MM_SCENE_GOTO_FAILURE] && !f.notices[THREE_MM_SCENE_INVALIDATE]);
    init(&f);
    f.texts_count = 2;
    f.texts[1].fail = 1;
    CHECK(!three_mm_scene_goto(&f.runtime, &f.data, 2, &f.services));
    CHECK(!f.notices[THREE_MM_SCENE_GOTO_FAILURE] && !f.notices[THREE_MM_SCENE_INVALIDATE]);
    init(&f);
    f.actors_count = 2;
    f.fail_replay = 2;
    CHECK(!three_mm_scene_replay(&f.runtime, &f.data, 8, &f.services));
    CHECK(f.used == 2 && !f.notices[THREE_MM_SCENE_GOTO_FAILURE]);
    init(&f);
    f.runtime.disabled = 12;
    f.actors_count = 3;
    f.texts_count = 2;
    CHECK(three_mm_scene_goto(&f.runtime, &f.data, 2, &f.services));
    CHECK(f.used == 3);
    init(&f);
    f.actors_count = 1;
    f.grow = 1;
    sound = 0;
    CHECK(three_mm_scene_force(1, 0, &sound, &f.services));
    CHECK(sound == 1 && f.used == 3 && f.ops[2] == 42);
    f.used = 0;
    f.grow = 0;
    CHECK(three_mm_scene_force(1, 0, 0, &f.services));
    f.actors[1].fail = 1;
    f.used = 0;
    sound = 0;
    CHECK(!three_mm_scene_force(1, 0, &sound, &f.services) && sound && f.used == 2);
    init(&f);
    f.runtime.disabled = 12;
    CHECK(three_mm_scene_goto(&f.runtime, &f.data, (-2147483647 - 1), &f.services));
    CHECK(f.data.firstFrame == (-2147483647 - 1));
    CHECK(three_mm_scene_goto(&f.runtime, &f.data, 2147483647, &f.services));
    CHECK(f.data.lastFrame == 2147483647);
}
static void ranges(void)
{
    Fixture f;
    init(&f);
    events(&f);
    f.count = 3;
    f.events[0].kind = THREE_MM_SCENE_CHANGE_CAMERA;
    f.events[1].kind = THREE_MM_SCENE_PAUSE;
    f.events[1].frame = 1;
    f.events[2].kind = THREE_MM_SCENE_CHANGE_CAMERA;
    f.events[2].frame = 1;
    f.actors_count = 2;
    f.texts_count = 1;
    f.actors[0].has_lifetime = 1;
    f.actors[0].first = -2;
    f.actors[0].last = 12;
    f.actors[1].first = -100;
    f.actors[1].last = 100;
    f.texts[0].has_lifetime = 1;
    f.texts[0].first = -4;
    f.texts[0].last = 11;
    three_mm_scene_extend_range(&f.data, &f.services);
    CHECK(f.data.firstFrame == -4 && f.data.lastFrame == 12);
    CHECK(f.events[0].frame == -4 && f.events[1].frame == -4 && f.events[2].frame == 1);
    CHECK(!f.notices[THREE_MM_SCENE_DIRTY]);
}
static void prerenders(void)
{
    Fixture f;
    int mode;
    for (mode = 0; mode < 4; ++mode)
    {
        init(&f);
        events(&f);
        f.playing = 1;
        f.actors_count = 3;
        f.actors[0].rendered = 1;
        f.actors[1].must[0] = f.actors[1].must[1] = 1;
        f.actors[1].rendered = 1;
        f.actors[2].must[0] = mode & 1;
        f.actors[2].must[1] = (mode >> 1) & 1;
        three_mm_scene_prerender(&f.runtime, &f.data, 0, &f.services);
        CHECK(f.snapshots == 1 && f.until == 7);
        CHECK(f.actors[0].queries == 2 && f.actors[0].shows == 1 && f.actors[0].hides == 1 && f.actors[0].rendered);
        CHECK(f.actors[1].queries == 2 && f.actors[1].shows == 2 && f.actors[1].hides == 1 && !f.actors[1].rendered);
        CHECK(f.actors[2].rendered == !f.actors[2].must[1]);
        three_mm_scene_end_prerender(&f.services);
        CHECK(f.ends == 1 && !f.actors[0].rendered && !f.actors[2].rendered);
    }
    init(&f);
    f.playing = 1;
    f.actors_count = 1;
    f.actors[0].must[0] = f.actors[0].must[1] = 1;
    three_mm_scene_prerender(&f.runtime, &f.data, 0, &f.services);
    CHECK(!f.snapshots && f.actors[0].queries == 2 && f.until == 10);
    init(&f);
    events(&f);
    f.playing = 1;
    f.data.currentFrame = 4;
    f.runtime.next_event = 3;
    three_mm_scene_prerender(&f.runtime, &f.data, 0, &f.services);
    CHECK(!f.used);
    f.actors_count = 1;
    three_mm_scene_prerender(&f.runtime, &f.data, 1, &f.services);
    CHECK(f.snapshots == 1);
    init(&f);
    events(&f);
    f.playing = 1;
    f.data.currentFrame = 7;
    f.runtime.next_event = 4;
    f.actors_count = 1;
    three_mm_scene_prerender(&f.runtime, &f.data, 0, &f.services);
    CHECK(f.snapshots == 1 && f.until == 10);
    init(&f);
    f.actors_count = 1;
    three_mm_scene_prerender(&f.runtime, &f.data, 1, &f.services);
    CHECK(!f.used);
    three_mm_scene_end_prerender(&f.services);
    CHECK(f.ends == 1);
}
int main(void)
{
    seeks();
    ranges();
    prerenders();
    puts("C89 Scene frame/replay, partial failures, range and two-pass prerender policy passed.");
    return 0;
}
