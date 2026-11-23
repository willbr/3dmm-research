#include "mm/scene_runtime.h"
#include "mm/scene_sound.h"
#include "scene_member_fixture.h"
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
    MMI32 first, last;
} Object;
typedef struct Fixture
{
    MMSceneRuntimeServices services;
    MMSceneOwner owner;
    SceneMemberMemory members;
    MMSceneTrack *frames;
    Object actors[3], texts[2];
    int playing, recording, fail_camera, fail_replay;
    int notices[7], sound, snapshots, ends, until, grow;
    int ops[128], used, sound_plays, sound_cursor, pause_cursor[4], camera, camera_calls, duplicates;
} Fixture;
static void log_op(Fixture *f, int op)
{
    CHECK(f->used < 128);
    f->ops[f->used++] = op;
}
static void pause_type(void *p, int reason)
{
    Fixture *f = (Fixture *)p;
    CHECK(reason == 1 || reason == 3);
    f->pause_cursor[reason] = f->owner.runtime.next_event;
    log_op(f, 9 + reason);
}
static void pause_movie(void *p, int reason, MMI32 delay)
{
    CHECK(delay == 10 * reason);
    log_op((Fixture *)p, 13 + reason);
}
static void play_sound(void *p, void *payload)
{
    Fixture *f = (Fixture *)p;
    CHECK(((MMSceneSound *)payload)->sty == 2);
    ++f->sound_plays;
    f->sound_cursor = f->owner.runtime.next_event;
    log_op(f, 11);
}
static void *duplicate_sound(void *p, void *payload)
{
    Fixture *f = (Fixture *)p;
    CHECK(((MMSceneSound *)payload)->sty == MM_SCENE_EVENT_MIDI);
    ++f->duplicates;
    return NULL;
}
static const MMBackgroundOwnerHost background_host = {0};
static int set_camera(void *p, MMBackgroundOwner *background, MMI32 camera)
{
    Fixture *f = (Fixture *)p;
    CHECK(background == f->owner.background);
    ++f->camera_calls;
    f->camera = camera;
    return !f->fail_camera;
}
static void event_diagnostic(void *p, int issue)
{
    CHECK(issue == MM_SCENE_EVENT_FIRST_CAMERA_FAILED);
    log_op((Fixture *)p, 30);
}
static void add_members(Fixture *f, int list, int count)
{
    Object *objects = list == MM_SCENE_TEXTS ? f->texts : f->actors;
    MMI32 i;
    for (i = mm_scene_member_count(&f->owner, list); i < count; ++i)
        CHECK(mm_scene_member_append(&f->owner, list, &objects[i]));
}
static int seek(void *p, int text, void *o, MMI32 frame, int *sound)
{
    Fixture *f = (Fixture *)p;
    Object *a = (Object *)o;
    CHECK(frame == f->owner.data.currentFrame);
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
        add_members(f, MM_SCENE_ACTORS, 3);
    return !a->fail;
}
static int lifetime(void *p, int text, void *o, MMI32 *first, MMI32 *last)
{
    Object *a = (Object *)o;
    (void)text;
    log_op((Fixture *)p, 50 + a->id);
    *first = a->first;
    *last = a->last;
    return a->has_lifetime;
}
static int replay_actor(void *p, void *o, MMU32 enabled)
{
    Fixture *f = (Fixture *)p;
    Object *a = (Object *)o;
    CHECK(enabled & MM_SCENE_EFFECT_ACTORS);
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
static int must_render(void *p, void *o, MMI32 until)
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
    if (kind == MM_SCENE_INVALIDATE)
        CHECK(f->owner.runtime.publication.phase == MM_MOVIE_FRAME_COMMITTED &&
              f->owner.runtime.publication.frame == f->owner.data.currentFrame);
    ++f->notices[kind];
    if (kind == MM_SCENE_SOUND_INDICATOR)
        f->sound = value;
    log_op(f, 110 + kind);
}
static void init(Fixture *f)
{
    int i;
    MMSceneRuntimeServices *s;
    if (f->frames)
        mm_scene_track_dispose(f->frames);
    mm_scene_owner_remove_background(&f->owner);
    memset(f, 0, sizeof(*f));
    init_scene_members(&f->owner, &f->members);
    f->frames = &f->owner.tracks[MM_SCENE_FRAMES];
    CHECK(mm_scene_track_init(f->frames, NULL, 0));
    f->owner.background = mm_background_owner_create(&background_host);
    CHECK(f->owner.background);
    f->owner.data.firstFrame = f->owner.data.currentFrame = 1;
    f->owner.data.lastFrame = 10;
    for (i = 0; i < 3; ++i)
        f->actors[i].id = i;
    for (i = 0; i < 2; ++i)
        f->texts[i].id = i + 3;
    s = &f->services;
    s->context = f;
    s->events.context = f;
    s->events.owner = &f->owner;
    s->events.pause_type = pause_type;
    s->events.pause = pause_movie;
    s->events.play_sound = play_sound;
    s->events.duplicate_sound = duplicate_sound;
    s->events.set_camera = set_camera;
    s->events.diagnostic = event_diagnostic;
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
static void append(Fixture *f, MMI32 frame, int kind, MMI32 bytes, const void *payload)
{
    MMSceneTrackEvent event;
    event.frame = frame;
    event.kind = kind;
    CHECK(mm_scene_track_insert(f->frames, f->frames->rows.count, bytes, payload, &event));
}
static void events(Fixture *f)
{
    MMI32 pause[2] = {1, 10}, camera = 4;
    MMSceneSound sound = {0, 2, 0, 0};
    append(f, 1, MM_SCENE_PAUSE, sizeof(pause), pause);
    append(f, 3, MM_SCENE_PLAY_SOUND, sizeof(sound), &sound);
    pause[0] = 3;
    pause[1] = 30;
    append(f, 3, MM_SCENE_PAUSE, sizeof(pause), pause);
    append(f, 7, MM_SCENE_CHANGE_CAMERA, sizeof(camera), &camera);
}
static void seeks(void)
{
    Fixture f = {0};
    MMSceneSound midi = {0, MM_SCENE_EVENT_MIDI, 0, 0};
    int sound;
    init(&f);
    events(&f);
    f.recording = 1;
    add_members(&f, MM_SCENE_ACTORS, 1);
    add_members(&f, MM_SCENE_TEXTS, 1);
    f.sound = 1;
    CHECK(mm_scene_goto(3, &f.services));
    CHECK(f.owner.runtime.next_event == 3 && f.owner.data.currentFrame == 3);
    CHECK(f.sound_plays == 1 && f.sound_cursor == 1 && f.pause_cursor[1] == 0 && f.pause_cursor[3] == 2);
    CHECK(f.used == 9 && f.ops[0] == 111 && f.ops[1] == 110 && f.ops[2] == 10 && f.ops[3] == 11 &&
          f.ops[4] == 12 && f.ops[5] == 40 && f.ops[6] == 43 && f.ops[7] == 113 && f.ops[8] == 114 && f.sound);
    f.used = 0;
    f.sound_plays = 0;
    f.sound = 0;
    CHECK(mm_scene_goto(3, &f.services));
    CHECK(!f.sound_plays && f.used == 3 && f.ops[0] == 40 && f.ops[1] == 43 && f.ops[2] == 113);
    f.used = 0;
    CHECK(mm_scene_replay(0, &f.services));
    CHECK(f.used == 1 && f.ops[0] == 12 && !f.sound_plays);
    f.used = 0;
    CHECK(mm_scene_replay((MMU32) ~(MMU32)0, &f.services));
    CHECK(f.used == 4 && f.ops[0] == 12 && f.ops[1] == 16 && f.ops[2] == 11 && f.ops[3] == 60);
    f.used = 0;
    f.sound = 0;
    CHECK(mm_scene_goto(1, &f.services));
    CHECK(f.ops[0] == 110 && f.ops[1] == 10);
    CHECK(f.owner.runtime.next_event == 1 && f.sound == 1);
    init(&f);
    events(&f);
    CHECK(mm_scene_track_put(f.frames, 1, sizeof(midi), &midi));
    CHECK(!mm_scene_goto(11, &f.services));
    CHECK(f.duplicates == 1 && !f.sound_plays && f.owner.runtime.next_event == 1);
    CHECK(f.owner.data.currentFrame == 11 && f.owner.data.lastFrame == 11 && f.notices[MM_SCENE_DIRTY] == 1);
    CHECK(f.notices[MM_SCENE_GOTO_FAILURE] == 1 && !f.notices[MM_SCENE_INVALIDATE]);
    init(&f);
    events(&f);
    f.fail_camera = 1;
    CHECK(!mm_scene_goto(7, &f.services));
    CHECK(f.owner.runtime.next_event == 3 && f.owner.data.currentFrame == 7 && f.camera == 4 && !f.sound_plays);
    CHECK(f.notices[MM_SCENE_GOTO_FAILURE] == 1 && !f.notices[MM_SCENE_INVALIDATE]);
    init(&f);
    events(&f);
    f.owner.runtime.next_event = 4;
    f.owner.data.currentFrame = 8;
    f.fail_camera = 1;
    CHECK(!mm_scene_goto(1, &f.services));
    CHECK(f.owner.runtime.next_event == 4 && f.owner.data.currentFrame == 1 && f.camera_calls == 1 && f.camera == 0);
    CHECK(f.notices[MM_SCENE_GOTO_FAILURE] == 1 && f.ops[1] == 30);
    init(&f);
    events(&f);
    f.owner.runtime.next_event = 3;
    CHECK(mm_scene_track_put(f.frames, 1, sizeof(midi), &midi));
    f.owner.data.currentFrame = 3;
    CHECK(!mm_scene_replay(8, &f.services));
    CHECK(f.owner.runtime.next_event == 3 && f.notices[MM_SCENE_GOTO_FAILURE] == 1);
    f.owner.runtime.next_event = 4;
    f.owner.data.currentFrame = 7;
    CHECK(mm_scene_replay((MMU32) ~(MMU32)0, &f.services) && !f.camera_calls);
    init(&f);
    add_members(&f, MM_SCENE_ACTORS, 2);
    f.actors[1].fail = 1;
    CHECK(!mm_scene_goto(2, &f.services));
    CHECK(!f.notices[MM_SCENE_GOTO_FAILURE] && !f.notices[MM_SCENE_INVALIDATE]);
    init(&f);
    add_members(&f, MM_SCENE_TEXTS, 2);
    f.texts[1].fail = 1;
    CHECK(!mm_scene_goto(2, &f.services));
    CHECK(!f.notices[MM_SCENE_GOTO_FAILURE] && !f.notices[MM_SCENE_INVALIDATE]);
    init(&f);
    add_members(&f, MM_SCENE_ACTORS, 2);
    f.fail_replay = 2;
    CHECK(!mm_scene_replay(8, &f.services));
    CHECK(f.used == 2 && !f.notices[MM_SCENE_GOTO_FAILURE]);
    init(&f);
    f.owner.runtime.disabled = 12;
    add_members(&f, MM_SCENE_ACTORS, 3);
    add_members(&f, MM_SCENE_TEXTS, 2);
    CHECK(mm_scene_goto(2, &f.services));
    CHECK(f.used == 3);
    init(&f);
    add_members(&f, MM_SCENE_ACTORS, 1);
    f.grow = 1;
    sound = 0;
    CHECK(mm_scene_force(1, 0, &sound, &f.services));
    CHECK(sound == 1 && f.used == 3 && f.ops[2] == 42);
    f.used = 0;
    f.grow = 0;
    CHECK(mm_scene_force(1, 0, 0, &f.services));
    f.actors[1].fail = 1;
    f.used = 0;
    sound = 0;
    CHECK(!mm_scene_force(1, 0, &sound, &f.services) && sound && f.used == 2);
    init(&f);
    f.owner.runtime.disabled = 12;
    CHECK(mm_scene_goto((-2147483647 - 1), &f.services));
    CHECK(f.owner.data.firstFrame == (-2147483647 - 1));
    CHECK(mm_scene_goto(2147483647, &f.services));
    CHECK(f.owner.data.lastFrame == 2147483647);
    mm_scene_track_dispose(f.frames);
    mm_scene_owner_remove_background(&f.owner);
}
static void ranges(void)
{
    Fixture f = {0};
    MMSceneTrackEvent row;
    MMI32 first_camera = 1, last_camera = 2, pause[2] = {1, 10}, camera;
    int i;
    init(&f);
    append(&f, 1, MM_SCENE_CHANGE_CAMERA, sizeof(first_camera), &first_camera);
    append(&f, 1, MM_SCENE_PAUSE, sizeof(pause), pause);
    append(&f, 1, MM_SCENE_CHANGE_CAMERA, sizeof(last_camera), &last_camera);
    add_members(&f, MM_SCENE_ACTORS, 2);
    add_members(&f, MM_SCENE_TEXTS, 1);
    f.actors[0].has_lifetime = 1;
    f.actors[0].first = -2;
    f.actors[0].last = 12;
    f.actors[1].first = -100;
    f.actors[1].last = 100;
    f.texts[0].has_lifetime = 1;
    f.texts[0].first = -4;
    f.texts[0].last = 11;
    mm_scene_extend_range(&f.services);
    CHECK(f.owner.data.firstFrame == -4 && f.owner.data.lastFrame == 12);
    for (i = 0; i < 2; ++i)
    {
        CHECK(mm_scene_track_get(f.frames, i, &row) && row.frame == -4 && row.kind == MM_SCENE_CHANGE_CAMERA);
        memcpy(&camera, mm_scene_track_payload(f.frames, i, NULL), sizeof(camera));
        CHECK(camera == i + 1);
    }
    CHECK(mm_scene_track_get(f.frames, 2, &row) && row.frame == 1 && row.kind == MM_SCENE_PAUSE);
    CHECK(!f.notices[MM_SCENE_DIRTY]);
    mm_scene_track_dispose(f.frames);
    mm_scene_owner_remove_background(&f.owner);
}
static void prerenders(void)
{
    Fixture f = {0};
    int mode;
    for (mode = 0; mode < 4; ++mode)
    {
        init(&f);
        events(&f);
        f.playing = 1;
        add_members(&f, MM_SCENE_ACTORS, 3);
        f.actors[0].rendered = 1;
        f.actors[1].must[0] = f.actors[1].must[1] = 1;
        f.actors[1].rendered = 1;
        f.actors[2].must[0] = mode & 1;
        f.actors[2].must[1] = (mode >> 1) & 1;
        mm_scene_prerender(0, &f.services);
        CHECK(f.snapshots == 1 && f.until == 7);
        CHECK(f.actors[0].queries == 2 && f.actors[0].shows == 1 && f.actors[0].hides == 1 && f.actors[0].rendered);
        CHECK(f.actors[1].queries == 2 && f.actors[1].shows == 2 && f.actors[1].hides == 1 && !f.actors[1].rendered);
        CHECK(f.actors[2].rendered == !f.actors[2].must[1]);
        mm_scene_end_prerender(&f.services);
        CHECK(f.ends == 1 && !f.actors[0].rendered && !f.actors[2].rendered);
    }
    init(&f);
    f.playing = 1;
    add_members(&f, MM_SCENE_ACTORS, 1);
    f.actors[0].must[0] = f.actors[0].must[1] = 1;
    mm_scene_prerender(0, &f.services);
    CHECK(!f.snapshots && f.actors[0].queries == 2 && f.until == 10);
    init(&f);
    events(&f);
    f.playing = 1;
    f.owner.data.currentFrame = 4;
    f.owner.runtime.next_event = 3;
    mm_scene_prerender(0, &f.services);
    CHECK(!f.used);
    add_members(&f, MM_SCENE_ACTORS, 1);
    mm_scene_prerender(1, &f.services);
    CHECK(f.snapshots == 1);
    init(&f);
    events(&f);
    f.playing = 1;
    f.owner.data.currentFrame = 7;
    f.owner.runtime.next_event = 4;
    add_members(&f, MM_SCENE_ACTORS, 1);
    mm_scene_prerender(0, &f.services);
    CHECK(f.snapshots == 1 && f.until == 10);
    init(&f);
    add_members(&f, MM_SCENE_ACTORS, 1);
    mm_scene_prerender(1, &f.services);
    CHECK(!f.used);
    mm_scene_end_prerender(&f.services);
    CHECK(f.ends == 1);
    mm_scene_track_dispose(f.frames);
    mm_scene_owner_remove_background(&f.owner);
}
int main(void)
{
    seeks();
    ranges();
    prerenders();
    puts("C89 Scene frame/replay, partial failures, range and two-pass prerender policy passed.");
    return 0;
}
