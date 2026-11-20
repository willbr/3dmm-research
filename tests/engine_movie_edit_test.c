/* Movie edit sequencing, placement ownership and undo failure contracts. */
#include "mm/movie_edit.h"
#include "mm/actor_undo.h"
#include "mm/scene_selection.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
enum
{
    FAIL_CACHE = 1,
    FAIL_NEW_ACTOR,
    FAIL_ADD,
    FAIL_DUP,
    FAIL_STAGE,
    FAIL_APPLY,
    FAIL_COMMIT,
    FAIL_PASTE,
    FAIL_UNDO,
    FAIL_NEW_SCENE,
    FAIL_BACKGROUND,
    FAIL_NEW_UNDO,
    FAIL_SOUND
};
enum
{
    VIEW = 100,
    LOOKUP,
    SELECT,
    DUP,
    RELEASE,
    PLACE,
    TRANSFER,
    APPLY,
    FRAME,
    DIRTY,
    INVALIDATE,
    NUKED,
    RESTORE,
    MAKE_UNDO,
    BEGIN,
    END,
    CACHE,
    TITLE,
    NEW_SCENE,
    REMOVE_SCENE,
    BACKGROUND,
    NEW_UNDO,
    CONFIGURE,
    UNDO_STATE,
    UNDO_VALIDATE,
    COMMIT,
    NOTIFY,
    CLEAR,
    UNDO_RELEASE,
    SOUND_FETCH,
    SOUND_VOLUME,
    SOUND_STYLE,
    SOUND_RELEASE,
    ROLL_CHANGED,
    CURSOR,
    SIZE_SOUND,
    SIZE_SOUND_ALTERNATE,
    WORLD_DIRTY,
    MARK_VIEWS
};
typedef struct Test Test;
typedef struct Actor
{
    Test *test;
    int id, refs, visible, value;
    MMTag tag;
} Actor;
typedef struct Undo
{
    Test *test;
    MMMovieUndoContext base;
    MMActorUndoState actor_state;
    int refs, bound, actor;
} Undo;
struct Test
{
    MMMovieEditServices s;
    MMMovieState data;
    MMMovieScenes scenes;
    Actor actor, fresh, copy;
    Actor *selected, *placement;
    Undo undo;
    int view, inScene, fail, selectedCalls, selectionCalls, frame, kind, actorCalls, sceneCalls;
    int dirty, invalidates, bars, nuked, error, diagnostics, entire, resets, restore, placeCalls, undoCount;
    int lastUndoCalls, notify, commits, clears, rowChanges;
    int begin, end, cacheBackground, defaults, empty, newScene, removeScene, removedIndex, backgroundUndoable;
    int soundEnabled, played, flushed, soundRefs, volumeCalls, styleCalls;
    int switchSelection, undoOnFresh;
    int events[128], eventCount;
    MMMovieEditRequest request;
};
static void event(Test *t, int e)
{
    t->events[t->eventCount++] = e;
}
static void *selected(void *p)
{
    Test *t = p;
    ++t->selectedCalls;
    event(t, SELECT);
    return t->selected;
}
static void *lookup(void *p, MMI32 id)
{
    Test *t = p;
    event(t, LOOKUP);
    return t->inScene && id == t->actor.id ? &t->actor : 0;
}
static void select_actor(void *p, void *a)
{
    Test *t = p;
    t->selected = a;
    ++t->selectionCalls;
    event(t, SELECT);
}
static void *view(void *p)
{
    Test *t = p;
    event(t, VIEW);
    return t->view ? t : 0;
}
static void *new_actor(void *p, const MMTag *tag)
{
    Test *t = p;
    if (t->fail == FAIL_NEW_ACTOR)
        return 0;
    t->fresh.refs = 1;
    t->fresh.tag = *tag;
    return &t->fresh;
}
static int visible(void *p)
{
    return ((Actor *)p)->visible;
}
static int onstage(void *p)
{
    Actor *a = p;
    a->value = 444;
    return a->test->fail != FAIL_STAGE;
}
static void *duplicate(void *p, int reset)
{
    Actor *a = p;
    Test *t = a->test;
    event(t, DUP);
    t->resets = reset;
    if (t->fail == FAIL_DUP)
        return 0;
    t->copy.refs = 1;
    return &t->copy;
}
static void release(void *p)
{
    Actor *a = p;
    --a->refs;
    event(a->test, RELEASE);
}
static void restore(void *p, void *copy)
{
    Actor *a = p;
    (void)copy;
    ++a->test->restore;
    a->value = 0;
    event(a->test, RESTORE);
}
static int add_actor(void *p, void *a)
{
    Test *t = p;
    if (t->fail == FAIL_ADD)
        return 0;
    ++((Actor *)a)->refs;
    t->inScene = 1;
    return 1;
}
static int remove_actor(void *p, MMI32 id)
{
    Test *t = p;
    if (id != t->actor.id)
        return 0;
    t->selected = 0;
    t->inScene = 0;
    return t->fail != FAIL_APPLY;
}
static int apply_actor(void *p, int kind, const MMMovieEditRequest *q)
{
    Actor *a = p;
    Test *t = a->test;
    ++t->actorCalls;
    t->kind = kind;
    t->request = *q;
    a->value = 555;
    event(t, APPLY);
    return t->fail != FAIL_APPLY;
}
static int apply_scene(void *p, int kind, const MMMovieEditRequest *q)
{
    Test *t = p;
    ++t->sceneCalls;
    t->kind = kind;
    t->request = *q;
    event(t, APPLY);
    return t->fail != FAIL_APPLY;
}
static MMI32 frame(void *p)
{
    Test *t = p;
    event(t, FRAME);
    return t->frame;
}
static MMActorUndoState *last_actor_undo(void *p)
{
    Test *t = p;
    ++t->lastUndoCalls;
    return t->undoCount && t->undo.actor ? &t->undo.actor_state : NULL;
}
static void place(void *p, int entire)
{
    Test *t = p;
    ++t->placeCalls;
    t->entire = entire;
    event(t, PLACE);
}
static void placement(void *p, void *owned)
{
    Test *t = p;
    t->placement = owned;
    event(t, TRANSFER);
}
static int paste_actor(void *p, void *a)
{
    Test *t = p;
    (void)a;
    return t->fail != FAIL_PASTE;
}
static int paste_route(void *p, void *source)
{
    Actor *a = p;
    (void)source;
    a->value = 777;
    if (a->test->switchSelection)
        a->test->selected = &a->test->fresh;
    return a->test->fail != FAIL_PASTE;
}
static int make_undo(void *p, void *copy)
{
    Actor *a = p;
    (void)copy;
    event(a->test, MAKE_UNDO);
    a->test->undoOnFresh = a == &a->test->fresh;
    return a->test->fail != FAIL_UNDO;
}
static void long_op(void *p, int begin)
{
    Test *t = p;
    if (begin)
    {
        ++t->begin;
        event(t, BEGIN);
    }
    else
    {
        ++t->end;
        event(t, END);
    }
}
static int cache(void *p, const MMTag *tag, int background)
{
    Test *t = p;
    (void)tag;
    t->cacheBackground = background;
    event(t, CACHE);
    return t->fail != FAIL_CACHE;
}
static void title(void *p)
{
    Test *t = p;
    ++t->defaults;
    t->data.title.count = 7;
    event(t, TITLE);
}
static int empty(void *p)
{
    return ((Test *)p)->empty;
}
static int new_scene(void *p, MMI32 index)
{
    Test *t = p;
    ++t->newScene;
    event(t, NEW_SCENE);
    if (t->fail == FAIL_NEW_SCENE)
        return 0;
    t->scenes.open = t;
    t->scenes.index = index;
    ++t->data.sceneCount;
    return 1;
}
static int remove_scene(void *p, MMI32 index)
{
    Test *t = p;
    ++t->removeScene;
    t->removedIndex = index;
    --t->data.sceneCount;
    event(t, REMOVE_SCENE);
    return 1;
}
static int background(void *p, const MMTag *tag, int undoable)
{
    Test *t = p;
    (void)tag;
    t->backgroundUndoable = undoable;
    event(t, BACKGROUND);
    return t->fail != FAIL_BACKGROUND;
}
static void *new_undo(void *p)
{
    Test *t = p;
    event(t, NEW_UNDO);
    if (t->fail == FAIL_NEW_UNDO)
        return 0;
    t->undo.refs = 1;
    return &t->undo;
}
static void configure(void *p, void *u, MMI32 index, const MMTag *tag)
{
    Test *t = p;
    (void)u;
    (void)tag;
    t->removedIndex = index;
    event(t, CONFIGURE);
}
static void release_undo(void *p)
{
    Undo *u = p;
    --u->refs;
    event(u->test, UNDO_RELEASE);
}
static int sounds(void *p)
{
    return ((Test *)p)->soundEnabled;
}
static void play(void *p)
{
    ++((Test *)p)->played;
}
static void flush(void *p)
{
    ++((Test *)p)->flushed;
}
static void *sound(void *p, const MMTag *tag)
{
    Test *t = p;
    (void)tag;
    event(t, SOUND_FETCH);
    if (t->fail == FAIL_SOUND)
        return 0;
    ++t->soundRefs;
    return t;
}
static MMI32 volume(void *p)
{
    Test *t = p;
    ++t->volumeCalls;
    event(t, SOUND_VOLUME);
    return 83;
}
static MMI32 style(void *p)
{
    Test *t = p;
    ++t->styleCalls;
    event(t, SOUND_STYLE);
    return 4;
}
static void release_sound(void *p)
{
    Test *t = p;
    --t->soundRefs;
    event(t, SOUND_RELEASE);
}
static void dirty(void *p)
{
    Test *t = p;
    ++t->dirty;
    event(t, DIRTY);
}
static void invalidate(void *p, int bars)
{
    Test *t = p;
    ++t->invalidates;
    t->bars = bars;
    event(t, INVALIDATE);
}
static void nuked(void *p)
{
    Test *t = p;
    ++t->nuked;
    event(t, NUKED);
}
static void diagnostic(void *p, int issue)
{
    Test *t = p;
    ++t->diagnostics;
    t->error = issue;
}
static MMMovieUndoContext *undo_state(void *p, void *u)
{
    event((Test *)p, UNDO_STATE);
    return &((Undo *)u)->base;
}
static void undo_validate(void *p, void *u)
{
    Undo *undo = u;
    if (undo->base.movie == p) undo->bound = 1;
    event((Test *)p, UNDO_VALIDATE);
}
static MMI32 scene_index(void *p)
{
    return ((Test *)p)->scenes.index;
}
static int commit(void *p, void *u)
{
    Test *t = p;
    ++t->commits;
    event(t, COMMIT);
    if (t->fail == FAIL_COMMIT)
        return 0;
    ++((Undo *)u)->refs;
    return 1;
}
static void clear(void *p)
{
    Test *t = p;
    ++t->clears;
    t->undo.refs = 0;
    event(t, CLEAR);
}
static void notify(void *p, int value)
{
    Test *t = p;
    t->notify = value;
    event(t, NOTIFY);
}
static int seed_rollcall(Test *t)
{
    MMRollCallActor row;
    memset(&row, 0, sizeof(row));
    row.data.actor_id = 5;
    row.data.reference_count = 1;
    row.data.source_id = 2;
    row.data.template_type = row.data.template_number = 1;
    return mm_rollcall_table_init(&t->data.actors, sizeof(row), 1, 0x0303, 0, NULL) == MM_STORAGE_OK &&
           mm_rollcall_table_add(&t->data.actors, "Actor", 5, &row);
}
static MMI32 actor_id(void *p)
{
    return ((Actor *)p)->id;
}
static void set_id(void *p, MMI32 id)
{
    ((Actor *)p)->id = id;
}
static void actor_tag(void *p, MMTag *out)
{
    *out = ((Actor *)p)->tag;
}
static int same_tag(const MMTag *a, const MMTag *b)
{
    return a->sid == b->sid && a->cache == b->cache && a->type == b->type && a->number == b->number;
}
static void changed_row(void *p)
{
    Test *t = p;
    ++t->rowChanges;
    event(t, ROLL_CHANGED);
}
static void init(Test *t)
{
    MMMovieEditServices *s;
    memset(t, 0, sizeof(*t));
    t->actor.test = t->fresh.test = t->copy.test = t;
    t->actor.id = 5;
    t->actor.refs = 1;
    t->actor.tag.sid = 2;
    t->actor.tag.type = 1;
    t->actor.tag.number = 1;
    t->undo.test = t;
    t->undo.actor = t->undo.actor_state.sooner_later = 1;
    t->undo.base.frame = 77;
    t->selected = &t->actor;
    t->scenes.open = t;
    t->scenes.index = 2;
    t->data.sceneCount = 3;
    t->data.title.count = 4;
    t->view = t->inScene = 1;
    t->frame = 9;
    s = &t->s;
    s->context = t;
    s->data = &t->data;
    s->scenes = &t->scenes;
    s->rollcall.context = t;
    s->rollcall.data = &t->data;
    s->rollcall.actor_id = actor_id;
    s->rollcall.set_actor_id = set_id;
    s->rollcall.actor_tag = actor_tag;
    s->rollcall.same_tag = same_tag;
    s->rollcall.changed = changed_row;
    s->undo.context = t;
    s->undo.movie = t;
    s->undo.state = undo_state;
    s->undo.validate = undo_validate;
    s->undo.scene_index = scene_index;
    s->undo.frame = frame;
    s->undo.commit = commit;
    s->undo.clear = clear;
    s->undo.notify = notify;
    s->selected_actor = selected;
    s->actor_from_id = lookup;
    s->select_actor = select_actor;
    s->first_view = view;
    s->new_actor = new_actor;
    s->actor_visible = visible;
    s->core_onstage = onstage;
    s->duplicate_actor = duplicate;
    s->release_actor = release;
    s->restore_actor = restore;
    s->add_actor = add_actor;
    s->remove_actor = remove_actor;
    s->apply_actor = apply_actor;
    s->apply_scene = apply_scene;
    s->frame = frame;
    s->last_actor_undo = last_actor_undo;
    s->place_actor = place;
    s->placement_undo = placement;
    s->paste_actor = paste_actor;
    s->paste_route = paste_route;
    s->create_actor_undo = make_undo;
    s->long_op = long_op;
    s->cache_tag = cache;
    s->default_title = title;
    s->empty_scene = empty;
    s->new_scene = new_scene;
    s->remove_scene = remove_scene;
    s->background = background;
    s->new_scene_undo = new_undo;
    s->configure_scene_undo = configure;
    s->release_undo = release_undo;
    s->sounds_enabled = sounds;
    s->play_sound = play;
    s->flush_sound = flush;
    s->fetch_sound = sound;
    s->sound_volume = volume;
    s->sound_style = style;
    s->release_sound = release_sound;
    s->dirty = dirty;
    s->invalidate = invalidate;
    s->actor_nuked = nuked;
    s->diagnostic = diagnostic;
}
static int registration(void)
{
    Test t;
    int current, fail;
    for (current = 0; current <= 1; ++current)
        for (fail = 0; fail <= 1; ++fail)
        {
            init(&t);
            if (!current)
                t.scenes.index = -1;
            if (fail)
                t.fail = FAIL_COMMIT;
            CHECK(mm_movie_undo_add(&t.undo, &t.s.undo) == !fail);
            CHECK(t.undo.bound && t.undo.base.scene == (current ? 2 : -1) && t.undo.base.frame == (current ? 9 : 77));
            CHECK(t.notify == (fail ? MM_MOVIE_UNDO_DISABLED : MM_MOVIE_UNDO_READY));
            CHECK(t.events[0] == UNDO_STATE && t.events[1] == UNDO_VALIDATE && t.events[t.eventCount - 2] == COMMIT &&
                  t.events[t.eventCount - 1] == NOTIFY);
            mm_movie_undo_clear(&t.s.undo);
            CHECK(t.clears == 1 && t.notify == MM_MOVIE_UNDO_DISABLED && !t.undo.refs &&
                  t.events[t.eventCount - 2] == CLEAR);
        }
    return 0;
}
static int choosing(void)
{
    Test t;
    MMRollCallActor row;
    int fault;
    init(&t);
    t.scenes.open = 0;
    CHECK(mm_movie_selected_actor(&t.s) == -1 && !t.selectedCalls);
    init(&t);
    t.selected = 0;
    CHECK(mm_movie_selected_actor(&t.s) == -1);
    init(&t);
    CHECK(mm_movie_selected_actor(&t.s) == 5);
    init(&t);
    t.view = 0;
    CHECK(!mm_movie_choose_actor(5, &t.s) && t.eventCount == 1 && t.events[0] == VIEW);
    init(&t);
    t.actor.visible = 1;
    CHECK(mm_movie_choose_actor(5, &t.s) && t.selectionCalls == 1 && t.invalidates == 1 && !t.placeCalls &&
          !t.dirty);
    for (fault = 0; fault <= 2; ++fault)
    {
        init(&t);
        CHECK(seed_rollcall(&t));
        t.fail = fault == 1 ? FAIL_DUP : fault == 2 ? FAIL_STAGE : 0;
        CHECK(mm_movie_choose_actor(5, &t.s) == !fault);
        CHECK(t.resets == 1 && !t.dirty);
        if (fault)
            CHECK(!t.copy.refs && !t.selectionCalls && !t.placeCalls && !t.invalidates);
        else
        {
            CHECK(t.copy.refs == 1 && t.placement == &t.copy && t.selectionCalls == 2 && t.placeCalls == 1 &&
                  !t.entire);
            CHECK(mm_rollcall_table_extra(&t.data.actors, 0, &row) && row.data.reference_count == 2 &&
                  t.rowChanges == 1 && t.invalidates == 1);
            release(t.placement);
            CHECK(!t.copy.refs);
        }
        if (fault == 2)
            CHECK(t.actor.value == 444); /* Core failure keeps its completed prefix. */
        CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    }
    for (fault = 0; fault <= 2; ++fault)
    {
        init(&t);
        CHECK(seed_rollcall(&t));
        t.inScene = 0;
        t.fail = fault == 1 ? FAIL_NEW_ACTOR : fault == 2 ? FAIL_ADD : 0;
        CHECK(mm_movie_choose_actor(5, &t.s) == !fault);
        CHECK(t.fresh.refs == !fault && !t.dirty && !t.invalidates && t.placeCalls == !fault);
        if (!fault)
        {
            CHECK(t.fresh.id == 5);
            release(&t.fresh);
            CHECK(!t.fresh.refs);
        }
        CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    }
    init(&t);
    CHECK(seed_rollcall(&t));
    t.inScene = 0;
    CHECK(!mm_movie_choose_actor(6, &t.s) && !t.fresh.refs && !t.placeCalls);
    CHECK(mm_rollcall_table_dispose(&t.data.actors) == MM_STORAGE_OK);
    return 0;
}
static int actor_edits(void)
{
    Test t;
    MMMovieEditRequest q;
    int kind, fail, type;
    memset(&q, 0, sizeof(q));
    q.factor = 49152;
    q.frame = 17;
    q.custom = 2;
    q.loop = 2;
    for (kind = MM_MOVIE_EDIT_ROTATE; kind <= MM_MOVIE_EDIT_REMOVE; ++kind)
        for (fail = 0; fail <= 1; ++fail)
        {
            init(&t);
            if (fail)
                t.fail = FAIL_APPLY;
            CHECK(mm_movie_edit_actor(kind, &q, &t.s) == !fail);
            CHECK(t.dirty == (!fail && kind != MM_MOVIE_EDIT_ACTOR_SOUND));
            CHECK(t.invalidates == t.dirty && t.nuked == (!fail && kind == MM_MOVIE_EDIT_REMOVE));
            if (t.invalidates)
                CHECK(t.bars == (kind == MM_MOVIE_EDIT_SCALE || kind == MM_MOVIE_EDIT_REMOVE));
            if (kind == MM_MOVIE_EDIT_SQUASH)
                CHECK(t.request.pull[0] == 49152 && t.request.pull[1] == 87381 && t.request.pull[2] == 49152);
            if (kind == MM_MOVIE_EDIT_TIME)
                CHECK(t.request.shift == 8 && t.events[0] == FRAME && t.events[1] == SELECT && t.lastUndoCalls == !fail);
            if (kind == MM_MOVIE_EDIT_ACTOR_SOUND)
                CHECK(!t.request.tag && t.request.loop == 2);
            if (fail && kind != MM_MOVIE_EDIT_REMOVE)
                CHECK(t.actor.value == 555);
        }
    init(&t);
    q.factor = 0;
    CHECK(mm_movie_edit_actor(MM_MOVIE_EDIT_SQUASH, &q, &t.s) && !t.actorCalls && !t.dirty &&
          t.selectedCalls == 1);
    for (type = 0; type < 8; ++type)
    {
        init(&t);
        t.undoCount = (type & 4) != 0;
        t.undo.actor = type & 1;
        t.undo.actor_state.sooner_later = (type & 2) != 0;
        CHECK(mm_movie_edit_actor(MM_MOVIE_EDIT_TIME, &q, &t.s));
        CHECK(t.lastUndoCalls == 1 && t.undo.actor_state.last_frame == (type == 7 ? 17 : 0));
    }
    init(&t);
    t.frame = 2147483647;
    q.frame = (-2147483647 - 1);
    CHECK(mm_movie_edit_actor(MM_MOVIE_EDIT_TIME, &q, &t.s) && t.request.shift == 1);
    CHECK(mm_movie_step_actor_time(1, &t.s) && t.request.shift == 1 && t.request.frame == q.frame);
    t.frame = q.frame;
    CHECK(mm_movie_step_actor_time(-1, &t.s) && t.request.shift == -1 && t.request.frame == 2147483647);
    init(&t);
    t.selected = NULL;
    CHECK(!mm_movie_step_actor_time(1, &t.s) && t.selectedCalls == 1 && !t.actorCalls && !t.lastUndoCalls && !t.dirty);
    t.scenes.open = NULL;
    CHECK(!mm_movie_step_actor_time(-1, &t.s) && t.eventCount == 2);
    init(&t);
    t.selected = 0;
    CHECK(!mm_movie_onstage(-1, &t.s) && t.error == MM_MOVIE_EDIT_NO_ACTOR && !t.selectionCalls);
    init(&t);
    CHECK(!mm_movie_onstage(99, &t.s) && !t.selected && t.selectionCalls == 1 &&
          t.error == MM_MOVIE_EDIT_NO_ACTOR);
    init(&t);
    CHECK(mm_movie_onstage(5, &t.s) && t.dirty && t.invalidates && !t.bars && t.selectionCalls == 1);
    return 0;
}
static void cursor(void *p) { event(p, CURSOR); }
static void size_sound(void *p, int alternate) { event(p, alternate ? SIZE_SOUND_ALTERNATE : SIZE_SOUND); }
static void world_dirty(void *p) { event(p, WORLD_DIRTY); }
static void mark_views(void *p) { event(p, MARK_VIEWS); }
static void drag_release(void *p, void *handle) { (void)p; release_undo(handle); }

