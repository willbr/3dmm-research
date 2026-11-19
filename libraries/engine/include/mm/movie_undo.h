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

enum { MM_MOVIE_SNAPSHOT_LIMIT = 64 };
typedef struct MMMovieSnapshotStack
{
    void *entries[MM_MOVIE_SNAPSHOT_LIMIT];
    unsigned first, count;
} MMMovieSnapshotStack;
/* Zero-initialize the stack. Put transfers the record and evicts the oldest
 * entry at capacity. Clear releases every retained record. */
void mm_movie_snapshot_put(MMMovieSnapshotStack *, void *record, void (*release)(void *));
void mm_movie_snapshot_clear(MMMovieSnapshotStack *, void (*release)(void *));

typedef struct MMMovieSnapshotView
{
    const void *file, *scenes, *props, *boards, *texts, *costumes, *ranges;
    MMI32 scene;
} MMMovieSnapshotView;
typedef enum MMMovieSnapshotEffect
{
    MM_MOVIE_SNAPSHOT_CANCEL,
    MM_MOVIE_SNAPSHOT_OPEN,
    MM_MOVIE_SNAPSHOT_INSTALL,
    MM_MOVIE_SNAPSHOT_DEFINITIONS,
    MM_MOVIE_SNAPSHOT_RANGES,
    MM_MOVIE_SNAPSHOT_TIMING,
    MM_MOVIE_SNAPSHOT_CLEAR_SCENE,
    MM_MOVIE_SNAPSHOT_SELECT,
    MM_MOVIE_SNAPSHOT_APPLY_VIEW,
    MM_MOVIE_SNAPSHOT_SOUNDS,
    MM_MOVIE_SNAPSHOT_SEEK,
    MM_MOVIE_SNAPSHOT_ABANDON,
    MM_MOVIE_SNAPSHOT_VERIFY,
    MM_MOVIE_SNAPSHOT_SELECT_RESET_FRAMING
} MMMovieSnapshotEffect;
typedef struct MMMovieSnapshotServices
{
    void *context;
    /* A null record requests the current live state. Other views borrow the
     * corresponding snapshot's native resources. */
    void (*describe)(void *, const void *record, MMMovieSnapshotView *);
    int (*same_file)(const void *, const void *);
    int (*matches_scene)(void *, MMI32 scene);
    int (*effect)(void *, MMMovieSnapshotEffect, const void *record, int recovering);
    void (*publish)(void *, const void *record); /* Null after abandoning the view. */
    void (*commit_replace)(void *);
} MMMovieSnapshotServices;
/* Callbacks are synchronous and cannot unwind or change the stack. The prior
 * live snapshot is borrowed for recovery. A failed restore keeps the undo
 * entry and restores the prior state or abandons the unfinished live view. */
int mm_movie_snapshot_undo(MMMovieSnapshotStack *, const void *prior,
                           void (*release)(void *), const MMMovieSnapshotServices *);
/* Owns the prior record. Opens and verifies the replacement before keeping
 * the prior snapshot in history. On failure, recovery runs before releasing
 * the prior snapshot. The replacement record is borrowed. */
int mm_movie_snapshot_replace(MMMovieSnapshotStack *, void *prior, const void *replacement,
                              void (*release)(void *), const MMMovieSnapshotServices *);
/* Publish a prepared scene insertion or background replacement against the
 * current file. Insertion can open the scene at its first frame. Replacement
 * can retain framing and seek the authored frame after selection.
 * Owns the prior record and borrows the replacement. If any operation fails,
 * restore the prior definitions, timing and scene position, or clear the
 * incomplete view. Failure releases the prior record and leaves undo
 * history and the published edit revision unchanged. */
int mm_movie_snapshot_edit_scene(MMMovieSnapshotStack *, void *prior, const void *replacement,
                                 int keep_framing, int seek_frame, void (*release)(void *),
                                 const MMMovieSnapshotServices *);
#ifdef __cplusplus
}
#endif
#endif
