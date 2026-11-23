#include "mm/native_archive_file.h"
#include "mm/native_runtime.h"
#include <stdlib.h>
#include <string.h>

struct MMNativeArchiveFile
{
    void *handle;
    MMNativeMutex *mutex;
    size_t references;
};

static int seek_extent(MMNativeArchiveFile *file, MMU32 offset, size_t bytes)
{
    return offset <= 0x7fffffffUL && bytes <= 0x7fffffffUL - offset && mm_native_file_seek(file->handle, offset);
}

static int read_at(void *p, MMU32 offset, void *data, size_t bytes)
{
    MMNativeArchiveFile *file = p;
    int ok;
    if (!bytes)
        return 1;
    if (mm_native_mutex_enter(file->mutex))
        return 0;
    ok = seek_extent(file, offset, bytes) && mm_native_file_read(file->handle, data, (MMU32)bytes);
    mm_native_mutex_leave(file->mutex);
    return ok;
}

static int read_block(void *context, void *p, MMU32 offset, void *data, size_t bytes)
{
    (void)context;
    return read_at(p, offset, data, bytes);
}

static int write_at(void *p, MMU32 offset, const void *data, size_t bytes)
{
    MMNativeArchiveFile *file = p;
    int ok;
    if (!bytes)
        return 1;
    if (mm_native_mutex_enter(file->mutex))
        return 0;
    ok = seek_extent(file, offset, bytes) && mm_native_file_write(file->handle, data, (MMU32)bytes) &&
         (offset || mm_native_file_flush(file->handle));
    mm_native_mutex_leave(file->mutex);
    return ok;
}

static int write_block(void *context, void *p, MMU32 offset, const void *data, size_t bytes)
{
    (void)context;
    return write_at(p, offset, data, bytes);
}

static int file_size(MMNativeArchiveFile *file, MMU32 *bytes)
{
    int ok;
    if (mm_native_mutex_enter(file->mutex))
        return 0;
    ok = mm_native_file_size(file->handle, bytes);
    mm_native_mutex_leave(file->mutex);
    return ok;
}

int mm_native_archive_file_stream(MMNativeArchiveFile *file, MMReadStream *out)
{
    MMU32 bytes;
    if (!file || !out || !file_size(file, &bytes))
        return 0;
    out->context = file;
    out->length = bytes;
    out->read_at = read_at;
    return 1;
}

static MMReadStream read_stream(void *p)
{
    MMReadStream stream = {0};
    mm_native_archive_file_stream(p, &stream);
    return stream;
}

static MMU32 size(void *p)
{
    MMU32 bytes;
    return file_size(p, &bytes) ? bytes : 0xffffffffUL;
}

static int resize(void *p, MMU32 bytes)
{
    MMNativeArchiveFile *file = p;
    int ok;
    if (mm_native_mutex_enter(file->mutex))
        return 0;
    ok = mm_native_file_resize(file->handle, bytes);
    mm_native_mutex_leave(file->mutex);
    return ok;
}

static void retain(void *context, void *p)
{
    MMNativeArchiveFile *file = p;
    (void)context;
    mm_native_mutex_enter(file->mutex);
    ++file->references;
    mm_native_mutex_leave(file->mutex);
}

void mm_native_archive_file_release(MMNativeArchiveFile *file)
{
    int close;
    if (!file)
        return;
    mm_native_mutex_enter(file->mutex);
    close = --file->references == 0;
    mm_native_mutex_leave(file->mutex);
    if (close)
    {
        mm_native_file_close(file->handle);
        mm_native_mutex_destroy(file->mutex);
        free(file);
    }
}

static void release(void *p)
{
    mm_native_archive_file_release(p);
}

static void release_block(void *context, void *p)
{
    (void)context;
    mm_native_archive_file_release(p);
}

static void *create_extra(void *context)
{
    (void)context;
    return NULL;
}

const MMBlockFileServices *mm_native_archive_file_blocks(void)
{
    static const MMBlockFileServices blocks = {NULL, retain, release_block, create_extra, read_block, write_block};
    return &blocks;
}

static MMNativeArchiveFile *open_file(const MMNativePathChar *path, int mode)
{
    MMNativeOpenOptions options;
    MMNativeArchiveFile *file;
    if (!path)
        return NULL;
    file = malloc(sizeof(*file));
    if (!file)
        return NULL;
    file->mutex = mm_native_mutex_create(NULL);
    if (file->mutex)
    {
        mm_native_open_options_init(&options);
        options.write = mode != MM_PRIVATE_ARCHIVE_READ;
        if (mode == MM_PRIVATE_ARCHIVE_CREATE)
            options.creation = MM_NATIVE_CREATE_ALWAYS;
        file->handle = mm_native_file_open(path, &options);
        if (file->handle)
        {
            file->references = 1;
            return file;
        }
        mm_native_mutex_destroy(file->mutex);
    }
    free(file);
    return NULL;
}

MMNativeArchiveFile *mm_native_archive_file_open(const MMNativePathChar *path)
{
    return open_file(path, MM_PRIVATE_ARCHIVE_READ);
}

