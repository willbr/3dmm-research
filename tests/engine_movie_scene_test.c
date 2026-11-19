/* Movie scene-container transactions, retry paths and ownership. */
#include "mm/movie_scene.h"
#include "mm/movie_scene_journal.h"
#include "mm/scene_runtime.h"
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
    MAKE = 1,
    TEMP,
    AUTO,
    WRITE,
    ADOPT,
    READ,
    START,
    SEEK,
    CREATE,
    UNDO,
    ADD_UNDO
};
typedef struct Test Test;
typedef struct Scene
{
    Test *test;
    int index, refs, closed;
} Scene;
struct Test
{
    MMMovieScenes state;
    MMI32 count;
    MMMovieSceneServices s;
    Scene scenes[32];
    int used, fail, failures, skip;
    int autosaves, reads, starts, seeks, stops, memory, changes, sound, invalidates;
    int change_index[8], change_open[8];
    int moves, from[8], to[8], saves, deletes, deleted_chunks, adds, removes;
    int dirty, rollcall, errors, enabled, played, flushed, hidden, cleared;
    int undo_refs, configured_index;
    Scene *undo_scene;
};
static int succeeds(Test *t, int operation)
{
    if (t->fail != operation || !t->failures)
        return 1;
    if (t->skip)
    {
        --t->skip;
        return 1;
    }
    --t->failures;
    return 0;
}
static Scene *new_scene(Test *t, int index)
{
    Scene *s = &t->scenes[t->used++];
    s->test = t;
    s->index = index;
    s->refs = 1;
    return s;
}
static int autosave(void *p)
{
    Test *t = p;
    ++t->autosaves;
    return succeeds(t, AUTO);
}
static int make(void *p)
{
    return succeeds(p, MAKE);
}
static int temporary(void *p)
{
    return succeeds(p, TEMP);
}
static void stop(void *p)
{
    ++((Test *)p)->stops;
}
static void memory(void *p)
{
    ++((Test *)p)->memory;
}
static void *read_scene(void *p, MMI32 i)
{
    Test *t = p;
    ++t->reads;
    return succeeds(t, READ) ? new_scene(t, i) : 0;
}
static void *create(void *p)
{
    return succeeds(p, CREATE) ? new_scene(p, -2) : 0;
}
static int start(void *p)
{
    Scene *s = p;
    ++s->test->starts;
    return succeeds(s->test, START);
}
static int seek(void *p)
{
    Scene *s = p;
    ++s->test->seeks;
    return succeeds(s->test, SEEK);
}
static void retain(void *p)
{
    ++((Scene *)p)->refs;
}
static void release(void *p)
{
    if (p)
        --((Scene *)p)->refs;
}
static void close_scene(void *p)
{
    if (p)
    {
        ++((Scene *)p)->closed;
        release(p);
    }
}
static void sound(void *p)
{
    ++((Scene *)p)->test->sound;
}
static void hide(void *p)
{
    ++((Scene *)p)->test->hidden;
}
static void change(void *p)
{
    Test *t = p;
    t->change_index[t->changes] = t->state.index;
    t->change_open[t->changes++] = t->state.open != 0;
}
static void invalidate(void *p)
{
    ++((Test *)p)->invalidates;
}
static void *file(void *p)
{
    return p;
}
static int write_scene(void *p, void *s, MMU32 *n)
{
    (void)s;
    *n = 123;
    return succeeds(p, WRITE);
}
static MMU32 number(void *p, void *f, MMI32 i)
{
    (void)p;
    (void)f;
    return 100 + i;
}
static void move(void *p, void *f, MMI32 from, MMI32 to)
{
    Test *t = p;
    (void)f;
    t->from[t->moves] = from;
    t->to[t->moves++] = to;
}
static int adopt(void *p, void *f, MMU32 n, MMI32 i)
{
    (void)f;
    (void)n;
    (void)i;
    return succeeds(p, ADOPT);
}
static void delete_child(void *p, void *f, MMU32 n, MMI32 i)
{
    (void)f;
    (void)n;
    (void)i;
    ++((Test *)p)->deletes;
}
static void delete_chunk(void *p, void *f, MMU32 n)
{
    (void)f;
    (void)n;
    ++((Test *)p)->deleted_chunks;
}
static void save(void *p)
{
    ++((Test *)p)->saves;
}
static void remove_actors(void *p)
{
    ++((Scene *)p)->test->removes;
}
static void add_actors(void *p)
{
    ++((Scene *)p)->test->adds;
}
static void dirty(void *p)
{
    ++((Test *)p)->dirty;
}
static void rollcall(void *p)
{
    ++((Test *)p)->rollcall;
}
static void error(void *p)
{
    ++((Test *)p)->errors;
}
static int enabled(void *p)
{
    return ((Test *)p)->enabled;
}
static void play(void *p)
{
    ++((Test *)p)->played;
}
static void flush(void *p)
{
    ++((Test *)p)->flushed;
}
static void *new_undo(void *p)
{
    Test *t = p;
    if (!succeeds(t, UNDO))
        return 0;
    t->undo_refs = 1;
    return t;
}
static void configure(void *p, void *u, MMI32 i, void *s)
{
    Test *t = p;
    (void)u;
    t->configured_index = i;
    t->undo_scene = s;
    retain(s);
}
static int add_undo(void *p, void *u)
{
    Test *t = p;
    (void)u;
    if (!succeeds(t, ADD_UNDO))
        return 0;
    ++t->undo_refs;
    return 1;
}
static void release_undo(void *p)
{
    Test *t = p;
    if (--t->undo_refs == 0)
    {
        release(t->undo_scene);
        t->undo_scene = 0;
    }
}
static void clear(void *p)
{
    Test *t = p;
    ++t->cleared;
    if (t->undo_refs)
        release_undo(t);
}
static void init(Test *t, int count, int current)
{
    MMMovieSceneServices *s;
    memset(t, 0, sizeof(*t));
    mm_movie_scenes_init(&t->state);
    t->count = count;
    if (current != -1)
    {
        t->state.open = new_scene(t, current);
        t->state.index = current;
    }
    t->enabled = 1;
    s = &t->s;
    s->context = t;
    s->state = &t->state;
    s->count = &t->count;
    s->autosave = autosave;
    s->make_resources = make;
    s->use_temporary = temporary;
    s->stop_loops = stop;
    s->memory_cleanup = memory;
    s->read = read_scene;
    s->create = create;
    s->start = start;
    s->seek_first = seek;
    s->close = close_scene;
    s->retain = retain;
    s->release = release;
    s->sound_frame = sound;
    s->hide = hide;
    s->scene_change = change;
    s->invalidate = invalidate;
    s->file = file;
    s->write = write_scene;
    s->scene_number = number;
    s->move_child = move;
    s->adopt = adopt;
    s->delete_child = delete_child;
    s->delete_chunk = delete_chunk;
    s->save = save;
    s->remove_actors = remove_actors;
    s->add_actors = add_actors;
    s->dirty = dirty;
    s->rollcall_changed = rollcall;
    s->switch_error = error;
    s->sounds_enabled = enabled;
    s->play_sound = play;
    s->flush_sound = flush;
    s->new_undo = new_undo;
    s->configure_undo = configure;
    s->add_undo = add_undo;
    s->release_undo = release_undo;
    s->clear_undo = clear;
}
static int clean(Test *t)
{
    int i;
    if (t->state.open)
        mm_movie_scene_close_open(&t->s);
    if (t->undo_refs)
        clear(t);
    for (i = 0; i < t->used; ++i)
        if (t->scenes[i].refs != 0)
            return 0;
    return 1;
}
static int switching(void)
{
    Test t;
    int fault, attempts;
    init(&t, 3, 0);
    CHECK(mm_movie_scene_switch(0, &t.s) && t.autosaves == 0 && t.stops == 0);
    CHECK(mm_movie_scene_switch(-1, &t.s) && !t.state.open && t.state.index == -1);
    CHECK(t.autosaves == 1 && t.stops == 1 && t.memory == 1 && t.changes == 0 && clean(&t));
    init(&t, 3, 0);
    t.fail = AUTO;
    t.failures = 1;
    CHECK(!mm_movie_scene_switch(1, &t.s) && t.state.index == 0 && t.stops == 0 && t.reads == 0);
    CHECK(t.scenes[0].refs == 1 && clean(&t));
    for (fault = READ; fault <= SEEK; ++fault)
        for (attempts = 1; attempts <= 2; ++attempts)
        {
            init(&t, 3, 0);
            t.fail = fault;
            t.failures = attempts;
            CHECK(!mm_movie_scene_switch(1, &t.s));
            CHECK(t.reads == 2 && t.stops == 1 && t.memory == 1 && t.autosaves == 1);
            CHECK(t.state.index == (attempts == 1 ? 0 : -1));
            CHECK((t.state.open != 0) == (attempts == 1));
            CHECK(t.sound == (attempts == 1) && t.invalidates == (attempts == 1));
            CHECK(t.changes == (fault == SEEK ? 2 : 1));
            CHECK(t.change_index[0] == -1);
            CHECK(t.change_open[0] == (fault == SEEK || attempts == 1));
            if (t.changes == 2)
                CHECK(t.change_index[1] == -1 && t.change_open[1]);
            CHECK(clean(&t));
        }
    init(&t, 3, -1);
    t.fail = READ;
    t.failures = 1;
    CHECK(!mm_movie_scene_switch(1, &t.s) && t.reads == 1 && t.changes == 1 && clean(&t));
    return 0;
}
static int insertion(void)
{
    Test t;
    Scene *input;
    int fault, muted;
    for (fault = MAKE; fault <= SEEK; ++fault)
    {
        init(&t, 3, 0);
        input = new_scene(&t, -2);
        t.fail = fault;
        t.failures = 1;
        CHECK(!mm_movie_scene_insert(1, input, &t.s));
        CHECK(input->refs == 1 && t.dirty == 0 && t.rollcall == 0 && t.adds == 0);
        CHECK(t.count == (fault == ADOPT ? 4 : 3));
        CHECK(t.deleted_chunks == (fault == ADOPT));
        CHECK(t.deletes == (fault >= READ));
        CHECK(t.saves == (fault >= READ ? 2 : 0));
        if (fault == ADOPT)
        {
            CHECK(t.moves == 5 && t.from[0] == 2 && t.from[1] == 1);
            CHECK(t.from[2] == 2 && t.from[3] == 3 && t.from[4] == 4);
        }
        if (fault >= READ)
            CHECK(t.state.index == 1 && t.moves == 4);
        release(input);
        CHECK(clean(&t));
    }
    for (muted = 0; muted < 2; ++muted)
    {
        init(&t, 3, 0);
        input = new_scene(&t, -2);
        t.enabled = !muted;
        CHECK(mm_movie_scene_insert(1, input, &t.s));
        CHECK(t.count == 4 && t.state.index == 1 && t.moves == 2 && t.saves == 1);
        CHECK(t.dirty == 1 && t.rollcall == 1 && t.adds == 1 && t.played == !muted && t.flushed == muted);
        CHECK(input->refs == 1 && input->closed == 1);
        release(input);
        CHECK(clean(&t));
    }
    init(&t, 0, -1);
    t.fail = CREATE;
    t.failures = 1;
    CHECK(!mm_movie_scene_new(0, &t.s) && t.used == 0 && clean(&t));
    init(&t, 0, -1);
    CHECK(mm_movie_scene_new(0, &t.s) && t.scenes[0].closed == 2 && clean(&t));
    return 0;
}
static int removal(void)
{
    Test t;
    int i;
    for (i = 0; i < 3; ++i)
    {
        init(&t, 3, i);
        CHECK(mm_movie_scene_remove(i, &t.s));
        CHECK(t.count == 2 && t.state.index == (i < 2 ? i : 1));
        CHECK(t.removes == 1 && t.deletes == 1 && t.saves == 1 && t.dirty == 1 && t.moves == 2 - i);
        CHECK(clean(&t));
    }
    init(&t, 1, 0);
    CHECK(mm_movie_scene_remove(0, &t.s) && t.count == 0 && !t.state.open && !t.dirty && clean(&t));
    init(&t, 3, 0);
    t.fail = TEMP;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove(1, &t.s) && t.state.index == 0 && t.reads == 0 && clean(&t));
    init(&t, 3, 0);
    t.fail = READ;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove(1, &t.s) && t.state.index == 0 && t.reads == 2 && !t.deletes && clean(&t));
    init(&t, 3, 0);
    t.fail = READ;
    t.failures = 1;
    CHECK(mm_movie_scene_remove(0, &t.s) && t.count == 2 && !t.state.open && t.errors == 1 && t.dirty == 1 &&
          clean(&t));
    for (i = 0; i < 2; ++i)
    {
        init(&t, 3, i);
        CHECK(mm_movie_scene_remove_undoable(0, &t.s));
        CHECK(t.count == 2 && t.configured_index == 0 && t.undo_scene->refs == 1 && t.undo_refs == 1);
        CHECK(t.hidden == i && clean(&t));
    }
    init(&t, 3, 0);
    t.fail = UNDO;
    t.failures = 1;
    CHECK(mm_movie_scene_remove_undoable(0, &t.s) && t.count == 3 && !t.removes && clean(&t));
    init(&t, 3, 0);
    t.fail = ADD_UNDO;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && t.count == 3 && !t.undo_refs && clean(&t));
    init(&t, 3, 0);
    t.fail = TEMP;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && t.count == 3 && t.cleared == 1 && clean(&t));
    init(&t, 3, 1);
    t.fail = START;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && !t.undo_refs && clean(&t));
    return 0;
}

