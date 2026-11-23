#include "mm/native_archive_file.h"
#include "mm/native_runtime.h"
#include "mm/native_text.h"
#include <stdlib.h>
#include <string.h>

struct MMNativeArchiveFile
{
    void *handle;
    MMNativeMutex *mutex;
    size_t references;
};

int mm_native_archive_legacy_path(const char *path, size_t length, int ansi, const MMTextHost *text, char *out)
{
    unsigned short units[MM_STRING_MAX], round_trip[MM_STRING_MAX];
    size_t count, bytes;
    int status;
    if (!length || memchr(path, 0, length))
        return MM_ARCHIVE_PATH_EMPTY;
    if (!ansi)
    {
        if (length > MM_STRING_MAX)
            return MM_ARCHIVE_PATH_TOO_LONG;
        memcpy(out, path, length);
        out[length] = 0;
        return MM_ARCHIVE_PATH_OK;
    }

    status = mm_text_read_utf8(path, length, units, MM_STRING_MAX, &count);
    if (status != 1)
        return status < 0 ? MM_ARCHIVE_PATH_TOO_LONG : MM_ARCHIVE_PATH_BAD_UTF8;
    bytes = mm_text_ansi_from_units(text, units, count, NULL, 0, 0);
    if (bytes > MM_STRING_MAX)
        return MM_ARCHIVE_PATH_TOO_LONG;
    if (!bytes || mm_text_ansi_from_units(text, units, count, out, bytes, 0) != bytes)
        return MM_ARCHIVE_PATH_ENCODE;
    if (mm_text_utf16(text, out, bytes, MM_TEXT_WINDOWS, round_trip, MM_STRING_MAX) != count ||
        memcmp(units, round_trip, count * sizeof(*units)))
        return MM_ARCHIVE_PATH_LOSSY;
    out[bytes] = 0;
    return MM_ARCHIVE_PATH_OK;
}

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