static int actor_drag(void)
{
    static const int rotate_steps[] = {SELECT, APPLY, DIRTY, INVALIDATE, COMMIT, UNDO_RELEASE, WORLD_DIRTY, MARK_VIEWS, CURSOR};
    static const int failure_steps[] = {SELECT, APPLY, WORLD_DIRTY, MARK_VIEWS, CURSOR};
    Test t;
    MMActorDragUndoServices undo = {0};
    MMSceneSelection state = {0};
    MMSceneSelectionServices selection = {0};
    MMSceneOwner owner = {0};
    MMMovieActorDrag drag;
    MMMovieActorDragInput input = {0};
    MMMovieActorDragServices services = {0};
    int axis, kind, tick;
    undo.context = &t;
    undo.commit = commit;
    undo.release = drag_release;
    selection.state = &state;
    selection.owner = &owner;
    services.selection = &selection;
    services.movie = &t.s;
    services.undo = &undo;
    services.view = &t;
    services.adjust_cursor = cursor;
    services.size_sound = size_sound;
    services.world_dirty = world_dirty;
    services.mark_views = mark_views;
    state.actor = &t.actor;
    for (axis = 0; axis < 3; ++axis)
    {
        init(&t);
        memset(&drag, 0, sizeof(drag));
        drag.undo.single = &t.undo;
        t.undo.refs = 1;
        drag.undo.group = &t.copy;
        input.tool = MM_MOVIE_DRAG_ROTATE_X + axis;
        input.command = axis != 2;
        input.mouse.dxr = 3 * 65536L;
        input.mouse.dyr = -65536L;
        mm_movie_actor_drag_sample(&drag, &input, &services);
        CHECK(t.kind == MM_MOVIE_EDIT_ROTATE && t.request.forward == input.command);
        CHECK(t.request.rotation[0] == (axis == 0 ? 65406 : 0));
        CHECK(t.request.rotation[1] == (axis == 1 ? 130 : 0));
        CHECK(t.request.rotation[2] == (axis == 2 ? 65406 : 0));
        CHECK(!drag.undo.single && drag.undo.group == &t.copy && t.undo.refs == 1);
        CHECK(t.eventCount == 9 && !memcmp(t.events, rotate_steps, sizeof(rotate_steps)));
    }

    init(&t);
    memset(&drag, 0, sizeof(drag));
    drag.undo.single = &t.undo;
    t.fail = FAIL_APPLY;
    mm_movie_actor_drag_sample(&drag, &input, &services);
    CHECK(drag.undo.single == &t.undo && t.eventCount == 5 && !memcmp(t.events, failure_steps, sizeof(failure_steps)));

    for (kind = MM_MOVIE_DRAG_RESIZE; kind <= MM_MOVIE_DRAG_SQUASH; ++kind)
    {
        const int squash = kind == MM_MOVIE_DRAG_SQUASH;
        init(&t);
        memset(&drag, 0, sizeof(drag));
        input.tool = kind;
        input.mouse.dyr = 0;
        for (tick = 0; tick < 5; ++tick)
        {
            int pixels = tick < 2 ? 1 : tick == 2 ? 0 : -1;
            t.eventCount = 0;
            input.mouse.dxr = pixels * 65536L;
            mm_movie_actor_drag_sample(&drag, &input, &services);
            CHECK(t.kind == (squash ? MM_MOVIE_EDIT_SQUASH : MM_MOVIE_EDIT_SCALE) && t.bars == !squash);
            CHECK(t.request.factor == 65536L + (squash ? -65 : 65) * pixels);
            CHECK(t.events[0] == (tick == 0 ? SIZE_SOUND : tick == 3 ? SIZE_SOUND_ALTERNATE : SELECT));
            CHECK(t.events[t.eventCount - 1] == CURSOR);
        }
    }

    init(&t);
    memset(&drag, 0, sizeof(drag));
    state.actor = NULL;
    input.tool = MM_MOVIE_DRAG_COMPOSE;
    input.command = 0;
    mm_movie_actor_drag_sample(&drag, &input, &services);
    CHECK(!t.eventCount);
    input.command = 1;
    mm_movie_actor_drag_sample(&drag, &input, &services);
    CHECK(t.eventCount == 2 && t.events[0] == CURSOR && t.events[1] == MARK_VIEWS);
    return 0;
}

