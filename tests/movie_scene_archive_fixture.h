#ifndef MOVIE_SCENE_ARCHIVE_FIXTURE_H
#define MOVIE_SCENE_ARCHIVE_FIXTURE_H
#include "mm/movie_session.h"
#include "mm/scene_data.h"
#include <string.h>

enum { FIXTURE_SCENE_BASE = 100, FIXTURE_SCENE_LIMIT = 32, FIXTURE_MOVIE_ROOT = 7 };
typedef struct MovieSceneArchiveFixture
{
    MMU32 slots[FIXTURE_SCENE_LIMIT];
} MovieSceneArchiveFixture;

static void fixture_scene_archive_init(MovieSceneArchiveFixture *f, int count)
{
    int i;
    memset(f, 0, sizeof(*f));
    for (i = 0; i < count; ++i)
        f->slots[i] = (MMU32)(FIXTURE_SCENE_BASE + i);
}
static MMI32 fixture_scene_slot(const MovieSceneArchiveFixture *f, MMU32 number)
{
    MMI32 i;
    for (i = 0; i < FIXTURE_SCENE_LIMIT; ++i)
        if (f->slots[i] == number)
            return i;
    return -1;
}
static int fixture_scene_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    MovieSceneArchiveFixture *f = p;
    unsigned i;
    if (position > FIXTURE_SCENE_LIMIT)
        return 0;
    memset(entry, 0, sizeof(*entry));
    entry->type = position ? MM_SCENE_CHUNK : MM_MOVIE_CHUNK;
    entry->number = position ? FIXTURE_SCENE_BASE + position - 1 : FIXTURE_MOVIE_ROOT;
    if (position == 0)
        for (i = 0; i < FIXTURE_SCENE_LIMIT; ++i)
            entry->child_count += f->slots[i] != 0;
    return 1;
}
static int fixture_scene_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    MovieSceneArchiveFixture *f = p;
    MMU32 i;
    if (parent != 0)
        return 0;
    for (i = 0; i < FIXTURE_SCENE_LIMIT; ++i)
    {
        if (f->slots[i] == 0)
            continue;
        if (position-- == 0)
        {
            child->type = MM_SCENE_CHUNK;
            child->number = f->slots[i];
            child->slot = i;
            return 1;
        }
    }
    return 0;
}
static MMChunkAccess fixture_scene_chunks(MovieSceneArchiveFixture *f)
{
    MMChunkAccess access = {NULL, FIXTURE_SCENE_LIMIT + 1, fixture_scene_entry, fixture_scene_child};
    access.context = f;
    return access;
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