typedef struct Undo
{
    int create, remove, insert, background, notify, removed, flush, clear, unknown, fail;
} Undo;
static int ucreate(void *p, MMI32 i)
{
    Undo *u = p;
    (void)i;
    ++u->create;
    return u->fail != CREATE;
}
static int uremove(void *p, MMI32 i)
{
    Undo *u = p;
    (void)i;
    ++u->remove;
    return u->fail != TEMP;
}
static int uinsert(void *p, MMI32 i, void *s)
{
    Undo *u = p;
    (void)i;
    (void)s;
    ++u->insert;
    return u->fail != WRITE;
}
static int background(void *p, const MMTag *tag)
{
    Undo *u = p;
    (void)tag;
    ++u->background;
    return u->fail != START;
}
static void notify(void *p, int removed)
{
    Undo *u = p;
    ++u->notify;
    u->removed = removed;
}
static void uflush(void *p)
{
    ++((Undo *)p)->flush;
}
static void uclear(void *p)
{
    Undo *u = p;
    if (u->flush != 1)
        u->clear = -100;
    else
        ++u->clear;
}
static void unknown(void *p)
{
    ++((Undo *)p)->unknown;
}
static int undoing(void)
{
    MMMovieSceneUndoServices s;
    Undo u;
    int kind, redo, fault, removed, result;
    memset(&s, 0, sizeof(s));
    s.context = &u;
    s.create = ucreate;
    s.remove = uremove;
    s.insert = uinsert;
    s.background = background;
    s.notify = notify;
    s.flush = uflush;
    s.clear = uclear;
    s.unknown = unknown;
    for (kind = 0; kind <= 2; ++kind)
        for (redo = 0; redo <= 1; ++redo)
            for (fault = 0; fault <= CREATE; ++fault)
            {
                memset(&u, 0, sizeof(u));
                u.fail = fault;
                result = mm_movie_scene_undo(kind, 2, 0, 0, redo, &s);
                removed = (kind == MM_MOVIE_REMOVE_SCENE) == redo;
                if (kind == 2)
                    CHECK(!result && u.unknown == 1);
                else if (removed)
                    CHECK(result == (fault != TEMP) && u.remove == 1 && !u.create && !u.insert);
                else if (!redo)
                    CHECK(result == (fault != WRITE) && u.insert == 1 && !u.create && !u.remove);
                else
                {
                    CHECK(result == (fault != CREATE && fault != START) && u.create == 1 && !u.insert);
                    CHECK(u.background == (fault != CREATE) && u.remove == (fault == START));
                }
                CHECK(u.flush == 1 && u.clear == !result && u.notify == result);
                if (result)
                    CHECK(u.removed == removed);
            }
    return 0;
}
typedef struct Cursor
{
    MMPolledMovie playback;
    MMI32 selected, frame, range_first, range_last;
    int keep_audio, keep_framing, count, fail, failure;
    char trace[24];
} Cursor;

