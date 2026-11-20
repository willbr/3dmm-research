#ifndef MM_ENGINE_MOVIE_SOUND_ARCHIVE_H
#define MM_ENGINE_MOVIE_SOUND_ARCHIVE_H
#include "movie_archive_edit.h"
#include "movie_session.h"

#ifdef __cplusplus
extern "C" {
#endif

enum MMMovieSoundArchiveResult
{
    MM_SOUND_ARCHIVE_OK,
    MM_SOUND_ARCHIVE_ROOT,
    MM_SOUND_ARCHIVE_ADOPT_SCENE,
    MM_SOUND_ARCHIVE_ADOPT_MOVIE,
    MM_SOUND_ARCHIVE_READ_FRAMES,
    MM_SOUND_ARCHIVE_INVALID_FRAMES,
    MM_SOUND_ARCHIVE_PACKET,
    MM_SOUND_ARCHIVE_NO_CUE,
    MM_SOUND_ARCHIVE_MEMORY,
    MM_SOUND_ARCHIVE_WRITE_FRAMES,
    MM_SOUND_ARCHIVE_LINK_FRAMES,
    MM_SOUND_ARCHIVE_READ_ACTOR,
    MM_SOUND_ARCHIVE_READ_TRACK,
    MM_SOUND_ARCHIVE_WRITE_TRACK,
    MM_SOUND_ARCHIVE_COPY_ACTOR,
    MM_SOUND_ARCHIVE_LINK_ACTOR
};

typedef struct MMMovieSoundArchiveCue
{
    MMI32 frame, source, actor, style, cel;
    MMU32 child, number;
    int queue, actor_sound;
} MMMovieSoundArchiveCue;

typedef struct MMMovieSoundArchiveEdit
{
    const MMMovieSoundArchiveCue *cue;
    const MMU32 *added; /* Optional imported MSND already in the private archive. */
    MMI32 frame, volume, style;
    int loop, queue, remove;
} MMMovieSoundArchiveEdit;

/* Applies one sound edit to a private archive. Imported sounds occupy the
 * first free MSND slots on the scene and movie. Editing a scene packet can
 * move it in time. Actor edits replace only this scene's actor instance. */
int mm_movie_archive_edit_sound(const MMMovieArchiveEditor *, MMU32 scene, const MMMovieSoundArchiveEdit *);

/* Before publishing any scene from a cloned movie, install its missing MSND
 * links using the cloned movie's bank. Explicit scene links take precedence.
 * The graph and editor borrow the same destination. The caller discards the
 * cloned graph on failure. */
int mm_movie_archive_link_imported_sounds(const MMMovieSessionGraph *, MMU32 movie, const MMChunkEdit *);

#ifdef __cplusplus
}
#endif
#endif
