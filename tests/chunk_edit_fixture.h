#ifndef CHUNK_EDIT_FIXTURE_H
#define CHUNK_EDIT_FIXTURE_H
#include "mm/chunk_edit.h"
#include <stdlib.h>
#include <string.h>

typedef struct FixtureChunkRow
{
    MMChunkEntry entry;
    MMChunkChild children[12];
    MMU32 links_used;
    MMI32 runtime;
} FixtureChunkRow;
typedef struct ChunkIndexFixture
{
    FixtureChunkRow rows[32];
    MMU32 count;
    void *observer;
    int (*before_insert)(void *, MMU32 parent, const MMChunkChild *);
    void (*before_unlink)(void *, MMU32 parent, const MMChunkChild *);
    void (*before_move)(void *, const MMChunkChild *old, const MMChunkChild *next);
    void (*before_delete)(void *, const MMChunkEntry *);
} ChunkIndexFixture;

static FixtureChunkRow *fixture_chunk_add(ChunkIndexFixture *f, MMU32 type, MMU32 number)
{
    MMU32 i;
    FixtureChunkRow *row;
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
static int fixture_chunk_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    ChunkIndexFixture *f = p;
    if (position >= f->count)
        return 0;
    *entry = f->rows[position].entry;
    return 1;
}
static int fixture_chunk_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    ChunkIndexFixture *f = p;
    if (parent >= f->count || position >= f->rows[parent].links_used)
        return 0;
    *child = f->rows[parent].children[position];
    return 1;
}
static MMChunkAccess fixture_chunk_access(ChunkIndexFixture *f)
{
    MMChunkAccess access = {NULL, 0, fixture_chunk_entry, fixture_chunk_child};
    access.context = f;
    access.count = f->count;
    return access;
}
static MMU32 fixture_chunk_count(void *p)
{
    return ((ChunkIndexFixture *)p)->count;
}
static int fixture_chunk_insert(void *p, MMU32 position)
{
    ChunkIndexFixture *f = p;
    if (position > f->count || f->count == sizeof(f->rows) / sizeof(f->rows[0]))
        return 0;
    memmove(f->rows + position + 1, f->rows + position, (f->count - position) * sizeof(*f->rows));
    memset(f->rows + position, 0, sizeof(*f->rows));
    ++f->count;
    return 1;
}
static void fixture_chunk_store(void *p, MMU32 position, const MMChunkEntry *entry, int initialize)
{
    ChunkIndexFixture *f = p;
    if (position >= f->count)
        abort();
    if (initialize)
        memset(f->rows + position, 0, sizeof(*f->rows));
    f->rows[position].entry = *entry;
}
static void fixture_chunk_erase(void *p, MMU32 position)
{
    ChunkIndexFixture *f = p;
    FixtureChunkRow *row = f->rows + position;
    if (f->before_delete)
        f->before_delete(f->observer, &row->entry);
    --f->count;
    memmove(row, row + 1, (f->count - position) * sizeof(*row));
}
static int fixture_chunk_insert_link(void *p, MMU32 parent, MMU32 position, const MMChunkChild *child)
{
    ChunkIndexFixture *f = p;
    FixtureChunkRow *row = f->rows + parent;
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
static void fixture_chunk_erase_link(void *p, MMU32 parent, MMU32 position)
{
    ChunkIndexFixture *f = p;
    FixtureChunkRow *row = f->rows + parent;
    MMChunkChild *link = row->children + position;
    if (f->before_unlink)
        f->before_unlink(f->observer, row->entry.number, link);
    --row->links_used;
    memmove(link, link + 1, (row->links_used - position) * sizeof(*link));
}
static void fixture_chunk_put_link(void *p, MMU32 parent, MMU32 position, const MMChunkChild *child)
{
    ChunkIndexFixture *f = p;
    MMChunkChild *old = &f->rows[parent].children[position];
    if (f->before_move)
        f->before_move(f->observer, old, child);
    *old = *child;
}
static void fixture_chunk_free(void *p, int extra, MMU32 offset, MMU32 bytes)
{
    (void)p;
    (void)extra;
    (void)offset;
    (void)bytes;
}
static MMI32 fixture_chunk_runtime(void *p, MMU32 type, MMU32 number)
{
    ChunkIndexFixture *f = p;
    const MMChunkAccess access = fixture_chunk_access(f);
    MMU32 position;
    return mm_chunk_find(&access, type, number, &position) == MM_CHUNK_OK ? f->rows[position].runtime : 0;
}
static int fixture_chunk_set_runtime(void *p, MMU32 type, MMU32 number, MMI32 runtime)
{
    ChunkIndexFixture *f = p;
    const MMChunkAccess access = fixture_chunk_access(f);
    MMU32 position;
    if (mm_chunk_find(&access, type, number, &position) != MM_CHUNK_OK)
        return 0;
    f->rows[position].runtime = runtime;
    return 1;
}
static void fixture_chunk_reset(void *p, MMU32 type, MMU32 number)
{
    fixture_chunk_set_runtime(p, type, number, 0);
}
static void fixture_chunk_move_entry(void *p, MMU32 source, MMU32 target)
{
    ChunkIndexFixture *f = p;
    FixtureChunkRow saved;
    if (source >= f->count || target > f->count)
        abort();
    saved = f->rows[source];
    if (source < target)
    {
        --target;
        memmove(f->rows + source, f->rows + source + 1, (target - source) * sizeof(saved));
    }
    else
        memmove(f->rows + target + 1, f->rows + target, (source - target) * sizeof(saved));
    f->rows[target] = saved;
}
static MMChunkEdit fixture_chunk_edit(ChunkIndexFixture *f)
{
    MMChunkEdit edit = {0};
    edit.context = f;
    edit.max_references = 65535;
    edit.count = fixture_chunk_count;
    edit.entry = fixture_chunk_entry;
    edit.child = fixture_chunk_child;
    edit.insert = fixture_chunk_insert;
    edit.store = fixture_chunk_store;
    edit.erase = fixture_chunk_erase;
    edit.free_space = fixture_chunk_free;
    edit.reset_runtime = fixture_chunk_reset;
    edit.insert_child = fixture_chunk_insert_link;
    edit.erase_child = fixture_chunk_erase_link;
    edit.put_child = fixture_chunk_put_link;
    edit.move_entry = fixture_chunk_move_entry;
    edit.runtime = fixture_chunk_runtime;
    edit.set_runtime = fixture_chunk_set_runtime;
    return edit;
}
#endif
