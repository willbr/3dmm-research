#include "mm/private_archive.h"
#include "mm/chunk_table.h"
#include "mm/chunk_copy.h"
#include "mm/handle_memory.h"
#include "mm/reference.h"
#include "mm/text_resource.h"
#include <stdlib.h>

struct MMPrivateArchive
{
    MMChunkArchive *backing;
    MMReference references;
    MMPrivateArchiveProfile profile;
    MMChunkTitleIO titles;
    MMMovieArchiveFile editor;
    int writable, editor_ready;
    MMChunkLink link;
};

typedef union HandlePrefix
{
    MMHandleState state;
    void *pointer;
    double scalar;
} HandlePrefix;

static const MMHandleLayout handle_layout = {sizeof(HandlePrefix), MM_STORAGE_LIMIT, 0, 0, 0};

static int allocate_handle(void *context, MMU32 bytes, void **out)
{
    (void)context;
    return mm_handle_allocate(&handle_layout, NULL, out, bytes, 0, 0);
}

static void release_handle(void *context, void *handle)
{
    (void)context;
    mm_handle_free(&handle_layout, NULL, &handle);
}

static MMU32 handle_size(void *context, void *handle)
{
    (void)context;
    return mm_handle_size(&handle_layout, NULL, handle);
}

static void *lock_handle(void *context, void *handle)
{
    (void)context;
    return mm_handle_lock(&handle_layout, NULL, handle);
}

static void unlock_handle(void *context, void *handle)
{
    (void)context;
    mm_handle_unlock(&handle_layout, NULL, handle);
}

static int resize_handle(void *context, void **handle, MMU32 bytes)
{
    (void)context;
    return mm_handle_resize(&handle_layout, NULL, handle, bytes, 0, 0);
}

static const MMHandleServices handles = {NULL, allocate_handle, release_handle, handle_size,
                                         lock_handle, unlock_handle, resize_handle};

static int decode_bytes(void *context, const void *input, MMU32 bytes, void *output,
                        MMU32 capacity, MMI32 *written)
{
    (void)context;
    return mm_chunk_packed_decode(input, bytes, output, capacity, written) == MM_DATA_OK;
}

static int decode_handle(void *context, void **handle)
{
    (void)context;
    return mm_handle_code(&handles, handle, NULL, decode_bytes, NULL) == MM_DATA_OK;
}

MMChunkOwnerRead *mm_private_archive_reader(MMPrivateArchive *archive)
{
    return mm_chunk_archive_reader(archive->backing);
}

static MMU32 max_title_bytes(const MMPrivateArchive *archive)
{
    return 2 + (MM_STRING_MAX + 2) * archive->profile.unit;
}

static MMU32 max_references(const MMPrivateArchive *archive)
{
    return archive->profile.expanded ? MM_STORAGE_LIMIT : 65535;
}

static int read_index(MMPrivateArchive *archive)
{
    MMChunkOwnerRead *reader = mm_private_archive_reader(archive);
    return mm_chunk_owner_read_index(reader->storage, NULL, archive->profile.trash_unused, reader->max_name_bytes,
                                       archive->profile.legacy_name, NULL) == MM_CHUNK_OK;
}

static int validate_index(void *context)
{
    MMPrivateArchive *archive = context;
    return mm_chunk_owner_validate_index(mm_private_archive_reader(archive)->storage,
                                          max_references(archive)) == MM_CHUNK_OK;
}

MMArchiveWriter mm_private_archive_writer(MMPrivateArchive *archive)
{
    return mm_chunk_owner_writer(mm_private_archive_reader(archive), max_references(archive),
                                 archive->profile.trash_unused, archive->profile.encoding, NULL);
}

static int read_title(void *context, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    const MMChunkOwnerRead *reader = context;
    return mm_chunk_owner_read_name(reader->storage, type, number, reader->max_name_bytes, wire, capacity, written);
}

