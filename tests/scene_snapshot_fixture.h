#ifndef SCENE_SNAPSHOT_FIXTURE_H
#define SCENE_SNAPSHOT_FIXTURE_H
#include "mm/private_archive.h"
#include "mm/scene_data.h"
#include <assert.h>
#include <string.h>

typedef struct SnapshotFile
{
    int refs, fail_resize;
    MMU32 size;
    unsigned char bytes[256];
} SnapshotFile;

static void snapshot_file_release(void *p)
{
    SnapshotFile *file = p;
    assert(file->refs > 0);
    --file->refs;
}
static void snapshot_block_release(void *p, void *file)
{
    (void)p;
    snapshot_file_release(file);
}
static void snapshot_block_retain(void *p, void *file)
{
    SnapshotFile *storage = file;
    (void)p;
    assert(storage->refs > 0);
    ++storage->refs;
}
static int snapshot_resize(void *p, MMU32 bytes)
{
    SnapshotFile *file = p;
    if (file->fail_resize || bytes > sizeof(file->bytes))
        return 0;
    file->size = bytes;
    return 1;
}
static MMU32 snapshot_size(void *p) { return ((SnapshotFile *)p)->size; }
static int snapshot_read(void *p, MMU32 at, void *bytes, size_t count)
{
    SnapshotFile *file = p;
    if (at > file->size || count > file->size - at)
        return 0;
    memcpy(bytes, file->bytes + at, count);
    return 1;
}
static int snapshot_write(void *p, MMU32 at, const void *bytes, size_t count)
{
    SnapshotFile *file = p;
    if (at > sizeof(file->bytes) || count > sizeof(file->bytes) - at)
        return 0;
    memcpy(file->bytes + at, bytes, count);
    if (at + count > file->size)
        file->size = (MMU32)(at + count);
    return 1;
}
static MMReadStream snapshot_stream(void *p)
{
    MMReadStream s;
    s.context = p;
    s.length = snapshot_size(p);
    s.read_at = snapshot_read;
    return s;
}
static void *snapshot_extra(void *p) { (void)p; return NULL; }
static int snapshot_block_read(void *p, void *file, MMU32 at, void *bytes, size_t count)
{
    (void)p;
    return snapshot_read(file, at, bytes, count);
}
static int snapshot_block_write(void *p, void *file, MMU32 at, const void *bytes, size_t count)
{
    (void)p;
    return snapshot_write(file, at, bytes, count);
}
static size_t snapshot_convert(void *p, const void *bytes, size_t count, int encoding, void *out, size_t capacity)
{
    (void)p;
    (void)bytes;
    (void)count;
    (void)encoding;
    (void)out;
    (void)capacity;
    return 0;
}
static MMPrivateArchive *snapshot_file_open(SnapshotFile *file)
{
    static const MMBlockFileServices blocks = {NULL, snapshot_block_retain, snapshot_block_release,
                                               snapshot_extra, snapshot_block_read, snapshot_block_write};
    const MMChunkOwnerFileIO io = {NULL, snapshot_extra, snapshot_stream, snapshot_size, snapshot_resize,
                                   NULL, snapshot_write, snapshot_file_release, NULL, NULL, NULL, NULL};
    const MMPrivateArchiveProfile profile = {1, 0, 0, 0, 0, NULL, snapshot_convert};
    assert(!file->refs);
    file->refs = 1;
    return mm_private_archive_open(file, MM_PRIVATE_ARCHIVE_CREATE, &io, &blocks, &profile);
}
static void put_snapshot(MMPrivateArchive *archive, MMU32 number)
{
    const MMArchiveWriter writer = mm_private_archive_writer(archive);
    const unsigned char marker = 0x1a;
    assert(mm_chunk_put_bytes(&writer.edit, writer.payloads->files, MM_SCENE_CHUNK, number,
                             &marker, 1) == MM_CHUNK_OK);
}
static int has_snapshot(MMPrivateArchive *archive, MMU32 number)
{
    const MMChunkAccess access = mm_chunk_owner_access(mm_private_archive_reader(archive)->storage);
    MMU32 row;
    return mm_chunk_find(&access, MM_SCENE_CHUNK, number, &row) == MM_CHUNK_OK;
}
#endif
