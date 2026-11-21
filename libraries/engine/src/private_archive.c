#include "mm/private_archive.h"
#include "mm/chunk_copy.h"
#include "mm/handle_memory.h"
#include "mm/text_resource.h"
#include <stdlib.h>

struct MMPrivateArchive
{
    MMChunkOwner owner;
    MMChunkOwnerStorage storage;
    MMChunkPayloadServices payloads;
    MMPrivateArchiveProfile profile;
    MMChunkTitleIO titles;
    MMMovieArchiveFile editor;
    int writable, editor_ready;
};

enum { VALIDATION_MARK = 8 };

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

static int locate(void *context, MMU32 type, MMU32 number, MMBlockFile *extent, int *packed)
{
    MMPrivateArchive *archive = context;
    return mm_chunk_owner_locate(&archive->storage, type, number, extent, packed);
}

static MMU32 max_title_bytes(const MMPrivateArchive *archive)
{
    return 2 + (MM_STRING_MAX + 2) * archive->profile.unit;
}

static MMU32 max_references(const MMPrivateArchive *archive)
{
    return archive->profile.expanded ? MM_STORAGE_LIMIT : 65535;
}

MMArchiveWriter mm_private_archive_writer(MMPrivateArchive *archive)
{
    const unsigned short order = 1;
    MMArchiveWriter writer = {0};
    writer.edit = mm_chunk_owner_editor(&archive->storage, max_references(archive), archive->profile.trash_unused);
    writer.names = mm_chunk_table_names(archive->owner.files.index, max_title_bytes(archive));
    writer.payloads = &archive->payloads;
    writer.big_endian = *(const unsigned char *)&order == 0;
    writer.system = archive->profile.encoding;
    return writer;
}

static int read_title(void *context, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    MMPrivateArchive *archive = context;
    return mm_chunk_owner_read_name(&archive->storage, type, number, max_title_bytes(archive), wire, capacity, written);
}

static int write_title(void *context, MMU32 type, MMU32 number, const void *wire, MMU32 bytes)
{
    MMPrivateArchive *archive = context;
    const MMArchiveWriter writer = mm_private_archive_writer(archive);
    MMChunkNameValue value = {NULL, 0, 0};
    value.wire = wire;
    value.bytes = bytes;
    value.characters = bytes != 0;
    return mm_chunk_name_set_key(&writer.edit, &writer.names, type, number, &value) == MM_CHUNK_OK;
}

static MMI32 convert_title(void *context, const void *bytes, MMU32 count, int encoding, void *out, MMU32 capacity)
{
    const MMPrivateArchive *archive = context;
    return (MMI32)archive->profile.convert(NULL, bytes, count, encoding, out, capacity);
}

static size_t convert_editor_text(void *context, const void *bytes, size_t count,
                                  int encoding, void *out, size_t capacity)
{
    const MMPrivateArchive *archive = ((const MMMovieArchiveFile *)context)->file;
    return archive->profile.convert(NULL, bytes, count, encoding, out, capacity);
}

static void release_file(void *context, void *file)
{
    MMPrivateArchive *archive = context;
    archive->storage.io.release(file);
}

static void close_table(void *context, void *table)
{
    (void)context;
    mm_chunk_table_close(table);
}

static void publish_disposed(void *context, const MMChunkReopenState *files)
{
    /* Disposal already cleared the sole live state. */
    (void)context;
    (void)files;
}

void mm_private_archive_close(MMPrivateArchive *archive)
{
    MMChunkDisposal disposal = {NULL, release_file, close_table, publish_disposed};
    if (!archive)
        return;
    if (archive->editor_ready)
        mm_movie_archive_file_clear(&archive->editor);
    disposal.context = archive;
    mm_chunk_owner_dispose(&archive->owner, &disposal);
    free(archive);
}