static int write_title(void *context, MMU32 type, MMU32 number, const void *wire, MMU32 bytes)
{
    const MMChunkOwnerRead *reader = context;
    return mm_chunk_owner_write_name(reader->storage, type, number, reader->max_name_bytes,
                                     wire, bytes) == MM_CHUNK_OK;
}

static MMI32 convert_title(void *context, const void *bytes, MMU32 count, int encoding, void *out, MMU32 capacity)
{
    const MMPrivateArchive *archive = context;
    return (MMI32)archive->profile.convert(NULL, bytes, count, encoding, out, capacity);
}

static size_t convert_editor_text(void *context, const void *bytes, size_t count,
                                  int encoding, void *out, size_t capacity)
{
    const MMPrivateArchive *archive = ((const MMMovieArchiveFile *)context)->titles.context;
    return archive->profile.convert(NULL, bytes, count, encoding, out, capacity);
}

static void destroy_archive(void *context)
{
    MMPrivateArchive *archive = context;
    if (archive->editor_ready)
        mm_movie_archive_file_clear(&archive->editor);
    mm_chunk_context_detach(&archive->link);
    mm_chunk_archive_close(archive->backing);
    free(archive);
}

void mm_private_archive_retain(MMPrivateArchive *archive)
{
    mm_reference_add(&archive->references);
}

void mm_private_archive_close(MMPrivateArchive *archive)
{
    if (archive)
        mm_reference_release(&archive->references, archive, destroy_archive);
}

