/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_undo.h"
#include <string.h>
void mm_movie_undo_context_init(MMMovieUndoContext *state)
{
    memset(state, 0, sizeof(*state));
}
int mm_movie_undo_add(void *undo, const MMMovieUndoServices *s)
{
    MMMovieUndoContext *state = s->state(s->context, undo);
    int result;
    state->movie = s->movie;
    state->scene = s->scene_index(s->context);
    if (s->validate) s->validate(s->context, undo);
    if (s->scene_index(s->context) != -1)
        state->frame = s->frame(s->context);
    result = s->commit(s->context, undo);
    s->notify(s->context, result ? MM_MOVIE_UNDO_READY : MM_MOVIE_UNDO_DISABLED);
    return result != 0;
}
void mm_movie_undo_clear(const MMMovieUndoServices *s)
{
    s->clear(s->context);
    s->notify(s->context, MM_MOVIE_UNDO_DISABLED);
}

void mm_movie_snapshot_put(MMMovieSnapshotStack *stack, void *record, void (*release)(void *))
{
    unsigned slot;
    if (stack->count == MM_MOVIE_SNAPSHOT_LIMIT)
    {
        slot = stack->first;
        release(stack->entries[slot]);
        stack->first = (slot + 1) % MM_MOVIE_SNAPSHOT_LIMIT;
    }
    else
    {
        slot = (stack->first + stack->count) % MM_MOVIE_SNAPSHOT_LIMIT;
        ++stack->count;
    }
    stack->entries[slot] = record;
}
void mm_movie_snapshot_clear(MMMovieSnapshotStack *stack, void (*release)(void *))
{
    while (stack->count)
    {
        unsigned slot = (stack->first + --stack->count) % MM_MOVIE_SNAPSHOT_LIMIT;
        release(stack->entries[slot]);
        stack->entries[slot] = NULL;
    }
    stack->first = 0;
}
typedef enum SnapshotRestoreMode
{
    SNAPSHOT_UNDO,
    SNAPSHOT_RECOVER,
    SNAPSHOT_REPLACE
} SnapshotRestoreMode;

static int restore_snapshot(const void *record, SnapshotRestoreMode mode, const MMMovieSnapshotServices *s)
{
    MMMovieSnapshotView live, target;
    void *context = s->context;
    const int recovering = mode == SNAPSHOT_RECOVER;
    const int replacing = mode == SNAPSHOT_REPLACE;
    int base, reload, definitions, timing;
    s->describe(context, NULL, &live);
    s->describe(context, record, &target);
    base = replacing ||
           (live.file != target.file && (!live.file || !target.file || !s->same_file(live.file, target.file)));
    reload = recovering || base || live.boards != target.boards || live.texts != target.texts ||
             live.costumes != target.costumes;
    definitions = reload || live.scenes != target.scenes || live.props != target.props;
    /* Opening a replacement already loads timing for its materialized range. */
    timing = !replacing && (definitions || live.ranges != target.ranges);
    if (base && !s->effect(context, MM_MOVIE_SNAPSHOT_OPEN, record, recovering))
        return 0;
    if (!s->effect(context, MM_MOVIE_SNAPSHOT_INSTALL, record, recovering) ||
        !s->effect(context, definitions ? MM_MOVIE_SNAPSHOT_DEFINITIONS : MM_MOVIE_SNAPSHOT_RANGES, record, recovering))
        return 0;
    if (timing && !s->effect(context, MM_MOVIE_SNAPSHOT_TIMING, record, recovering))
        return 0;
    if (target.scene < 0)
        return !replacing && s->effect(context, MM_MOVIE_SNAPSHOT_CLEAR_SCENE, record, recovering);
    if (reload || !s->matches_scene(context, target.scene))
    {
        if (!s->effect(context, MM_MOVIE_SNAPSHOT_SELECT, record, recovering))
            return 0;
    }
    else if (!s->effect(context, MM_MOVIE_SNAPSHOT_APPLY_VIEW, record, recovering) ||
             (timing && !s->effect(context, MM_MOVIE_SNAPSHOT_SOUNDS, record, recovering)))
        return 0;
    return s->effect(context, MM_MOVIE_SNAPSHOT_SEEK, record, recovering);
}

static int recover_snapshot(const void *prior, const MMMovieSnapshotServices *s)
{
    int recovered = restore_snapshot(prior, SNAPSHOT_RECOVER, s);
    if (!recovered)
        s->effect(s->context, MM_MOVIE_SNAPSHOT_ABANDON, prior, 1);
    return recovered;
}

int mm_movie_snapshot_undo(MMMovieSnapshotStack *stack, const void *prior, void (*release)(void *),
                           const MMMovieSnapshotServices *s)
{
    unsigned slot;
    void *record;
    if (!stack->count || !s->effect(s->context, MM_MOVIE_SNAPSHOT_CANCEL, NULL, 0))
        return 0;
    slot = (stack->first + stack->count - 1) % MM_MOVIE_SNAPSHOT_LIMIT;
    record = stack->entries[slot];
    if (!restore_snapshot(record, SNAPSHOT_UNDO, s))
    {
        s->publish(s->context, recover_snapshot(prior, s) ? prior : NULL);
        return 0;
    }
    s->publish(s->context, record);
    stack->entries[slot] = NULL;
    --stack->count;
    release(record);
    return 1;
}

int mm_movie_snapshot_replace(MMMovieSnapshotStack *stack, void *prior, const void *replacement,
                              void (*release)(void *), const MMMovieSnapshotServices *s)
{
    if (!restore_snapshot(replacement, SNAPSHOT_REPLACE, s) ||
        !s->effect(s->context, MM_MOVIE_SNAPSHOT_VERIFY, replacement, 0))
    {
        s->publish(s->context, recover_snapshot(prior, s) ? prior : NULL);
        release(prior);
        return 0;
    }
    mm_movie_snapshot_put(stack, prior, release);
    s->commit_replace(s->context);
    return 1;
}

int mm_movie_snapshot_edit_scene(MMMovieSnapshotStack *stack, void *prior, const void *replacement,
                                 int keep_framing, int seek_frame, void (*release)(void *),
                                 const MMMovieSnapshotServices *s)
{
    void *context = s->context;
    const MMMovieSnapshotEffect select = keep_framing ? MM_MOVIE_SNAPSHOT_SELECT : MM_MOVIE_SNAPSHOT_SELECT_RESET_FRAMING;
    if (!s->effect(context, MM_MOVIE_SNAPSHOT_INSTALL, replacement, 0) ||
        !s->effect(context, MM_MOVIE_SNAPSHOT_DEFINITIONS, replacement, 0) ||
        !s->effect(context, select, replacement, 0) ||
        (seek_frame && !s->effect(context, MM_MOVIE_SNAPSHOT_SEEK, replacement, 0)))
    {
        recover_snapshot(prior, s);
        release(prior);
        return 0;
    }
    mm_movie_snapshot_put(stack, prior, release);
    s->publish(context, replacement);
    return 1;
}
