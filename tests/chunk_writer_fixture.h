#ifndef CHUNK_WRITER_FIXTURE_H
#define CHUNK_WRITER_FIXTURE_H
#include "mm/chunk_storage.h"
#include "mm/chunk_loading.h"
#include "chunk_edit_fixture.h"
#include "engine_resource_block_fixture.h"

typedef struct ChunkWriterFixture
{
    ChunkIndexFixture index;
    ResourceTestBlock block;
    MMChunkPayloadServices payloads;
    void *context;
    int (*grow_name)(void *);
    unsigned char name[MM_CHUNK_NAME_WIRE_BYTES];
    int file_refs, pins;
} ChunkWriterFixture;
static int writer_fixture_read(void *p, void *file, MMU32 offset, void *bytes, size_t count)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)count;
    return 0;
}
static void writer_fixture_retain(void *p, void *file)
{
    ChunkWriterFixture *f = p;
    assert(file == p && !f->file_refs);
    ++f->file_refs;
}
static void writer_fixture_release(void *p, void *file)
{
    ChunkWriterFixture *f = p;
    assert(file == p && f->file_refs == 1);
    --f->file_refs;
}
static MMU32 writer_name_count(void *p) { return ((ChunkWriterFixture *)p)->index.count; }
static int writer_name_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    ChunkWriterFixture *f = p;
    if (!fixture_chunk_entry(&f->index, position, entry))
        return 0;
    entry->variable_bytes += entry->child_count * 12;
    return 1;
}
static int writer_name_insert(void *p, MMU32 position, MMU32 offset, MMU32 bytes)
{
    ChunkWriterFixture *f = p;
    MMChunkEntry *entry = &f->index.rows[position].entry;
    assert(offset == entry->child_count * 12 && bytes + offset <= sizeof(f->name));
    if (f->grow_name && !f->grow_name(f->context))
        return 0;
    entry->variable_bytes += bytes;
    return 1;
}
static void writer_name_erase(void *p, MMU32 position, MMU32 offset, MMU32 bytes)
{
    ChunkWriterFixture *f = p;
    MMChunkEntry *entry = &f->index.rows[position].entry;
    assert(offset == entry->child_count * 12 && bytes <= entry->variable_bytes);
    entry->variable_bytes -= bytes;
}
static void *writer_fixture_pin(void *p, MMU32 position)
{
    ChunkWriterFixture *f = p;
    assert(position < f->index.count && !f->pins);
    ++f->pins;
    return f->name;
}
static void writer_fixture_unpin(void *p)
{
    ChunkWriterFixture *f = p;
    assert(f->pins == 1);
    --f->pins;
}
static MMArchiveWriter chunk_writer_fixture(ChunkWriterFixture *f, void *context,
                                          int (*allocate)(void *, MMU32, MMChunkLocation *),
                                          int (*write)(void *, void *, MMU32, const void *, size_t))
{
    MMArchiveWriter writer = {0};
    MMChunkNames names = {NULL, writer_name_count, writer_name_entry, writer_name_insert,
                          writer_name_erase, writer_fixture_pin, writer_fixture_unpin, 40};
    memset(f, 0, sizeof(*f));
    f->context = context;
    f->index.observer = context;
    resource_block_init(&f->block, f, writer_fixture_read, write);
    f->block.files.retain = writer_fixture_retain;
    f->block.files.release = writer_fixture_release;
    f->payloads.handles = f->block.owner.handles;
    f->payloads.files = &f->block.files;
    names.context = f;
    writer.edit = fixture_chunk_edit(&f->index);
    writer.edit.allocate = allocate;
    writer.names = names;
    writer.payloads = &f->payloads;
    return writer;
}
#endif
