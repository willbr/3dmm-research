#include "mm/scene_edit.h"
#include "scene_sound_fixture.h"
typedef struct Evt
{
    MMI32 frame, bytes;
    int kind;
    void *data;
} Evt;
typedef struct Group
{
    int count;
    Evt v[16];
} Group;
typedef struct Back
{
    int refs, id, camera;
} Back;
typedef struct Undo
{
    MMScenePauseUndo pause;
    MMSceneBackgroundUndo back;
    int kind, refs;
} Undo;
typedef struct Edit
{
    Fixture packets;
    MMSceneEditServices s;
    MMSceneEditUndoServices us;
    MMSceneState data;
    MMSceneRuntime runtime;
    MMSceneOwner owner;
    Group groups[2];
    Back backgrounds[3];
    MMTag weak, published, undo_tag;
    Undo *active;
    MMScenePauseUndo cleared_pause;
    MMSceneBackgroundUndo cleared_back;
    int dirty, puts, erases, insert_fail, put_fail, fetches, fetch_fail, fetch_fail_all, put_background_calls,
        put_background_fail;
    int camera_calls, camera_fail, camera_fail_all, names, changes, loaded, available, diag[2], event_diag, lights,
        releases, closes;
    int undo_live, new_fail, commit_fail, commits, clears, not_undoable, switched, seeks, switch_fail, seek_fail,
        flushes, pause_type;
    int publications, first, publish_check, temp_refs, temp_closed, child_fail, cache_fail, cache_calls;
    MMI32 published_camera;
    int camera_output_check, expected_camera_output;
    int log[512], used;
} Edit;
static void note(Edit *e, int value)
{
    CHECK(e->used < 512);
    e->log[e->used++] = value;
}
static MMI32 count(void *p, void *g)
{
    (void)p;
    return ((Group *)g)->count;
}
static void event(void *p, void *g, MMI32 i, MMI32 *frame, int *kind)
{
    (void)p;
    *frame = ((Group *)g)->v[i].frame;
    *kind = ((Group *)g)->v[i].kind;
}
static void *payload(void *p, void *g, MMI32 i)
{
    (void)p;
    return ((Group *)g)->v[i].data;
}
static void erase(void *p, void *g, MMI32 i)
{
    Edit *e = (Edit *)p;
    Group *group = (Group *)g;
    CHECK(i >= 0 && i < group->count);
    free(group->v[i].data);
    memmove(group->v + i, group->v + i + 1, (size_t)(group->count - i - 1) * sizeof(Evt));
    --group->count;
    ++e->erases;
    note(e, 20);
}
static int insert_into(Edit *e, Group *g, int i, MMI32 frame, int kind, int bytes, const void *value)
{
    void *data;
    CHECK(i >= 0 && i <= g->count && g->count < 16);
    if (e->insert_fail)
        return 0;
    data = malloc((size_t)bytes);
    CHECK(data);
    memcpy(data, value, (size_t)bytes);
    memmove(g->v + i + 1, g->v + i, (size_t)(g->count - i) * sizeof(Evt));
    g->v[i].frame = frame;
    g->v[i].kind = kind;
    g->v[i].bytes = bytes;
    g->v[i].data = data;
    ++g->count;
    return 1;
}
static int insert_frame(void *p, MMI32 i, MMI32 frame, int kind, MMI32 bytes, const void *value)
{
    Edit *e = (Edit *)p;
    CHECK(e->runtime.next_event == i + 1);
    return insert_into(e, &e->groups[0], i, frame, kind, bytes, value);
}
static void dirty(void *p)
{
    Edit *e = (Edit *)p;
    ++e->dirty;
    note(e, 21);
}
static void put(void *p, void *g, MMI32 i, const void *value)
{
    Edit *e = (Edit *)p;
    Evt *v = &((Group *)g)->v[i];
    memcpy(v->data, value, (size_t)v->bytes);
    ++e->puts;
    note(e, 22);
}
static void event_tag(void *p, void *value, MMTag *tag)
{
    (void)p;
    *tag = *(MMTag *)value;
}
static void publish_owner(void *p, void *scene, const MMSceneOwner *owner)
{
    Edit *e = (Edit *)p;
    CHECK(scene == e && owner == &e->owner);
    note(e, 23);
}
static void release_back(void *p, void *back)
{
    Edit *e = (Edit *)p;
    Back *b = (Back *)back;
    CHECK(b->refs > 0);
    --b->refs;
    ++e->releases;
    note(e, 24);
}
static void lights_off(void *p, void *back)
{
    Edit *e = (Edit *)p;
    CHECK(back == e->owner.owned[MM_SCENE_BACKGROUND]);
    ++e->lights;
    note(e, 25);
}
static void *fetch_back(void *p, MMTag *tag)
{
    Edit *e = (Edit *)p;
    Back *b;
    CHECK(tag->number < 3);
    ++e->fetches;
    note(e, 26);
    if (e->fetch_fail_all || e->fetches == e->fetch_fail)
        return NULL;
    b = &e->backgrounds[tag->number];
    ++b->refs;
    return b;
}
static MMI32 camera_value(void *p, void *value)
{
    (void)p;
    return *(MMI32 *)value;
}
static int set_camera(void *p, void *back, MMI32 camera)
{
    Edit *e = (Edit *)p;
    Back *b = (Back *)back;
    if (e->camera_output_check)
    {
        CHECK(e->published_camera == e->expected_camera_output);
        e->camera_output_check = 0;
    }
    ++e->camera_calls;
    note(e, 27);
    if (e->camera_fail_all || e->camera_calls == e->camera_fail)
        return 0;
    b->camera = camera;
    return 1;
}
static int transition(void *p)
{
    (void)p;
    return 0;
}
static void weak_tag(void *p, const MMTag *tag)
{
    ((Edit *)p)->weak = *tag;
}
static void event_issue(void *p, int issue)
{
    Edit *e = (Edit *)p;
    (void)issue;
    ++e->event_diag;
}
static void tag_output(void *p, const MMTag *tag, int first)
{
    Edit *e = (Edit *)p;
    e->published = *tag;
    e->first = first;
    ++e->publications;
    note(e, 28);
}
static void camera_output(void *p, MMI32 camera)
{
    Edit *e = (Edit *)p;
    e->published_camera = camera;
    note(e, 29);
}
static int put_background(void *p, MMI32 i, const MMTag *tag)
{
    Edit *e = (Edit *)p;
    MMTag *old = (MMTag *)e->groups[1].v[i].data;
    ++e->put_background_calls;
    if (e->publish_check)
    {
        CHECK(e->publications && e->published.number == old->number);
        --e->publish_check;
    }
    if (e->put_background_calls == e->put_background_fail)
        return 0;
    *old = *tag;
    note(e, 30);
    return 1;
}
static int insert_background(void *p, MMI32 frame, const MMTag *tag)
{
    Edit *e = (Edit *)p;
    CHECK(!frame);
    return insert_into(e, &e->groups[1], 0, frame, 4, sizeof(*tag), tag);
}
static int play_background(void *p, MMI32 frame, const MMTag *tag, MMU32 mask)
{
    Edit *e = (Edit *)p;
    MMTag value = *tag;
    return mm_scene_play_event(frame, 4, &value, mask, &e->s.sound.events);
}
static void name_background(void *p)
{
    Edit *e = (Edit *)p;
    CHECK(e->owner.owned[MM_SCENE_BACKGROUND]);
    ++e->names;
    note(e, 31);
}
static void scene_change(void *p)
{
    Edit *e = (Edit *)p;
    ++e->changes;
    note(e, 32);
}
static int available(void *p)
{
    return ((Edit *)p)->available;
}
static void loaded(void *p)
{
    Edit *e = (Edit *)p;
    ++e->loaded;
    note(e, 33);
}
static MMI32 current_camera(void *p)
{
    Edit *e = (Edit *)p;
    CHECK(e->owner.owned[MM_SCENE_BACKGROUND]);
    return ((Back *)e->owner.owned[MM_SCENE_BACKGROUND])->camera;
}
static void close_sound_tag(void *p, void *sound, MMI32 i)
{
    Edit *e = (Edit *)p;
    Record *r = at((MMSceneSound *)sound, i);
    ++e->closes;
    note(e, 34);
    if (!r->tag.sid)
    {
        CHECK(r->tag.cache == &e->packets && e->packets.refs[r->tag.number] > 0);
        --e->packets.refs[r->tag.number];
        r->tag.cache = NULL;
    }
}
static void undo_release(void *p, void *u)
{
    Edit *e = (Edit *)p;
    Undo *undo = (Undo *)u;
    CHECK(undo->refs > 0);
    if (!--undo->refs)
    {
        --e->undo_live;
        free(undo);
    }
}
static void *new_undo(void *p, int kind)
{
    Edit *e = (Edit *)p;
    Undo *u;
    if (e->new_fail)
        return NULL;
    u = (Undo *)calloc(1, sizeof(*u));
    CHECK(u);
    u->refs = 1;
    u->kind = kind;
    ++e->undo_live;
    return u;
}
static MMScenePauseUndo *pause_state(void *p, void *u)
{
    (void)p;
    return &((Undo *)u)->pause;
}
static MMSceneBackgroundUndo *background_state(void *p, void *u)
{
    (void)p;
    return &((Undo *)u)->back;
}
static int commit(void *p, void *u)
{
    Edit *e = (Edit *)p;
    Undo *undo = (Undo *)u;
    ++e->commits;
    if (e->commit_fail)
        return 0;
    CHECK(!e->active);
    e->active = undo;
    ++undo->refs;
    return 1;
}
static void clear(void *p)
{
    Edit *e = (Edit *)p;
    Undo *u = e->active;
    ++e->clears;
    note(e, 40);
    e->active = NULL;
    if (u)
    {
        e->cleared_pause = u->pause;
        e->cleared_back = u->back;
        undo_release(e, u);
    }
}
static void not_undoable(void *p)
{
    Edit *e = (Edit *)p;
    ++e->not_undoable;
    note(e, 41);
}
static int camera_tag(void *p, MMI32 camera, MMTag *tag)
{
    Edit *e = (Edit *)p;
    if (e->child_fail)
        return 0;
    tag->sid = 0;
    tag->cache = &e->packets;
    tag->number = (MMU32)camera;
    ++e->temp_refs;
    return 1;
}
static int cache_tag(void *p, const MMTag *tag)
{
    Edit *e = (Edit *)p;
    CHECK(tag->cache == &e->packets && e->temp_refs == 1);
    ++e->cache_calls;
    return !e->cache_fail;
}
static void close_tag(void *p, MMTag *tag)
{
    Edit *e = (Edit *)p;
    CHECK(tag->cache == &e->packets && e->temp_refs == 1);
    --e->temp_refs;
    ++e->temp_closed;
}
static void issue(void *p, int n)
{
    Edit *e = (Edit *)p;
    CHECK(n >= 0 && n < 2);
    ++e->diag[n];
}
static int switch_scene(void *p)
{
    Edit *e = (Edit *)p;
    ++e->switched;
    return !e->switch_fail;
}
static int seek(void *p)
{
    Edit *e = (Edit *)p;
    ++e->seeks;
    return !e->seek_fail;
}
static int undo_pause(void *p, MMI32 *reason, MMI32 *delay)
{
    Edit *e = (Edit *)p;
    return mm_scene_pause_edit(reason, delay, &e->s);
}
static int has_background(void *p)
{
    return ((Edit *)p)->owner.owned[MM_SCENE_BACKGROUND] != NULL;
}
static int undo_background(void *p, const MMTag *tag, MMTag *old)
{
    Edit *e = (Edit *)p;
    return mm_scene_background_edit(tag, old, &e->s);
}
static int undo_camera(void *p, MMI32 camera, MMI32 *old)
{
    Edit *e = (Edit *)p;
    return mm_scene_camera_edit(camera, old, &e->s);
}
static void pause_type(void *p, MMI32 reason)
{
    ((Edit *)p)->pause_type = reason;
}
static void flush(void *p)
{
    Edit *e = (Edit *)p;
    ++e->flushes;
    note(e, 42);
}
static void init_edit(Edit *e)
{
    MMSceneEditServices *s;
    MMSceneEventServices *v;
    MMSceneOwnerServices *o;
    int i;
    memset(e, 0, sizeof(*e));
    init(&e->packets);
    s = &e->s;
    s->context = e;
    s->sound.context = e;
    s->sound.events.context = e;
    s->sound.events.scene = e;
    s->sound.events.owner = &e->owner;
    s->sound.events.data = &e->data;
    s->sound.events.runtime = &e->runtime;
    s->sound.dirty = dirty;
    s->sound.insert = insert_frame;
    s->put = put;
    s->erase = erase;
    s->put_background = put_background;
    s->insert_background = insert_background;
    s->play_background = play_background;
    s->background_name = name_background;
    s->scene_change = scene_change;
    s->can_notify = available;
    s->loaded = loaded;
    s->current_camera = current_camera;
    s->new_undo = new_undo;
    s->pause_state = pause_state;
    s->background_state = background_state;
    s->commit_undo = commit;
    s->release_undo = undo_release;
    s->clear_undo = clear;
    s->not_undoable = not_undoable;
    s->camera_tag = camera_tag;
    s->cache_tag = cache_tag;
    s->close_tag = close_tag;
    s->diagnostic = issue;
    s->output_context = e;
    s->publish_tag = tag_output;
    s->publish_camera = camera_output;
    v = &s->sound.events;
    v->event = event;
    v->payload = payload;
    v->read_tag = event_tag;
    v->fetch_background = fetch_back;
    v->set_camera = set_camera;
    v->camera = camera_value;
    v->transition = transition;
    v->publish_tag = weak_tag;
    v->diagnostic = event_issue;
    o = &v->owners;
    o->context = e;
    o->count = count;
    o->publish = publish_owner;
    o->release = release_back;
    o->lights_off = lights_off;
    o->sound.context = e;
    o->sound.close = close_sound_tag;
    e->owner.owned[0] = &e->groups[0];
    e->owner.owned[1] = &e->groups[1];
    e->data.currentFrame = e->data.firstFrame = e->data.lastFrame = 1;
    e->available = 1;
    for (i = 0; i < 3; ++i)
        e->backgrounds[i].id = i;
    e->us.context = e;
    e->us.switch_scene = switch_scene;
    e->us.seek_frame = seek;
    e->us.pause = undo_pause;
    e->us.background_present = has_background;
    e->us.current_camera = current_camera;
    e->us.background = undo_background;
    e->us.camera = undo_camera;
    e->us.pause_type = pause_type;
    e->us.flush = flush;
    e->us.clear = clear;
}
static MMTag tag(int n)
{
    MMTag t;
    t.sid = 1;
    t.cache = NULL;
    t.type = 23;
    t.number = (MMU32)n;
    return t;
}
static void existing_background(Edit *e, int n)
{
    MMTag value = tag(n);
    CHECK(insert_into(e, &e->groups[1], 0, 0, 4, sizeof(value), &value));
    e->backgrounds[n].refs = 1;
    e->owner.owned[MM_SCENE_BACKGROUND] = &e->backgrounds[n];
    e->weak = value;
}
static void add_camera(Edit *e, int frame, int camera)
{
    MMI32 value = camera;
    CHECK(insert_into(e, &e->groups[0], e->groups[0].count, frame, 3, 4, &value));
}
static void clean_edit(Edit *e)
{
    int i, j;
    Group *g;
    clear(e);
    CHECK(!e->undo_live && !e->temp_refs);
    if (e->owner.owned[MM_SCENE_BACKGROUND])
        release_back(e, e->owner.owned[MM_SCENE_BACKGROUND]);
    for (i = 0; i < 2; ++i)
    {
        g = &e->groups[i];
        while (g->count)
        {
            Evt *v = &g->v[g->count - 1];
            if (v->kind == 1)
            {
                MMSceneSound *p = (MMSceneSound *)v->data;
                for (j = 0; j < p->ctagc; ++j)
                    close_sound_tag(e, p, j);
            }
            erase(e, g, g->count - 1);
        }
    }
    if (e->owner.owned[MM_SCENE_BACKGROUND_SOUND])
        drop(&e->packets, (MMSceneSound *)e->owner.owned[MM_SCENE_BACKGROUND_SOUND]);
    for (i = 0; i < 3; ++i)
        CHECK(!e->backgrounds[i].refs);
    clean(&e->packets);
}
static void test_pause(void)
{
    Edit e;
    MMI32 reason = 0, delay = 0;
    init_edit(&e);
    CHECK(mm_scene_pause_edit(&reason, &delay, &e.s) && e.groups[0].count == 1 && e.runtime.next_event == 1 &&
          e.dirty == 1);
    reason = 3;
    delay = 13;
    CHECK(mm_scene_pause_edit(&reason, &delay, &e.s) && reason == 0 && delay == 0 && e.dirty == 1);
    reason = 0;
    delay = 0;
    CHECK(mm_scene_pause_edit(&reason, &delay, &e.s) && reason == 3 && delay == 13 && !e.groups[0].count &&
          !e.runtime.next_event && e.dirty == 1);
    add_camera(&e, 1, 1);
    e.runtime.next_event = 1;
    e.data.currentFrame = 3;
    reason = 1;
    delay = 17;
    CHECK(mm_scene_pause_edit(&reason, &delay, &e.s) && reason == 0 && delay == 0 && e.groups[0].count == 2);
    e.data.currentFrame = 4;
    e.insert_fail = 1;
    reason = 2;
    delay = 29;
    CHECK(!mm_scene_pause_edit(&reason, &delay, &e.s) && reason == 2 && delay == 29 && e.runtime.next_event == 2);
    clean_edit(&e);
}
static void test_camera(void)
{
    Edit e;
    MMI32 old;
    init_edit(&e);
    existing_background(&e, 0);
    CHECK(mm_scene_camera_edit(0, &old, &e.s) && old == 0 && !e.dirty && !e.groups[0].count);
    CHECK(mm_scene_camera_edit(1, &old, &e.s) && old == 0 && e.runtime.next_event == 1 &&
          e.backgrounds[0].camera == 1);
    e.camera_output_check = 1;
    e.expected_camera_output = 1;
    CHECK(mm_scene_camera_edit(2, &old, &e.s) && old == 1 && e.groups[0].count == 1 && e.published_camera == 1);
    CHECK(mm_scene_camera_edit(0, &old, &e.s) && old == 2 && !e.groups[0].count && !e.runtime.next_event);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    add_camera(&e, 1, 1);
    add_camera(&e, 4, 2);
    add_camera(&e, 7, 2);
    e.data.currentFrame = 4;
    e.runtime.next_event = 2;
    CHECK(mm_scene_camera_edit(1, &old, &e.s) && old == 2 && e.groups[0].count == 2 && e.runtime.next_event == 1);
    CHECK(mm_scene_camera_edit(2, &old, &e.s) && old == 1 && e.groups[0].count == 2 && e.runtime.next_event == 2);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    add_camera(&e, 1, 1);
    e.runtime.next_event = 1;
    e.camera_fail = 1;
    CHECK(!mm_scene_camera_edit(2, &old, &e.s) && old == 1 && *(MMI32 *)e.groups[0].v[0].data == 2 &&
          e.dirty == 1);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.camera_fail = 1;
    CHECK(!mm_scene_camera_edit(1, &old, &e.s) && !e.groups[0].count && !e.runtime.next_event && e.dirty == 1);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    add_camera(&e, 1, 1);
    e.runtime.next_event = 1;
    e.camera_fail = 1;
    CHECK(!mm_scene_camera_edit(0, &old, &e.s) && e.groups[0].count == 1 && e.runtime.next_event == 1 &&
          !e.dirty);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.runtime.disabled = MM_SCENE_EFFECT_CAMERAS;
    CHECK(mm_scene_camera_edit(1, &old, &e.s) && !e.camera_calls);
    clean_edit(&e);
}
static void test_background(void)
{
    Edit e;
    MMTag value = tag(1), old;
    MMSceneSound *sound, *cached;
    int j;
    init_edit(&e);
    sound = make(&e.packets, 3);
    cached = mm_scene_sound_duplicate(sound, &e.packets.s);
    CHECK(cached);
    e.owner.owned[MM_SCENE_BACKGROUND_SOUND] = cached;
    CHECK(insert_into(&e, &e.groups[0], 0, 1, 1, 16 + 3 * sizeof(Record), sound));
    raw_free(&e.packets, sound);
    e.runtime.next_event = 1;
    CHECK(mm_scene_background_edit(&value, &old, &e.s) && old.sid == -1 && e.first && e.loaded == 1 &&
          e.names == 1 && e.changes == 1 && !e.groups[0].count && !e.runtime.next_event);
    CHECK(e.closes == 3 && e.packets.refs[1] == 1 && e.packets.refs[3] == 1 &&
          e.owner.owned[MM_SCENE_BACKGROUND_SOUND] == cached);
    clean_edit(&e);
    init_edit(&e);
    e.available = 0;
    e.runtime.disabled = 0xffffffffUL;
    CHECK(mm_scene_background_edit(&value, &old, &e.s) && !e.loaded && e.changes == 1 && e.camera_calls == 1);
    clean_edit(&e);
    init_edit(&e);
    e.camera_fail = 1;
    CHECK(!mm_scene_background_edit(&value, &old, &e.s) && !e.owner.owned[MM_SCENE_BACKGROUND] && !e.groups[1].count &&
          e.releases == 1);
    clean_edit(&e);
    for (j = 0; j < 5; ++j)
    {
        init_edit(&e);
        existing_background(&e, 0);
        add_camera(&e, 1, 2);
        e.runtime.next_event = 1;
        e.publish_check = 1;
        if (j == 1)
            e.put_background_fail = 1;
        if (j == 2)
            e.fetch_fail = 1;
        if (j == 3)
            e.fetch_fail_all = 1;
        if (j == 4)
        {
            e.fetch_fail = 1;
            e.put_background_fail = 2;
        }
        CHECK(mm_scene_background_edit(&value, &old, &e.s) == (j == 0));
        CHECK(old.number == 0 && e.publications == 1 && !e.first);
        if (j == 0)
            CHECK(!e.groups[0].count && e.names == 1 && e.dirty == 1);
        else
            CHECK(e.groups[0].count == 1 && !e.names && e.dirty == (j == 1 ? 0 : 1));
        if (j == 3)
            CHECK(!e.owner.owned[MM_SCENE_BACKGROUND]);
        else
            CHECK(e.owner.owned[MM_SCENE_BACKGROUND] == &e.backgrounds[j == 0 ? 1 : 0]);
        if (j == 4)
            CHECK(((MMTag *)e.groups[1].v[0].data)->number == 1);
        clean_edit(&e);
    }
    init_edit(&e);
    e.insert_fail = 1;
    CHECK(!mm_scene_background_edit(&value, &old, &e.s) && e.first && !e.dirty && !e.fetches);
    clean_edit(&e);
    init_edit(&e);
    e.fetch_fail = 1;
    CHECK(!mm_scene_background_edit(&value, &old, &e.s) && !e.groups[1].count && !e.dirty);
    clean_edit(&e);
    init_edit(&e);
    e.backgrounds[0].refs = 1;
    e.owner.owned[MM_SCENE_BACKGROUND] = &e.backgrounds[0];
    CHECK(!mm_scene_background_edit(&value, &old, &e.s) && e.diag[0]);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    CHECK(mm_scene_is_empty(&e.s));
    e.data.firstFrame = -2147483647L;
    e.data.lastFrame = 2147483647L;
    CHECK(mm_scene_is_empty(&e.s));
    clean_edit(&e);
}
static void test_public(void)
{
    Edit e;
    MMTag value = tag(1);
    init_edit(&e);
    e.commit_fail = 1;
    CHECK(!mm_scene_pause_undo_edit(3, 45, &e.s) && !e.groups[0].count && !e.undo_live && e.commits == 1);
    clean_edit(&e);
    init_edit(&e);
    CHECK(mm_scene_pause_undo_edit(3, 45, &e.s) && e.active->pause.reason == 0 && e.active->pause.add);
    clean_edit(&e);
    init_edit(&e);
    e.new_fail = 1;
    CHECK(!mm_scene_pause_undo_edit(3, 45, &e.s) && !e.groups[0].count);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.cache_fail = 1;
    CHECK(!mm_scene_camera_undo_edit(1, &e.s) && e.temp_closed == 1 && !e.groups[0].count);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.child_fail = 1;
    CHECK(!mm_scene_camera_undo_edit(1, &e.s) && !e.temp_closed && !e.cache_calls && !e.groups[0].count);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.new_fail = 1;
    CHECK(!mm_scene_camera_undo_edit(1, &e.s) && e.backgrounds[0].camera == 1 && e.temp_closed == 1);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.commit_fail = 1;
    e.camera_fail = 2;
    CHECK(!mm_scene_camera_undo_edit(1, &e.s) && e.clears == 1 && e.not_undoable == 1 && !e.temp_refs);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    CHECK(mm_scene_camera_undo_edit(1, &e.s) && e.active->back.camera == 0 && !e.active->back.background &&
          !e.temp_refs);
    clean_edit(&e);
    init_edit(&e);
    CHECK(!mm_scene_background_undo_edit(&value, &e.s) && e.diag[1] && !e.fetches);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.new_fail = 1;
    CHECK(!mm_scene_background_undo_edit(&value, &e.s) && e.owner.owned[MM_SCENE_BACKGROUND] == &e.backgrounds[0] &&
          value.number == 1);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    e.new_fail = 1;
    e.fetch_fail = 2;
    CHECK(!mm_scene_background_undo_edit(&value, &e.s) && e.clears == 1 && e.not_undoable == 1);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    CHECK(mm_scene_background_undo_edit(&value, &e.s) && e.active->back.tag.number == 0 &&
          e.active->back.background);
    clean_edit(&e);
}
static void test_undo(void)
{
    Edit e;
    Undo *u;
    MMI32 reason, delay;
    int fail;
    init_edit(&e);
    reason = 3;
    delay = 71;
    CHECK(mm_scene_pause_edit(&reason, &delay, &e.s));
    u = (Undo *)new_undo(&e, 0);
    e.active = u;
    CHECK(mm_scene_pause_undo(&u->pause, &e.us) && u->pause.reason == 3 && u->pause.delay == 71 &&
          e.pause_type == 0 && e.flushes == 1);
    CHECK(mm_scene_pause_undo(&u->pause, &e.us) && u->pause.reason == 0 && e.pause_type == 3);
    clean_edit(&e);
    for (fail = 0; fail < 3; ++fail)
    {
        init_edit(&e);
        u = (Undo *)new_undo(&e, 0);
        e.active = u;
        u->pause.reason = 3;
        e.switch_fail = fail == 0;
        e.seek_fail = fail == 1;
        e.insert_fail = fail == 2;
        CHECK(!mm_scene_pause_undo(&u->pause, &e.us) && !e.active && !e.undo_live && e.flushes == 1 &&
              e.clears == 1);
        CHECK(e.log[e.used - 2] == 42 && e.log[e.used - 1] == 40);
        clean_edit(&e);
    }
    init_edit(&e);
    existing_background(&e, 0);
    e.backgrounds[0].camera = 2;
    u = (Undo *)new_undo(&e, 1);
    e.active = u;
    u->back.background = 1;
    u->back.tag = tag(1);
    u->back.camera = 3;
    CHECK(mm_scene_background_undo(&u->back, &e.us) && u->back.tag.number == 0 && u->back.camera == 2 &&
          !e.seeks && e.backgrounds[1].camera == 3);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    u = (Undo *)new_undo(&e, 1);
    e.active = u;
    u->back.background = 1;
    u->back.tag = tag(1);
    u->back.camera = 3;
    e.camera_fail = 2;
    CHECK(!mm_scene_background_undo(&u->back, &e.us) && !e.active && !e.undo_live &&
          e.cleared_back.tag.number == 1 && e.cleared_back.camera == 3 &&
          e.owner.owned[MM_SCENE_BACKGROUND] == &e.backgrounds[1]);
    clean_edit(&e);
    init_edit(&e);
    existing_background(&e, 0);
    u = (Undo *)new_undo(&e, 1);
    e.active = u;
    u->back.camera = 2;
    CHECK(mm_scene_background_undo(&u->back, &e.us) && e.seeks == 1 && u->back.camera == 0 &&
          e.backgrounds[0].camera == 2);
    clean_edit(&e);
}
typedef struct CameraFixture
{
    MMI32 at[6], kind[6], count, key;
    MMU32 camera;
    int opened, stored, dropped, prepared, applied, published, fail;
} CameraFixture;
static MMI32 count_camera_events(void *p) { return ((CameraFixture *)p)->count; }
static void camera_store_event(void *p, MMI32 i, MMI32 *frame, int *kind)
{
    CameraFixture *s = p;
    *frame = s->at[i];
    *kind = s->kind[i];
}
static void camera_store_erase(void *p, MMI32 i)
{
    CameraFixture *s = p;
    --s->count;
    for (; i < s->count; ++i)
    {
        s->at[i] = s->at[i + 1];
        s->kind[i] = s->kind[i + 1];
    }
}
static int camera_store_insert(void *p, MMI32 i, MMI32 frame, MMU32 camera)
{
    CameraFixture *s = p;
    MMI32 j;
    CHECK(s->count < 6);
    for (j = s->count++; j > i; --j)
    {
        s->at[j] = s->at[j - 1];
        s->kind[j] = s->kind[j - 1];
    }
    s->at[i] = frame;
    s->kind[i] = MM_SCENE_CHANGE_CAMERA;
    s->camera = camera;
    return 1;
}
static int camera_store_open(void *p) { ++((CameraFixture *)p)->opened; return 1; }
static int camera_store_write(void *p) { ++((CameraFixture *)p)->stored; return 1; }
static int camera_store_thumb(void *p) { ++((CameraFixture *)p)->dropped; return 1; }
static void camera_journal_position(void *p, MMI32 *first, MMI32 *frame, MMU32 *count)
{
    (void)p;
    *first = *frame = -5;
    *count = 4;
}
static int camera_journal_prepare(void *p, MMI32 key, MMU32 camera)
{
    CameraFixture *s = p;
    s->key = key;
    s->camera = camera;
    ++s->prepared;
    return 1;
}
static int camera_journal_apply(void *p)
{
    CameraFixture *s = p;
    ++s->applied;
    return !s->fail;
}
static void camera_journal_publish(void *p) { ++((CameraFixture *)p)->published; }
static void test_camera_journal(void)
{
    CameraFixture f = {{-5, -5, -5, -5, 13}, {MM_SCENE_CHANGE_CAMERA, MM_SCENE_PAUSE,
                       MM_SCENE_CHANGE_CAMERA, MM_SCENE_PLAY_SOUND, MM_SCENE_CHANGE_CAMERA},
                       5, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    MMSceneCameraJournalServices j = {NULL, camera_journal_position, camera_journal_prepare, camera_journal_apply,
                                     camera_journal_publish};
    MMSceneCameraStoreServices s = {{NULL, count_camera_events, camera_store_event, camera_store_erase,
                                     camera_store_insert}, camera_store_open, camera_store_write, camera_store_thumb};
    j.context = s.events.context = &f;
    CHECK(mm_scene_camera_journal(0, 4, &j) == MM_SCENE_CAMERA_INVALID && !f.prepared);
    f.fail = 1;
    CHECK(!mm_scene_camera_journal(0, 2, &j) && f.prepared == 1 && f.applied == 1 && !f.published);
    f.fail = 0;
    CHECK(mm_scene_camera_journal(0, 2, &j) == 1 && f.published == 1);
    CHECK(f.key == MM_SCENE_OPENING_CAMERA_FRAME && mm_scene_camera_frame(-7, f.key) == -7 && f.camera == 2);
    CHECK(mm_scene_camera_store(-5, f.key, 2, &s) && f.opened == 1 && f.stored == 1 && f.dropped == 1);
    CHECK(f.count == 4 && f.kind[0] == MM_SCENE_PAUSE && f.kind[1] == MM_SCENE_PLAY_SOUND &&
          f.kind[2] == MM_SCENE_CHANGE_CAMERA && f.at[2] == -5 && f.at[3] == 13);
    CHECK(mm_scene_camera_store(-5, 13, 1, &s) && f.dropped == 1 && f.camera == 1);
}
int main(void)
{
    test_pause();
    test_camera();
    test_camera_journal();
    test_background();
    test_public();
    test_undo();
    puts("C89 Scene edit checks passed");
    return 0;
}
