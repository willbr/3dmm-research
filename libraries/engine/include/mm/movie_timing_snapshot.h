#ifndef MM_ENGINE_MOVIE_TIMING_SNAPSHOT_H
#define MM_ENGINE_MOVIE_TIMING_SNAPSHOT_H
#include "types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieTimingRange
{
    MMU32 chunk;
    MMI32 first, last;
} MMMovieTimingRange;

typedef struct MMMovieTimingScroll
{
    MMI32 frame;
    double seconds;
} MMMovieTimingScroll;

typedef struct MMMovieTimingPause
{
    MMI32 frame, kind, milliseconds;
} MMMovieTimingPause;

enum
{
    MM_TIMING_ACTOR_SOUND = 1, MM_TIMING_LOOP = 2, MM_TIMING_QUEUE = 4,
    MM_TIMING_SILENT = 8, MM_TIMING_MOTION = 16
};

typedef struct MMMovieTimingCue
{
    MMI32 frame, source, actor, style, volume;
    MMU32 sound_type, sound_number, scene_type, scene_number, child, flags;
    MMI32 motion_cel, audio_kind;
    MMU32 resolved_type, resolved_chunk;
    MMI32 resolved_source;
    double seconds;
} MMMovieTimingCue;

typedef struct MMMovieTimingScene
{
    MMMovieTimingRange range;
    double transition;
    size_t scroll_count, pause_count, cue_count;
    const MMMovieTimingScroll *scrolls;
    const MMMovieTimingPause *pauses;
    const MMMovieTimingCue *cues;
} MMMovieTimingScene;

typedef struct MMMovieTimingWriter
{
    void *context;
    int (*write)(void *, const void *, size_t);
    /* Borrow scene arrays until the next scene callback. */
    int (*scene)(void *, size_t, MMMovieTimingScene *);
} MMMovieTimingWriter;

typedef struct MMMovieTimingReader
{
    void *context;
    int (*read)(void *, void *, size_t);
    int (*at_end)(void *);
    int (*expected)(void *, size_t, MMMovieTimingRange *);
    /* Copy or consume the scene before returning. The codec owns its arrays. */
    int (*scene)(void *, size_t, const MMMovieTimingScene *);
    /* Optional initialization of the staged destination after header validation. */
    int (*begin)(void *, double frame_seconds);
} MMMovieTimingReader;

/* Matching-build helper IPC with little-endian words and binary64 seconds.
 * Callbacks return 1 on success and 0 on failure. Stage received scenes until
 * read succeeds. frame_seconds is published only after EOF is verified. */
int mm_movie_timing_snapshot_read(const MMMovieTimingReader *, size_t expected_count, double *frame_seconds);
int mm_movie_timing_snapshot_write(const MMMovieTimingWriter *, size_t count, double frame_seconds);

#ifdef __cplusplus
}
#endif
#endif