MMPrivateArchive *mm_private_archive_open(void *file, int mode, const MMChunkOwnerFileIO *io,
                                         const MMBlockFileServices *blocks, const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive;
    MMChunkOwnerRead *reader;
    MMChunkReopenState *files;
    MMArchiveWriter writer;
    MMChunkPayloadServices payloads = {&handles, NULL, NULL, NULL, decode_handle};
    if (!file || !io || !io->release)
        return NULL;
    if (!profile || (profile->unit != 1 && profile->unit != 2) || !profile->convert ||
        !io->read_stream || !blocks ||
        (mode < MM_PRIVATE_ARCHIVE_READ || mode > MM_PRIVATE_ARCHIVE_WORKING) ||
        (mode != MM_PRIVATE_ARCHIVE_READ && (!io->resize || !io->write_at || !io->size || !io->create_extra)))
    {
        io->release(file);
        return NULL;
    }
    archive = calloc(1, sizeof(*archive));
    if (!archive)
    {
        io->release(file);
        return NULL;
    }
    archive->profile = *profile;
    payloads.files = blocks;
    archive->backing = mm_chunk_archive_new(profile->expanded, NULL, io, &payloads, max_title_bytes(archive));
    if (!archive->backing)
    {
        free(archive);
        io->release(file);
        return NULL;
    }
    mm_reference_init(&archive->references);
    mm_chunk_link_init(&archive->link, archive);
    reader = mm_private_archive_reader(archive);
    files = &reader->storage->owner->files;
    files->main_file = file;
    archive->writable = mode != MM_PRIVATE_ARCHIVE_READ;
    if (mode == MM_PRIVATE_ARCHIVE_CREATE)
    {
        MMChunkTable *table;
        const MMU32 width = profile->expanded ? sizeof(MMChunkRecordBig) : sizeof(MMChunkRecordSmall);
        if (mm_chunk_table_create(NULL, width, 0, profile->trash_unused, &table) != MM_STORAGE_OK)
            goto fail;
        files->index = table;
        if (!io->resize(file, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        files->main_end = MM_CHUNK_PREFIX_BYTES;
    }
    else
    {
        if (!read_index(archive) || !validate_index(archive))
            goto fail;
        if (mode == MM_PRIVATE_ARCHIVE_EDIT)
            mm_chunk_owner_read_free_map(reader->storage);
        else if (mode == MM_PRIVATE_ARCHIVE_WORKING)
            reader->storage->owner->add_to_extra = 1;
    }
    archive->titles.context = archive;
    archive->titles.encoding = profile->encoding;
    archive->titles.swapped_unicode_encoding = profile->swapped_unicode_encoding;
    archive->titles.convert = convert_title;
    archive->titles.read = read_title;
    archive->titles.write = write_title;
    writer = mm_private_archive_writer(archive);
    archive->editor_ready = mm_movie_archive_file_init(&archive->editor, reader, &writer, &archive->titles);
    if (archive->editor_ready)
        return archive;
fail:
    mm_private_archive_close(archive);
    return NULL;
}

MMPrivateArchive *mm_private_archive_find(const MMChunkContext *registry, void *file)
{
    MMPrivateArchive *archive;
    if (!file)
        return NULL;
    for (archive = mm_chunk_context_first(registry); archive; archive = mm_chunk_context_next(&archive->link))
    {
        const MMChunkReopenState *files = &mm_private_archive_reader(archive)->storage->owner->files;
        if (!files->invalid_main && files->main_file == file)
            return archive;
    }
    return NULL;
}

MMPrivateArchive *mm_private_archive_open_registered(MMChunkContext *registry, void *file, int mode,
                                                    const MMChunkOwnerFileIO *io, const MMBlockFileServices *blocks,
                                                    const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive;
    if (!file || !io || !io->release)
        return NULL;
    archive = mm_private_archive_find(registry, file);
    if (archive)
    {
        if (mode == MM_PRIVATE_ARCHIVE_WORKING)
            mm_private_archive_retain(archive);
        io->release(file);
        return mode == MM_PRIVATE_ARCHIVE_WORKING ? archive : NULL;
    }
    archive = mm_private_archive_open(file, mode, io, blocks, profile);
    if (archive && mm_chunk_context_attach(registry, &archive->link) != MM_CHUNK_OK)
    {
        mm_private_archive_close(archive);
        return NULL;
    }
    return archive;
}

static int read_only(void *context, void *file)
{
    (void)context;
    (void)file;
    return 0;
}

static void retain_backing(void *context, void *file)
{
    const MMBlockFileServices *blocks = mm_private_archive_reader(context)->payloads.files;
    blocks->retain(blocks->context, file);
}

static void release_backing(void *context, void *file)
{
    MMChunkOwnerStorage *storage = mm_private_archive_reader(context)->storage;
    storage->io.release(file);
}

static void release_index(void *context, void *index)
{
    (void)context;
    mm_chunk_table_close(index);
}

static int reopen_index(void *context, MMChunkReopenState *state)
{
    (void)state;
    return read_index(context);
}

static void publish_index(void *context, const MMChunkReopenState *state)
{
    (void)context;
    (void)state;
}

int mm_private_archive_reopen(MMPrivateArchive *archive, int backing_writable)
{
    MMChunkReopenServices services = {NULL, read_only, retain_backing, release_backing, release_index,
                                      reopen_index, validate_index, publish_index};
    MMChunkOwnerRead *reader;
    MMChunkOwner *owner;
    MMU32 map_offset, map_bytes;
    int result;
    if (!archive || backing_writable)
        return MM_CHUNK_BAD_FILE;
    reader = mm_private_archive_reader(archive);
    if (!reader->payloads.files->retain)
        return MM_CHUNK_BAD_FILE;
    services.context = archive;
    owner = reader->storage->owner;
    map_offset = owner->map_offset;
    map_bytes = owner->map_bytes;
    result = mm_chunk_owner_reopen(owner, &services);
    if (result == MM_CHUNK_OK)
        mm_movie_archive_file_clear(&archive->editor);
    else
    {
        owner->map_offset = map_offset;
        owner->map_bytes = map_bytes;
    }
    return result;
}

MMMovieArchiveEditor mm_private_archive_editor(MMPrivateArchive *archive)
{
    return mm_movie_archive_file_editor(&archive->editor, archive->profile.unit, convert_editor_text);
}

static int locate_text(void *context, MMU32 number, MMTextLocation *text)
{
    MMPrivateArchive *archive = context;
    MMBlockFile extent;
    int packed;
    if (!archive || !mm_chunk_owner_locate(mm_private_archive_reader(archive)->storage, MM_TEXT_BYTES,
                                          number, &extent, &packed) ||
        extent.length > 0x7fffffffUL || extent.offset > 0x7fffffffUL - extent.length)
        return 0;
    text->file = extent.file;
    text->offset = extent.offset;
    text->bytes = (MMI32)extent.length;
    return 1;
}

static int read_text(void *context, const MMTextLocation *text, MMU32 offset, void *out, size_t bytes)
{
    MMPrivateArchive *archive = context;
    const MMBlockFileServices *files;
    if (text->bytes < 0 || offset > (MMU32)text->bytes || bytes > (MMU32)text->bytes - offset)
        return 0;
    if (!bytes)
        return 1;
    if (!archive || !text->file || text->offset > 0x7fffffffUL - (MMU32)text->bytes)
        return 0;
    files = mm_private_archive_reader(archive)->payloads.files;
    return files->read(files->context, text->file, text->offset + offset, out, bytes);
}

static void retain_text_file(void *context, void *file)
{
    const MMBlockFileServices *files = mm_private_archive_reader(context)->payloads.files;
    files->retain(files->context, file);
}

static void release_text_file(void *context, void *file)
{
    const MMBlockFileServices *files = mm_private_archive_reader(context)->payloads.files;
    files->release(files->context, file);
}

MMRichTextArchive mm_private_archive_rich_text(MMPrivateArchive *archive, const MMMovieArchiveEditor *editor)
{
    MMRichTextArchive text = {NULL, NULL, 0, locate_text, read_text, retain_text_file, release_text_file, NULL};
    text.editor = editor;
    text.context = archive;
    if (archive)
        text.swapped_unicode_encoding = archive->profile.swapped_unicode_encoding;
    return text;
}

MMChunkEndpoint mm_private_archive_copy_endpoint(MMPrivateArchive *archive, MMChunkFileCopy *binding)
{
    const MMArchiveWriter writer = mm_private_archive_writer(archive);
    return mm_chunk_file_copy_endpoint(binding, mm_private_archive_reader(archive), &writer, &archive->titles,
                                       archive->profile.unit, 0);
}

MMChunkEndpoint mm_private_archive_copy_target(MMPrivateArchive *archive, MMChunkFileCopy *binding)
{
    const MMChunkEndpoint denied = {0};
    return archive->writable ? mm_private_archive_copy_endpoint(archive, binding) : denied;
}

int mm_private_archive_copy(MMPrivateArchive *source, MMPrivateArchive *destination, MMU32 type,
                            MMU32 number, int clone, MMU32 *copy)
{
    MMChunkFileCopy source_binding, destination_binding;
    const MMChunkEndpoint source_end = mm_private_archive_copy_endpoint(source, &source_binding);
    const MMChunkEndpoint destination_end = mm_private_archive_copy_endpoint(destination, &destination_binding);
    return destination->writable &&
           mm_chunk_copy(&source_end, &destination_end, type, number, clone, copy) == MM_CHUNK_OK;
}

int mm_private_archive_bind_font(MMPrivateArchive *archive, const MMChunkEndpoint *source, MMU32 tmpl, MMU32 font)
{
    MMChunkFileCopy binding;
    const MMChunkEndpoint draft = mm_private_archive_copy_endpoint(archive, &binding);
    return archive->writable ? mm_text_bind_preview_font(source, &draft, tmpl, font) : MM_CHUNK_BAD_FILE;
}

int mm_private_archive_save(MMPrivateArchive *archive, MMU32 creator)
{
    return archive->writable &&
           mm_chunk_owner_save(mm_private_archive_reader(archive)->storage, NULL, 0, creator,
                               archive->profile.encoding) == MM_CHUNK_OK;
}