static int cursor_effect(void *p, MMMovieSceneCursorEffect effect, MMMovieSceneCursorRequest *request)
{
    static const char steps[] = "CSLBRGQFAPIVOWXH";
    Cursor *c = (Cursor *)p;
    if (c->count >= (int)sizeof(c->trace) - 1)
        return -99;
    c->trace[c->count++] = steps[effect];
    c->trace[c->count] = 0;
    if ((int)effect == c->fail)
        return c->failure;
    switch (effect)
    {
    case MM_MOVIE_SCENE_LOAD:
        c->keep_framing = request->keep_framing;
        c->keep_audio = request->keep_audio;
        break;
    case MM_MOVIE_SCENE_READ_RANGE:
        request->first = -3; request->last = 10;
        break;
    case MM_MOVIE_SCENE_SET_RANGE:
        c->range_first = request->first; c->range_last = request->last;
        break;
    case MM_MOVIE_SCENE_READ_FRAME:
        request->frame = c->frame;
        break;
    case MM_MOVIE_SCENE_SET_FRAME:
        c->frame = request->frame;
        break;
    default:
        break;
    }
    return 1;
}

static void cursor_clear_trace(Cursor *c)
{
    c->count = 0;
    c->trace[0] = 0;
}

static int cursor_operations(void)
{
    Cursor c;
    MMMovieSceneCursorServices services = {NULL, NULL, NULL, cursor_effect};
    const unsigned keep = MM_MOVIE_SCENE_KEEP_AUDIO | MM_MOVIE_SCENE_KEEP_FRAMING;
    memset(&c, 0, sizeof(c));
    services.context = &c;
    services.selected = &c.selected;
    services.playback = &c.playback;
    c.selected = -1;
    c.fail = -1;
    CHECK(!mm_movie_scene_cursor_select(2, 2, 0, &services) && !c.count && c.selected == -1);
    CHECK(mm_movie_scene_cursor_select(0, 2, MM_MOVIE_SCENE_INITIAL | keep, &services) == 1);
    CHECK(!strcmp(c.trace, "LBVOW") && c.selected == 0 && c.keep_framing && !c.keep_audio);
    CHECK(c.playback.restore_audio && !c.playback.finished);
    cursor_clear_trace(&c);
    CHECK(mm_movie_scene_cursor_select(1, 2, keep, &services) == 1);
    CHECK(!strcmp(c.trace, "CLBRGQFAPVOW") && c.selected == 1 && c.keep_framing && c.keep_audio);
    CHECK(c.frame == -3 && c.range_first == -3 && c.range_last == 10 && !c.playback.restore_audio);
    cursor_clear_trace(&c);
    CHECK(mm_movie_scene_cursor_select(0, 2, 0, &services) == 1);
    CHECK(!strcmp(c.trace, "CSLBRGQAPIVOW") && c.selected == 0 && !c.keep_framing && !c.keep_audio);
    cursor_clear_trace(&c);
    c.fail = MM_MOVIE_SCENE_READ_SOUNDS;
    c.playback.finished = 1;
    CHECK(!mm_movie_scene_cursor_select(1, 2, keep, &services));
    CHECK(!strcmp(c.trace, "CLBRGQAPVOWS") && c.selected == -1 && c.playback.finished);
    cursor_clear_trace(&c);
    c.fail = MM_MOVIE_SCENE_LOAD;
    c.failure = -7;
    CHECK(mm_movie_scene_cursor_select(0, 2, keep, &services) == -7 && !strcmp(c.trace, "CL"));
    cursor_clear_trace(&c);
    CHECK(!mm_movie_scene_cursor_seek(4, &services) && !strcmp(c.trace, "X"));
    c.selected = 0;
    c.fail = MM_MOVIE_SCENE_SET_FRAME;
    c.failure = 0;
    cursor_clear_trace(&c);
    CHECK(!mm_movie_scene_cursor_seek(4, &services) && !strcmp(c.trace, "XF") && c.playback.finished);
    c.fail = -1;
    cursor_clear_trace(&c);
    CHECK(mm_movie_scene_cursor_seek(-2, &services) == 1 && !strcmp(c.trace, "XFHO"));
    CHECK(c.frame == -2 && c.selected == 0 && c.playback.restore_audio && !c.playback.finished);
    return 0;
}

