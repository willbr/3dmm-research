#ifndef TEMPLATE_WRITER_FIXTURE_H
#define TEMPLATE_WRITER_FIXTURE_H
#include "mm/template_create.h"
#include "chunk_edit_fixture.h"
#include "engine_resource_block_fixture.h"
#include <assert.h>

typedef struct TemplateWriterFixture
{
    ChunkIndexFixture index;
    ResourceTestBlock block;
    MMChunkPayloadServices payloads;
    unsigned char header[MM_TEMPLATE_HEADER_BYTES], descriptor[24], name[16];
    int step, fail, file_refs, pins, deleted;
    MMU32 last_deleted;
} TemplateWriterFixture;

static int writer_fail(TemplateWriterFixture *f) { return ++f->step == f->fail; }
static int writer_extent(void *p, MMU32 bytes, MMChunkLocation *location)
{
    TemplateWriterFixture *f = ((ChunkIndexFixture *)p)->observer;
    assert(bytes == sizeof(f->header) || bytes == sizeof(f->descriptor));
    if (writer_fail(f))
        return 0;
    location->file = f;
    location->offset = bytes == sizeof(f->header) ? 128 : 152;
    location->bytes = bytes;
    location->flags = 0;
    return 1;
}
static int writer_adopt(void *p, MMU32 parent, const MMChunkChild *child)
{
    assert(parent == 0 && child->slot == 0 && child->number == 0);
    return !writer_fail(p);
}
static void writer_delete(void *p, const MMChunkEntry *entry)
{
    TemplateWriterFixture *f = p;
    if (entry->type)
    {
        ++f->deleted;
        f->last_deleted = entry->type;
    }
}
static void writer_retain(void *p, void *file)
{
    TemplateWriterFixture *f = p;
    assert(p == file && !f->file_refs);
    ++f->file_refs;
}
static void writer_release(void *p, void *file)
{
    TemplateWriterFixture *f = p;
    assert(p == file && f->file_refs == 1);
    --f->file_refs;
}
static int writer_write(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    TemplateWriterFixture *f = p;
    assert(p == file);
    assert((offset == 128 && count == sizeof(f->header)) || (offset == 152 && count == sizeof(f->descriptor)));
    if (offset == 152)
    {
        MMChunkAccess chunks = fixture_chunk_access(&f->index);
        MMU32 linked;
        assert(mm_chunk_link_number(&chunks, MM_TMPL, 0, 0, MM_TDT, &linked) ==
               (f->file_refs ? MM_CHUNK_OK : MM_CHUNK_NOT_FOUND));
    }
    if (writer_fail(f))
        return 0;
    memcpy(offset == 128 ? f->header : f->descriptor, bytes, count);
    return 1;
}
static int writer_read(void *p, void *file, MMU32 offset, void *bytes, size_t count)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)count;
    return 0;
}
static int writer_name_insert(void *p, MMU32 position, MMU32 offset, MMU32 bytes)
{
    ChunkIndexFixture *index = p;
    TemplateWriterFixture *f = index->observer;
    MMChunkEntry *entry = &index->rows[position].entry;
    assert(entry->type == MM_TMPL && !offset && bytes <= sizeof(f->name));
    if (writer_fail(f))
        return 0;
    entry->variable_bytes += bytes;
    return 1;
}
static void writer_name_erase(void *p, MMU32 position, MMU32 offset, MMU32 bytes)
{
    ChunkIndexFixture *index = p;
    MMChunkEntry *entry = &index->rows[position].entry;
    assert(!offset && bytes <= entry->variable_bytes);
    entry->variable_bytes -= bytes;
}
static void *writer_pin(void *p, MMU32 position)
{
    ChunkIndexFixture *index = p;
    TemplateWriterFixture *f = index->observer;
    assert(index->rows[position].entry.type == MM_TMPL);
    ++f->pins;
    return f->name;
}
static void writer_unpin(void *p)
{
    TemplateWriterFixture *f = ((ChunkIndexFixture *)p)->observer;
    assert(f->pins == 1);
    --f->pins;
}
static MMTemplateWriter writer_fixture(TemplateWriterFixture *f, int big, MMU32 system)
{
    MMTemplateWriter writer = {0};
    MMChunkNames names = {NULL, fixture_chunk_count, fixture_chunk_entry, writer_name_insert,
                          writer_name_erase, writer_pin, writer_unpin, 16};
    memset(f, 0, sizeof(*f));
    f->index.observer = f;
    f->index.before_insert = writer_adopt;
    f->index.before_delete = writer_delete;
    resource_block_init(&f->block, f, writer_read, writer_write);
    f->block.files.retain = writer_retain;
    f->block.files.release = writer_release;
    f->payloads.handles = f->block.owner.handles;
    f->payloads.files = &f->block.files;
    names.context = &f->index;
    writer.edit = fixture_chunk_edit(&f->index);
    writer.edit.allocate = writer_extent;
    writer.names = names;
    writer.payloads = &f->payloads;
    writer.big_endian = big;
    writer.system = system;
    return writer;
}
#endif
