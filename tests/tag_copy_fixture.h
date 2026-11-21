#ifndef TAG_COPY_FIXTURE_H
#define TAG_COPY_FIXTURE_H
#include "mm/chunk_copy.h"
#include "chunk_edit_fixture.h"
#include "engine_resource_block_fixture.h"

typedef struct TagCopyFile
{
    ChunkIndexFixture index;
    ResourceTestBlock block;
    MMChunkPayloadServices payloads;
} TagCopyFile;

/* Empty test chunks exercise the actual graph and number allocation. */
static int tag_copy_read(void *p, void *file, MMU32 offset, void *bytes, size_t count)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)count;
    return 0;
}
static int tag_copy_write(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)count;
    return 0;
}
static int tag_copy_allocate(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    extent->file = ((ChunkIndexFixture *)p)->observer;
    extent->offset = 0;
    extent->bytes = bytes;
    extent->flags = 0;
    return bytes == 0;
}
static int tag_copy_locate(void *p, MMU32 type, MMU32 number, MMBlockFile *extent, int *packed)
{
    TagCopyFile *file = p;
    const MMChunkAccess chunks = fixture_chunk_access(&file->index);
    MMU32 position;
    if (mm_chunk_find(&chunks, type, number, &position) != MM_CHUNK_OK)
        return 0;
    extent->file = file;
    extent->offset = 0;
    extent->length = 0;
    *packed = 0;
    return 1;
}
static int tag_copy_read_name(void *p, MMU32 type, MMU32 number, void *bytes, MMU32 capacity, MMU32 *written)
{
    (void)p; (void)type; (void)number; (void)bytes; (void)capacity; (void)written;
    return 0;
}
static int tag_copy_write_name(void *p, MMU32 type, MMU32 number, const void *bytes, MMU32 size)
{
    (void)p; (void)type; (void)number; (void)bytes; (void)size;
    return 0;
}
static void tag_copy_file_init(TagCopyFile *file)
{
    memset(file, 0, sizeof(*file));
    file->index.observer = file;
    resource_block_init(&file->block, file, tag_copy_read, tag_copy_write);
    file->payloads.handles = file->block.owner.handles;
    file->payloads.files = &file->block.files;
    file->payloads.locate = tag_copy_locate;
}
static MMChunkEndpoint tag_copy_file_endpoint(TagCopyFile *file, MMChunkFileCopy *binding)
{
    static const MMChunkTitleIO titles = {NULL, 0, 0, NULL, tag_copy_read_name, tag_copy_write_name, NULL};
    MMArchiveWriter archive = {0};
    archive.edit = fixture_chunk_edit(&file->index);
    archive.edit.allocate = tag_copy_allocate;
    archive.payloads = &file->payloads;
    return mm_chunk_file_copy_endpoint(binding, file, &archive, &titles, 1, 0);
}
#endif
