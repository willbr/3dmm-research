#ifndef TEMPLATE_WRITER_FIXTURE_H
#define TEMPLATE_WRITER_FIXTURE_H
#include "mm/template_create.h"
#include "chunk_writer_fixture.h"

typedef struct TemplateWriterFixture
{
    ChunkWriterFixture archive;
    unsigned char header[MM_TEMPLATE_HEADER_BYTES], descriptor[24];
    int step, fail, deleted;
    MMU32 last_deleted;
} TemplateWriterFixture;

static int writer_step(TemplateWriterFixture *f) { return ++f->step != f->fail; }
static int writer_extent(void *p, MMU32 bytes, MMChunkLocation *location)
{
    TemplateWriterFixture *f = ((ChunkIndexFixture *)p)->observer;
    assert(bytes == sizeof(f->header) || bytes == sizeof(f->descriptor));
    if (!writer_step(f))
        return 0;
    location->file = &f->archive;
    location->offset = bytes == sizeof(f->header) ? 128 : 152;
    location->bytes = bytes;
    location->flags = 0;
    return 1;
}
static int writer_adopt(void *p, MMU32 parent, const MMChunkChild *child)
{
    assert(parent == 0 && child->slot == 0 && child->number == 0);
    return writer_step(p);
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
static int writer_write(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    ChunkWriterFixture *archive = p;
    TemplateWriterFixture *f = archive->context;
    assert(p == file);
    assert((offset == 128 && count == sizeof(f->header)) || (offset == 152 && count == sizeof(f->descriptor)));
    if (offset == 152)
    {
        MMChunkAccess chunks = fixture_chunk_access(&archive->index);
        MMU32 linked;
        assert(mm_chunk_link_number(&chunks, MM_TMPL, 0, 0, MM_TDT, &linked) ==
               (archive->file_refs ? MM_CHUNK_OK : MM_CHUNK_NOT_FOUND));
    }
    if (!writer_step(f))
        return 0;
    memcpy(offset == 128 ? f->header : f->descriptor, bytes, count);
    return 1;
}
static int writer_name(void *p) { return writer_step(p); }
static MMArchiveWriter writer_fixture(TemplateWriterFixture *f, int big, MMU32 system)
{
    MMArchiveWriter writer;
    memset(f, 0, sizeof(*f));
    writer = chunk_writer_fixture(&f->archive, f, writer_extent, writer_write);
    f->archive.index.before_insert = writer_adopt;
    f->archive.index.before_delete = writer_delete;
    f->archive.grow_name = writer_name;
    writer.big_endian = big;
    writer.system = system;
    return writer;
}
#endif