typedef struct RangeTest
{
    MMI32 selected, camera;
    MMPolledMovie playback;
    MMMovieRangeEdit live, published;
    MMSceneState stored;
    int fail, fail_restore, steps;
    char trace[20];
} RangeTest;

static void range_step(RangeTest *r, char step)
{
    r->trace[r->steps++] = step;
    r->trace[r->steps] = 0;
}

static void range_position(void *p, MMMovieRangeEdit *edit)
{
    *edit = ((RangeTest *)p)->live;
}

static int range_effect(void *p, MMMovieRangeEffect effect, const MMMovieRangeEdit *edit, int restoring)
{
    static const char forward[] = "CPBFRX", restore[] = "cpbfrx";
    RangeTest *r = p;
    range_step(r, (restoring ? restore : forward)[effect]);
    if ((!restoring && (int)effect == r->fail) ||
        (restoring && r->fail_restore && effect == MM_MOVIE_RANGE_SET_BOUNDS))
        return 0;
    if (effect == MM_MOVIE_RANGE_SET_BOUNDS)
    {
        r->live.first = edit->first;
        r->live.last = edit->last;
    }
    if (effect == MM_MOVIE_RANGE_SET_FRAME)
        r->live.frame = edit->frame;
    return 1;
}

static void range_publish(void *p)
{
    RangeTest *r = p;
    range_step(r, '!');
    r->published = r->live;
}
static MMI32 one_camera(void *p) { (void)p; return 1; }
static void camera_at(void *p, MMI32 index, MMI32 *frame, int *kind)
{
    (void)index;
    *frame = ((RangeTest *)p)->camera;
    *kind = MM_SCENE_CHANGE_CAMERA;
}
static void move_opening_camera(void *p, MMI32 index, MMI32 frame)
{
    (void)index;
    range_step(p, 'M');
    ((RangeTest *)p)->camera = frame;
}
static int range_open_events(void *p) { range_step(p, 'O'); return 1; }
static int range_replace_events(void *p) { range_step(p, 'E'); return !((RangeTest *)p)->fail; }
static int range_drop_thumbnail(void *p) { range_step(p, 'T'); return 1; }
static int range_write_scene(void *p) { range_step(p, 'W'); return 1; }

