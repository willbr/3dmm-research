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
    puts("C89 movie undo publication, sentinel, separate operation index and destruction checks passed");
    return 0;
}
