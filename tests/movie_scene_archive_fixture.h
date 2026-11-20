#ifndef MOVIE_SCENE_ARCHIVE_FIXTURE_H
#define MOVIE_SCENE_ARCHIVE_FIXTURE_H
#include "chunk_edit_fixture.h"
#include "mm/movie_data.h"
#include "mm/scene_data.h"

static void fixture_scene_seed_movie(ChunkIndexFixture *f, MMU32 root, MMU32 first, MMU32 count)
{
    MMU32 i;
    FixtureChunkRow *movie = fixture_chunk_add(f, MM_MOVIE_CHUNK, root);
    if (count > sizeof(movie->children) / sizeof(movie->children[0]))
        abort();
    for (i = 0; i < count; ++i)
    {
        FixtureChunkRow *scene = fixture_chunk_add(f, MM_SCENE_CHUNK, first + i);
        movie->children[i].type = MM_SCENE_CHUNK;
        movie->children[i].number = first + i;
        movie->children[i].slot = i;
        scene->entry.parent_count = 1;
        scene->entry.flags &= ~MM_CHUNK_LONER;
    }
    movie->links_used = movie->entry.child_count = count;
}
static MMI32 fixture_scene_slot(const ChunkIndexFixture *f, MMU32 number)
{
    MMU32 row, i;
    const FixtureChunkRow *movie;
    for (row = 0; row < f->count; ++row)
        if (f->rows[row].entry.type == MM_MOVIE_CHUNK)
            break;
    if (row == f->count)
        return -1;
    movie = &f->rows[row];
    for (i = 0; i < movie->links_used; ++i)
        if (movie->children[i].number == number)
            return (MMI32)movie->children[i].slot;
    return -1;
}
#endif
