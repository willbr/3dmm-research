/* Movie scene-container transactions, retry paths and ownership. */
#include "mm/movie_scene.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_scene_journal.h"
#include "mm/material_resource.h"
#include "mm/scene_runtime.h"
#include "movie_scene_session_fixture.h"
#include "scene_snapshot_fixture.h"
#include <stdio.h>
#include <stdlib.h>
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
enum { ACTR = 0x41435452, BKGD = 0x424b4744, MTRL = 0x4d54524c, SCEN = 0x5343454e };
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
    ADD_UNDO,
    PRESERVE_MATERIALS,
    SNAPSHOT_OPEN,
    SNAPSHOT_WRITE,
    SNAPSHOT_COPY
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
    MMMovieState data;
    ChunkIndexFixture archive;
    MMStorageAllocator archive_allocator;
    MMMovieSceneServices s;
    Scene scenes[32];
    int used, fail, failures, skip;
    int autosaves, reads, starts, seeks, stops, memory, changes, sound, invalidates;
    int change_index[8], change_open[8];
    int moves, from[8], to[8], saves, deletes, adds, removes;
    int dirty, rollcall, errors, enabled, played, flushed, cleared, missing;
    int undo_refs, tag_refs, snapshot_copies, snapshot_writes;
    MMMovieSceneUndoRecord undo;
    SnapshotFile snapshot_file;
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
static void *resize_archive(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Test *t = p;
    (void)before;
    (void)priority;
    if (t->fail == PRESERVE_MATERIALS)
    {
        if (t->removes || t->deletes)
            abort();
        if (!succeeds(t, PRESERVE_MATERIALS))
            return NULL;
    }
    return realloc(old, bytes);
}
static void release_archive(void *p, void *data, MMU32 bytes)
{
    (void)p;
    (void)bytes;
    free(data);
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
static void movie_graph(void *file, MMMovieSessionGraph *out)
{
    *out = fixture_scene_graph(&((Test *)file)->archive);
}
static void *read_scene(void *p, void *resources, MMU32 number, const MMMovieSession *session)
{
    Test *t = p;
    MMI32 i = fixture_scene_slot(&t->archive, number);
    if (p != resources || session != t->data.session || i < 0)
        return NULL;
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
static MMChunkAccess chunks(void *p)
{
    return fixture_chunk_access(&((Test *)p)->archive);
}
static MMChunkEdit edit_archive(void *p)
{
    Test *t = p;
    MMChunkEdit edit = fixture_chunk_edit(&t->archive);
    edit.walk_allocator = &t->archive_allocator;
    return edit;
}
static int write_scene(void *p, void *s, MMU32 *n)
{
    Test *t = p;
    (void)s;
    if (!succeeds(t, WRITE))
        return 0;
    *n = 123;
    fixture_chunk_add(&t->archive, MM_SCENE_CHUNK, *n);
    return 1;
}
static void move(void *p, const MMChunkChild *old, const MMChunkChild *next)
{
    Test *t = p;
    if (old->number != next->number)
        abort();
    t->from[t->moves] = old->slot;
    t->to[t->moves++] = next->slot;
}
static int insert_link(void *p, MMU32 parent, const MMChunkChild *child)
{
    Test *t = p;
    if (parent != FIXTURE_MOVIE_ROOT || child->type != MM_SCENE_CHUNK || child->number != 123)
        abort();
    return succeeds(t, ADOPT);
}
static void erase_link(void *p, MMU32 parent, const MMChunkChild *child)
{
    Test *t = p;
    if (parent != FIXTURE_MOVIE_ROOT || child->type != MM_SCENE_CHUNK)
        abort();
    ++t->deletes;
}
static void bad_archive_edit(void *p, int issue)
{
    (void)p;
    fprintf(stderr, "unexpected archive issue %d\n", issue);
    abort();
}
static void missing_link(void *p)
{
    ++((Test *)p)->missing;
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
static void *new_undo(void *p, MMMovieSceneUndoRecord **record)
{
    Test *t = p;
    if (!succeeds(t, UNDO))
        return 0;
    t->undo_refs = 1;
    *record = &t->undo;
    return t;
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
        mm_movie_scene_undo_dispose(&t->undo, t->s.undo_tags);
}
static MMPrivateArchive *open_snapshot(void *p)
{
    Test *t = p;
    return succeeds(t, SNAPSHOT_OPEN) ? snapshot_file_open(&t->snapshot_file) : NULL;
}
static void snapshot_long_op(void *p) { (void)p; }
static int write_snapshot(void *p, void *scene, MMPrivateArchive *archive, MMU32 *number)
{
    Test *t = p;
    const Scene *value = scene;
    ++t->snapshot_writes;
    if (!succeeds(t, SNAPSHOT_WRITE))
        return 0;
    *number = (MMU32)value->index + 40U;
    put_snapshot(archive, *number);
    return 1;
}
static int copy_snapshot(void *p, const MMSceneSnapshot *snapshot, MMU32 *number)
{
    Test *t = p;
    ++t->snapshot_copies;
    return succeeds(t, SNAPSHOT_COPY) && has_snapshot(snapshot->archive, snapshot->number) &&
        write_scene(p, NULL, number);
}
static void retain_tag(void *p) { ++((Test *)p)->tag_refs; }
static void release_tag(void *p) { --((Test *)p)->tag_refs; }
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
    t->data.sceneCount = count;
    t->data.movieNumber = FIXTURE_MOVIE_ROOT;
    t->data.resources = t;
    fixture_scene_archive_init(&t->archive, count);
    t->archive_allocator.context = t;
    t->archive_allocator.resize = resize_archive;
    t->archive_allocator.release = release_archive;
    t->archive.observer = t;
    t->archive.before_insert = insert_link;
    t->archive.before_unlink = erase_link;
    t->archive.before_move = move;
    if (current != -1)
    {
        t->state.open = new_scene(t, current);
        t->state.index = current;
    }
    t->enabled = 1;
    s = &t->s;
    s->context = t;
    s->state = &t->state;
    s->movie = &t->data;
    s->autosave = autosave;
    s->make_resources = make;
    s->use_temporary = temporary;
    s->stop_loops = stop;
    s->memory_cleanup = memory;
    s->session.graph = movie_graph;
    s->read = read_scene;
    s->create = create;
    s->start = start;
    s->seek_first = seek;
    s->close = close_scene;
    s->retain = retain;
    s->release = release;
    s->sound_frame = sound;
    s->scene_change = change;
    s->invalidate = invalidate;
    s->edit = edit_archive;
    s->write = write_scene;
    s->archive_issue = bad_archive_edit;
    s->missing_link = missing_link;
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
    s->snapshot.context = t;
    s->snapshot.open = open_snapshot;
    s->snapshot.begin = s->snapshot.end = snapshot_long_op;
    s->snapshot.write = write_snapshot;
    s->copy_snapshot = copy_snapshot;
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
    mm_movie_session_close(t->data.session);
    if (t->snapshot_file.refs)
        return 0;
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
        CHECK(t.data.sceneCount == (fault == ADOPT ? 4 : 3));
        CHECK(t.archive.count == 4 && t.archive.rows[0].entry.child_count == 3);
        CHECK(t.deletes == (fault >= READ));
        CHECK(t.saves == (fault >= READ ? 2 : 0));
        if (fault == ADOPT)
        {
            CHECK(t.moves == 4 && t.from[0] == 2 && t.from[1] == 1);
            CHECK(t.from[2] == 2 && t.from[3] == 3 && !t.missing);
            CHECK(fixture_scene_slot(&t.archive, 100) == 0 && fixture_scene_slot(&t.archive, 101) == 1 &&
                  fixture_scene_slot(&t.archive, 102) == 2 && fixture_scene_slot(&t.archive, 123) == -1);
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
        CHECK(t.data.sceneCount == 4 && t.state.index == 1 && t.moves == 2 && t.saves == 1);
        CHECK(fixture_scene_slot(&t.archive, 100) == 0 && fixture_scene_slot(&t.archive, 123) == 1 &&
              fixture_scene_slot(&t.archive, 101) == 2 && fixture_scene_slot(&t.archive, 102) == 3);
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
    MMChunkEdit edit;
    const MMChunkChild missing = {MM_SCENE_CHUNK, 100, 0};
    const MMChunkChild background = {BKGD, 2, 0};
    MMChunkAccess index;
    MMChunkEntry asset;
    MMU32 row;
    int i;
    for (i = 0; i < 3; ++i)
    {
        init(&t, 3, i);
        CHECK(mm_movie_scene_remove(i, &t.s));
        CHECK(t.data.sceneCount == 2 && t.state.index == (i < 2 ? i : 1));
        CHECK(t.removes == 1 && t.deletes == 1 && t.saves == 1 && t.dirty == 1 && t.moves == 2 - i);
        CHECK(t.archive.count == 3 && fixture_scene_slot(&t.archive, 100 + i) == -1);
        CHECK(clean(&t));
    }
    init(&t, 1, 0);
    CHECK(mm_movie_scene_remove(0, &t.s) && t.data.sceneCount == 0 && !t.state.open && !t.dirty && clean(&t));
    init(&t, 3, 0);
    t.fail = TEMP;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove(1, &t.s) && t.state.index == 0 && t.reads == 0 && clean(&t));
    init(&t, 3, 0);
    t.fail = READ;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove(1, &t.s) && t.state.index == 0 && t.reads == 2 && !t.deletes && clean(&t));
    init(&t, 3, 0);
    edit = edit_archive(&t);
    t.archive.before_unlink = NULL;
    CHECK(mm_chunk_unlink(&edit, MM_MOVIE_CHUNK, FIXTURE_MOVIE_ROOT, &missing) == MM_CHUNK_OK);
    CHECK(!mm_movie_scene_remove(0, &t.s) && t.state.index == 0 && t.data.sceneCount == 3 &&
          t.missing == 1 && !t.deletes && !t.removes && clean(&t));
    init(&t, 3, 0);
    fixture_chunk_add(&t.archive, BKGD, background.number);
    edit = edit_archive(&t);
    t.archive.before_insert = NULL;
    CHECK(mm_chunk_adopt(&edit, SCEN, 101, &background, 1) == MM_CHUNK_OK);
    t.fail = PRESERVE_MATERIALS;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove(1, &t.s) && t.state.index == 0 && t.data.sceneCount == 3 && !t.removes &&
          !t.deletes && !t.saves && !t.dirty && !t.failures);
    index = chunks(&t);
    CHECK(mm_chunk_at(&index, 0, &asset) == MM_CHUNK_OK && asset.type == BKGD && !asset.flags &&
          asset.parent_count == 1 && fixture_scene_slot(&t.archive, 101) == 1);
    t.fail = 0;
    CHECK(mm_movie_scene_remove(1, &t.s) && t.data.sceneCount == 2 && t.state.index == 1);
    index = chunks(&t);
    CHECK(mm_chunk_find(&index, SCEN, 101, &row) == MM_CHUNK_NOT_FOUND &&
          mm_chunk_at(&index, 0, &asset) == MM_CHUNK_OK && asset.type == BKGD &&
          (asset.flags & MM_CHUNK_LONER) && !asset.parent_count && clean(&t));
    init(&t, 3, 0);
    t.fail = READ;
    t.failures = 1;
    CHECK(mm_movie_scene_remove(0, &t.s) && t.data.sceneCount == 2 && !t.state.open && t.errors == 1 && t.dirty == 1 &&
          clean(&t));
    for (i = 0; i < 2; ++i)
    {
        Scene *captured;
        init(&t, 3, i);
        captured = i ? &t.scenes[1] : t.state.open;
        CHECK(mm_movie_scene_remove_undoable(0, &t.s));
        CHECK(t.data.sceneCount == 2 && t.undo.operation == MM_MOVIE_REMOVE_SCENE && t.undo.index == 0 &&
              !captured->refs && t.undo_refs == 1 && t.snapshot_file.refs == 1 && t.snapshot_writes == 1 &&
              has_snapshot(t.undo.saved.snapshot.archive, 40));
        CHECK(t.starts == 1 + i && clean(&t));
    }
    init(&t, 3, 0);
    t.fail = UNDO;
    t.failures = 1;
    CHECK(mm_movie_scene_remove_undoable(0, &t.s) && t.data.sceneCount == 3 && !t.removes && clean(&t));
    init(&t, 3, 0);
    t.fail = ADD_UNDO;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && t.data.sceneCount == 3 && !t.undo_refs && clean(&t));
    init(&t, 3, 0);
    t.fail = TEMP;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && t.data.sceneCount == 3 && t.cleared == 1 && clean(&t));
    init(&t, 3, 1);
    t.fail = START;
    t.failures = 1;
    CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && !t.undo_refs && clean(&t));
    for (i = SNAPSHOT_OPEN; i <= SNAPSHOT_WRITE; ++i)
    {
        init(&t, 3, 0);
        t.fail = i;
        t.failures = 1;
        CHECK(!mm_movie_scene_remove_undoable(0, &t.s) && t.state.index == 0 && t.data.sceneCount == 3 &&
              !t.removes && !t.undo_refs && !t.snapshot_file.refs && clean(&t));
    }
    return 0;
}

