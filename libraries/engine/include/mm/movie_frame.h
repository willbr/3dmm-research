#ifndef MM_ENGINE_MOVIE_FRAME_H
#define MM_ENGINE_MOVIE_FRAME_H
#include "read_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MMMovieSession;

enum
{
    MM_MOVIE_FRAME_UNPUBLISHED,
    MM_MOVIE_FRAME_PARTIAL,
    MM_MOVIE_FRAME_PREPARED,
    MM_MOVIE_FRAME_COMMITTED
};

typedef struct MMMovieLiveFrame
{
    MMI32 frame;
    int phase;
} MMMovieLiveFrame;

typedef struct MMMovieFrameServices
{
    void *context;
    int (*begin)(void *, MMI32 frame);
    MMU32 (*actor_count)(void *);
    int (*seek_actor)(void *, MMU32 ordinal, MMI32 frame);
    int (*finish)(void *, MMI32 frame);
    MMI32 *position;
    void (*published)(void *);
} MMMovieFrameServices;

/* Begin once, seek actors in scene order, then finish. Effects return zero
 * or a caller-defined error. The first error stops the transaction and leaves
 * completed effects in place. Actor count is live. After success, commit the
 * optional position before notifying observers. Finish and published are optional.
 * Callbacks run synchronously and must return through the driver. */
int mm_movie_seek_frame(MMI32 frame, const MMMovieFrameServices *);

/* A live scene owns its frame record and initializes it to zero. During finish
 * the session exposes the prepared pose for synchronous prerendering. A failed
 * effect leaves a partial pose; rejection before any change to the initialized
 * cursor keeps the previous frame. Callbacks may seek a different scene.
 * The movie and scene both outlive the call. Pass NULL for a scene without
 * a movie graph. */
int mm_movie_session_seek_live(struct MMMovieSession *, MMMovieLiveFrame *, MMI32 frame,
                                const MMMovieFrameServices *);

#ifdef __cplusplus
}
#endif
#endif