void mm_native_archive_source_clear(MMNativeArchiveSource *source)
{
    if (!source)
        return;
    mm_chunk_index_close(source->index);
    mm_native_archive_file_release(source->file);
    memset(source, 0, sizeof(*source));
}

int mm_native_archive_source_open(MMNativeArchiveSource *source, const MMNativePathChar *path,
                                  int expanded, MMU32 max_name_bytes)
{
    const unsigned short order = 1;
    MMChunkPolicy policy = {0};
    MMNativeArchiveSource opened = {0};
    int status = MM_ARCHIVE_INVALID_PAYLOAD;
    if (!source)
        return status;
    policy.expanded_runtime = expanded;
    policy.native_big_endian = *(const unsigned char *)&order == 0;
    policy.max_name_bytes = max_name_bytes;
    opened.file = mm_native_archive_file_open(path);
    if (mm_native_archive_file_stream(opened.file, &opened.stream))
        status = mm_archive_read_index(&opened.stream, &policy, &opened.index);
    if (status == MM_ARCHIVE_READ_OK)
    {
        mm_native_archive_source_clear(source);
        *source = opened;
    }
    else
        mm_native_archive_source_clear(&opened);
    return status;
}

int mm_native_archive_source_reader(const MMNativeArchiveSource *source, const MMArchiveReaderOptions *options,
                                    MMArchiveReader **out)
{
    return source ? mm_archive_reader_open_wire(source->index, &source->stream, options, out) :
                    MM_ARCHIVE_INVALID_PAYLOAD;
}

MMResourceCache *mm_native_archive_source_cache(const MMNativeArchiveSource *source, MMI32 budget,
                                                 const MMResourceServices *profile,
                                                 const MMStorageAllocator *owner, const MMStorageAllocator *storage)
{
    MMResourceSource wire;
    MMResourceServices services;
    if (!source || !source->index || !source->file || !profile)
        return NULL;
    wire.live = NULL;
    wire.index = source->index;
    wire.backing = source->file;
    services = *profile;
    services.block_files = mm_native_archive_file_blocks();
    return mm_resource_cache_create(&wire, budget, &services, owner, storage);
}

void mm_native_archive_input_close(MMNativeArchiveInput *input)
{
    mm_archive_reader_close(input->reader);
    if (input->live)
        mm_resource_cache_release(input->live);
    mm_native_archive_source_clear(&input->stored);
    memset(input, 0, sizeof(*input));
}

int mm_native_archive_input_open(MMNativeArchiveInput *input, int mode, const MMArchiveReaderOptions *options,
                                 const MMNativeArchiveInputHost *host)
{
    MMNativeArchiveInput next = {0};
    int selected = 0, status = MM_ARCHIVE_INVALID_PAYLOAD;
    if (mode != MM_NATIVE_ARCHIVE_STORED && host->open_live)
        next.live = host->open_live(host->context, mode == MM_NATIVE_ARCHIVE_LIVE, &selected, &next.live_reader);
    if (selected)
    {
        if (!next.live)
            return MM_NATIVE_ARCHIVE_INPUT_OPEN;
        status = mm_archive_reader_open_owner(next.live_reader, options, &next.reader);
    }
    else
    {
        if (next.live)
            mm_resource_cache_release(next.live);
        next.live = NULL;
        next.live_reader = NULL;
        status = host->open_stored(host->context, &next.stored);
        if (status == MM_ARCHIVE_READ_OK)
            status = mm_native_archive_source_reader(&next.stored, options, &next.reader);
    }
    if (status == MM_ARCHIVE_READ_OK)
    {
        next.mode = mode;
        mm_native_archive_input_close(input);
        *input = next;
        return MM_NATIVE_ARCHIVE_INPUT_OK;
    }
    mm_native_archive_input_close(&next);
    if (status == MM_ARCHIVE_MEMORY)
        return MM_NATIVE_ARCHIVE_INPUT_MEMORY;
    return selected ? MM_NATIVE_ARCHIVE_INPUT_LIVE_INDEX : MM_NATIVE_ARCHIVE_INPUT_OPEN;
}

MMMovieSessionGraph mm_native_archive_input_graph(const MMNativeArchiveInput *input)
{
    return input->live ? mm_movie_owner_graph(input->live_reader) : mm_archive_reader_graph(input->reader);
}

MMPrivateArchive *mm_native_private_archive_open(const MMNativePathChar *path, int mode,
                                                const MMPrivateArchiveProfile *profile)
{
    static const MMChunkOwnerFileIO owner = {NULL, create_extra, read_stream, size, resize, NULL,
                                             write_at, release, NULL, NULL, NULL, NULL, NULL};
    MMNativeArchiveFile *file;
    if (mode != MM_PRIVATE_ARCHIVE_CREATE && mode != MM_PRIVATE_ARCHIVE_EDIT && mode != MM_PRIVATE_ARCHIVE_READ)
        return NULL;
    file = open_file(path, mode);
    return mm_private_archive_open(file, mode, &owner, mm_native_archive_file_blocks(), profile);
}