typedef struct Undo
{
    Test movie;
    MMSceneOwner owner;
    int background, notify, removed, unknown, fail_background;
} Undo;
static void *fetch_background(void *p, MMTag *tag)
{
    Undo *u = p;
    (void)tag;
    ++u->background;
    return u->fail_background ? NULL : u;
}
static int set_camera(void *p, void *background, MMI32 camera)
{
    return p == background && camera == 0;
}
static int transition(void *p)
{
    (void)p;
    return MM_SCENE_EVENT_CUT_TRANSITION;
}
static void ignore_background(void *p, void *background)
{
    (void)p;
    (void)background;
}
static void ignore_change(void *p) { (void)p; }
static int cannot_notify(void *p) { (void)p; return 0; }
static MMSceneEditServices bind_scene_edit(void *p)
{
    Undo *u = (Undo *)((Scene *)p)->test;
    MMSceneEditServices s = {0};
    s.context = s.sound.context = s.sound.events.context = s.sound.events.owners.context = u;
    s.sound.events.owner = &u->owner;
    s.sound.events.owners.release = ignore_background;
    s.sound.events.owners.release_background = ignore_background;
    s.sound.events.fetch_background = fetch_background;
    s.sound.events.set_camera = set_camera;
    s.sound.events.transition = transition;
    s.sound.dirty = ignore_change;
    s.background_name = ignore_change;
    s.scene_change = ignore_change;
    s.can_notify = cannot_notify;
    return s;
}
static void notify(void *p, int removed)
{
    Undo *u = p;
    ++u->notify;
    u->removed = removed;
}
static void uclear(void *p)
{
    Test *t = p;
    t->cleared = t->flushed == 1 ? 1 : -1;
}
static void unknown(void *p)
{
    ++((Undo *)p)->unknown;
}
static int undoing(void)
{
    const int kinds[] = {MM_MOVIE_INSERT_SCENE, MM_MOVIE_REMOVE_SCENE, MM_MOVIE_SCENE_UNDO_EMPTY};
    MMMovieSceneUndoServices s;
    Undo u;
    MMTag tag = {2, NULL, 4, 5};
    int k, redo, fault;
    memset(&s, 0, sizeof(s));
    s.scenes = &u.movie.s;
    s.scene_edit = bind_scene_edit;
    s.notify = notify;
    s.unknown = unknown;
    for (k = 0; k < 3; ++k)
        for (redo = 0; redo <= 1; ++redo)
            for (fault = 0; fault <= SNAPSHOT_COPY; ++fault)
            {
                Scene *snapshot;
                MMSceneOwnerServices owners;
                MMMovieSceneUndoRecord record = {0};
                const int kind = kinds[k];
                int result, removed = (kind == MM_MOVIE_REMOVE_SCENE) == redo;
                memset(&u, 0, sizeof(u));
                init(&u.movie, 3, 2);
                CHECK(mm_scene_owner_init(&u.owner, NULL, 1, NULL));
                u.movie.s.clear_undo = uclear;
                u.movie.fail = (removed && fault == TEMP) ||
                                   (!removed && !redo && (fault == AUTO || fault == WRITE || fault == SNAPSHOT_COPY)) ||
                                   (!removed && redo && fault == CREATE)
                               ? fault : 0;
                u.movie.failures = 1;
                u.fail_background = fault == START;
                snapshot = new_scene(&u.movie, 2);
                owners = bind_scene_edit(snapshot).sound.events.owners;
                record.operation = kind;
                record.index = 2;
                if (kind == MM_MOVIE_REMOVE_SCENE)
                    CHECK(mm_scene_snapshot_save(&record.saved.snapshot, snapshot, &u.movie.s.snapshot));
                else if (kind == MM_MOVIE_INSERT_SCENE)
                    record.saved.background = tag;
                result = mm_movie_scene_undo(&record, redo, &s);
                if (kind == MM_MOVIE_SCENE_UNDO_EMPTY)
                    CHECK(!result && u.unknown == 1 && u.movie.data.sceneCount == 3);
                else if (removed)
                    CHECK(result == (fault != TEMP) && u.movie.deletes == result &&
                          u.movie.data.sceneCount == 3 - result);
                else if (!redo)
                {
                    const MMChunkAccess archive = chunks(&u.movie);
                    MMU32 position;
                    CHECK(result == (fault != AUTO && fault != WRITE && fault != SNAPSHOT_COPY) &&
                          u.movie.adds == result && u.movie.data.sceneCount == 3 + result && u.movie.snapshot_copies == 1);
                    CHECK(mm_chunk_find(&archive, MM_SCENE_CHUNK, 123, &position) ==
                          (result ? MM_CHUNK_OK : MM_CHUNK_NOT_FOUND));
                    if (fault == AUTO)
                        CHECK(u.movie.state.open && u.movie.state.index == 2);
                }
                else
                {
                    CHECK(result == (fault != CREATE && fault != START));
                    CHECK(u.background == (fault != CREATE) && u.movie.deletes == (fault == START));
                    CHECK(u.movie.data.sceneCount == 3 + result);
                    CHECK(u.owner.tracks[MM_SCENE_STARTS].rows.count == (unsigned)result);
                    if (result)
                        CHECK(u.owner.background_tag.number == tag.number);
                }
                CHECK(u.movie.flushed == 1 && u.movie.cleared == !result && u.notify == result);
                if (result)
                    CHECK(u.removed == removed);
                mm_scene_owner_dispose(&u.owner, &owners);
                mm_movie_scene_undo_dispose(&record, NULL);
                release(snapshot);
                CHECK(clean(&u.movie));
            }
    return 0;
}
static int undo_insertion_ownership(void)
{
    Test t;
    MMTagServices tags = {0};
    MMTag tag = {0};
    const int failures[] = {0, UNDO, ADD_UNDO};
    int i;
    tags.retain = retain_tag;
    tags.release = release_tag;
    tag.cache = &t;
    tag.type = BKGD;
    tag.number = 17;
    for (i = 0; i < 3; ++i)
    {
        init(&t, 3, 2);
        t.s.undo_tags = &tags;
        t.fail = failures[i];
        t.failures = 1;
        CHECK(mm_movie_scene_record_insertion(2, &tag, &t.s) == (i == 0));
        CHECK(t.tag_refs == (i == 0) && t.undo_refs == (i == 0));
        if (!i)
            CHECK(t.undo.operation == MM_MOVIE_INSERT_SCENE && t.undo.index == 2 &&
                  t.undo.saved.background.cache == &t && t.undo.saved.background.number == 17);
        CHECK(clean(&t) && !t.tag_refs && t.undo.operation == MM_MOVIE_SCENE_UNDO_EMPTY);
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
static void camera_at(void *p, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    (void)group;
    (void)index;
    *frame = ((RangeTest *)p)->camera;
    *kind = MM_SCENE_CHANGE_CAMERA;
}
static void move_opening_camera(void *p, void *group, MMI32 index, MMI32 frame)
{
    (void)group;
    (void)index;
    range_step(p, 'M');
    ((RangeTest *)p)->camera = frame;
}
static int range_open_events(void *p, MMMovieEventView *view)
{
    range_step(p, 'O');
    view->context = p;
    view->group = NULL;
    view->count = 1;
    view->read = camera_at;
    return 1;
}
static int range_replace_events(void *p) { range_step(p, 'E'); return !((RangeTest *)p)->fail; }
static int range_drop_thumbnail(void *p) { range_step(p, 'T'); return 1; }
static int range_write_scene(void *p) { range_step(p, 'W'); return 1; }

static int extending(void)
{
    RangeTest r;
    MMMovieRangeServices edit = {NULL, NULL, NULL, range_position, range_effect, range_publish};
    MMSceneRangeStoreServices store = {NULL, range_open_events, move_opening_camera, range_replace_events,
                                      range_drop_thumbnail, range_write_scene};
    memset(&r, 0, sizeof(r));
    edit.context = store.context = &r;
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

typedef struct MaterialGraph
{
    MMChunkEntry rows[12];
    MMChunkChild kept[4];
    MMU32 count;
    int unreadable, allocCalls, failAt, liveBlocks;
} MaterialGraph;

static void material_graph_init(MaterialGraph *graph)
{
    static const MMChunkEntry rows[] = {
        {ACTR, 0, 0, 0, 0, 4, 1, 0}, {ACTR, 1, 0, 0, 0, 2, 1, 0},
        {ACTR, 2, 0, 0, 0, 1, 2, 0}, {ACTR, 3, 0, 0, 0, 1, 1, 0},
        {BKGD, 10, 0, 0, 0, 0, 1, 0},
        {MTRL, 10, 0, 0, 0, 1, 2, 0}, {MTRL, 11, 0, 0, 0, 0, 2, 0},
        {MTRL, 12, 0, 0, MM_CHUNK_LONER, 0, 1, 0}, {MTRL, 13, 0, 0, 0, 0, 1, 0},
        {MTRL, 14, 0, 0, 0, 0, 1, 0}, {SCEN, 1, 0, 0, 0, 3, 1, 0},
        {SCEN, 2, 0, 0, 0, 1, 1, 0}
    };
    memset(graph, 0, sizeof(*graph));
    memcpy(graph->rows, rows, sizeof(rows));
}
static int material_entry(void *p, MMU32 row, MMChunkEntry *entry)
{
    *entry = ((MaterialGraph *)p)->rows[row];
    return 1;
}

static int material_child(void *p, MMU32 row, MMU32 i, MMChunkChild *child)
{
    static const MMChunkChild links[][4] = {
        {{ACTR, 2, 0}, {MTRL, 10, 1}, {MTRL, 11, 2}, {MTRL, 12, 3}},
        {{ACTR, 2, 0}, {MTRL, 10, 1}}, {{MTRL, 13, 0}}, {{MTRL, 11, 0}},
        {{0}}, {{MTRL, 14, 0}}, {{0}}, {{0}}, {{0}}, {{0}},
        {{ACTR, 0, 0}, {BKGD, 10, 0}, {ACTR, 1, 1}}, {{ACTR, 3, 0}}
    };
    MaterialGraph *graph = (MaterialGraph *)p;
    if (graph->unreadable && row == 1)
        return 0;
    *child = links[row][i];
    return 1;
}

static MMU32 material_count(void *p)
{
    (void)p;
    return 12;
}
static void material_store(void *p, MMU32 row, const MMChunkEntry *entry, int initialize)
{
    MaterialGraph *graph = p;
    if (initialize || !(entry->flags & MM_CHUNK_LONER))
        abort();
    graph->rows[row] = *entry;
    graph->kept[graph->count].type = entry->type;
    graph->kept[graph->count++].number = entry->number;
}
static void *material_resize(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    MaterialGraph *graph = p;
    void *data;
    (void)before;
    (void)priority;
    ++graph->allocCalls;
    if (graph->failAt && graph->allocCalls >= graph->failAt)
        return NULL;
    data = realloc(old, bytes);
    if (data && !old)
        ++graph->liveBlocks;
    return data;
}
static void material_release(void *p, void *data, MMU32 bytes)
{
    MaterialGraph *graph = p;
    (void)bytes;
    --graph->liveBlocks;
    free(data);
}

static void edit_file_retain(void *file) { ++*(int *)file; }
static void edit_file_release(void *file) { --*(int *)file; }

static int prop_journal(void)
{
    const MMMovieEditFileServices files = {edit_file_retain, edit_file_release};
    MaterialGraph memory;
    MMStorageAllocator allocator = {NULL, material_resize, material_release};
    MMMovieSceneDefinitions scenes;
    MMMoviePropJournal live, undo;
    MMMovieLiveDefinitions definitions;
    MMPreviewLiveScene scene = {9, 2, 18}, selected;
    MMPreviewLiveProp prop = {9, 3, 8, 18, 4, -2, 10, {32700, 0, -32700}}, visible;
    MMPreviewLiveDefinitions view;
    char name[] = "Generated text";
    char oversized[MM_MOVIE_PROP_NAME_BYTES + 1];
    int file = 1;
    memset(&memory, 0, sizeof(memory));
    allocator.context = &memory;
    definitions.scenes = &scenes;
    definitions.props = &undo;
    CHECK(mm_movie_scene_definitions_init(&scenes, &allocator) &&
          mm_movie_prop_journal_init(&live, &allocator, &files) &&
          mm_movie_prop_journal_init(&undo, &allocator, &files));
    CHECK(mm_movie_scene_definitions_append(&scenes, &scene));
    memory.failAt = memory.allocCalls + 1;
    CHECK(!mm_movie_prop_journal_append(&live, &prop, name, &file) && !live.count && file == 1);
    memory.failAt = 0;
    CHECK(mm_movie_prop_journal_append(&live, &prop, name, &file) && file == 2);
    name[0] = 'X';
    prop.number = 2;
    prop.first = 12;
    CHECK(mm_movie_prop_journal_append(&live, &prop, "Shelf", NULL));
    memset(oversized, 'X', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    CHECK(!mm_movie_prop_journal_append(&live, &prop, oversized, &file) && live.count == 2 && file == 2);
    memory.failAt = memory.allocCalls + 1;
    CHECK(!mm_movie_prop_journal_copy(&undo, &live) && !undo.count && file == 2);
    memory.failAt = 0;
    CHECK(mm_movie_prop_journal_copy(&undo, &live) && file == 3);
    mm_movie_prop_journal_dispose(&live);
    CHECK(file == 2 && !strcmp(mm_movie_prop_journal_data(&undo)[0].name, "Generated text"));
    CHECK(mm_movie_prop_journal_started(&undo, 9, -2) && !mm_movie_prop_journal_started(&undo, 9, -3) &&
          !mm_movie_prop_journal_started(&undo, 8, 99));
    view = mm_movie_definitions_preview(&definitions);
    CHECK(view.scene_count == 1 && view.prop_count == 2);
    view.scene(view.context, 0, &selected);
    CHECK(selected.number == scene.number && selected.background == scene.background && selected.source == scene.source);
    view.prop(view.context, 0, &visible);
    CHECK(visible.number == 3 && mm_movie_prop_placement_valid(visible.point));
    view.prop(view.context, 1, &visible);
    CHECK(visible.number == 2 && visible.first == 12);
    visible.point[2] = -32701;
    CHECK(!mm_movie_prop_placement_valid(visible.point));
    mm_movie_prop_journal_dispose(&undo);
    mm_movie_scene_definitions_dispose(&scenes);
    CHECK(file == 1 && !memory.liveBlocks);
    return 0;
}

static int range_journal(void)
{
    MaterialGraph memory;
    MMStorageAllocator allocator = {NULL, material_resize, material_release};
    MMMovieRangeJournal live, undo;
    MMMovieSceneRange range = {(MMU32)-1, -20, 30};
    memset(&memory, 0, sizeof(memory));
    allocator.context = &memory;
    CHECK(mm_movie_range_journal_init(&live, &allocator) && mm_movie_range_journal_init(&undo, &allocator));
    CHECK(mm_movie_range_journal_set(&live, &range));
    range.scene = 3;
    CHECK(mm_movie_range_journal_set(&live, &range));
    CHECK(mm_movie_range_journal_data(&live)[0].scene == 3 && !mm_movie_range_journal_find(&live, 4));
    memory.failAt = memory.allocCalls + 1;
    CHECK(!mm_movie_range_journal_copy(&undo, &live) && !undo.count);
    memory.failAt = 0;
    CHECK(mm_movie_range_journal_copy(&undo, &live));
    range.first = 31;
    CHECK(!mm_movie_range_journal_set(&live, &range) && mm_movie_range_journal_find(&live, 3)->first == -20);
    range.first = 1;
    CHECK(mm_movie_range_journal_set(&live, &range) && live.count == 2 &&
          mm_movie_range_journal_find(&live, 3)->first == 1 && mm_movie_range_journal_find(&undo, 3)->first == -20);
    CHECK(mm_movie_range_journal_find(&live, (MMU32)-1)->last == 30);
    mm_movie_range_journal_dispose(&undo);
    mm_movie_range_journal_dispose(&live);
    CHECK(!memory.liveBlocks);
    return 0;
}

static int track_journal(void)
{
    const MMMovieEditFileServices files = {edit_file_retain, edit_file_release};
    MaterialGraph memory;
    MMStorageAllocator allocator = {NULL, material_resize, material_release};
    MMMovieActorTrackJournal current, undo, empty;
    MMMovieActorTrackEdit edit = {9, 7, 20, NULL};
    int original = 1, other = 1, replacement = 1;
    memset(&memory, 0, sizeof(memory));
    allocator.context = &memory;
    CHECK(mm_movie_track_journal_init(&current, &allocator, &files) &&
          mm_movie_track_journal_init(&undo, &allocator, &files) &&
          mm_movie_track_journal_init(&empty, &allocator, &files));
    edit.file = &original;
    CHECK(mm_movie_track_journal_set(&current, &edit) && original == 2);
    edit.actor = 3;
    edit.file = &other;
    CHECK(mm_movie_track_journal_set(&current, &edit) && other == 2 &&
          mm_movie_track_journal_data(&current)[0].actor == 3);
    edit.scene = (MMU32)-1;
    edit.file = &replacement;
    CHECK(mm_movie_track_journal_set(&undo, &edit) && replacement == 2);
    memory.failAt = memory.allocCalls + 1;
    CHECK(!mm_movie_track_journal_set(&empty, &edit) && !empty.count && replacement == 2);
    CHECK(!mm_movie_track_journal_copy(&undo, &current) && undo.count == 1 &&
          mm_movie_track_journal_has_scene(&undo, (MMU32)-1) && original == 2 && other == 2 && replacement == 2);
    memory.failAt = 0;
    CHECK(mm_movie_track_journal_copy(&undo, &current) && original == 3 && other == 3 && replacement == 1);
    edit.scene = 9;
    edit.actor = 7;
    edit.source = 21;
    CHECK(mm_movie_track_journal_set(&current, &edit) && original == 2 && replacement == 2 &&
          mm_movie_track_journal_find(&undo, 9, 7)->source == 20);
    CHECK(mm_movie_track_journal_set(&current, mm_movie_track_journal_find(&current, 9, 7)) && replacement == 2);
    CHECK(mm_movie_track_journal_copy(&undo, &undo) && mm_movie_track_journal_has_scene(&current, 9) &&
          !mm_movie_track_journal_has_scene(&current, 8) && !mm_movie_track_journal_find(&current, 9, 6));
    mm_movie_track_journal_dispose(&current);
    CHECK(mm_movie_track_journal_find(&undo, 9, 7)->file == &original && original == 2 && replacement == 1);
    mm_movie_track_journal_dispose(&empty);
    mm_movie_track_journal_dispose(&undo);
    CHECK(original == 1 && other == 1 && !memory.liveBlocks);
    return 0;
}

static int pose_journal(void)
{
    MMMoviePoseJournal live, undo;
    const MMMovieActorPoseEdit *pose;
    const MMPreviewActorShape changed = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {2, 1, 1}};
    const MMPreviewActorShape identity = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {1, 1, 1}};
    const float forward[3] = {2, 0, -3}, back[3] = {-2, 0, 3};
    float offset[3] = {99, 99, 99};
    MaterialGraph memory;
    MMStorageAllocator allocator = {NULL, material_resize, material_release};
    memset(&memory, 0, sizeof(memory));
    allocator.context = &memory;
    CHECK(mm_movie_pose_journal_init(&live, &allocator) && mm_movie_pose_journal_init(&undo, &allocator));
    memory.failAt = 1;
    CHECK(!mm_movie_pose_journal_move(&live, 4, 8, forward, offset) && !live.count && offset[0] == 99);
    memory.failAt = 0;
    CHECK(mm_movie_pose_journal_move(&live, 4, 8, forward, offset) && offset[0] == 2 && offset[2] == -3);
    CHECK(mm_movie_pose_journal_move(&live, 4, 8, back, offset) && !live.count && !offset[0] && !offset[2]);
    CHECK(mm_movie_pose_journal_shape(&live, 4, 8, &changed));
    CHECK(mm_movie_pose_journal_move(&live, 4, 8, forward, offset));
    CHECK(mm_movie_pose_journal_copy(&undo, &live));
    CHECK(mm_movie_pose_journal_include(&live, 4, 8) && live.count == 1);
    CHECK(mm_movie_pose_journal_move(&live, 4, 8, back, offset) && live.count == 1);
    pose = mm_movie_pose_journal_find(&live, 4, 8);
    CHECK(pose && pose->has_shape && !pose->offset[0] && pose->shape.scale[0] == 2);
    CHECK(mm_movie_pose_journal_shape(&live, 4, 8, &identity) && live.count == 1);
    CHECK(mm_movie_pose_journal_include(&live, (MMU32)-1, 0));
    CHECK(mm_movie_pose_journal_include(&live, 4, 3) && live.count == 3);
    CHECK(mm_movie_pose_journal_data(&live)[0].actor == 3);
    pose = mm_movie_pose_journal_find(&live, (MMU32)-1, 0);
    CHECK(pose && !pose->has_shape && pose->shape.rotation[4] == 1 && pose->shape.scale[2] == 1);
    CHECK(mm_movie_pose_journal_has_scene(&live, 4) && !mm_movie_pose_journal_has_scene(&live, 5));
    mm_movie_pose_journal_dispose(&live);
    pose = mm_movie_pose_journal_find(&undo, 4, 8);
    CHECK(pose && pose->offset[0] == 2 && pose->shape.scale[0] == 2);
    mm_movie_pose_journal_dispose(&undo);
    CHECK(!memory.liveBlocks);
    return 0;
}

static int text_journal(void)
{
    const MMMovieEditFileServices files = {edit_file_retain, edit_file_release};
    MaterialGraph memory;
    MMStorageAllocator allocator = {NULL, material_resize, material_release};
    MMMovieTextJournal live, undo;
    MMMovieActorTrackJournal tracks;
    MMMoviePoseJournal poses, exported;
    MMMovieActorTextEdit edit = {0};
    MMMovieActorTrackEdit track = {4, 10, 22, NULL};
    MMMovieTextDescription before = {"Text", 4, 19, 0, 1}, after;
    MMMaterialAppearance current = {0};
    const float move[3] = {4, 0, 0};
    float offset[3];
    int old_file = 1, new_file = 1, track_file = 1;
    int changed = MM_MOVIE_TEXT_CHANGED | MM_MOVIE_TEXT_WRITE_MATERIAL;
    memset(&memory, 0, sizeof(memory));
    allocator.context = &memory;
    CHECK(mm_movie_text_journal_init(&live, &allocator, &files) &&
          mm_movie_text_journal_init(&undo, &allocator, &files) &&
          mm_movie_track_journal_init(&tracks, &allocator, &files) &&
          mm_movie_pose_journal_init(&poses, &allocator) && mm_movie_pose_journal_init(&exported, &allocator));

    after = before;
    CHECK(mm_movie_text_font_change(&before, &after, 2) == MM_MOVIE_TEXT_KEEP_FONT);
    CHECK(!mm_movie_text_change(&before, &after, &current, NULL, 0, NULL));
    CHECK(mm_movie_text_change(&before, &after, &current, &current, 1, NULL) == changed);
    after.font = 2;
    CHECK(mm_movie_text_font_change(&before, &after, 2) == MM_MOVIE_TEXT_UNAVAILABLE_FONT);
    after.font_source = 2;
    CHECK(mm_movie_text_font_change(&before, &after, 2) == MM_MOVIE_TEXT_RETAIL_FONT);
    CHECK(mm_movie_text_change(&before, &after, &current, NULL, 0, NULL) == MM_MOVIE_TEXT_CHANGED);
    edit.custom_material = 1;
    CHECK(mm_movie_text_change(&before, &after, &current, NULL, 0, &edit) == changed);

    edit.scene = 4;
    edit.actor = 8;
    edit.source = 21;
    edit.file = &old_file;
    CHECK(mm_text_author_prepare_name("First", &edit.name) == MM_TEXT_NAME_OK);
    CHECK(mm_movie_text_journal_set(&live, &edit) && old_file == 2);
    memory.failAt = memory.allocCalls + 1;
    CHECK(!mm_movie_text_journal_copy(&undo, &live) && !undo.count && old_file == 2);
    memory.failAt = 0;
    CHECK(mm_movie_text_journal_copy(&undo, &live) && old_file == 3);
    edit.file = &new_file;
    CHECK(mm_text_author_prepare_name("Second", &edit.name) == MM_TEXT_NAME_OK);
    CHECK(mm_movie_text_journal_set(&live, &edit) && old_file == 2 && new_file == 2);
    CHECK(!strcmp(mm_movie_text_journal_find(&undo, 4, 8)->name.utf8, "First"));
    CHECK(!strcmp(mm_movie_text_journal_find(&live, 4, 8)->name.legacy, "Second"));
    edit.actor = 3;
    CHECK(mm_movie_text_journal_set(&live, &edit) && new_file == 3);
    CHECK(mm_movie_text_journal_data(&live)[0].actor == 3);

    track.file = &track_file;
    CHECK(mm_movie_track_journal_set(&tracks, &track));
    CHECK(mm_movie_pose_journal_move(&poses, 4, 8, move, offset));
    CHECK(mm_movie_pose_journal_include(&exported, 9, 9));
    memory.failAt = memory.allocCalls + 2;
    CHECK(!mm_movie_pose_journal_export(&exported, &poses, &live, &tracks));
    CHECK(exported.count == 1 && mm_movie_pose_journal_find(&exported, 9, 9));
    memory.failAt = 0;
    CHECK(mm_movie_pose_journal_export(&exported, &poses, &live, &tracks));
    CHECK(poses.count == 1 && exported.count == 3 && mm_movie_pose_journal_data(&exported)[0].actor == 3 &&
          mm_movie_pose_journal_find(&exported, 4, 8)->offset[0] == 4 &&
          mm_movie_pose_journal_find(&exported, 4, 10)->shape.scale[1] == 1);
    mm_movie_pose_journal_dispose(&poses);
    mm_movie_pose_journal_dispose(&exported);
    mm_movie_track_journal_dispose(&tracks);
    mm_movie_text_journal_dispose(&live);
    mm_movie_text_journal_dispose(&undo);
    CHECK(old_file == 1 && new_file == 1 && track_file == 1 && !memory.liveBlocks);
    return 0;
}

static int remove_materials(void)
{
    MaterialGraph graph;
    MMChunkEdit edit = {0};
    MMStorageAllocator allocator = {NULL, material_resize, material_release};
    int i;
    allocator.context = &graph;
    edit.context = &graph;
    edit.walk_allocator = &allocator;
    edit.count = material_count;
    edit.entry = material_entry;
    edit.child = material_child;
    edit.store = material_store;
    material_graph_init(&graph);
    CHECK(mm_movie_archive_preserve_scene_assets(&edit, 1));
    CHECK(graph.count == 3 && graph.kept[0].type == BKGD && graph.kept[0].number == 10 &&
          graph.kept[1].type == MTRL && graph.kept[1].number == 10 && graph.kept[2].number == 13 &&
          !graph.liveBlocks);
    CHECK(graph.rows[4].flags == MM_CHUNK_LONER && graph.rows[5].flags == MM_CHUNK_LONER &&
          graph.rows[8].flags == MM_CHUNK_LONER && !graph.rows[6].flags && !graph.rows[9].flags);
    material_graph_init(&graph);
    CHECK(mm_movie_archive_preserve_scene_assets(&edit, 2) && !graph.count && !graph.liveBlocks);
    material_graph_init(&graph);
    graph.unreadable = 1;
    CHECK(!mm_movie_archive_preserve_scene_assets(&edit, 1) && !graph.count && !graph.liveBlocks);
    for (i = 1; i <= 2; ++i)
    {
        material_graph_init(&graph);
        graph.failAt = i;
        CHECK(!mm_movie_archive_preserve_scene_assets(&edit, 1) &&
              graph.allocCalls >= i && !graph.count && !graph.liveBlocks);
    }
    return 0;
}

int main(void)
{
    if (switching() || insertion() || removal() || remove_materials() || undoing() || undo_insertion_ownership() ||
        cursor_operations() ||
        extending() || journal() || range_journal() || prop_journal() || track_journal() || pose_journal() || text_journal())
        return 1;
    puts("Movie scene ownership, retry, rollback and undo passed");
    return 0;
}
