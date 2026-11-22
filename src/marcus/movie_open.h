#ifndef MARCUS_MOVIE_OPEN_H
#define MARCUS_MOVIE_OPEN_H

#include "archive_file.h"
#include "live_document.h"
#include "mm/movie_source_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusMovieSource
{
    MMI32 id;
    size_t name_bytes;
    char *name;
} MarcusMovieSource;

typedef struct MarcusMovieOpen
{
    MMCollectionStorage sources;
    MMU32 source_count;
    MMArchiveMovieIds ids;
    MMI32 retail_source;
    int retail_prepared;
} MarcusMovieOpen;

void marcus_movie_open_init(MarcusMovieOpen *);
void marcus_movie_open_dispose(MarcusMovieOpen *);
const MarcusMovieSource *marcus_movie_open_sources(const MarcusMovieOpen *);

/* Read into fresh staging owners. Keep them private until the complete read
 * succeeds. A failure can leave a staged prefix and returns a diagnostic. */
const char *marcus_movie_open_read(MarcusMovieOpen *, MMArchiveReader *, MarcusSceneRows *, MMMovieSourceIds *,
                                   const MMPrivateArchiveProfile *, const struct MMTextHost *, MarcusActorNames *);

#ifdef __cplusplus
}
const char *MarcusReadMovieOpen(MarcusMovieOpen &, MMArchiveReader *, MarcusSceneRows &, MMMovieSourceIds &,
                                MarcusActorNames &);
#endif
#endif
