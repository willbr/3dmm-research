#include "mm/scene_chop.h"
#include "scene_member_fixture.h"
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
typedef struct Frame
{
    MMI32 frame;
    int kind, tags;
} Frame;
typedef struct Host
{
    MMSceneState data;
    MMSceneRuntime runtime;
    MMSceneOwner owner;
    SceneMemberMemory members;
    Obj objects[5];
    Frame frames[8];
    int frame_count;
    int trace[128], traces, fail, stage, removal_sticks, seek_fails, show_fails, write_calls, switch_calls, dead;
    int file, resource, loaded, old, undo, has_resource;
    MMSceneChopUndo *record;
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
static MMI32 count(void *p, void *group)
{
    Host *h = (Host *)p;
    CHECK(group == h->frames);
    return h->frame_count;
}
static void read_event(void *p, void *g, MMI32 i, MMI32 *frame, int *kind)
{
    Host *h = (Host *)p;
    (void)g;
    CHECK(i >= 0 && i < h->frame_count);
    *frame = h->frames[i].frame;
    *kind = h->frames[i].kind;
}
static void *payload(void *p, void *g, MMI32 i)
{
    (void)g;
    return &((Host *)p)->frames[i];
}
static MMI32 tags(void *p, void *data)
{
    if (data == &((Host *)p)->resource)
        return 0;
    return ((Frame *)data)->tags;
}
static MMI32 style(void *p, void *data)
{
    (void)p;
    (void)data;
    return 0;
}
static void close_tag(void *p, void *data, MMI32 i)
{
    (void)data;
    event((Host *)p, 80 + (int)i);
}
static void release(void *p, void *obj)
{
    Host *h = (Host *)p;
    CHECK(obj);
    event(h, obj == &h->file ? 21 : obj == &h->old ? 22 : 23);
    if (obj == &h->resource && h->record)
        CHECK(h->record->resource == obj);
}
static void publish(void *p, void *scene, const MMSceneOwner *o)
{
    Host *h = (Host *)p;
    (void)scene;
    CHECK(!o->owned[MM_SCENE_BACKGROUND_SOUND]);
    event(h, 24);
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
static void erase(void *p, MMI32 i)
{
    Host *h = (Host *)p;
    CHECK(i >= 0 && i < h->frame_count);
    event(h, 140);
    memmove(h->frames + i, h->frames + i + 1, (h->frame_count - i - 1) * sizeof(Frame));
    --h->frame_count;
}
static void set_frame(void *p, MMI32 i, MMI32 frame)
{
    Host *h = (Host *)p;
    h->frames[i].frame = frame;
    event(h, 141);
}
static void dirty(void *p)
{
    event((Host *)p, 142);
}
static void discard(void *p)
{
    Host *h = (Host *)p;
    CHECK(!h->owner.owned[MM_SCENE_BACKGROUND_SOUND]);
    h->runtime.background_frame = -99;
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
    h->data.firstFrame = 1;
    h->data.lastFrame = 10;
    h->data.currentFrame = 5;
    for (i = 0; i < 5; ++i)
    {
        h->objects[i].id = i;
        h->objects[i].lifetime = 1;
        h->objects[i].first = 1;
        h->objects[i].last = 10;
    }
    h->owner.owned[MM_SCENE_FRAMES] = h->frames;
    s.context = h;
    s.data = &h->data;
    s.events.context = h;
    s.events.owner = &h->owner;
    s.events.runtime = &h->runtime;
    s.events.data = &h->data;
    s.events.owners.context = h;
    s.events.owners.count = count;
    s.events.owners.release = release;
    s.events.owners.publish = publish;
    s.events.owners.sound.context = h;
    s.events.owners.sound.count = tags;
    s.events.owners.sound.style = style;
    s.events.owners.sound.free = release;
    s.events.owners.sound.close = close_tag;
    s.events.event = read_event;
    s.events.payload = payload;
    s.events.seek = seek;
    s.trim_actor = trim;
    s.remove_actor = remove_actor;
    s.remove_text = remove_text;
    s.lifetime = lifetime;
    s.show_text = show;
    s.start_text = start_text;
    s.erase_frame = erase;
    s.set_frame = set_frame;
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
static void test_core(void)
{
    Host h;
    MMSceneChopServices s = init(&h);
    int i;
    const int forward[] = {100, 120, 101, 102, 120, 121, 130, 132, 131, 80, 81, 140, 140, 142};
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, h.objects + i));
    h.objects[1].alive = 1;
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects + i));
    h.objects[0].first = 6;
    h.objects[2].last = 4;
    h.runtime.next_event = 1;
    h.frame_count = 3;
    h.frames[0].frame = 1;
    h.frames[1].kind = MM_SCENE_PLAY_SOUND;
    h.frames[1].tags = 2;
    h.frames[2].kind = MM_SCENE_PAUSE;
    CHECK(mm_scene_chop(0, &s));
    expect(&h, forward, 14);
    CHECK(mm_scene_member_count(&h.owner, MM_SCENE_ACTORS) == 1 &&
          mm_scene_member_at(&h.owner, MM_SCENE_ACTORS, 0) == h.objects + 1 &&
          mm_scene_member_count(&h.owner, MM_SCENE_TEXTS) == 2 && h.frame_count == 1 &&
          h.data.lastFrame == 5);
    CHECK(mm_scene_chop_edit(0, &s) && !h.traces);
    s = init(&h);
    h.removal_sticks = 1;
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, h.objects));
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, h.objects));
    h.objects[0].first = 6;
    CHECK(mm_scene_chop(0, &s) && mm_scene_member_count(&h.owner, MM_SCENE_ACTORS) == 1 &&
          mm_scene_member_count(&h.owner, MM_SCENE_TEXTS) == 1 && h.data.lastFrame == 5);
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
    h.frame_count = 6;
    h.runtime.next_event = 5;
    for (i = 0; i < 6; ++i)
    {
        h.frames[i].frame = i < 3 ? i + 1 : i == 5 ? 7 : 5;
        h.frames[i].kind = MM_SCENE_PAUSE;
    }
    h.frames[0].kind = h.frames[2].kind = MM_SCENE_CHANGE_CAMERA;
    h.frames[1].kind = MM_SCENE_PLAY_SOUND;
    h.frames[1].tags = 1;
    h.owner.owned[MM_SCENE_BACKGROUND_SOUND] = &h.resource;
    h.runtime.background_frame = 2;
    CHECK(mm_scene_chop(1, &s));
    {
        const int e[] = {121, 133, 141, 80, 140, 140, 142, 23, 24, 143};
        expect(&h, e, 10);
    }
    CHECK(h.data.firstFrame == 5 && h.runtime.next_event == 3 && h.frame_count == 4 &&
          h.frames[0].kind == MM_SCENE_CHANGE_CAMERA && h.frames[0].frame == 5);
    CHECK(h.objects[1].first == 5 && mm_scene_member_count(&h.owner, MM_SCENE_TEXTS) == 1 &&
          h.runtime.background_frame == -99);
    CHECK(mm_scene_chop_edit(1, &s) && !h.traces);
    s = init(&h);
    h.owner.owned[MM_SCENE_BACKGROUND_SOUND] = &h.resource;
    h.runtime.background_frame = 5;
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
        CHECK(h.data.lastFrame == (i ? 10 : 5));
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
static void *new_file(void *p)
{
    Host *h = (Host *)p;
    return step(h, 1) ? &h->file : NULL;
}
static void *new_resource(void *p, void *f)
{
    Host *h = (Host *)p;
    CHECK(f == &h->file);
    return step(h, 2) ? &h->resource : NULL;
}
static void begin(void *p)
{
    event((Host *)p, 3);
}
static void end(void *p)
{
    event((Host *)p, 4);
}
static int write(void *p, void *scene, void *r, MMU32 *number)
{
    Host *h = (Host *)p;
    CHECK(scene == &h->old && r == &h->resource);
    ++h->write_calls;
    *number = 42;
    return step(h, 5);
}
static void *read_snapshot(void *p, void *r, MMU32 number)
{
    Host *h = (Host *)p;
    CHECK(r == &h->resource && number == 17);
    return step(h, 6) ? &h->loaded : NULL;
}
static int play(void *p, void *scene)
{
    Host *h = (Host *)p;
    CHECK(scene == &h->loaded);
    return step(h, 7);
}
static int switch_scene(void *p, MMI32 index)
{
    Host *h = (Host *)p;
    CHECK(index == 2);
    return step(h, ++h->switch_calls == 1 ? 8 : 16);
}
static void *current(void *p)
{
    Host *h = (Host *)p;
    CHECK(step(h, 9));
    return &h->old;
}
static void retain(void *p, void *scene)
{
    Host *h = (Host *)p;
    CHECK(scene == &h->old);
    CHECK(step(h, 10));
}
static void close_scene(void *p, void *scene)
{
    Host *h = (Host *)p;
    event(h, scene == &h->loaded ? 25 : 26);
}
static int insert(void *p, MMI32 index, void *scene)
{
    Host *h = (Host *)p;
    CHECK(index == 2 && scene == &h->loaded);
    return step(h, 11);
}
static int remove_scene(void *p, MMI32 index)
{
    Host *h = (Host *)p;
    CHECK(index == 3);
    return step(h, 15);
}
static void delete_chunk(void *p, void *r, MMU32 number)
{
    Host *h = (Host *)p;
    CHECK(r == &h->resource && number == 17);
    CHECK(step(h, 12));
}
static void rewind_scene(void *p, void *scene)
{
    Host *h = (Host *)p;
    CHECK(scene == &h->old);
    CHECK(step(h, 13));
}
static void seek_scene(void *p, MMI32 frame)
{
    Host *h = (Host *)p;
    CHECK(frame == 9);
    CHECK(step(h, 17));
}
static void invalidate(void *p)
{
    CHECK(step((Host *)p, 18));
}
static void flush(void *p)
{
    CHECK(step((Host *)p, 19));
}
static void clear_snapshot(void *p)
{
    Host *h = (Host *)p;
    event(h, 20);
    free(h->record);
    h->record = NULL;
    h->dead = 1;
}
static MMSceneSnapshotServices snapshots(Host *h)
{
    MMSceneSnapshotServices s;
    memset(&s, 0, sizeof(s));
    s.context = h;
    s.new_file = new_file;
    s.new_resource = new_resource;
    s.release = release;
    s.begin = begin;
    s.end = end;
    s.write = write;
    s.read = read_snapshot;
    s.start = play;
    s.switch_scene = switch_scene;
    s.current = current;
    s.retain = retain;
    s.close = close_scene;
    s.insert = insert;
    s.remove = remove_scene;
    s.delete_chunk = delete_chunk;
    s.rewind = rewind_scene;
    s.seek = seek_scene;
    s.invalidate = invalidate;
    s.flush = flush;
    s.clear = clear_snapshot;
    return s;
}
static void test_snapshot(void)
{
    Host h;
    MMSceneSnapshotServices s;
    MMSceneChopUndo record;
    int i;
    const int failures[] = {0, 6, 7, 8, 11, 5, 15, 16};
    for (i = 0; i < 4; ++i)
    {
        memset(&h, 0, sizeof(h));
        s = snapshots(&h);
        memset(&record, 0, sizeof(record));
        h.record = &record;
        h.fail = i == 3 ? 5 : i;
        CHECK(mm_scene_chop_save(&record, &h.old, &s) == (i == 0));
        CHECK(record.resource == ((i == 1 || i == 2) ? NULL : &h.resource));
        if (i == 0)
        {
            const int e[] = {1, 2, 21, 3, 5, 4};
            expect(&h, e, 6);
        }
        if (i == 1)
        {
            const int e[] = {1};
            expect(&h, e, 1);
        }
        if (i == 2)
        {
            const int e[] = {1, 2, 21};
            expect(&h, e, 3);
        }
        if (i == 3)
        {
            const int e[] = {1, 2, 21, 3, 5, 4};
            expect(&h, e, 6);
        }
        mm_scene_chop_dispose(&record, &s);
        CHECK(!record.resource);
    }
    for (i = 0; i < 8; ++i)
    {
        int ok;
        memset(&h, 0, sizeof(h));
        s = snapshots(&h);
        h.fail = failures[i];
        h.record = (MMSceneChopUndo *)malloc(sizeof(*h.record));
        CHECK(h.record);
        h.record->resource = &h.resource;
        h.record->number = 17;
        ok = mm_scene_chop_swap(h.record, 2, 9, &s);
        CHECK(ok == (i == 0 || i == 7));
        if (!ok)
        {
            CHECK(!h.record && h.dead);
            CHECK(h.traces >= 3 && h.trace[h.traces - 3] == 19 && h.trace[h.traces - 2] == 20 &&
                  h.trace[h.traces - 1] == 4);
        }
        if (i == 0)
        {
            const int e[] = {3, 6, 7, 8, 9, 10, 11, 25, 12, 13, 5, 26, 15, 16, 17, 18, 4};
            expect(&h, e, 17);
            CHECK(h.record->number == 42);
        }
        if (i == 4)
        {
            const int e[] = {3, 6, 7, 8, 9, 10, 11, 22, 25, 19, 20, 4};
            expect(&h, e, 12);
        }
        if (i == 5)
        {
            const int e[] = {3, 6, 7, 8, 9, 10, 11, 25, 12, 13, 5, 15, 26, 19, 20, 4};
            expect(&h, e, 16);
        }
        if (i == 7)
        {
            CHECK(h.record && h.trace[h.traces - 2] == 16 && h.trace[h.traces - 1] == 4);
        }
        if (h.record)
        {
            mm_scene_chop_dispose(h.record, &s);
            free(h.record);
        }
    }
}
int main(void)
{
    test_core();
    test_transaction();
    test_snapshot();
    puts("Scene chop policy passed");
    return 0;
}
