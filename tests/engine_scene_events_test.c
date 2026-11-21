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
            fprintf(stderr, "Scene events line %d\n", __LINE__);                                                       \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
enum
{
    DUP = 1,
    FREE,
    PLAY,
    FETCH,
    CAMERA,
    TRANS,
    SET_TRANS,
    BIND,
    SEEK,
    PAUSE_TYPE,
    PAUSE,
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
    MMSceneSound sound;
    MMI32 pause[2];
    Object *object;
    MMTag tag;
} Payload;
typedef struct Fixture
{
    MMSceneOwner owner;
    SceneMemberMemory members;
    MMSceneEventServices s;
    MMSceneTrack *frames, *starts;
    Object objects[4];
    Payload packet;
    int ops[128], used, issues[5], live, dup_fail, fetch_fail, camera_fail;
    int seek_fail, transition, camera, releases, sounds, duplicate_volume, mutate_delay;
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
static void release(void *p, void *object)
{
    Fixture *f = (Fixture *)p;
    CHECK(object == &f->objects[3]);
    ++f->releases;
    log_op(f, RELEASE);
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
    MMSceneSound *copy;
    log_op(f, DUP);
    f->duplicate_volume = ((const MMSceneSound *)packet)->vlm;
    if (f->dup_fail)
        return NULL;
    copy = (MMSceneSound *)malloc(sizeof(*copy));
    CHECK(copy);
    *copy = *(const MMSceneSound *)packet;
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
static void bind(void *p, void *object)
{
    ((Object *)object)->bound = 1;
    log_op((Fixture *)p, BIND);
}
static int seek(void *p, int text, void *object, MMI32 frame)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)object;
    CHECK(frame == f->owner.data.currentFrame && (text || o->bound));
    ++o->seeks;
    log_op(f, SEEK);
    return f->seek_fail != o->id;
}
static void pause_type(void *p, int reason)
{
    Fixture *f = (Fixture *)p;
    CHECK(reason == f->packet.pause[0]);
    log_op(f, PAUSE_TYPE);
    if (f->mutate_delay)
        f->packet.pause[1] = 42;
}
static void pause_movie(void *p, int reason, MMI32 delay)
{
    Fixture *f = (Fixture *)p;
    CHECK(reason == f->packet.pause[0] && delay == f->packet.pause[1]);
    log_op(f, PAUSE);
}
static void diagnostic(void *p, int issue)
{
    Fixture *f = (Fixture *)p;
    CHECK(issue >= 0 && issue < 5);
    ++f->issues[issue];
    log_op(f, DIAG);
}
static void append(MMSceneTrack *track, MMI32 frame, int kind, MMI32 bytes, const void *payload)
{
    MMSceneTrackEvent event;
    event.frame = frame;
    event.kind = kind;
    CHECK(mm_scene_track_insert(track, track->rows.count, bytes, payload, &event));
}
static void init(Fixture *f)
{
    int i;
    MMSceneEventServices *s;
    memset(f, 0, sizeof(*f));
    init_scene_members(&f->owner, &f->members);
    f->owner.data.currentFrame = 3;
    f->transition = -1;
    f->packet.sound.sty = MM_SCENE_EVENT_MIDI;
    f->packet.tag.sid = 3;
    f->packet.tag.cache = f;
    f->packet.tag.type = 7;
    f->packet.tag.number = 9;
    for (i = 0; i < 4; ++i)
        f->objects[i].id = i + 1;
    f->frames = &f->owner.tracks[MM_SCENE_FRAMES];
    f->starts = &f->owner.tracks[MM_SCENE_STARTS];
    CHECK(mm_scene_track_init(f->frames, NULL, 0) && mm_scene_track_init(f->starts, NULL, 0));
    s = &f->s;
    s->context = f;
    s->owner = &f->owner;
    s->owners.context = f;
    s->owners.release = release;
    s->owners.release_background = release;
    s->owners.sound.context = f;
    s->owners.sound.free = free_sound;
    s->duplicate_sound = duplicate;
    s->play_sound = play;
    s->fetch_background = fetch;
    s->set_camera = camera;
    s->transition = transition;
    s->set_transition = set_transition;
    s->bind_actor = bind;
    s->seek = seek;
    s->pause_type = pause_type;
    s->pause = pause_movie;
    s->diagnostic = diagnostic;
}
static int dispatch(Fixture *f, int kind, unsigned mask)
{
    void *payload = NULL;
    switch (kind)
    {
    case MM_SCENE_SET_BACKGROUND:
        payload = &f->packet.tag;
        break;
    case MM_SCENE_PLAY_SOUND:
        payload = &f->packet.sound;
        break;
    case MM_SCENE_ADD_ACTOR:
    case MM_SCENE_ADD_TEXT:
        payload = &f->packet.object;
        break;
    case MM_SCENE_PAUSE:
        payload = f->packet.pause;
        break;
    case MM_SCENE_CHANGE_CAMERA:
        payload = &f->packet.camera;
        break;
    }
    return mm_scene_play_event(8, kind, payload, mask, &f->s);
}
static void cleanup(Fixture *f)
{
    mm_scene_owner_drop(&f->owner, MM_SCENE_BACKGROUND_SOUND, &f->s.owners);
    mm_scene_track_dispose(f->starts);
    mm_scene_track_dispose(f->frames);
    CHECK(!f->live);
}
static void sounds(void)
{
    Fixture f;
    void *old;
    static const int first[] = {DUP}, failed[] = {DUP};
    static const int replaced[] = {DUP, FREE, PLAY}, played[] = {PLAY};
    init(&f);
    f.owner.runtime.background_frame = 2;
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 1));
    expect(&f, first, 1);
    CHECK(f.live == 1 && f.owner.runtime.background_frame == 8 && !f.sounds);
    old = f.owner.owned[MM_SCENE_BACKGROUND_SOUND];
    CHECK(old);
    f.dup_fail = 1;
    CHECK(!mm_scene_play_event(9, MM_SCENE_PLAY_SOUND, &f.packet.sound, 0, &f.s));
    expect(&f, failed, 1);
    CHECK(f.owner.owned[MM_SCENE_BACKGROUND_SOUND] == old && f.owner.runtime.background_frame == 8);
    f.dup_fail = 0;
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 0));
    expect(&f, replaced, 3);
    CHECK(f.live == 1 && f.sounds == 1);
    f.packet.sound.sty = 2;
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 1));
    CHECK(!f.used);
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 0));
    expect(&f, played, 1);
    CHECK(f.live == 1);
    cleanup(&f);
}
static void backgrounds(void)
{
    Fixture f;
    static const int no_fetch[] = {FETCH}, no_camera[] = {FETCH, CAMERA, RELEASE};
    static const int loaded[] = {FETCH, CAMERA, TRANS, SET_TRANS};
    static const int retained[] = {FETCH, CAMERA, TRANS};
    init(&f);
    f.owner.background_tag.number = 41;
    f.fetch_fail = 1;
    CHECK(!dispatch(&f, MM_SCENE_SET_BACKGROUND, 65535));
    expect(&f, no_fetch, sizeof(no_fetch) / sizeof(*no_fetch));
    CHECK(!f.owner.owned[MM_SCENE_BACKGROUND] && f.owner.background_tag.number == 41 && f.transition == -1);
    f.fetch_fail = 0;
    f.camera_fail = 1;
    CHECK(!dispatch(&f, MM_SCENE_SET_BACKGROUND, 0));
    expect(&f, no_camera, sizeof(no_camera) / sizeof(*no_camera));
    CHECK(f.releases == 1 && !f.owner.owned[MM_SCENE_BACKGROUND] && f.owner.background_tag.number == 41 &&
          f.transition == -1);
    f.camera_fail = 0;
    CHECK(dispatch(&f, MM_SCENE_SET_BACKGROUND, 65535));
    expect(&f, loaded, sizeof(loaded) / sizeof(*loaded));
    CHECK(f.owner.owned[MM_SCENE_BACKGROUND] == &f.objects[3] &&
          f.owner.background_tag.number == 17 && f.owner.background_tag.cache == &f &&
          f.owner.background_tag.sid == 3 && f.transition == 0 && f.camera == 0);
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
    static const int bound[] = {BIND, SEEK}, caption[] = {SEEK};
    for (text = 0; text < 2; ++text)
    {
        list = text ? MM_SCENE_TEXTS : MM_SCENE_ACTORS;
        init(&f);
        f.packet.object = &f.objects[0];
        f.members.fail_at = 1;
        CHECK(!dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 65535));
        CHECK(!f.used);
        CHECK(!mm_scene_member_count(&f.owner, list) && !f.objects[0].bound && !f.objects[0].seeks);
        f.members.fail_at = 0;
        f.seek_fail = 1;
        CHECK(!dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 0));
        expect(&f, text ? caption : bound, text ? 1 : 2);
        CHECK(!mm_scene_member_count(&f.owner, list) && f.objects[0].bound == !text);
        f.seek_fail = 0;
        CHECK(dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 65535));
        expect(&f, text ? caption : bound, text ? 1 : 2);
        CHECK(mm_scene_member_count(&f.owner, list) == 1 && mm_scene_member_at(&f.owner, list, 0) == &f.objects[0]);
        f.packet.object = &f.objects[1];
        CHECK(dispatch(&f, text ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR, 0));
        CHECK(mm_scene_member_count(&f.owner, list) == 2 && mm_scene_member_at(&f.owner, list, 1) == &f.objects[1]);
    }
}
static void pauses_and_cameras(void)
{
    Fixture f;
    static const int suppressed[] = {PAUSE_TYPE}, paused[] = {PAUSE_TYPE, PAUSE};
    static const int absent[] = {DIAG}, changed[] = {CAMERA};
    init(&f);
    f.packet.pause[0] = 3;
    f.packet.pause[1] = (-2147483647 - 1);
    f.mutate_delay = 1;
    CHECK(dispatch(&f, MM_SCENE_PAUSE, 2));
    expect(&f, suppressed, 1);
    CHECK(dispatch(&f, MM_SCENE_PAUSE, 0));
    expect(&f, paused, 2);
    CHECK(f.packet.pause[1] == 42);
    CHECK(dispatch(&f, MM_SCENE_CHANGE_CAMERA, 64));
    expect(&f, absent, 1);
    CHECK(f.issues[MM_SCENE_EVENT_NO_BACKGROUND] == 1);
    f.owner.owned[MM_SCENE_BACKGROUND] = &f.objects[3];
    f.packet.camera = 2;
    CHECK(dispatch(&f, MM_SCENE_CHANGE_CAMERA, 0));
    expect(&f, changed, 1);
    CHECK(f.camera == 2);
    f.camera_fail = 1;
    CHECK(!dispatch(&f, MM_SCENE_CHANGE_CAMERA, 0));
    expect(&f, changed, 1);
    CHECK(dispatch(&f, 99, 0));
    CHECK(f.issues[MM_SCENE_EVENT_UNKNOWN] == 1);
    f.used = 0;
    CHECK(mm_scene_unplay_event(99, &f.s) && !f.used);
}
static void rewind_events(void)
{
    Fixture f;
    int i;
    MMSceneSound sound = {0};
    MMSceneTrackEvent event;
    MMI32 camera_number;
    init(&f);
    CHECK(dispatch(&f, MM_SCENE_PLAY_SOUND, 1));
    f.used = 0;
    f.owner.runtime.next_event = 4;
    for (i = 0; i < 4; ++i)
    {
        sound.sty = i == 2 ? MM_SCENE_EVENT_MIDI : 2;
        sound.vlm = 10 + i;
        append(f.frames, i + 1, MM_SCENE_PLAY_SOUND, sizeof(sound), &sound);
    }
    CHECK(mm_scene_unplay_event(MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(!f.used && f.live == 1 && !f.sounds);
    sound.sty = MM_SCENE_EVENT_MIDI;
    sound.vlm = 11;
    CHECK(mm_scene_track_put(f.frames, 1, sizeof(sound), &sound));
    f.dup_fail = 1;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(f.duplicate_volume == 11 && f.live == 1 && f.owner.runtime.background_frame == 8);
    f.owner.data.currentFrame = 1;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_PLAY_SOUND, &f.s));
    CHECK(!f.live && f.owner.runtime.background_frame == 8);
    for (i = 0; i < 4; ++i)
    {
        event.kind = MM_SCENE_CHANGE_CAMERA;
        event.frame = i == 0 ? 2 : i == 3 ? 1 : i + 1;
        camera_number = i + 1;
        CHECK(mm_scene_track_set(f.frames, i, &event));
        CHECK(mm_scene_track_put(f.frames, i, sizeof(camera_number), &camera_number));
    }
    f.owner.runtime.next_event = 3;
    f.owner.owned[MM_SCENE_BACKGROUND] = &f.objects[3];
    f.owner.data.currentFrame = 3;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.camera == 2 && f.used == 1 && f.ops[0] == CAMERA);
    CHECK(mm_scene_play_frame_event(3, 0, &f.s) && f.camera == 4);
    CHECK(!mm_scene_play_frame_event(4, 0, &f.s) && f.camera == 4);
    f.camera_fail = 1;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.issues[MM_SCENE_EVENT_RESTORE_FAILED] == 1);
    f.owner.data.currentFrame = 1;
    f.used = 0;
    CHECK(!mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.camera == 0 && f.issues[MM_SCENE_EVENT_FIRST_CAMERA_FAILED] == 1);
    f.owner.runtime.disabled = 64;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(!f.used);
    f.owner.owned[MM_SCENE_BACKGROUND] = NULL;
    f.owner.runtime.next_event = 0;
    f.used = 0;
    CHECK(mm_scene_unplay_event(MM_SCENE_CHANGE_CAMERA, &f.s));
    CHECK(f.used == 2 && f.issues[MM_SCENE_EVENT_NO_BACKGROUND] == 2);
    cleanup(&f);
}
static void add_start_object(Fixture *f, int kind, Object *object)
{
    append(f->starts, 0, kind, sizeof(object), &object);
}
static void starts(void)
{
    Fixture f;
    int i;
    init(&f);
    f.owner.runtime.disabled = 65535;
    append(f.starts, 0, MM_SCENE_SET_BACKGROUND, sizeof(f.packet.tag), &f.packet.tag);
    add_start_object(&f, MM_SCENE_ADD_ACTOR, &f.objects[0]);
    add_start_object(&f, MM_SCENE_ADD_TEXT, &f.objects[1]);
    CHECK(mm_scene_start_events(1, &f.s));
    CHECK(mm_scene_member_at(&f.owner, MM_SCENE_ACTORS, 0) == &f.objects[0] &&
          !mm_scene_member_count(&f.owner, MM_SCENE_TEXTS) && f.transition == -1);
    cleanup(&f);
    init(&f);
    for (i = 0; i < 3; ++i)
        add_start_object(&f, MM_SCENE_ADD_ACTOR, &f.objects[i]);
    f.seek_fail = 2;
    CHECK(!mm_scene_start_events(0, &f.s));
    CHECK(mm_scene_member_count(&f.owner, MM_SCENE_ACTORS) == 1 && f.objects[1].bound &&
          !f.objects[2].bound);
    cleanup(&f);
    init(&f);
    append(f.starts, 0, MM_SCENE_SET_BACKGROUND, sizeof(f.packet.tag), &f.packet.tag);
    add_start_object(&f, MM_SCENE_ADD_ACTOR, &f.objects[0]);
    add_start_object(&f, MM_SCENE_ADD_TEXT, &f.objects[1]);
    CHECK(mm_scene_start_events(0, &f.s));
    CHECK(mm_scene_member_count(&f.owner, MM_SCENE_ACTORS) == 1 &&
          mm_scene_member_count(&f.owner, MM_SCENE_TEXTS) == 1 && f.transition == 0);
    cleanup(&f);
}
static void background_query(void)
{
    Fixture f;
    MMTag out, later;
    init(&f);
    out.sid = 99;
    out.cache = &f.objects[3];
    out.type = 12;
    out.number = 34;
    f.owner.owned[MM_SCENE_BACKGROUND] = &f.objects[0];
    CHECK(!mm_scene_background_tag(&out, &f.s));
    CHECK(!f.used && out.sid == 99 && out.cache == &f.objects[3] && out.type == 12 && out.number == 34);
    add_start_object(&f, MM_SCENE_ADD_ACTOR, &f.objects[0]);
    append(f.starts, 0, MM_SCENE_SET_BACKGROUND, sizeof(f.packet.tag), &f.packet.tag);
    later = f.packet.tag;
    later.number = 42;
    append(f.starts, 0, MM_SCENE_SET_BACKGROUND, sizeof(later), &later);
    CHECK(mm_scene_background_tag(&out, &f.s));
    CHECK(out.sid == f.packet.tag.sid && out.cache == f.packet.tag.cache && out.type == f.packet.tag.type &&
          out.number == f.packet.tag.number);
    CHECK(!f.used && !f.releases && !f.live);
    CHECK(mm_scene_track_put(f.starts, 1, 0, NULL));
    CHECK(!mm_scene_background_tag(&out, &f.s));
    CHECK(out.number == f.packet.tag.number && !f.used);
    CHECK(mm_group_delete(&f.starts->rows, 2) == MM_STORAGE_OK);
    CHECK(mm_group_delete(&f.starts->rows, 1) == MM_STORAGE_OK);
    CHECK(!mm_scene_background_tag(&out, &f.s));
    CHECK(out.number == f.packet.tag.number && !f.used);
    cleanup(&f);
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
