#include "mm/scene_chop.h"
#include "mm/scene_sound.h"
#include "mm/background_resource.h"
#include "mm/chunk_copy.h"
#include "mm/material_resource.h"
#include "mm/movie_sound.h"
#include "mm/template_create.h"
#include "../libraries/engine/src/actor_archive_internal.h"
#include "scene_member_fixture.h"
#include "scene_snapshot_fixture.h"
#include "movie_scene_session_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene chop: %d\n", __LINE__);                                                             \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Obj
{
    int id, alive, lifetime;
    MMI32 first, last;
} Obj;
typedef struct SnapshotScene
{
    struct Host *host;
    int refs;
} SnapshotScene;
typedef struct Host
{
    MMSceneOwner owner;
    SceneMemberMemory members;
    MMSceneSound background_sound;
    Obj objects[5];
    MMSceneTrack *frames;
    int trace[128], traces, fail, stage, removal_sticks, seek_fails, show_fails, write_calls, switch_calls, dead;
    int undo, sought;
    SnapshotFile file;
    MMMovieScenes movie;
    MMMovieState data;
    ChunkIndexFixture archive;
    SnapshotScene old, initial, restored, neighbor;
    MMSceneSnapshot *record;
} Host;
static void event(Host *h, int value)
{
    CHECK(h->traces < 128);
    h->trace[h->traces++] = value;
}
static void expect(Host *h, const int *values, int n)
{
    CHECK(h->traces == n && !memcmp(h->trace, values, n * sizeof(*values)));
    h->traces = 0;
}
static void close_tag(void *p, MMSceneSoundTag *tag)
{
    CHECK(tag->child < 2);
    event((Host *)p, 80 + (int)tag->child);
}
static void release(void *p, void *obj)
{
    CHECK(obj);
    event((Host *)p, 23);
}
static int trim(void *p, void *o, int back)
{
    Obj *v = (Obj *)o;
    event((Host *)p, 100 + back * 10 + v->id);
    return v->alive;
}
static void remove_object(Host *h, void *p, int text)
{
    int i, list = text ? MM_SCENE_TEXTS : MM_SCENE_ACTORS;
    event(h, 120 + text);
    if (h->removal_sticks)
        return;
    for (i = 0; i < mm_scene_member_count(&h->owner, list); ++i)
        if (mm_scene_member_at(&h->owner, list, i) == p)
        {
            mm_scene_member_remove(&h->owner, list, i);
            return;
        }
    CHECK(0);
}
static void remove_actor(void *p, void *o)
{
    remove_object((Host *)p, o, 0);
}
static int remove_text(void *p, void *o)
{
    Host *h = (Host *)p;
    remove_object(h, o, 1);
    return !h->removal_sticks;
}
static int lifetime(void *p, void *o, MMI32 *first, MMI32 *last)
{
    Obj *v = (Obj *)o;
    (void)p;
    if (!v->lifetime)
        return 0;
    *first = v->first;
    *last = v->last;
    return 1;
}
static int seek(void *p, int text, void *o, MMI32 frame)
{
    Host *h = (Host *)p;
    (void)o;
    CHECK(text);
    event(h, frame == 2147483646L ? 130 : 131);
    return !h->seek_fails;
}
static int show(void *p, void *o)
{
    Host *h = (Host *)p;
    (void)o;
    event(h, 132);
    return !h->show_fails;
}
static void start_text(void *p, void *o, MMI32 frame)
{
    ((Obj *)o)->first = frame;
    event((Host *)p, 133);
}
static void dirty(void *p)
{
    event((Host *)p, 142);
}
static void discard(void *p)
{
    Host *h = (Host *)p;
    CHECK(!h->owner.owned[MM_SCENE_BACKGROUND_SOUND]);
    h->owner.runtime.background_frame = -99;
    event(h, 143);
}
static void bad_lifetime(void *p)
{
    event((Host *)p, 144);
}
static void *new_undo(void *p)
{
    Host *h = (Host *)p;
    event(h, 150);
    return h->fail == 1 ? NULL : &h->undo;
}
static int save_undo(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == &h->undo);
    event(h, 151);
    return h->fail != 2;
}
static int commit(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == &h->undo);
    event(h, 152);
    return h->fail != 3;
}
static void release_undo(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == &h->undo);
    event(h, 153);
}
static void clear_undo(void *p)
{
    event((Host *)p, 154);
}
static MMSceneChopServices init(Host *h)
{
    MMSceneChopServices s;
    int i;
    memset(h, 0, sizeof(*h));
    init_scene_members(&h->owner, &h->members);
    memset(&s, 0, sizeof(s));
    h->owner.data.firstFrame = 1;
    h->owner.data.lastFrame = 10;
    h->owner.data.currentFrame = 5;
    for (i = 0; i < 5; ++i)
    {
        h->objects[i].id = i;
        h->objects[i].lifetime = 1;
        h->objects[i].first = 1;
        h->objects[i].last = 10;
    }
    h->frames = &h->owner.tracks[MM_SCENE_FRAMES];
    CHECK(mm_scene_track_init(h->frames, NULL, 0));
    s.context = h;
    s.events.context = h;
    s.events.owner = &h->owner;
    s.events.owners.context = h;
    s.events.owners.release = release;
    s.events.owners.sound.context = h;
    s.events.owners.sound.free = release;
    s.events.owners.sound.close = close_tag;
    s.events.seek = seek;
    s.trim_actor = trim;
    s.remove_actor = remove_actor;
    s.remove_text = remove_text;
    s.lifetime = lifetime;
    s.show_text = show;
    s.start_text = start_text;
    s.dirty = dirty;
    s.discard_background_frame = discard;
    s.bad_lifetime = bad_lifetime;
    s.new_undo = new_undo;
    s.save_undo = save_undo;
    s.commit_undo = commit;
    s.release_undo = release_undo;
    s.clear_undo = clear_undo;
    return s;
}
static void append_frame(Host *h, MMI32 frame, int kind, MMI32 count)
{
    MMSceneTrackEvent row;
    struct
    {
        MMSceneSound header;
        MMSceneSoundTag tags[2];
    } sound = {0};
    MMI32 pause[2] = {0, 0}, bytes = sizeof(frame);
    const void *data = &frame;
    row.frame = frame;
    row.kind = kind;
    if (kind == MM_SCENE_PLAY_SOUND)
    {
        sound.header.ctagc = count;
        sound.tags[1].child = 1;
        data = &sound;
        bytes = mm_scene_sound_size(count);
    }
    else if (kind == MM_SCENE_PAUSE)
    {
        data = pause;
        bytes = sizeof(pause);
    }
    CHECK(mm_scene_track_insert(h->frames, h->frames->rows.count, bytes, data, &row));
}
static void test_core(void)
{
    Host h;
    MMSceneChopServices s = init(&h);
    int i;
    MMSceneTrackEvent row;
    MMI32 camera;
    const int forward[] = {100, 120, 101, 102, 120, 121, 130, 132, 131, 80, 81, 142};
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, h.objects + i));
    h.objects[1].alive = 1;
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects + i));
    h.objects[0].first = 6;
    h.objects[2].last = 4;
    h.owner.runtime.next_event = 1;
    append_frame(&h, 1, MM_SCENE_CHANGE_CAMERA, 0);
    append_frame(&h, 6, MM_SCENE_PLAY_SOUND, 2);
    append_frame(&h, 7, MM_SCENE_PAUSE, 0);
    CHECK(mm_scene_chop(0, &s));
    expect(&h, forward, sizeof(forward) / sizeof(*forward));
    CHECK(mm_scene_member_count(&h.owner, MM_SCENE_ACTORS) == 1 &&
          mm_scene_member_at(&h.owner, MM_SCENE_ACTORS, 0) == h.objects + 1 &&
          mm_scene_member_count(&h.owner, MM_SCENE_TEXTS) == 2 && h.frames->rows.count == 1 &&
          h.owner.data.lastFrame == 5);
    CHECK(mm_scene_chop_edit(0, &s) && !h.traces);
    mm_scene_track_dispose(h.frames);
    s = init(&h);
    h.removal_sticks = 1;
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, h.objects));
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects));
    h.objects[0].first = 6;
    CHECK(mm_scene_chop(0, &s) && mm_scene_member_count(&h.owner, MM_SCENE_ACTORS) == 1 &&
          mm_scene_member_count(&h.owner, MM_SCENE_TEXTS) == 1 && h.owner.data.lastFrame == 5);
    s = init(&h);
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects));
    h.objects[0].lifetime = 0;
    CHECK(mm_scene_chop(0, &s));
    {
        const int e[] = {144, 142};
        expect(&h, e, 2);
    }
    for (i = 0; i < 2; ++i)
    {
        s = init(&h);
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects));
        h.seek_fails = !i;
        h.show_fails = i;
        CHECK(mm_scene_chop(0, &s));
        if (!i)
        {
            const int e[] = {130, 142};
            expect(&h, e, 2);
        }
        else
        {
            const int e[] = {130, 132, 131, 142};
            expect(&h, e, 4);
        }
    }
    s = init(&h);
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects));
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects + 1));
    h.objects[0].last = 4;
    h.owner.runtime.next_event = 5;
    append_frame(&h, 1, MM_SCENE_CHANGE_CAMERA, 0);
    append_frame(&h, 2, MM_SCENE_PLAY_SOUND, 1);
    append_frame(&h, 3, MM_SCENE_CHANGE_CAMERA, 0);
    append_frame(&h, 5, MM_SCENE_PAUSE, 0);
    append_frame(&h, 5, MM_SCENE_PAUSE, 0);
    append_frame(&h, 7, MM_SCENE_PAUSE, 0);
    h.owner.owned[MM_SCENE_BACKGROUND_SOUND] = &h.background_sound;
    h.owner.runtime.background_frame = 2;
    CHECK(mm_scene_chop(1, &s));
    {
        const int e[] = {121, 133, 80, 142, 23, 143};
        expect(&h, e, sizeof(e) / sizeof(*e));
    }
    CHECK(h.owner.data.firstFrame == 5 && h.owner.runtime.next_event == 3 && h.frames->rows.count == 4);
    CHECK(mm_scene_track_get(h.frames, 0, &row) && row.kind == MM_SCENE_CHANGE_CAMERA && row.frame == 5);
    memcpy(&camera, mm_scene_track_payload(h.frames, 0, NULL), sizeof(camera));
    CHECK(camera == 3);
    CHECK(h.objects[1].first == 5 && mm_scene_member_count(&h.owner, MM_SCENE_TEXTS) == 1 &&
          h.owner.runtime.background_frame == -99);
    CHECK(mm_scene_chop_edit(1, &s) && !h.traces);
    mm_scene_track_dispose(h.frames);
    s = init(&h);
    h.owner.owned[MM_SCENE_BACKGROUND_SOUND] = &h.background_sound;
    h.owner.runtime.background_frame = 5;
    CHECK(mm_scene_chop(1, &s) && h.owner.owned[MM_SCENE_BACKGROUND_SOUND] && h.traces == 1);
}
static void test_transaction(void)
{
    Host h;
    MMSceneChopServices s;
    int i;
    for (i = 0; i < 4; ++i)
    {
        s = init(&h);
        h.fail = i;
        CHECK(mm_scene_chop_edit(0, &s) == !i);
        CHECK(h.owner.data.lastFrame == (i ? 10 : 5));
        if (i == 0)
        {
            const int e[] = {150, 151, 152, 153, 142};
            expect(&h, e, 5);
        }
        if (i == 1)
        {
            const int e[] = {150};
            expect(&h, e, 1);
        }
        if (i == 2)
        {
            const int e[] = {150, 151, 153};
            expect(&h, e, 3);
        }
        if (i == 3)
        {
            const int e[] = {150, 151, 152, 153};
            expect(&h, e, 4);
        }
    }
}
/* Snapshot operations are individually numbered; each failure stops at that
 * phase. Clear destroys the record immediately to check failure lifetime rules. */