static int scene_edits(void)
{
    Test t;
    MMMovieEditRequest q;
    MMTag tag;
    int kind, fail, missing;
    memset(&q, 0, sizeof(q));
    q.volume = 101;
    q.style = 3;
    q.queue = 2;
    for (kind = MM_MOVIE_EDIT_CAMERA; kind <= MM_MOVIE_EDIT_BACKGROUND_SOUND; ++kind)
        for (fail = 0; fail <= 1; ++fail)
        {
            init(&t);
            if (fail)
                t.fail = FAIL_APPLY;
            CHECK(mm_movie_edit_scene(kind, &q, &t.s) == !fail);
            CHECK(t.dirty == (!fail && kind != MM_MOVIE_EDIT_BACKGROUND_SOUND));
            CHECK(t.invalidates == (!fail && kind == MM_MOVIE_EDIT_CAMERA));
        }
    memset(&tag, 0, sizeof(tag));
    tag.sid = 2;
    q.tag = &tag;
    for (missing = 0; missing < 4; ++missing)
    {
        init(&t);
        q.volume = (missing & 1) ? -1 : 101;
        q.style = (missing & 2) ? 0 : 3;
        CHECK(mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &t.s));
        CHECK(t.request.volume == ((missing & 1) ? 83 : 101) && t.request.style == ((missing & 2) ? 4 : 3));
        CHECK(t.volumeCalls == !!(missing & 1) && t.styleCalls == !!(missing & 2) && !t.soundRefs && !t.dirty &&
              t.request.queue == 2);
        CHECK(q.volume == ((missing & 1) ? -1 : 101) && q.style == ((missing & 2) ? 0 : 3));
        if (missing)
            CHECK(t.events[t.eventCount - 2] == SOUND_RELEASE && t.events[t.eventCount - 1] == APPLY);
    }
    init(&t);
    t.fail = FAIL_SOUND;
    q.volume = -1;
    CHECK(!mm_movie_edit_scene(MM_MOVIE_EDIT_BACKGROUND_SOUND, &q, &t.s) && !t.sceneCalls && !t.soundRefs &&
          !t.dirty);
    return 0;
}
static int pasting(void)
{
    Test t;
    int fault;
    for (fault = 0; fault <= 3; ++fault)
    {
        init(&t);
        t.fail = fault == 1 ? FAIL_DUP : fault == 2 ? FAIL_PASTE : fault == 3 ? FAIL_UNDO : 0;
        CHECK(mm_movie_paste_route(&t.fresh, &t.s) == !fault);
        CHECK(!t.copy.refs && t.restore == (fault == 3) && t.dirty == !fault && t.invalidates == !fault);
        if (fault == 2)
            CHECK(t.actor.value == 777);
        if (fault == 3)
            CHECK(t.actor.value == 0 && t.events[t.eventCount - 2] == RESTORE && t.events[t.eventCount - 1] == RELEASE);
    }
    init(&t);
    t.selected = 0;
    CHECK(!mm_movie_paste_route(&t.fresh, &t.s) && t.error == MM_MOVIE_EDIT_NO_ACTOR);
    init(&t);
    t.view = 0;
    CHECK(!mm_movie_paste_actor(&t.actor, &t.s) && !t.selectionCalls && !t.dirty);
    init(&t);
    t.fail = FAIL_PASTE;
    CHECK(!mm_movie_paste_actor(&t.actor, &t.s) && !t.selectionCalls && !t.placeCalls);
    init(&t);
    CHECK(mm_movie_paste_actor(&t.actor, &t.s) && t.selectionCalls == 1 && t.entire && t.dirty && t.bars &&
          t.actor.refs == 1);
    init(&t);
    t.switchSelection = 1;
    t.fail = FAIL_UNDO;
    t.fresh.value = 123;
    CHECK(!mm_movie_paste_route(&t.fresh, &t.s) && t.undoOnFresh && t.actor.value == 777 && t.fresh.value == 0 &&
          !t.copy.refs && !t.dirty);
    return 0;
}

