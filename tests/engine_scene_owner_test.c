#include "mm/scene_owner.h"
#include "mm/scene_sound.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Sound
{
    MMSceneSound header;
    MMSceneSoundTag tags[4];
    int closed[4], freed;
} Sound;
typedef struct FrameSound
{
    MMSceneSound header;
    MMSceneSoundTag records[3];
} FrameSound;
typedef struct Object
{
    int slot, id, refs, lifetime, first, seek_ok;
} Object;
typedef struct Fixture
{
    MMSceneOwner owner;
    MMSceneOwnerServices s;
    Object scene, slots[MM_SCENE_OWNERS], children[5];
    FrameSound frame[2];
    Sound background;
    int frame_closed[2][2];
    int events[256], n, live, attempts, fail, available, loaded, diagnostics, seeks, last_frame;
} Fixture;
static void event(Fixture *f, int n)
{
    assert(f->n < 256);
    f->events[f->n++] = n;
}
static void *resize_track(void *p, void *data, MMU32 old_bytes, MMU32 bytes, int priority)
{
    (void)p;
    (void)old_bytes;
    (void)priority;
    return realloc(data, (size_t)bytes);
}
static void release_track(void *p, void *data, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    int track;
    (void)bytes;
    for (track = 0; track < MM_SCENE_TRACKS; ++track)
        if (data == f->owner.tracks[track].storage.buffers[0].data)
            event(f, 210 + track);
    free(data);
}
static MMSceneOwner *state(void *p, void *scene)
{
    Fixture *f = (Fixture *)p;
    assert(scene == &f->scene);
    return &f->owner;
}
static void *create_scene(void *p, void *movie)
{
    Fixture *f = (Fixture *)p;
    MMStorageAllocator allocator = {NULL, resize_track, release_track};
    if (++f->attempts == f->fail)
        return NULL;
    f->scene.refs = 1;
    f->live = 1;
    allocator.context = f;
    assert(mm_scene_owner_init(&f->owner, movie, 1, &allocator));
    return &f->scene;
}
static void release(void *p, void *object)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)object;
    assert(o && o->refs > 0);
    if (o == &f->scene)
    {
        event(f, 500);
        if (!--o->refs)
        {
            mm_scene_owner_dispose(&f->owner, &f->s);
            --f->live;
        }
    }
    else
    {
        if (o->slot >= 0)
            assert(f->owner.owned[o->slot] == object);
        event(f, o->slot >= 0 ? 200 + o->slot : 100 + o->id);
        assert(!--o->refs);
        --f->live;
    }
}
static int available(void *p)
{
    return ((Fixture *)p)->available;
}
static void loaded(void *p, void *scene)
{
    Fixture *f = (Fixture *)p;
    int i;
    assert(scene == &f->scene);
    for (i = 0; i < MM_SCENE_TRACKS; ++i)
        assert(f->owner.tracks[i].rows.storage == &f->owner.tracks[i].storage && !f->owner.tracks[i].rows.count);
    ++f->loaded;
    event(f, 30);
}
static void visibility(void *p, void *actor, int show)
{
    event((Fixture *)p, (show ? 700 : 600) + ((Object *)actor)->id);
}
static void lights(void *p, void *background)
{
    (void)background;
    event((Fixture *)p, 550);
}
static int lifetime(void *p, void *text, MMI32 *first)
{
    Object *o = (Object *)text;
    (void)p;
    *first = o->first;
    return o->lifetime;
}
static int seek(void *p, void *text, MMI32 frame)
{
    Fixture *f = (Fixture *)p;
    ++f->seeks;
    f->last_frame = frame;
    event(f, 800 + ((Object *)text)->id);
    return ((Object *)text)->seek_ok;
}
static void diagnostic(void *p, int issue)
{
    Fixture *f = (Fixture *)p;
    ++f->diagnostics;
    event(f, 900 + issue);
}
static void sound_close(void *p, MMSceneSoundTag *tag)
{
    Fixture *f = (Fixture *)p;
    MMU32 i = tag->child;
    if (tag->tag.cache == &f->background)
    {
        Sound *s = &f->background;
        assert(i < 4 && !s->closed[i]);
        s->closed[i] = 1;
        event(f, 450 + i);
    }
    else
    {
        MMU32 packet = tag->tag.number;
        assert(packet < 2 && i < 2 && !f->frame_closed[packet][i]);
        f->frame_closed[packet][i] = 1;
        event(f, 400 + 20 * packet + i);
    }
    tag->tag.cache = NULL;
}
static void sound_free(void *p, void *sound)
{
    Fixture *f = (Fixture *)p;
    Sound *s = (Sound *)sound;
    assert(!s->freed);
    s->freed = 1;
    event(f, 460);
}
static void init(Fixture *f)
{
    MMSceneOwnerServices *s;
    int i;
    memset(f, 0, sizeof(*f));
    for (i = 0; i < MM_SCENE_OWNERS; ++i)
        f->slots[i].slot = i;
    for (i = 0; i < 5; ++i)
    {
        f->children[i].slot = -1;
        f->children[i].id = i;
    }
    s = &f->s;
    s->context = f;
    s->create_scene = create_scene;
    s->state = state;
    s->can_notify = available;
    s->loaded = loaded;
    s->release = release;
    s->actor_visibility = visibility;
    s->lights_off = lights;
    s->text_lifetime = lifetime;
    s->text_seek = seek;
    s->diagnostic = diagnostic;
    s->sound.context = f;
    s->sound.close = sound_close;
    s->sound.free = sound_free;
    s->sound.diagnostic = diagnostic;
}
static void populate(Fixture *f)
{
    int i, j;
    MMSceneTrack *starts = &f->owner.tracks[MM_SCENE_STARTS], *frames = &f->owner.tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent row;
    void *object;
    assert(mm_scene_owner_new(f, &f->s) == &f->scene);
    for (i = 0; i < 5; ++i)
    {
        f->children[i].refs = 1;
        ++f->live;
    }
    for (i = 0; i < 3; ++i)
    {
        row.frame = i;
        row.kind = i == 2 ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR;
        object = &f->children[i];
        assert(mm_scene_track_insert(starts, i, sizeof(object), &object, &row));
    }
    for (i = 0; i < 2; ++i)
    {
        f->frame[i].header.ctagc = 2;
        for (j = 0; j < 2; ++j)
        {
            f->frame[i].records[j].child = j;
            f->frame[i].records[j].tag.number = i;
        }
        row.frame = i + 1;
        row.kind = MM_SCENE_PLAY_SOUND;
        assert(mm_scene_track_insert(frames, i, mm_scene_sound_size(2), &f->frame[i], &row));
    }
    f->background.header.ctagc = 3;
    f->background.header.sty = 4;
    f->background.tags[1].child = 1;
    f->background.tags[1].tag.cache = &f->background;
    f->owner.owned[MM_SCENE_BACKGROUND_SOUND] = &f->background;
    for (i = MM_SCENE_BACKGROUND; i <= MM_SCENE_THUMBNAIL; ++i)
    {
        f->slots[i].refs = 1;
        ++f->live;
        f->owner.owned[i] = &f->slots[i];
    }
    /* Two spare children model other externally owned objects, not list owners. */
    f->children[3].refs = f->children[4].refs = 0;
    f->live -= 2;
    f->n = 0;
}
static void factories(void)
{
    Fixture f;
    void *p;
    int i;
    assert(!mm_scene_owner_init(&f.owner, NULL, 3, NULL));
    init(&f);
    f.fail = 1;
    assert(!mm_scene_owner_new(&f, &f.s));
    assert(!f.live && !f.loaded);
    for (i = 0; i < MM_SCENE_OWNERS; ++i)
        assert(!f.owner.owned[i]);
    init(&f);
    f.available = 1;
    p = mm_scene_owner_new(&f, &f.s);
    assert(p && f.loaded == 1 && f.n == 1 && f.events[0] == 30);
    mm_scene_owner_close(&p, &f.s);
    assert(!p && !f.live);
    init(&f);
    assert(mm_scene_owner_new(&f, &f.s));
    assert(!f.loaded);
    f.owner.selected_actor = &f;
    f.owner.selected_text = &f;
    assert(mm_storage_resize_exact(&f.owner.tracks[MM_SCENE_FRAMES].storage, 0, 16, MM_STORAGE_NORMAL) == MM_STORAGE_OK);
    mm_scene_owner_dispose(&f.owner, &f.s);
    assert(f.n == 1 && f.events[0] == 210 && !f.owner.tracks[MM_SCENE_FRAMES].storage.buffers[0].data);
    assert(!f.owner.selected_actor && !f.owner.selected_text);
    i = f.n;
    mm_scene_owner_dispose(&f.owner, &f.s);
    assert(i == f.n);
}
static void disposal(void)
{
    Fixture f;
    int i;
    const int expected[] = {100, 101, 102, 211, 400, 401, 420, 421, 210,
                            200 + MM_SCENE_BACKGROUND, 200 + MM_SCENE_THUMBNAIL, 451, 460};
    init(&f);
    populate(&f);
    mm_scene_owner_dispose(&f.owner, &f.s);
    assert(f.n == (int)(sizeof(expected) / sizeof(expected[0])) && f.live == 1 && !f.diagnostics);
    for (i = 0; i < f.n; ++i)
        assert(f.events[i] == expected[i]);
    assert(f.background.freed && !f.background.closed[0] && f.background.closed[1] && !f.background.closed[2]);
    init(&f);
    populate(&f);
    mm_scene_owner_unwind(&f.owner, &f.s);
    assert(f.events[0] == 102 && f.events[1] == 101 && f.events[2] == 100 && f.events[3] == 211);
    assert(f.events[4] == 420 && f.events[5] == 421 && f.events[6] == 400 && f.events[7] == 401 && f.events[8] == 210);
    assert(!f.diagnostics);
    mm_scene_owner_dispose(&f.owner, &f.s);
    assert(f.live == 1);
}
static void close_and_visibility(void)
{
    Fixture f;
    void *p, *alias;
    init(&f);
    populate(&f);
    assert(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.children[0]));
    assert(mm_scene_member_append(&f.owner, MM_SCENE_ACTORS, &f.children[1]));
    assert(mm_scene_member_append(&f.owner, MM_SCENE_TEXTS, &f.children[2]));
    assert(mm_scene_member_append(&f.owner, MM_SCENE_SELECTION, &f.children[1]));
    f.children[2].lifetime = f.children[2].seek_ok = 1;
    f.children[2].first = 0;
    mm_scene_owner_actors(&f.owner, 1, &f.s);
    assert(f.events[0] == 700 && f.events[1] == 701);
    f.n = 0;
    p = alias = &f.scene;
    ++f.scene.refs;
    mm_scene_owner_close(&p, &f.s);
    assert(!p && f.scene.refs == 1 && !f.background.freed && f.last_frame == -1);
    assert(f.events[0] == 550 && f.events[1] == 600 && f.events[2] == 601 && f.events[3] == 802 && f.events[4] == 500);
    mm_scene_owner_close(&alias, &f.s);
    assert(!alias && !f.live);
    init(&f);
    populate(&f);
    assert(mm_scene_member_append(&f.owner, MM_SCENE_TEXTS, &f.children[2]));
    mm_scene_owner_hide_texts(&f.owner, &f.s);
    assert(!f.seeks);
    f.children[2].lifetime = 1;
    f.children[2].first = (-2147483647 - 1);
    mm_scene_owner_hide_texts(&f.owner, &f.s);
    assert(!f.seeks && f.diagnostics == 1);
    f.children[2].first = 10;
    mm_scene_owner_hide_texts(&f.owner, &f.s);
    assert(f.seeks == 1 && f.diagnostics == 2);
    mm_scene_owner_dispose(&f.owner, &f.s);
}
static void malformed(void)
{
    Fixture f;
    void *p;
    int scenario;
    MMSceneTrack *frames = &f.owner.tracks[MM_SCENE_FRAMES], *starts = &f.owner.tracks[MM_SCENE_STARTS];
    MMSceneTrackEvent row;
    for (scenario = 0; scenario < 5; ++scenario)
    {
        init(&f);
        populate(&f);
        if (scenario == 0)
            assert(mm_scene_track_put(frames, 0, MM_SCENE_SOUND_HEADER_BYTES - 1, &f.frame[0]));
        if (scenario == 1)
        {
            f.frame[0].header.ctagc = 2147483647;
            assert(mm_scene_track_put(frames, 0, sizeof(f.frame[0]), &f.frame[0]));
        }
        if (scenario == 2)
            assert(mm_scene_track_put(starts, 0, 0, NULL));
        if (scenario == 3)
        {
            assert(mm_scene_track_get(starts, 0, &row));
            row.kind = MM_SCENE_PLAY_SOUND;
            assert(mm_scene_track_set(starts, 0, &row));
        }
        if (scenario == 4)
        {
            assert(mm_scene_track_get(frames, 0, &row));
            row.kind = MM_SCENE_SET_BACKGROUND;
            assert(mm_scene_track_set(frames, 0, &row));
        }
        mm_scene_owner_dispose(&f.owner, &f.s);
        assert(f.diagnostics == 1);
        if (scenario < 2)
            assert(!f.frame_closed[0][0]);
    }
    init(&f);
    f.background.header.ctagc = 1000;
    p = &f.background;
    mm_scene_sound_release(&p, &f.s.sound);
    assert(!p && f.background.freed && f.diagnostics == 1);
    mm_scene_sound_release(&p, &f.s.sound);
    assert(f.diagnostics == 1);
    init(&f);
    populate(&f);
    assert(mm_scene_track_put(frames, 0, sizeof(f.frame[0]), &f.frame[0]));
    mm_scene_owner_unwind(&f.owner, &f.s);
    assert(f.diagnostics == 1 && !f.frame_closed[0][0] && f.frame_closed[1][0]);
}
int main(void)
{
    factories();
    disposal();
    close_and_visibility();
    malformed();
    puts("C89 Scene owner: factories, release order, prefix unwind, sound tags and close effects passed.");
    return 0;
}