static int copy_extent(void *context, void *source, MMU32 from, void *destination, MMU32 to, MMU32 bytes)
{
    MMBlockIO input = {0}, output = {0};
    (void)context;
    input.view.kind = output.view.kind = MM_BLOCK_FILE;
    input.view.start = from;
    output.view.start = to;
    input.view.length = output.view.length = bytes;
    input.context = source;
    output.context = destination;
    input.read_file = read_at;
    output.write_file = write_at;
    return mm_block_copy(&input, &output, 0, NULL) == MM_DATA_OK;
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

static MMNativeArchiveFile *allocate_file(void)
{
    MMNativeArchiveFile *file = malloc(sizeof(*file));
    if (!file)
        return NULL;
    file->mutex = mm_native_mutex_create(NULL);
    if (!file->mutex)
    {
        free(file);
        return NULL;
    }
    file->handle = NULL;
    file->references = 1;
    return file;
}

static void *create_temporary(void *context)
{
    MMNativeArchiveFile *file = allocate_file();
    (void)context;
    if (!file)
        return NULL;
    file->handle = mm_native_file_temporary();
    if (file->handle)
        return file;
    mm_native_archive_file_release(file);
    return NULL;
}

const MMBlockFileServices *mm_native_archive_file_blocks(void)
{
    static const MMBlockFileServices blocks = {NULL, retain, release_block, create_temporary, read_block, write_block};
    return &blocks;
}

static const MMChunkOwnerFileIO private_files = {NULL, create_temporary, read_stream, size, resize, copy_extent,
                                                 write_at, release, NULL, NULL, NULL, NULL, NULL};

static MMNativeArchiveFile *open_file(const void *path, int utf8, int mode)
{
    MMNativeOpenOptions options;
    MMNativeArchiveFile *file;
    if (!path)
        return NULL;
    file = allocate_file();
    if (!file)
        return NULL;
    mm_native_open_options_init(&options);
    options.write = mode != MM_PRIVATE_ARCHIVE_READ;
    options.deny_read = utf8 && options.write;
    if (mode == MM_PRIVATE_ARCHIVE_CREATE)
        options.creation = MM_NATIVE_CREATE_ALWAYS;
    file->handle = utf8 ? mm_native_file_open_utf8(path, &options) : mm_native_file_open(path, &options);
    if (file->handle)
        return file;
    mm_native_archive_file_release(file);
    return NULL;
}

MMNativeArchiveFile *mm_native_archive_file_open(const MMNativePathChar *path)
{
    return open_file(path, 0, MM_PRIVATE_ARCHIVE_READ);
}

void mm_native_archive_source_clear(MMNativeArchiveSource *source)
{
    if (!source)
        return;
    mm_chunk_index_close(source->index);
    mm_native_archive_file_release(source->file);
    memset(source, 0, sizeof(*source));
}

static int open_source(MMNativeArchiveSource *source, MMNativeArchiveFile *file, int expanded, MMU32 max_name_bytes)
{
    const unsigned short order = 1;
    MMChunkPolicy policy = {0};
    MMNativeArchiveSource opened = {0};
    int status = MM_ARCHIVE_INVALID_PAYLOAD;
    policy.expanded_runtime = expanded;
    policy.native_big_endian = *(const unsigned char *)&order == 0;
    policy.max_name_bytes = max_name_bytes;
    opened.file = file;
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

int mm_native_archive_source_open(MMNativeArchiveSource *source, const MMNativePathChar *path,
                                  int expanded, MMU32 max_name_bytes)
{
    return source ? open_source(source, mm_native_archive_file_open(path), expanded, max_name_bytes) :
                    MM_ARCHIVE_INVALID_PAYLOAD;
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
    if (!services.handles)
        services.handles = mm_chunk_handle_services();
    services.block_files = mm_native_archive_file_blocks();
    return mm_resource_cache_create(&wire, budget, &services, owner, storage);
}

static MMResourceCache *open_live_cache(const MMNativeArchiveInputHost *host, int prefer_working,
                                        int *selected, struct MMChunkOwnerRead **reader)
{
    MMResourceCache *cache = NULL;
    if (host->open_live)
        cache = host->open_live(host->context, prefer_working, selected, reader);
    if (*selected)
        return cache;
    if (cache)
        mm_resource_cache_release(cache);
    *reader = NULL;
    return NULL;
}

MMResourceCache *mm_native_archive_open_cache(const MMNativeArchiveInputHost *host, int media, int removable,
                                              MMI32 budget, const MMResourceServices *profile,
                                              const MMStorageAllocator *owner, const MMStorageAllocator *storage)
{
    MMNativeArchiveSource source = {0};
    struct MMChunkOwnerRead *reader = NULL;
    int selected = 0;
    MMResourceCache *cache = open_live_cache(host, media || removable, &selected, &reader);
    if (selected)
        return cache;
    if (host->open_stored(host->context, &source) == MM_ARCHIVE_READ_OK)
        cache = mm_native_archive_source_cache(&source, budget, profile, owner, storage);
    mm_native_archive_source_clear(&source);
    return cache;
}

void mm_native_archive_input_close(MMNativeArchiveInput *input)
{
    mm_archive_reader_close(input->reader);
    if (input->live)
        mm_resource_cache_release(input->live);
    mm_native_archive_source_clear(&input->stored);
    memset(input, 0, sizeof(*input));
}

static void publish_input(MMNativeArchiveInput *input, MMNativeArchiveInput *next, int mode)
{
    next->mode = mode;
    mm_native_archive_input_close(input);
    *input = *next;
}

int mm_native_archive_input_open(MMNativeArchiveInput *input, int mode, const MMArchiveReaderOptions *options,
                                 const MMNativeArchiveInputHost *host)
{
    MMNativeArchiveInput next = {0};
    int selected = 0, status = MM_ARCHIVE_INVALID_PAYLOAD;
    if (mode != MM_NATIVE_ARCHIVE_STORED)
        next.live = open_live_cache(host, mode == MM_NATIVE_ARCHIVE_LIVE, &selected, &next.live_reader);
    if (selected)
    {
        if (!next.live)
            return MM_NATIVE_ARCHIVE_INPUT_OPEN;
        status = mm_archive_reader_open_owner(next.live_reader, options, &next.reader);
    }
    else
    {
        status = host->open_stored(host->context, &next.stored);
        if (status == MM_ARCHIVE_READ_OK)
            status = mm_native_archive_source_reader(&next.stored, options, &next.reader);
    }
    if (status == MM_ARCHIVE_READ_OK)
    {
        publish_input(input, &next, mode);
        return MM_NATIVE_ARCHIVE_INPUT_OK;
    }
    mm_native_archive_input_close(&next);
    if (status == MM_ARCHIVE_MEMORY)
        return MM_NATIVE_ARCHIVE_INPUT_MEMORY;
    return selected ? MM_NATIVE_ARCHIVE_INPUT_LIVE_INDEX : MM_NATIVE_ARCHIVE_INPUT_OPEN;
}

int mm_native_archive_input_open_utf8(MMNativeArchiveInput *input, const char *path, int expanded, MMU32 max_name_bytes,
                                      const MMArchiveReaderOptions *options)
{
    MMNativeArchiveInput next = {0};
    int status;
    if (!input || !options)
        return MM_ARCHIVE_INVALID_PAYLOAD;
    status = open_source(&next.stored, open_file(path, 1, MM_PRIVATE_ARCHIVE_READ), expanded, max_name_bytes);
    if (status == MM_ARCHIVE_READ_OK)
        status = mm_native_archive_source_reader(&next.stored, options, &next.reader);
    if (status == MM_ARCHIVE_READ_OK)
        publish_input(input, &next, MM_NATIVE_ARCHIVE_STORED);
    else
        mm_native_archive_input_close(&next);
    return status;
}

MMMovieSessionGraph mm_native_archive_input_graph(const MMNativeArchiveInput *input)
{
    return input->live ? mm_movie_owner_graph(input->live_reader) : mm_archive_reader_graph(input->reader);
}

static size_t convert_ansi(void *context, const void *input, size_t bytes, int encoding, void *out, size_t capacity)
{
    (void)context;
    return mm_text_ansi(mm_native_text_host(), input, bytes, encoding, capacity ? out : NULL, capacity);
}

static size_t convert_utf16(void *context, const void *input, size_t bytes, int encoding, void *out, size_t capacity)
{
    (void)context;
    return mm_text_utf16(mm_native_text_host(), input, bytes, encoding, capacity ? out : NULL, capacity);
}

static int name_mac(void *context, const char *name, void *out, MMU32 capacity, MMU32 *written)
{
    (void)context;
    return mm_string_wire_encode_ansi(NULL, MM_TEXT_MAC, name, out, capacity, written);
}

static int name_windows(void *context, const char *name, void *out, MMU32 capacity, MMU32 *written)
{
    (void)context;
    return mm_string_wire_encode_ansi(NULL, MM_TEXT_WINDOWS, name, out, capacity, written);
}

static int name_utf16_be(void *context, const char *name, void *out, MMU32 capacity, MMU32 *written)
{
    (void)context;
    return mm_string_wire_encode_ansi(mm_native_text_host(), MM_TEXT_UTF16_BE, name, out, capacity, written);
}

static int name_utf16_le(void *context, const char *name, void *out, MMU32 capacity, MMU32 *written)
{
    (void)context;
    return mm_string_wire_encode_ansi(mm_native_text_host(), MM_TEXT_UTF16_LE, name, out, capacity, written);
}

MMPrivateArchiveProfile mm_native_archive_profile(int encoding, int swapped_unicode, int expanded, int trash_unused)
{
    static const struct
    {
        int encoding;
        unsigned unit;
        MMChunkLegacyName name;
        MMStringConvert convert;
    } codecs[] = {
        {MM_TEXT_MAC, 1, name_mac, convert_ansi},
        {MM_TEXT_WINDOWS, 1, name_windows, convert_ansi},
        {MM_TEXT_UTF16_BE, 2, name_utf16_be, convert_utf16},
        {MM_TEXT_UTF16_LE, 2, name_utf16_le, convert_utf16}
    };
    MMPrivateArchiveProfile profile = {0};
    size_t i;
    for (i = 0; i < sizeof(codecs) / sizeof(*codecs); ++i)
    {
        if (codecs[i].encoding == encoding)
        {
            profile.unit = codecs[i].unit;
            profile.encoding = encoding;
            profile.swapped_unicode_encoding = swapped_unicode;
            profile.expanded = expanded;
            profile.trash_unused = trash_unused;
            profile.legacy_name = codecs[i].name;
            profile.convert = codecs[i].convert;
            break;
        }
    }
    return profile;
}

static MMPrivateArchive *open_private(const void *path, int utf8, int mode, const MMPrivateArchiveProfile *profile)
{
    MMNativeArchiveFile *file;
    if (mode != MM_PRIVATE_ARCHIVE_CREATE && mode != MM_PRIVATE_ARCHIVE_EDIT && mode != MM_PRIVATE_ARCHIVE_READ)
        return NULL;
    file = open_file(path, utf8, mode);
    return mm_private_archive_open(file, mode, &private_files, mm_native_archive_file_blocks(), profile);
}

MMPrivateArchive *mm_native_private_archive_open(const MMNativePathChar *path, int mode,
                                                const MMPrivateArchiveProfile *profile)
{
    return open_private(path, 0, mode, profile);
}

MMPrivateArchive *mm_native_private_archive_open_utf8(const char *path, int mode, const MMPrivateArchiveProfile *profile)
{
    return open_private(path, 1, mode, profile);
}

MMPrivateArchive *mm_native_private_archive_temporary(const MMPrivateArchiveProfile *profile)
{
    return mm_private_archive_open(create_temporary(NULL), MM_PRIVATE_ARCHIVE_CREATE,
                                   &private_files, mm_native_archive_file_blocks(), profile);
}
