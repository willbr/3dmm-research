#ifndef MM_ENGINE_MOVIE_SESSION_H
#define MM_ENGINE_MOVIE_SESSION_H

#include "chunky_file.h"
#include "movie_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieSession MMMovieSession;

typedef struct MMMovieSessionRoot
{
    MMU32 number;
    MMMovieHeader header;
} MMMovieSessionRoot;

typedef struct MMMovieSessionScene
{
    MMU32 number;
    MMI32 first_frame, last_frame, transition;
} MMMovieSessionScene;

enum { MM_MOVIE_SCENE_NOT_FOUND = MM_MOVIE_MEMORY + 1 };

/* Select the first MVIE by chunk number and validate its decoded header.
 * The session owns the index and copies the stream descriptor. Its underlying
 * storage/context must stay valid and unmodified until close. The session
 * performs synchronous reads and has the same concurrency limits as the stream.
 * Open and scene lookup publish outputs only on MM_MOVIE_OK. */
int mm_movie_session_open(const MMReadStream *, const MMChunkPolicy *, MMMovieSession **);
void mm_movie_session_close(MMMovieSession *);
const MMMovieSessionRoot *mm_movie_session_root(const MMMovieSession *);

/* Look up an exact SCEN child at the authored slot. A missing slot returns
 * MM_MOVIE_SCENE_NOT_FOUND. A link to an absent or invalid chunk is an error. */
int mm_movie_session_scene(const MMMovieSession *, MMU32 slot, MMMovieSessionScene *);

#ifdef __cplusplus
}
#endif
#endif