typedef struct ClipboardTest
{
    MMMovieClipboardServices services;
    int has_scene, text_mode, route_only, formats, refs, paste_refs, text_success;
    int pasted, enabled_text, tool, text_queued, generic, diagnosis, cues;
} ClipboardTest;

static void *clip_scene(void *p)
{
    return ((ClipboardTest *)p)->has_scene ? p : 0;
}
static int clip_text_mode(void *p)
{
    return ((ClipboardTest *)p)->text_mode;
}
static void *clip_selected(void *p)
{
    return p;
}
static int clip_yes(void *p)
{
    return p != 0;
}
static int clip_empty(void *p)
{
    return !((ClipboardTest *)p)->formats;
}
static int clip_format(void *p, int kind, void **document)
{
    ClipboardTest *t = p;
    if (!(t->formats & (1 << kind)))
        return 0;
    if (document)
    {
        *document = p;
        ++t->refs;
    }
    return 1;
}
static int clip_route(void *p)
{
    return ((ClipboardTest *)p)->route_only;
}
static void clip_release(void *p)
{
    --((ClipboardTest *)p)->refs;
}
static void clip_paste_actor(void *p, void *document)
{
    ClipboardTest *t = document;
    t->paste_refs = t->refs;
    ((ClipboardTest *)p)->pasted = 1 << MM_MOVIE_CLIP_ACTOR;
}
static int clip_paste_text(void *p, void *document)
{
    ClipboardTest *t = document;
    t->paste_refs = t->refs;
    ((ClipboardTest *)p)->pasted = 1 << MM_MOVIE_CLIP_TEXT_BOX;
    return t->text_success;
}
static void clip_enable_text(void *p)
{
    ++((ClipboardTest *)p)->enabled_text;
}
static void clip_set_tool(void *p, int tool)
{
    ((ClipboardTest *)p)->tool = tool;
}
static void clip_cue(void *p, int tool)
{
    if (tool == MM_MOVIE_CLIP_PASTE)
        ++((ClipboardTest *)p)->cues;
}
static void clip_queue_text(void *p, void *text)
{
    ((ClipboardTest *)p)->text_queued += (text == p);
}
static void clip_queue_generic(void *p, int command)
{
    ((ClipboardTest *)p)->generic = command;
}
static void clip_diagnose(void *p, int code)
{
    ((ClipboardTest *)p)->diagnosis = code;
}
static void clip_init(ClipboardTest *t)
{
    MMMovieClipboardServices *s = &t->services;
    memset(t, 0, sizeof(*t));
    t->has_scene = 1;
    t->diagnosis = -1;
    s->context = t;
    s->scene = clip_scene;
    s->text_mode = clip_text_mode;
    s->selected_text = s->selected_actor = clip_selected;
    s->text_visible = s->text_selected = s->actor_visible = clip_yes;
    s->empty = clip_empty;
    s->format = clip_format;
    s->route_only = clip_route;
    s->release = clip_release;
    s->paste_actor = clip_paste_actor;
    s->paste_text = clip_paste_text;
    s->enable_text = clip_enable_text;
    s->cue = clip_cue;
    s->set_tool = clip_set_tool;
    s->queue_text = clip_queue_text;
    s->queue_generic = clip_queue_generic;
    s->diagnostic = clip_diagnose;
}
static int clipboard(void)
{
    ClipboardTest t;
    const int actor = 1 << MM_MOVIE_CLIP_ACTOR, text_box = 1 << MM_MOVIE_CLIP_TEXT_BOX;

    clip_init(&t);
    t.formats = actor;
    mm_movie_clipboard_click(MM_MOVIE_CLIP_PASTE, 1, &t.services);
    CHECK(!t.refs && !t.pasted && !t.cues);
    t.route_only = 1;
    mm_movie_clipboard_click(MM_MOVIE_CLIP_PASTE, 0, &t.services);
    CHECK(!t.refs && !t.pasted && !t.cues);
    mm_movie_clipboard_click(MM_MOVIE_CLIP_PASTE, 1, &t.services);
    CHECK(!t.refs && t.pasted == actor && t.paste_refs == 2 && t.cues == 1);

    clip_init(&t);
    t.formats = actor;
    t.has_scene = 0;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(!t.refs && !t.pasted && t.cues == 1 && t.diagnosis == MM_MOVIE_CLIP_NO_SCENE);
    t.formats = text_box;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(!t.refs && !t.pasted && t.cues == 1);

    clip_init(&t);
    t.text_mode = 1;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_COPY_TOOL, &t.services);
    CHECK(t.text_queued == 1 && t.generic == MM_MOVIE_CLIP_COPY && !t.tool);
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.text_queued == 2 && t.generic == MM_MOVIE_CLIP_PASTE && !t.tool);
    t.formats = actor;
    t.route_only = 1;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.tool == MM_MOVIE_CLIP_PASTE && t.text_queued == 2 && !t.refs && !t.pasted);
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(!t.refs && !t.pasted && t.diagnosis == MM_MOVIE_CLIP_UNSUPPORTED && t.text_queued == 3);
    t.route_only = 0;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.pasted == actor && t.text_queued == 3 && t.paste_refs == 1 && !t.refs);
    t.formats = actor | text_box;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE_TOOL, &t.services);
    CHECK(t.pasted == text_box && !t.enabled_text && t.cues == 3 && !t.refs);
    t.text_success = 1;
    mm_movie_clipboard_command(MM_MOVIE_CLIP_PASTE, &t.services);
    CHECK(t.enabled_text == 1 && t.cues == 4 && !t.refs);
    return 0;
}