static int extending(void)
{
    RangeTest r;
    MMMovieRangeServices edit = {NULL, NULL, NULL, range_position, range_effect, range_publish};
    MMSceneRuntimeServices events;
    MMSceneRangeStoreServices store = {NULL, range_open_events, range_replace_events, range_drop_thumbnail,
                                      range_write_scene};
    memset(&r, 0, sizeof(r));
    edit.context = events.context = &r;
    edit.selected = &r.selected;
    edit.playback = &r.playback;
    r.live.first = r.live.last = r.live.frame = 1;
    r.playback.finished = 1;
    r.fail = MM_MOVIE_RANGE_PREPARE;
    CHECK(!mm_movie_scene_extend(-1, &edit) && !strcmp(r.trace, "CP"));
    CHECK(r.live.first == 1 && r.playback.finished);
    r.steps = 0;
    r.fail = MM_MOVIE_RANGE_PREPARE_FRAME;
    CHECK(!mm_movie_scene_extend(-1, &edit) && !strcmp(r.trace, "CPBFRbf"));
    CHECK(r.live.first == 1 && r.live.frame == 1 && r.playback.finished && r.selected == 0);
    r.steps = 0;
    r.fail = -1;
    CHECK(mm_movie_scene_extend(-1, &edit) == 1 && !strcmp(r.trace, "CPBFR!"));
    CHECK(r.published.first == 0 && r.published.frame == 0 && r.published.last == 1);
    CHECK(!r.playback.finished && r.playback.restore_audio);
    r.steps = 0;
    CHECK(mm_movie_scene_extend(1, &edit) == MM_MOVIE_RANGE_BOUNDARY && !strcmp(r.trace, "C"));
    r.steps = 0;
    r.fail = MM_MOVIE_RANGE_SET_FRAME;
    r.fail_restore = 1;
    CHECK(!mm_movie_scene_extend(-1, &edit) && !strcmp(r.trace, "CPBFbx") && r.selected == -1);
    CHECK(r.published.first == 0);
    r.steps = 0;
    memset(&events, 0, sizeof(events));
    events.context = &r;
    events.event_count = one_camera;
    events.event = camera_at;
    events.move_camera = move_opening_camera;
    store.events = &events;
    r.stored.firstFrame = r.stored.lastFrame = r.camera = 1;
    CHECK(mm_scene_range_store(&r.stored, -2147483647L - 1L, 2, &store) == MM_SCENE_RANGE_INVALID);
    CHECK(!r.steps);
    r.fail = 0;
    CHECK(mm_scene_range_store(&r.stored, 1, 2, &store) == 1 && !strcmp(r.trace, "W"));
    CHECK(r.stored.lastFrame == 2 && r.camera == 1);
    r.steps = 0;
    CHECK(mm_scene_range_store(&r.stored, -2, 3, &store) == 1 && !strcmp(r.trace, "OMETW"));
    CHECK(r.stored.firstFrame == -2 && r.stored.lastFrame == 3 && r.camera == -2);
    r.steps = 0;
    r.fail = 1;
    CHECK(!mm_scene_range_store(&r.stored, -3, 4, &store) && !strcmp(r.trace, "OME"));
    CHECK(r.stored.lastFrame == 3);
    return 0;
}

