/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_UNDO_H
#define MM_ENGINE_MOVIE_UNDO_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMMovieUndoContext
{
    void *movie; /* borrowed; undo does not retain the owning movie */
    MMI32 scene, frame;
} MMMovieUndoContext;
enum { MM_MOVIE_UNDO_DISABLED = 1, MM_MOVIE_UNDO_READY = 2 };
typedef struct MMMovieUndoServices
{
    void *context, *movie;
    MMMovieUndoContext *(*state)(void *, void *undo);
    void (*validate)(void *, void *undo); /* optional, after movie/scene publication */
    MMI32 (*scene_index)(void *), (*frame)(void *);
    int (*commit)(void *, void *undo);
    void (*clear)(void *), (*notify)(void *, int mode);
} MMMovieUndoServices;
void mm_movie_undo_context_init(MMMovieUndoContext *);
/* State access is a nonthrowing borrowed view. Primitive callbacks do not unwind
 * across C. Publish movie, read/publish scene, validate, then query scene again
 * before optionally reading/publishing frame. No current scene preserves the
 * existing frame. Commit/clear can destroy the record: no later record access.
 * Commit false disables undo; success announces ready. Clear disables afterward. */
int mm_movie_undo_add(void *undo, const MMMovieUndoServices *);
void mm_movie_undo_clear(const MMMovieUndoServices *);
#ifdef __cplusplus
}
#endif
#endif