static int step(Host *h, int n)
{
    CHECK(!h->dead);
    event(h, n);
    return h->fail != n;
}
static MMPrivateArchive *snapshot_open(void *p)
{
    Host *h = p;
    if (!step(h, 1))
        return NULL;
    h->file.fail_resize = h->fail == 2;
    return snapshot_file_open(&h->file);
}
static void begin(void *p)
{
    event((Host *)p, 3);
}
static void end(void *p)
{
    event((Host *)p, 4);
}
static int write(void *p, void *scene, MMPrivateArchive *archive, MMU32 *number)
{
    Host *h = (Host *)p;
    CHECK(scene == &h->old && archive == h->record->archive);
    ++h->write_calls;
    *number = 42;
    if (!step(h, 5))
        return 0;
    put_snapshot(archive, *number);
    return 1;
}
static void movie_graph(void *file, MMMovieSessionGraph *out)
{
    *out = fixture_scene_graph(&((Host *)file)->archive);
}
static void *read_movie_scene(void *p, void *resources, MMU32 number, const MMMovieSession *session)
{
    Host *h = (Host *)p;
    const MMI32 index = fixture_scene_slot(&h->archive, number);
    SnapshotScene *scene;
    CHECK(resources == p && h->data.session == session);
    if (index == 2 && ++h->switch_calls == 1)
    {
        if (!step(h, 8))
            return NULL;
        event(h, 9);
        scene = &h->old;
    }
    else if (index == 2)
    {
        if (h->data.sceneCount == 4 && !step(h, 16))
            return NULL;
        scene = &h->restored;
    }
    else
    {
        CHECK(index == 1 || index == 3);
        scene = index == 1 ? &h->initial : &h->neighbor;
    }
    CHECK(!scene->refs);
    scene->refs = 1;
    return scene;
}
static void retain(void *p)
{
    SnapshotScene *scene = p;
    Host *h = scene->host;
    CHECK(scene->refs > 0);
    if (scene == &h->old)
        event(h, 10);
    ++scene->refs;
}
static void close_scene(void *p)
{
    SnapshotScene *scene = p;
    Host *h = scene->host;
    CHECK(scene->refs > 0);
    if (!--scene->refs && scene == &h->old)
        event(h, 26);
}
static void release_movie_scene(void *p)
{
    SnapshotScene *scene = p;
    Host *h = scene->host;
    CHECK(scene->refs > 0);
    if (scene == &h->old)
        event(h, 22);
    --scene->refs;
}
static int open_archive(void *p)
{
    return step(p, 11);
}
static int temporary(void *p)
{
    Host *h = (Host *)p;
    return h->data.sceneCount == 4 || step(h, 15);
}
static int copy_snapshot(void *p, const MMSceneSnapshot *snapshot, MMU32 *number)
{
    Host *h = (Host *)p;
    CHECK(snapshot == h->record && snapshot->number == 17 && has_snapshot(snapshot->archive, snapshot->number));
    if (!step(h, 6))
        return 0;
    *number = 124;
    fixture_chunk_add(&h->archive, MM_SCENE_CHUNK, *number);
    return 1;
}
static MMChunkEdit edit_archive(void *p) { return fixture_chunk_edit(&((Host *)p)->archive); }
static int succeed(void *p) { (void)p; return 1; }
static void ignore(void *p) { (void)p; }
static int insert_scene_link(void *p, MMU32 parent, const MMChunkChild *child)
{
    (void)p;
    CHECK(parent == FIXTURE_MOVIE_ROOT && child->type == MM_SCENE_CHUNK &&
          child->number == 124 && child->slot == 2);
    return 1;
}
static void erase_scene_link(void *p, MMU32 parent, const MMChunkChild *child)
{
    (void)p;
    CHECK(parent == FIXTURE_MOVIE_ROOT && child->type == MM_SCENE_CHUNK &&
          child->number == 102 && child->slot == 3);
}
static MMMovieSceneServices scene_services(Host *h)
{
    MMMovieSceneServices s = {0};
    h->initial.host = h->old.host = h->restored.host = h->neighbor.host = h;
    h->initial.refs = 1;
    h->movie.open = &h->initial;
    h->movie.index = 1;
    h->data.sceneCount = 4;
    h->data.movieNumber = FIXTURE_MOVIE_ROOT;
    h->data.resources = h;
    fixture_scene_archive_init(&h->archive, h->data.sceneCount);
    h->archive.before_insert = insert_scene_link;
    h->archive.before_unlink = erase_scene_link;
    s.context = h;
    s.state = &h->movie;
    s.movie = &h->data;
    s.autosave = succeed;
    s.make_resources = open_archive;
    s.use_temporary = temporary;
    s.stop_loops = s.memory_cleanup = ignore;
    s.session.graph = movie_graph;
    s.read = read_movie_scene;
    s.start = succeed;
    s.seek_first = succeed;
    s.retain = retain;
    s.release = release_movie_scene;
    s.close = close_scene;
    s.sound_frame = s.scene_change = ignore;
    s.edit = edit_archive;
    s.copy_snapshot = copy_snapshot;
    s.save = s.dirty = s.rollcall_changed = ignore;
    s.remove_actors = s.add_actors = ignore;
    s.sounds_enabled = succeed;
    s.play_sound = ignore;
    return s;
}
static void rewind_scene(void *p, void *scene)
{
    Host *h = (Host *)p;
    CHECK(scene == &h->old);
    CHECK(!has_snapshot(h->record->archive, 17));
    CHECK(step(h, 13));
}
static void seek_scene(void *p, MMI32 frame)
{
    SnapshotScene *scene = p;
    Host *h = scene->host;
    CHECK(frame == 9 && scene == &h->restored && h->movie.index == 2);
    h->sought = 1;
    CHECK(step(h, 17));
}
static void invalidate(void *p)
{
    Host *h = p;
    if (h->sought)
        CHECK(step(h, 18));
}
static void flush(void *p)
{
    CHECK(step((Host *)p, 19));
}
static void clear_snapshot(void *p)
{
    Host *h = (Host *)p;
    event(h, 20);
    mm_scene_snapshot_dispose(h->record);
    free(h->record);
    h->record = NULL;
    h->dead = 1;
}
static MMSceneChopSwapServices snapshots(Host *h)
{
    MMSceneChopSwapServices s;
    memset(&s, 0, sizeof(s));
    s.snapshot.context = h;
    s.snapshot.open = snapshot_open;
    s.snapshot.begin = begin;
    s.snapshot.end = end;
    s.snapshot.write = write;
    s.rewind = rewind_scene;
    s.seek = seek_scene;
    return s;
}
static void test_snapshot(void)
{
    Host h;
    MMSceneChopSwapServices s;
    MMSceneSnapshot record;
    int i;
    const int failures[] = {0, 6, 8, 11, 5, 15, 16};
    for (i = 0; i < 4; ++i)
    {
        memset(&h, 0, sizeof(h));
        s = snapshots(&h);
        memset(&record, 0, sizeof(record));
        h.record = &record;
        h.fail = i == 3 ? 5 : i;
        CHECK(mm_scene_snapshot_save(&record, &h.old, &s.snapshot) == (i == 0));
        if (i == 0)
        {
            const int e[] = {1, 3, 5, 4};
            CHECK(record.archive && has_snapshot(record.archive, record.number));
            expect(&h, e, 4);
        }
        if (i == 1 || i == 2)
        {
            const int e[] = {1};
            CHECK(!record.archive);
            expect(&h, e, 1);
        }
        if (i == 3)
        {
            const int e[] = {1, 3, 5, 4};
            CHECK(record.archive && !has_snapshot(record.archive, record.number));
            expect(&h, e, 4);
        }
        mm_scene_snapshot_dispose(&record);
        CHECK(!record.archive && !h.file.refs);
    }
    for (i = 0; i < 7; ++i)
    {
        MMMovieSceneServices scenes;
        int ok;
        memset(&h, 0, sizeof(h));
        s = snapshots(&h);
        scenes = scene_services(&h);
        scenes.invalidate = invalidate;
        scenes.flush_sound = flush;
        scenes.clear_undo = clear_snapshot;
        h.fail = failures[i];
        h.record = (MMSceneSnapshot *)malloc(sizeof(*h.record));
        CHECK(h.record);
        h.record->archive = snapshot_open(&h);
        CHECK(h.record->archive);
        h.record->number = 17;
        put_snapshot(h.record->archive, h.record->number);
        h.traces = 0;
        ok = mm_scene_chop_swap(h.record, 2, 9, &s, &scenes);
        CHECK(ok == (h.fail == 0 || h.fail == 16));
        if (!ok)
        {
            CHECK(!h.record && h.dead);
            CHECK(h.traces >= 3 && h.trace[h.traces - 3] == 19 && h.trace[h.traces - 2] == 20 &&
                  h.trace[h.traces - 1] == 4);
        }
        if (h.fail == 0)
        {
            const int e[] = {3, 8, 9, 10, 11, 6, 13, 5, 26, 15, 16, 17, 18, 4};
            expect(&h, e, 14);
            CHECK(h.record->number == 42 && has_snapshot(h.record->archive, h.record->number));
        }
        if (h.fail == 11)
        {
            const int e[] = {3, 8, 9, 10, 11, 22, 19, 20, 4};
            expect(&h, e, 9);
        }
        if (h.fail == 5)
        {
            const int e[] = {3, 8, 9, 10, 11, 6, 13, 5, 15, 26, 19, 20, 4};
            expect(&h, e, 13);
        }
        if (h.fail == 16)
        {
            CHECK(h.record && h.trace[h.traces - 2] == 16 && h.trace[h.traces - 1] == 4);
        }
        if (h.record)
        {
            mm_scene_snapshot_dispose(h.record);
            free(h.record);
        }
        mm_movie_scene_close_open(&scenes);
        mm_movie_session_close(h.data.session);
        CHECK(!h.old.refs && !h.initial.refs && !h.restored.refs && !h.neighbor.refs);
        CHECK(!h.file.refs);
    }
}
static void test_snapshot_copy(void)
{
    static const MMChunkChild links[] = {
        {MM_ACTOR_CHUNK, 23, 0}, {MM_BACKGROUND, 35, 7}, {MM_SOUND_CHUNK, 37, 4},
        {MM_TMPL, 31, 0}, {MM_MATERIAL_CHUNK, 33, 5}
    };
    SnapshotFile source = {0}, destination = {0};
    MMSceneSnapshot snapshot;
    MMPrivateArchive *target = snapshot_file_open(&destination);
    MMChunkFileCopy binding;
    MMChunkEndpoint endpoint;
    MMMovieArchiveEditor before, after;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMActorHeader header = {0};
    MMActorEventTable *events;
    MMActorEvent event = {0};
    MMActorCostume costume = {0};
    MMActorSound sound = {0};
    MMSceneTrack frames, starts;
    MMSceneTrackEvent start = {1, MM_SCENE_SET_BACKGROUND}, frame = {2, MM_SCENE_PLAY_SOUND};
    const MMU32 background[] = {0, 0, MM_BACKGROUND, 35};
    const MMU32 cue[] = {80, 1, 0, 1, 4, 0, 0, MM_SOUND_CHUNK, 37};
    MMMovieSceneSource reader_source = {0};
    MMMovieSceneReader reader;
    MMMovieSceneGroup group;
    MMMovieSceneEvent row;
    MMSceneSoundWire wire;
    MMTag tag;
    MMU32 i, scene, actor, bg, msnd, tmpl, mtrl, number, slot, count;

    snapshot.archive = snapshot_file_open(&source);
    snapshot.number = 17;
    CHECK(snapshot.archive && target);
    before = mm_private_archive_editor(snapshot.archive);
    after = mm_private_archive_editor(target);
    put_snapshot(snapshot.archive, snapshot.number);
    put_snapshot(target, snapshot.number);
    for (i = 0; i < sizeof(links) / sizeof(*links); ++i)
    {
        const MMChunkChild *link = &links[i];
        if (i != 0)
            CHECK(before.put(before.context, link->type, link->number, "a", 1));
        CHECK(after.put(after.context, link->type, link->number, "b", 1));
    }
    header.order = 1;
    header.template_type = MM_TMPL;
    header.template_number = links[3].number;
    actor = links[0].number;
    CHECK(before.put(before.context, MM_ACTOR_CHUNK, actor, &header, sizeof(header)));
    for (i = 0; i < sizeof(links) / sizeof(*links); ++i)
        CHECK(mm_chunk_adopt(&before.edit, i < 3 ? MM_SCENE_CHUNK : MM_ACTOR_CHUNK,
                             i < 3 ? snapshot.number : actor, &links[i], 1) == MM_CHUNK_OK);
    events = mm_actor_events_create(NULL, 0, 0);
    CHECK(events);
    event.aet = MM_ACTOR_COSTUME;
    costume.tag.type = MM_MATERIAL_CHUNK;
    costume.tag.number = links[4].number;
    CHECK(mm_actor_events_insert(events, 0, sizeof(costume), &costume, &event));
    event.aet = MM_ACTOR_SOUND;
    sound.child = links[2].slot;
    sound.tag.type = MM_SOUND_CHUNK;
    sound.tag.number = links[2].number;
    CHECK(mm_actor_events_insert(events, 1, sizeof(sound), &sound, &event));
    CHECK(mm_actor_archive_write_event_group(&before, events, &number, &actor));
    mm_actor_events_release(events);
    CHECK(mm_scene_track_init(&frames, NULL, 0) && mm_scene_track_init(&starts, NULL, 0));
    CHECK(mm_scene_track_insert(&frames, 0, sizeof(cue), cue, &frame));
    CHECK(mm_scene_track_insert(&starts, 0, sizeof(background), background, &start));
    CHECK(mm_scene_archive_write_tracks(&before, snapshot.number, &frames, &starts));
    mm_scene_track_dispose(&frames);
    mm_scene_track_dispose(&starts);

    endpoint = mm_private_archive_copy_target(target, &binding);
    CHECK(mm_scene_snapshot_copy(&snapshot, &endpoint, &after, &scene) && scene != snapshot.number);
    chunks = mm_chunk_edit_access(&after.edit);
    CHECK(mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, links[0].slot, MM_ACTOR_CHUNK, &actor) == MM_CHUNK_OK &&
          actor != links[0].number);
    CHECK(mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, links[1].slot, MM_BACKGROUND, &bg) == MM_CHUNK_OK &&
          bg != links[1].number);
    CHECK(mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, links[2].slot, MM_SOUND_CHUNK, &msnd) == MM_CHUNK_OK &&
          msnd != links[2].number);
    CHECK(mm_chunk_link_number(&chunks, MM_ACTOR_CHUNK, actor, links[3].slot, MM_TMPL, &tmpl) == MM_CHUNK_OK &&
          tmpl != links[3].number);
    CHECK(mm_chunk_link_number(&chunks, MM_ACTOR_CHUNK, actor, links[4].slot, MM_MATERIAL_CHUNK, &mtrl) == MM_CHUNK_OK &&
          mtrl != links[4].number);
    CHECK(mm_actor_archive_read_actor(&after, actor, &header) && header.template_number == tmpl);
    events = mm_actor_archive_read_event_group(&after, actor, NULL);
    CHECK(events && mm_actor_events_count(events) == 2);
    CHECK(mm_actor_events_get_payload(events, 0, &costume) && costume.tag.number == mtrl);
    CHECK(mm_actor_events_get_payload(events, 1, &sound) && sound.tag.number == msnd && sound.child == links[2].slot);
    mm_actor_events_release(events);

    reader_source.graph = mm_movie_owner_graph(mm_private_archive_reader(target));
    reader = mm_movie_scene_reader(&reader_source);
    CHECK(reader.group(reader.context, scene, 1, &group) == MM_MOVIE_OK && group.count == 1);
    reader.event(reader.context, &group, 0, &row);
    mm_movie_scene_event_tag(&row, &tag);
    CHECK(row.kind == MM_SCENE_SET_BACKGROUND && tag.number == bg);
    reader.close(reader.context, &group);
    CHECK(reader.group(reader.context, scene, 0, &group) == MM_MOVIE_OK && group.count == 1);
    reader.event(reader.context, &group, 0, &row);
    CHECK(row.kind == MM_SCENE_PLAY_SOUND && mm_scene_sound_wire(row.data, row.bytes, row.big_endian, &wire));
    mm_scene_sound_wire_tag(&wire, 0, &slot, &tag);
    CHECK(slot == links[2].slot && tag.number == msnd);
    reader.close(reader.context, &group);

    for (count = 0; mm_chunk_at(&chunks, count, &entry) == MM_CHUNK_OK; ++count)
        ;
    CHECK(mm_chunk_unlink(&before.edit, MM_SCENE_CHUNK, snapshot.number, &links[1]) == MM_CHUNK_OK);
    number = scene;
    CHECK(!mm_scene_snapshot_copy(&snapshot, &endpoint, &after, &number) && number == scene);
    chunks = mm_chunk_edit_access(&after.edit);
    for (i = 0; mm_chunk_at(&chunks, i, &entry) == MM_CHUNK_OK; ++i)
        ;
    CHECK(i == count && has_snapshot(target, scene) && has_snapshot(snapshot.archive, snapshot.number));
    mm_scene_snapshot_dispose(&snapshot);
    mm_private_archive_close(target);
    CHECK(!source.refs && !destination.refs);
}
int main(void)
{
    test_core();
    test_transaction();
    test_snapshot();
    test_snapshot_copy();
    puts("Scene chop policy passed");
    return 0;
}
