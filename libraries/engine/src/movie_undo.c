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
