#ifndef MOVIE_SCENE_ARCHIVE_FIXTURE_H
#define MOVIE_SCENE_ARCHIVE_FIXTURE_H
#include "mm/chunk_edit.h"
#include "mm/movie_data.h"
#include "mm/scene_data.h"
#include <stdlib.h>
#include <string.h>

typedef struct FixtureSceneRow
{
    MMChunkEntry entry;
    MMChunkChild children[8];
    MMU32 links_used;
} FixtureSceneRow;
typedef struct MovieSceneArchiveFixture
{
    FixtureSceneRow rows[32];
    MMU32 count;
    void *observer;
    int (*before_insert)(void *, MMU32 parent, const MMChunkChild *);
    void (*before_unlink)(void *, MMU32 parent, const MMChunkChild *);
    void (*before_move)(void *, const MMChunkChild *old, const MMChunkChild *next);
    void (*before_delete)(void *, const MMChunkEntry *);
} MovieSceneArchiveFixture;

static FixtureSceneRow *fixture_scene_add(MovieSceneArchiveFixture *f, MMU32 type, MMU32 number)
{
    MMU32 i;
    FixtureSceneRow *row;
    for (i = 0; i < f->count; ++i)
    {
        const MMChunkEntry *entry = &f->rows[i].entry;
        if (entry->type > type || (entry->type == type && entry->number > number))
            break;
        if (entry->type == type && entry->number == number)
            abort();
    }
    if (f->count == sizeof(f->rows) / sizeof(f->rows[0]))
        abort();
    row = f->rows + i;
    memmove(row + 1, row, (f->count - i) * sizeof(*row));
    ++f->count;
    memset(row, 0, sizeof(*row));
    row->entry.type = type;
    row->entry.number = number;
    row->entry.flags = MM_CHUNK_LONER;
    return row;
}
static void fixture_scene_seed_movie(MovieSceneArchiveFixture *f, MMU32 root, MMU32 first, MMU32 count)
{
    MMU32 i;
    FixtureSceneRow *movie = fixture_scene_add(f, MM_MOVIE_CHUNK, root);
    if (count > sizeof(movie->children) / sizeof(movie->children[0]))
        abort();
    for (i = 0; i < count; ++i)
    {
        FixtureSceneRow *scene = fixture_scene_add(f, MM_SCENE_CHUNK, first + i);
        movie->children[i].type = MM_SCENE_CHUNK;
        movie->children[i].number = first + i;
        movie->children[i].slot = i;
        scene->entry.parent_count = 1;
        scene->entry.flags &= ~MM_CHUNK_LONER;
    }
    movie->links_used = movie->entry.child_count = count;
}
static MMI32 fixture_scene_slot(const MovieSceneArchiveFixture *f, MMU32 number)
{
    MMU32 row, i;
    const FixtureSceneRow *movie;
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
static int fixture_scene_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    MovieSceneArchiveFixture *f = p;
    if (position >= f->count)
        return 0;
    *entry = f->rows[position].entry;
    return 1;
}
static int fixture_scene_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    MovieSceneArchiveFixture *f = p;
    if (parent >= f->count || position >= f->rows[parent].links_used)
        return 0;
    *child = f->rows[parent].children[position];
    return 1;
}
static MMChunkAccess fixture_scene_chunks(MovieSceneArchiveFixture *f)
{
    MMChunkAccess access = {NULL, 0, fixture_scene_entry, fixture_scene_child};
    access.context = f;
    access.count = f->count;
    return access;
}
static MMU32 fixture_scene_count(void *p)
{
    return ((MovieSceneArchiveFixture *)p)->count;
}
static void fixture_scene_edit_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    if (!fixture_scene_entry(p, position, entry))
        abort();
}
static void fixture_scene_edit_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    if (!fixture_scene_child(p, parent, position, child))
        abort();
}
static void fixture_scene_store(void *p, MMU32 position, const MMChunkEntry *entry, int initialize)
{
    MovieSceneArchiveFixture *f = p;
    if (initialize || position >= f->count)
        abort();
    f->rows[position].entry = *entry;
}
static void fixture_scene_erase(void *p, MMU32 position)
{
    MovieSceneArchiveFixture *f = p;
    FixtureSceneRow *row = f->rows + position;
    if (f->before_delete)
        f->before_delete(f->observer, &row->entry);
    --f->count;
    memmove(row, row + 1, (f->count - position) * sizeof(*row));
}
static int fixture_scene_insert_link(void *p, MMU32 parent, MMU32 position, const MMChunkChild *child)
{
    MovieSceneArchiveFixture *f = p;
    FixtureSceneRow *row = f->rows + parent;
    MMChunkChild *link = row->children + position;
    if (row->links_used == sizeof(row->children) / sizeof(row->children[0]) || position > row->links_used)
        abort();
    if (f->before_insert && !f->before_insert(f->observer, row->entry.number, child))
        return 0;
    memmove(link + 1, link, (row->links_used - position) * sizeof(*link));
    ++row->links_used;
    *link = *child;
    return 1;
}
static void fixture_scene_erase_link(void *p, MMU32 parent, MMU32 position)
{
    MovieSceneArchiveFixture *f = p;
    FixtureSceneRow *row = f->rows + parent;
    MMChunkChild *link = row->children + position;
    if (f->before_unlink)
        f->before_unlink(f->observer, row->entry.number, link);
    --row->links_used;
    memmove(link, link + 1, (row->links_used - position) * sizeof(*link));
}
static void fixture_scene_put_link(void *p, MMU32 parent, MMU32 position, const MMChunkChild *child)
{
    MovieSceneArchiveFixture *f = p;
    MMChunkChild *old = &f->rows[parent].children[position];
    if (f->before_move)
        f->before_move(f->observer, old, child);
    *old = *child;
}
static void fixture_scene_free(void *p, int extra, MMU32 offset, MMU32 bytes)
{
    (void)p;
    (void)extra;
    (void)offset;
    (void)bytes;
}
static void fixture_scene_reset(void *p, MMU32 type, MMU32 number)
{
    (void)p;
    (void)type;
    (void)number;
}
static MMChunkEdit fixture_scene_edit(MovieSceneArchiveFixture *f)
{
    MMChunkEdit edit = {0};
    edit.context = f;
    edit.max_references = 65535;
    edit.count = fixture_scene_count;
    edit.entry = fixture_scene_edit_entry;
    edit.child = fixture_scene_edit_child;
    edit.store = fixture_scene_store;
    edit.erase = fixture_scene_erase;
    edit.free_space = fixture_scene_free;
    edit.reset_runtime = fixture_scene_reset;
    edit.insert_child = fixture_scene_insert_link;
    edit.erase_child = fixture_scene_erase_link;
    edit.put_child = fixture_scene_put_link;
    return edit;
}
#endif