MMPrivateArchive *mm_private_archive_open(void *file, int mode, const MMChunkOwnerFileIO *io,
                                         const MMBlockFileServices *blocks, const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive;
    MMChunkTable *table;
    MMChunkAccess access;
    MMArchiveWriter writer;
    if (!file || !io || !io->release)
        return NULL;
    if (!profile || (profile->unit != 1 && profile->unit != 2) || !profile->convert ||
        !io->read_stream || !blocks ||
        (mode != MM_PRIVATE_ARCHIVE_READ && mode != MM_PRIVATE_ARCHIVE_EDIT && mode != MM_PRIVATE_ARCHIVE_CREATE) ||
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
    mm_chunk_owner_init(&archive->owner, NULL, profile->expanded);
    archive->owner.files.main_file = file;
    mm_chunk_owner_bind_storage(&archive->storage, &archive->owner, NULL,
                                profile->expanded ? 0x7fffffffUL : 0x00ffffffUL, io);
    archive->profile = *profile;
    archive->writable = mode != MM_PRIVATE_ARCHIVE_READ;
    if (mode == MM_PRIVATE_ARCHIVE_CREATE)
    {
        const MMU32 width = profile->expanded ? sizeof(MMChunkRecordBig) : sizeof(MMChunkRecordSmall);
        if (mm_chunk_table_create(NULL, width, 0, profile->trash_unused, &table) != MM_STORAGE_OK)
            goto fail;
        archive->owner.files.index = table;
        if (!io->resize(file, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        archive->owner.files.main_end = MM_CHUNK_PREFIX_BYTES;
    }
    else
    {
        if (mm_chunk_owner_read_index(&archive->storage, NULL, profile->trash_unused, max_title_bytes(archive),
                                      profile->legacy_name, NULL) != MM_CHUNK_OK)
            goto fail;
        table = archive->owner.files.index;
        if (mm_chunk_table_clear_flags(table, VALIDATION_MARK) != MM_STORAGE_OK)
            goto fail;
        access = mm_chunk_table_access(table);
        if (mm_chunk_validate_graph(&access, max_references(archive)) != MM_CHUNK_OK)
            goto fail;
        if (archive->writable)
            mm_chunk_owner_read_free_map(&archive->storage);
    }
    archive->payloads.handles = &handles;
    archive->payloads.files = blocks;
    archive->payloads.locate = locate;
    archive->payloads.decode = decode_handle;
    archive->titles.context = archive;
    archive->titles.encoding = profile->encoding;
    archive->titles.swapped_unicode_encoding = profile->swapped_unicode_encoding;
    archive->titles.convert = convert_title;
    archive->titles.read = read_title;
    archive->titles.write = write_title;
    writer = mm_private_archive_writer(archive);
    archive->editor_ready = mm_movie_archive_file_init(&archive->editor, archive, &writer, &archive->titles);
    if (archive->editor_ready)
        return archive;
fail:
    mm_private_archive_close(archive);
    return NULL;
}

MMMovieArchiveEditor mm_private_archive_editor(MMPrivateArchive *archive)
{
    return mm_movie_archive_file_editor(&archive->editor, archive->profile.unit, convert_editor_text);
}

static int locate_text(void *context, MMU32 number, MMTextLocation *text)
{
    MMBlockFile extent;
    int packed;
    if (!context || !locate(context, MM_TEXT_BYTES, number, &extent, &packed) ||
        extent.length > 0x7fffffffUL || extent.offset > 0x7fffffffUL - extent.length)
        return 0;
    text->file = extent.file;
    text->offset = extent.offset;
    text->bytes = (MMI32)extent.length;
    return 1;
}

static int read_text(void *context, const MMTextLocation *text, MMU32 offset, void *out, size_t bytes)
{
    const MMPrivateArchive *archive = context;
    const MMBlockFileServices *files;
    if (text->bytes < 0 || offset > (MMU32)text->bytes || bytes > (MMU32)text->bytes - offset)
        return 0;
    if (!bytes)
        return 1;
    if (!archive || !text->file || text->offset > 0x7fffffffUL - (MMU32)text->bytes)
        return 0;
    files = archive->payloads.files;
    return files->read(files->context, text->file, text->offset + offset, out, bytes);
}

static void retain_text_file(void *context, void *file)
{
    const MMBlockFileServices *files = ((const MMPrivateArchive *)context)->payloads.files;
    files->retain(files->context, file);
}

static void release_text_file(void *context, void *file)
{
    const MMBlockFileServices *files = ((const MMPrivateArchive *)context)->payloads.files;
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

static MMChunkEndpoint copy_endpoint(MMPrivateArchive *archive, MMChunkFileCopy *binding)
{
    const MMArchiveWriter writer = mm_private_archive_writer(archive);
    return mm_chunk_file_copy_endpoint(binding, archive, &writer, &archive->titles, archive->profile.unit, 0);
}

int mm_private_archive_copy(MMPrivateArchive *source, MMPrivateArchive *destination, MMU32 type,
                            MMU32 number, int clone, MMU32 *copy)
{
    MMChunkFileCopy source_binding, destination_binding;
    const MMChunkEndpoint source_end = copy_endpoint(source, &source_binding);
    const MMChunkEndpoint destination_end = copy_endpoint(destination, &destination_binding);
    return destination->writable &&
           mm_chunk_copy(&source_end, &destination_end, type, number, clone, copy) == MM_CHUNK_OK;
}

int mm_private_archive_bind_font(MMPrivateArchive *archive, const MMChunkEndpoint *source, MMU32 tmpl, MMU32 font)
{
    MMChunkFileCopy binding;
    const MMChunkEndpoint draft = copy_endpoint(archive, &binding);
    return archive->writable ? mm_text_bind_preview_font(source, &draft, tmpl, font) : MM_CHUNK_BAD_FILE;
}

int mm_private_archive_save(MMPrivateArchive *archive, MMU32 creator)
{
    return archive->writable &&
           mm_chunk_owner_save(&archive->storage, NULL, 0, creator, archive->profile.encoding) == MM_CHUNK_OK;
}