static int journal(void)
{
    const MMI32 opening = (-2147483647 - 1);
    MMMovieSceneDefinitions scenes, old_scenes;
    MMMovieCameraJournal cameras, old_cameras;
    MMPreviewLiveScene scene = {8, 20, 2};
    MMMovieCameraEdit cut = {8, 3, 1};
    const MMPreviewLiveScene *definitions;
    const MMMovieCameraEdit *edits;
    MMPreviewLiveCameraEdits view;
    MMPreviewLiveCamera cue;
    MMU32 count;

    CHECK(mm_movie_scene_definitions_init(&scenes, NULL) && mm_movie_scene_definitions_init(&old_scenes, NULL));
    CHECK(mm_movie_scene_definitions_append(&scenes, &scene));
    scene.number = 2;
    CHECK(mm_movie_scene_definitions_append(&scenes, &scene));
    CHECK(mm_movie_scene_definitions_copy(&old_scenes, &scenes));
    scene.number = 8;
    scene.source = 6;
    scene.background = 80;
    mm_movie_scene_definitions_retarget(&scenes, &scene);
    definitions = mm_movie_scene_definitions_data(&scenes);
    CHECK(definitions[0].number == 8 && definitions[0].background == 80 && definitions[1].number == 2);
    CHECK(mm_movie_scene_definitions_uses_source(&scenes, 6) && !mm_movie_scene_definitions_uses_source(&old_scenes, 6));
    mm_movie_scene_definitions_dispose(&scenes);
    mm_movie_scene_definitions_dispose(&old_scenes);

    CHECK(mm_movie_camera_journal_init(&cameras, NULL) && mm_movie_camera_journal_init(&old_cameras, NULL));
    CHECK(mm_movie_camera_journal_set(&cameras, &cut));
    cut.frame = opening;
    cut.camera = 2;
    CHECK(mm_movie_camera_journal_set(&cameras, &cut));
    cut.scene = (MMU32)-1;
    CHECK(mm_movie_camera_journal_set(&cameras, &cut));
    cut.scene = 2;
    CHECK(mm_movie_camera_journal_set(&cameras, &cut));
    CHECK(mm_movie_camera_journal_copy(&old_cameras, &cameras));
    cut.scene = 8;
    cut.frame = 3;
    cut.camera = 5;
    CHECK(mm_movie_camera_journal_set(&cameras, &cut) && cameras.count == 4);
    edits = mm_movie_camera_journal_find(&cameras, 8, &count);
    CHECK(edits && count == 2 && edits[0].frame == opening && edits[1].frame == 3 && edits[1].camera == 5);
    edits = mm_movie_camera_journal_find(&old_cameras, 8, &count);
    CHECK(edits && count == 2 && edits[1].camera == 1);
    CHECK(!mm_movie_camera_journal_find(&cameras, 7, &count) && count == 0);
    view = mm_movie_camera_journal_preview(&cameras, (MMU32)-1);
    CHECK(view.count == 1);
    view.get(view.context, 0, &cue);
    CHECK(cue.frame == opening && cue.camera == 2);
    mm_movie_camera_journal_dispose(&cameras);
    mm_movie_camera_journal_dispose(&old_cameras);
    return 0;
}

int main(void)
{
    if (switching() || insertion() || removal() || undoing() || cursor_operations() || extending() || journal())
        return 1;
    puts("Movie scene ownership, retry, rollback and undo passed");
    return 0;
}
