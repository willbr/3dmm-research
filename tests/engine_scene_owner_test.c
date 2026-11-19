#include "mm/scene_owner.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct Sound
{
    MMI32 count, style;
    int opened[4], closed[4], freed;
} Sound;
typedef struct Object Object;
typedef struct Event
{
    int kind, bytes;
    Object *object;
    Sound *sound;
} Event;
struct Object
{
    int slot, id, refs, count, lifetime, first, seek_ok;
    Event events[8];
};
typedef struct Fixture
{
    MMSceneOwner owner;
    void *published[MM_SCENE_OWNERS];
    MMSceneOwnerServices s;
    Object scene, slots[MM_SCENE_OWNERS], children[5];
    Sound frame[2], background;
    int events[256], n, live, attempts, fail, available, late_notify, cursor, loaded, diagnostics, seeks, last_frame;
} Fixture;
static void event(Fixture *f, int n)
{
    assert(f->n < 256);
    f->events[f->n++] = n;
}
static void publish(void *p, void *scene, const MMSceneOwner *w)
{
    Fixture *f = (Fixture *)p;
    assert(scene == &f->scene);
    memcpy(f->published, w->owned, sizeof(f->published));
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
    if (++f->attempts == f->fail)
        return NULL;
    f->scene.refs = 1;
    f->live = 1;
    mm_scene_owner_init(&f->owner, movie, NULL);
    publish(f, &f->scene, &f->owner);
    return &f->scene;
}
static void *create_slot(void *p, int slot)
{
    Fixture *f = (Fixture *)p;
    if (++f->attempts == f->fail)
        return NULL;
    f->slots[slot].refs = 1;
    ++f->live;
    if (f->late_notify && slot == MM_SCENE_STARTS)
        f->available = 1;
    event(f, 10 + slot);
    return &f->slots[slot];
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
            mm_scene_owner_dispose(&f->owner, object, &f->s);
            --f->live;
        }
    }
    else
    {
        if (o->slot >= 0)
            assert(f->owner.owned[o->slot] == object && f->published[o->slot] == object);
        event(f, o->slot >= 0 ? 200 + o->slot : 100 + o->id);
        assert(!--o->refs);
        --f->live;
    }
}
static void cursor(void *p, void *scene)
{
    Fixture *f = (Fixture *)p;
    assert(scene == &f->scene);
    f->cursor = 0;
    event(f, 20);
}
static int available(void *p)
{
    return ((Fixture *)p)->available;
}
static void loaded(void *p, void *scene)
{
    Fixture *f = (Fixture *)p;
    int i;
    assert(scene == &f->scene && !f->cursor);
    for (i = MM_SCENE_FRAMES; i <= MM_SCENE_STARTS; ++i)
        assert(f->owner.owned[i] == &f->slots[i] && f->published[i] == &f->slots[i]);
    ++f->loaded;
    event(f, 30);
}
static MMI32 count(void *p, void *group)
{
    (void)p;
    assert(group);
    return ((Object *)group)->count;
}
static MMI32 kind(void *p, void *group, MMI32 index)
{
    (void)p;
    return ((Object *)group)->events[index].kind;
}
static void *data(void *p, void *group, MMI32 index, MMI32 *bytes)
{
    Event *e = &((Object *)group)->events[index];
    (void)p;
    *bytes = e->bytes;
    return e->kind == MM_SCENE_PLAY_SOUND ? (void *)e->sound : (void *)&e->object;
}
static void *object(void *p, void *group, MMI32 index)
{
    (void)p;
    return ((Object *)group)->events[index].object;
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
static MMI32 sound_count(void *p, void *sound)
{
    (void)p;
    return ((Sound *)sound)->count;
}
static MMI32 sound_style(void *p, void *sound)
{
    (void)p;
    return ((Sound *)sound)->style;
}
static int sound_open(void *p, void *sound, MMI32 i)
{
    (void)p;
    return ((Sound *)sound)->opened[i];
}
static void sound_close(void *p, void *sound, MMI32 i)
{
    Fixture *f = (Fixture *)p;
    Sound *s = (Sound *)sound;
    assert(i >= 0 && i < 4 && !s->closed[i]);
    s->closed[i] = 1;
    s->opened[i] = 0;
    event(f, s == &f->background ? 450 + i : s == &f->frame[0] ? 400 + i : 420 + i);
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
    f->cursor = 99;
    for (i = 0; i < MM_SCENE_OWNERS; ++i)
        f->slots[i].slot = i;
    for (i = 0; i < 5; ++i)
    {
        f->children[i].slot = -1;
        f->children[i].id = i;
    }
    s = &f->s;
    s->context = f;
    s->pointer_bytes = sizeof(void *);
    s->sound_header_bytes = 16;
    s->sound_tag_bytes = 8;
    s->create_scene = create_scene;
    s->create_slot = create_slot;
    s->state = state;
    s->publish = publish;
    s->reset_cursor = cursor;
    s->can_notify = available;
    s->loaded = loaded;
    s->release = release;
    s->count = count;
    s->kind = kind;
    s->data = data;
    s->object = object;
    s->actor_visibility = visibility;
    s->lights_off = lights;
    s->text_lifetime = lifetime;
    s->text_seek = seek;
    s->diagnostic = diagnostic;
    s->sound.context = f;
    s->sound.count = sound_count;
    s->sound.style = sound_style;
    s->sound.open = sound_open;
    s->sound.close = sound_close;
    s->sound.free = sound_free;
    s->sound.diagnostic = diagnostic;
}
static void populate(Fixture *f)
{
    int i;
    Object *starts = &f->slots[MM_SCENE_STARTS], *frames = &f->slots[MM_SCENE_FRAMES];
    assert(mm_scene_owner_new(f, &f->s) == &f->scene);
    for (i = 0; i < 5; ++i)
    {
        f->children[i].refs = 1;
        ++f->live;
    }
    starts->count = 3;
    for (i = 0; i < 3; ++i)
    {
        starts->events[i].kind = i == 2 ? MM_SCENE_ADD_TEXT : MM_SCENE_ADD_ACTOR;
        starts->events[i].bytes = sizeof(void *);
        starts->events[i].object = &f->children[i];
    }
    frames->count = 2;
    for (i = 0; i < 2; ++i)
    {
        f->frame[i].count = 2;
        frames->events[i].kind = MM_SCENE_PLAY_SOUND;
        frames->events[i].bytes = 32;
        frames->events[i].sound = &f->frame[i];
    }
    f->background.count = 3;
    f->background.style = 4;
    f->background.opened[1] = 1;
    mm_scene_owner_set(&f->owner, &f->scene, MM_SCENE_BACKGROUND_SOUND, &f->background, &f->s);
    for (i = MM_SCENE_BACKGROUND; i <= MM_SCENE_THUMBNAIL; ++i)
    {
        f->slots[i].refs = 1;
        ++f->live;
        mm_scene_owner_set(&f->owner, &f->scene, i, &f->slots[i], &f->s);
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
    int fail, i;
    for (fail = 1; fail <= 3; ++fail)
    {
        init(&f);
        f.fail = fail;
        assert(!mm_scene_owner_new(&f, &f.s));
        assert(!f.live && !f.loaded);
        for (i = 0; i < MM_SCENE_OWNERS; ++i)
            assert(!f.owner.owned[i]);
    }
    init(&f);
    f.late_notify = 1;
    p = mm_scene_owner_new(&f, &f.s);
    assert(p && f.loaded == 1 && f.events[0] == 10 && f.events[1] == 20 && f.events[2] == 11);
    mm_scene_owner_close(&p, &f.s);
    assert(!p && !f.live);
    init(&f);
    assert(mm_scene_owner_new(&f, &f.s));
    assert(!f.loaded);
    mm_scene_owner_dispose(&f.owner, &f.scene, &f.s);
    i = f.n;
    mm_scene_owner_dispose(&f.owner, &f.scene, &f.s);
    assert(i == f.n);
}
static void disposal(void)
{
    Fixture f;
    int i;
    const int expected[] = {100, 101, 102, 201, 400, 401, 420, 421, 200,
                            200 + MM_SCENE_BACKGROUND, 200 + MM_SCENE_THUMBNAIL, 451, 460};
    init(&f);
    populate(&f);
    mm_scene_owner_dispose(&f.owner, &f.scene, &f.s);
    assert(f.n == (int)(sizeof(expected) / sizeof(expected[0])) && f.live == 1 && !f.diagnostics);
    for (i = 0; i < f.n; ++i)
        assert(f.events[i] == expected[i]);
    assert(f.background.freed && !f.background.closed[0] && f.background.closed[1] && !f.background.closed[2]);
    init(&f);
    populate(&f);
    mm_scene_owner_unwind(&f.owner, &f.scene, 3, 2, &f.s);
    assert(f.events[0] == 102 && f.events[1] == 101 && f.events[2] == 100 && f.events[3] == 201);
    assert(f.events[4] == 420 && f.events[5] == 421 && f.events[6] == 400 && f.events[7] == 401 && f.events[8] == 200);
    assert(!f.diagnostics);
    mm_scene_owner_dispose(&f.owner, &f.scene, &f.s);
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
    mm_scene_owner_dispose(&f.owner, &f.scene, &f.s);
}
static void malformed(void)
{
    Fixture f;
    void *p;
    int scenario;
    for (scenario = 0; scenario < 5; ++scenario)
    {
        init(&f);
        populate(&f);
        if (scenario == 0)
            f.slots[MM_SCENE_FRAMES].events[0].bytes = 15;
        if (scenario == 1)
            f.frame[0].count = 2147483647;
        if (scenario == 2)
            f.slots[MM_SCENE_STARTS].events[0].bytes = 0;
        if (scenario == 3)
            f.slots[MM_SCENE_STARTS].events[0].kind = MM_SCENE_PLAY_SOUND;
        if (scenario == 4)
            f.slots[MM_SCENE_FRAMES].events[0].kind = MM_SCENE_SET_BACKGROUND;
        mm_scene_owner_dispose(&f.owner, &f.scene, &f.s);
        assert(f.diagnostics == 1);
        if (scenario < 2)
            assert(!f.frame[0].closed[0]);
    }
    init(&f);
    f.background.count = 1000;
    p = &f.background;
    mm_scene_sound_release(&p, &f.s.sound);
    assert(!p && f.background.freed && f.diagnostics == 1);
    mm_scene_sound_release(&p, &f.s.sound);
    assert(f.diagnostics == 1);
    init(&f);
    populate(&f);
    f.slots[MM_SCENE_FRAMES].events[0].bytes = 40;
    mm_scene_owner_unwind(&f.owner, &f.scene, 3, 2, &f.s);
    assert(f.diagnostics == 1 && !f.frame[0].closed[0] && f.frame[1].closed[0]);
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
