#include "mm/movie_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { printf("movie undo check %d: %s\n", __LINE__, #e); exit(1); } } while (0)
typedef struct Record { MMMovieUndoContext base; int operationIndex; } Record;
typedef struct Host
{
    Record local, *record;
    int movie, scenes[2], sceneCalls, frameCalls, validateCalls, commitResult, destroy, notifications, mode;
    int events[12], count;
} Host;
static void event(Host *h, int value) { h->events[h->count++] = value; }
static MMMovieUndoContext *view(void *context, void *record)
{
    Host *h = context; CHECK(record == h->record); event(h, 1); return &h->record->base;
}
static MMI32 scene(void *context)
{
    Host *h = context;
    CHECK(h->record->base.movie == &h->movie);
    event(h, 2); return h->scenes[h->sceneCalls++];
}
static void validate(void *context, void *record)
{
    Host *h = context;
    CHECK(record == h->record && h->record->base.scene == h->scenes[0]);
    CHECK(h->record->base.frame == 77); ++h->validateCalls; event(h, 3);
}
static MMI32 frame(void *context)
{
    Host *h = context; CHECK(h->sceneCalls == 2 && h->validateCalls == 1);
    ++h->frameCalls; event(h, 4); return 9;
}
static int commit(void *context, void *record)
{
    Host *h = context; CHECK(record == h->record && h->record->operationIndex == 23);
    event(h, 5);
    if (h->destroy) { free(h->record); h->record = NULL; }
    return h->commitResult;
}
static void notify(void *context, int mode)
{
    Host *h = context; ++h->notifications; h->mode = mode; event(h, 6);
}
static void clear(void *context)
{
    Host *h = context; event(h, 7);
    if (h->destroy) { free(h->record); h->record = NULL; }
}
static MMMovieUndoServices setup(Host *h)
{
    MMMovieUndoServices s;
    memset(h, 0, sizeof(*h)); memset(&s, 0, sizeof(s));
    h->record = &h->local; h->local.operationIndex = 23;
    mm_movie_undo_context_init(&h->local.base); h->local.base.frame = 77;
    h->scenes[0] = h->scenes[1] = 2; h->commitResult = 1;
    s.context = h; s.movie = &h->movie; s.state = view; s.validate = validate;
    s.scene_index = scene; s.frame = frame; s.commit = commit; s.clear = clear; s.notify = notify;
    return s;
}
typedef struct SnapshotFixture
{
    MMMovieSnapshotView live;
    int fail, fail_recovery, count, committed;
    char trace[24];
    const void *published;
} SnapshotFixture;
static int snapshots_released;
static const void *last_released;
static void release_snapshot(void *record)
{
    last_released = record;
    ++snapshots_released;
}
static void describe_snapshot(void *p, const void *record, MMMovieSnapshotView *out)
{
    *out = record ? *(const MMMovieSnapshotView *)record : ((SnapshotFixture *)p)->live;
}
static int same_snapshot_file(const void *a, const void *b)
{
    return *(const int *)a == *(const int *)b;
}
static int matches_snapshot_scene(void *p, MMI32 scene)
{
    return ((SnapshotFixture *)p)->live.scene == scene;
}
static void snapshot_step(SnapshotFixture *s, char step)
{
    CHECK(s->count < (int)sizeof(s->trace) - 1);
    s->trace[s->count++] = step;
    s->trace[s->count] = 0;
}
static int snapshot_effect(void *p, MMMovieSnapshotEffect effect, const void *record, int recovering)
{
    static const char steps[] = "COIDRTNLVSKXYF";
    SnapshotFixture *s = p;
    const MMMovieSnapshotView *state = record;
    snapshot_step(s, steps[effect]);
    if ((recovering ? s->fail_recovery : s->fail) == (int)effect)
        return 0;
    switch (effect)
    {
    case MM_MOVIE_SNAPSHOT_OPEN: s->live.file = state->file; break;
    case MM_MOVIE_SNAPSHOT_INSTALL:
    {
        MMI32 scene = s->live.scene;
        s->live = *state;
        s->live.scene = scene;
        break;
    }
    case MM_MOVIE_SNAPSHOT_SELECT:
    case MM_MOVIE_SNAPSHOT_SELECT_RESET_FRAMING: s->live.scene = state->scene; break;
    case MM_MOVIE_SNAPSHOT_CLEAR_SCENE:
    case MM_MOVIE_SNAPSHOT_ABANDON: s->live.scene = -1; break;
    default: break;
    }
    return 1;
}
static void publish_snapshot(void *p, const void *record)
{
    SnapshotFixture *s = p;
    snapshot_step(s, '!');
    s->published = record;
}
static void commit_replacement(void *p)
{
    SnapshotFixture *s = p;
    snapshot_step(s, '+');
    ++s->committed;
}
static void test_snapshots(void)
{
    MMMovieSnapshotStack stack = {{NULL}, 0, 0};
    MMMovieSnapshotView prior, records[68];
    SnapshotFixture f;
    MMMovieSnapshotServices s = {NULL, describe_snapshot, same_snapshot_file, matches_snapshot_scene,
                                 snapshot_effect, publish_snapshot, commit_replacement};
    int i, file = 1, same = 1, other = 2;
    memset(&f, 0, sizeof(f));
    memset(records, 0, sizeof(records));
    prior = f.live;
    f.fail = f.fail_recovery = -1;
    s.context = &f;
    CHECK(!mm_movie_snapshot_undo(&stack, &prior, release_snapshot, &s) && !f.count);
    for (i = 0; i < 68; ++i)
        mm_movie_snapshot_put(&stack, &records[i], release_snapshot);
    CHECK(stack.count == 64 && snapshots_released == 4 && last_released == &records[3]);

    f.live.scene = 2;
    f.live.file = &file;
    prior = f.live;
    records[67] = prior;
    records[67].file = &same;
    records[67].ranges = &records[67];
    CHECK(mm_movie_snapshot_undo(&stack, &prior, release_snapshot, &s));
    CHECK(!strcmp(f.trace, "CIRTVSK!") && f.published == &records[67]);
    CHECK(stack.count == 63 && snapshots_released == 5 && last_released == &records[67]);

    f.count = 0;
    prior = f.live;
    records[66] = prior;
    records[66].file = &other;
    records[66].scene = 3;
    f.fail = MM_MOVIE_SNAPSHOT_SEEK;
    CHECK(!mm_movie_snapshot_undo(&stack, &prior, release_snapshot, &s));
    CHECK(!strcmp(f.trace, "COIDTLKOIDTLK!") && f.published == &prior);
    CHECK(f.live.file == prior.file && f.live.scene == prior.scene && stack.count == 63 && snapshots_released == 5);

    f.count = 0;
    f.fail_recovery = MM_MOVIE_SNAPSHOT_SELECT;
    CHECK(!mm_movie_snapshot_undo(&stack, &prior, release_snapshot, &s));
    CHECK(!strcmp(f.trace, "COIDTLKOIDTLX!") && f.live.scene == -1 && stack.count == 63 && !f.published);
    f.count = 0;
    f.fail = f.fail_recovery = -1;
    records[66].scene = -1;
    CHECK(mm_movie_snapshot_undo(&stack, &prior, release_snapshot, &s) && !strcmp(f.trace, "COIDTN!"));
    CHECK(f.live.scene == -1 && stack.count == 62 && snapshots_released == 6);
    mm_movie_snapshot_clear(&stack, release_snapshot);
    CHECK(stack.first == 0 && stack.count == 0 && snapshots_released == 68 && last_released == &records[4]);
}
static void test_snapshot_replacement(void)
{
    MMMovieSnapshotStack stack = {{NULL}, 0, 0};
    MMMovieSnapshotView prior, older, replacement;
    SnapshotFixture f;
    MMMovieSnapshotServices s = {NULL, describe_snapshot, same_snapshot_file, matches_snapshot_scene,
                                 snapshot_effect, publish_snapshot, commit_replacement};
    int file = 1, other = 2;
    memset(&f, 0, sizeof(f));
    f.live.file = &file;
    f.live.scene = 2;
    prior = older = replacement = f.live;
    s.context = &f;
    snapshots_released = 0;
    mm_movie_snapshot_put(&stack, &older, release_snapshot);

    replacement.file = &other;
    f.fail = MM_MOVIE_SNAPSHOT_VERIFY;
    f.fail_recovery = -1;
    CHECK(!mm_movie_snapshot_replace(&stack, &prior, &replacement, release_snapshot, &s));
    CHECK(!strcmp(f.trace, "OIDLKYOIDTLK!") && f.published == &prior && !f.committed);
    CHECK(f.live.file == &file && f.live.scene == 2 && stack.count == 1 && stack.entries[0] == &older);
    CHECK(snapshots_released == 1 && last_released == &prior);

    f.count = 0;
    f.fail = MM_MOVIE_SNAPSHOT_OPEN;
    CHECK(!mm_movie_snapshot_replace(&stack, &prior, &replacement, release_snapshot, &s));
    CHECK(!strcmp(f.trace, "OIDTLK!") && f.published == &prior && !f.committed && stack.count == 1);
    CHECK(snapshots_released == 2 && last_released == &prior);

    f.count = 0;
    f.fail = -1;
    replacement.file = &file;
    CHECK(mm_movie_snapshot_replace(&stack, &prior, &replacement, release_snapshot, &s));
    CHECK(!strcmp(f.trace, "OIDLKY+") && f.committed == 1);
    CHECK(stack.count == 2 && stack.entries[1] == &prior && snapshots_released == 2);
    mm_movie_snapshot_clear(&stack, release_snapshot);
    CHECK(snapshots_released == 4 && last_released == &older);
}
static void test_snapshot_scene_edit(void)
{
    static const struct
    {
        int fail, fail_recovery, previous, keep_framing, seek_frame;
        const char *trace;
    } cases[] = {
        {-1, -1, -1, 0, 0, "IDF!"},
        {-1, -1, 2, 1, 1, "IDLK!"},
        {MM_MOVIE_SNAPSHOT_DEFINITIONS, -1, 2, 0, 0, "IDIDTLK"},
        {MM_MOVIE_SNAPSHOT_SELECT_RESET_FRAMING, -1, -1, 0, 0, "IDFIDTN"},
        {MM_MOVIE_SNAPSHOT_SEEK, -1, 2, 1, 1, "IDLKIDTLK"},
        {MM_MOVIE_SNAPSHOT_SEEK, MM_MOVIE_SNAPSHOT_SELECT, 2, 1, 1, "IDLKIDTLX"}
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        MMMovieSnapshotStack stack = {{NULL}, 0, 0};
        MMMovieSnapshotView prior, older, next;
        SnapshotFixture f;
        MMMovieSnapshotServices s = {NULL, describe_snapshot, same_snapshot_file, matches_snapshot_scene,
                                     snapshot_effect, publish_snapshot, commit_replacement};
        memset(&f, 0, sizeof(f));
        s.context = &f;
        f.live.scene = cases[i].previous;
        f.live.scenes = &older;
        prior = older = next = f.live;
        next.scene = 4;
        next.scenes = &next;
        f.fail = cases[i].fail;
        f.fail_recovery = cases[i].fail_recovery;
        snapshots_released = 0;
        mm_movie_snapshot_put(&stack, &older, release_snapshot);
        CHECK(mm_movie_snapshot_edit_scene(&stack, &prior, &next, cases[i].keep_framing, cases[i].seek_frame,
                                           release_snapshot, &s) == (f.fail < 0));
        CHECK(!strcmp(f.trace, cases[i].trace) && stack.entries[0] == &older && !f.committed);
        if (f.fail < 0)
        {
            CHECK(stack.count == 2 && stack.entries[1] == &prior && f.published == &next && !snapshots_released);
            CHECK(f.live.scene == next.scene && f.live.scenes == next.scenes);
        }
        else
        {
            CHECK(stack.count == 1 && !f.published && snapshots_released == 1 && last_released == &prior);
            CHECK(f.live.scene == (f.fail_recovery < 0 ? prior.scene : -1) && f.live.scenes == prior.scenes);
        }
        mm_movie_snapshot_clear(&stack, release_snapshot);
        CHECK(snapshots_released == 2);
    }
}
int main(void)
{
    Host h;
    MMMovieUndoServices s;
    int first, second, success;
    for (first = 0; first < 2; ++first) for (second = 0; second < 2; ++second) for (success = 0; success < 2; ++success)
    {
        s = setup(&h); h.scenes[0] = first ? 2 : -1; h.scenes[1] = second ? 2 : -1; h.commitResult = success;
        CHECK(mm_movie_undo_add(h.record, &s) == success);
        CHECK(h.local.base.movie == &h.movie && h.local.base.scene == (first ? 2 : -1));
        CHECK(h.local.base.frame == (second ? 9 : 77) && h.frameCalls == second && h.sceneCalls == 2);
        CHECK(h.notifications == 1 && h.mode == (success ? MM_MOVIE_UNDO_READY : MM_MOVIE_UNDO_DISABLED));
        CHECK(h.events[0] == 1 && h.events[1] == 2 && h.events[2] == 3 && h.events[3] == 2);
        CHECK(h.events[h.count - 2] == 5 && h.events[h.count - 1] == 6);
        mm_movie_undo_clear(&s); CHECK(h.notifications == 2 && h.mode == MM_MOVIE_UNDO_DISABLED);
        CHECK(h.events[h.count - 2] == 7 && h.events[h.count - 1] == 6);
    }
    for (success = 0; success < 2; ++success)
    {
        s = setup(&h); h.record = malloc(sizeof(*h.record)); CHECK(h.record != NULL);
        *h.record = h.local; h.destroy = 1; h.commitResult = success;
        CHECK(mm_movie_undo_add(h.record, &s) == success && !h.record && h.notifications == 1);
    }
    s = setup(&h); h.record = malloc(sizeof(*h.record)); CHECK(h.record != NULL); h.destroy = 1;
    mm_movie_undo_clear(&s); CHECK(!h.record && h.notifications == 1);
    s = setup(&h); memset(&h.local.base, 0xa5, sizeof(h.local.base)); mm_movie_undo_context_init(&h.local.base);
    CHECK(!h.local.base.movie && !h.local.base.scene && !h.local.base.frame);
    test_snapshots();
    test_snapshot_replacement();
    test_snapshot_scene_edit();
    puts("C89 movie undo publication, sentinel, separate operation index and destruction checks passed");
    return 0;
}