static int insertion(void)
{
    Test t;
    MMTag tag;
    int fault;
    memset(&tag, 0, sizeof(tag));
    for (fault = 0; fault <= 3; ++fault)
    {
        init(&t);
        t.fail = fault == 1 ? FAIL_CACHE : fault == 2 ? FAIL_NEW_ACTOR : fault == 3 ? FAIL_ADD : 0;
        CHECK(mm_movie_insert_actor(&tag, &t.s) == !fault);
        CHECK(t.begin == 1 && t.end == 1 && t.events[0] == BEGIN && t.events[1] == CACHE && t.events[2] == END &&
              !t.cacheBackground);
        CHECK(t.fresh.refs == !fault && t.dirty == !fault && t.invalidates == !fault);
        if (!fault)
            release(&t.fresh);
    }
    return 0;
}
static int backgrounds(void)
{
    Test t;
    MMTag tag;
    int fault, enabled;
    memset(&tag, 0, sizeof(tag));
    for (enabled = 0; enabled <= 1; ++enabled)
        for (fault = 0; fault < 6; ++fault)
        {
            init(&t);
            t.soundEnabled = enabled;
            t.fail = fault == 1   ? FAIL_CACHE
                     : fault == 2 ? FAIL_NEW_SCENE
                     : fault == 3 ? FAIL_BACKGROUND
                     : fault == 4 ? FAIL_NEW_UNDO
                     : fault == 5 ? FAIL_COMMIT
                                  : 0;
            CHECK(mm_movie_add_background(&tag, &t.s) == !fault);
            CHECK(t.begin == 1 && t.end == 1 && t.cacheBackground && t.dirty == !fault && t.invalidates == !fault);
            CHECK(t.newScene == (fault != 1) && t.removeScene == (fault >= 3));
            CHECK(t.played == (enabled && (fault == 0 || fault >= 4)) &&
                  t.flushed == (!enabled && (fault == 0 || fault >= 4)));
            CHECK(t.undo.refs == !fault && !t.clears);
            if (fault >= 3)
                CHECK(t.removedIndex == 3);
            if (fault == 5)
                CHECK(t.notify == MM_MOVIE_UNDO_DISABLED && t.events[t.eventCount - 2] == UNDO_RELEASE &&
                      t.events[t.eventCount - 1] == REMOVE_SCENE);
            if (!fault)
                CHECK(t.undo.base.scene == 3 && t.undo.base.frame == 9 && t.bars && !t.backgroundUndoable);
        }
    init(&t);
    t.scenes.open = 0;
    t.scenes.index = -1;
    t.data.title.count = 0;
    t.fail = FAIL_NEW_SCENE;
    CHECK(!mm_movie_add_background(&tag, &t.s) && t.defaults == 1 && t.data.title.count == 7 && !t.removeScene);
    init(&t);
    t.empty = 1;
    CHECK(mm_movie_add_background(&tag, &t.s) && t.backgroundUndoable && !t.newScene && !t.undo.refs &&
          !t.played && t.dirty);
    init(&t);
    t.empty = 1;
    t.fail = FAIL_BACKGROUND;
    CHECK(!mm_movie_add_background(&tag, &t.s) && !t.removeScene &&
          t.error == MM_MOVIE_EDIT_BACKGROUND_FAILED && !t.dirty);
    return 0;
}
int main(void)
{
    if (registration() || choosing() || actor_edits() || actor_drag() || scene_edits() || pasting() || clipboard() ||
        insertion() || backgrounds())
        return 1;
    puts("Movie actor/scene edits, placement, sound defaults, clipboard and undo control passed");
    return 0;
}
