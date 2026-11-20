#ifndef MOVIE_SCENE_SESSION_FIXTURE_H
#define MOVIE_SCENE_SESSION_FIXTURE_H
#include "movie_scene_archive_fixture.h"
#include "mm/movie_session.h"

enum { FIXTURE_SCENE_BASE = 100, FIXTURE_MOVIE_ROOT = 7 };

static void fixture_scene_archive_init(MovieSceneArchiveFixture *f, int count)
{
    memset(f, 0, sizeof(*f));
    fixture_scene_seed_movie(f, FIXTURE_MOVIE_ROOT, FIXTURE_SCENE_BASE, (MMU32)count);
}
static void fixture_scene_access(void *p, MMChunkAccess *access)
{
    *access = fixture_scene_chunks(p);
}
static int fixture_scene_root(void *p, void *request, MMU32 position, const MMChunkEntry *entry,
                              void *bytes, MMU32 size)
{
    const unsigned char header[] = {1, 0, 3, 3, 2, 0, 2, 0};
    (void)p;
    (void)request;
    if (position != 0 || entry->type != MM_MOVIE_CHUNK || size != sizeof(header))
        return MM_MOVIE_BAD_FILE;
    memcpy(bytes, header, sizeof(header));
    return MM_MOVIE_OK;
}
static int fixture_scene_open(MovieSceneArchiveFixture *f, MMU32 *number, MMMovieSession **out)
{
    MMMovieSessionGraph graph = {NULL, fixture_scene_access, fixture_scene_root, NULL, NULL};
    graph.context = f;
    return mm_movie_session_open_graph(&graph, NULL, number, out) == MM_MOVIE_OK;
}
#endif
