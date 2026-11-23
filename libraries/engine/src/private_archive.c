#include "mm/private_archive_cache.h"
#include "mm/chunk_table.h"
#include "mm/chunk_copy.h"
#include "mm/chunk_forest.h"
#include "mm/file_owner.h"
#include "mm/file_ops.h"
#include "mm/reference.h"
#include "mm/text_resource.h"
#include "mm/string_wire.h"
#include <stdlib.h>
#include <string.h>

struct MMPrivateArchive
{
    MMChunkArchive *backing;
    MMReference references;
    MMPrivateArchiveProfile profile;
    MMChunkTitleIO titles;
    MMResourceCopyServices copy;
    MMMovieArchiveFile editor;
    int writable, editor_ready;
    MMChunkLink link;
};

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

MMArchiveWriter mm_private_archive_writer(MMPrivateArchive *archive)
{
    return mm_chunk_owner_writer(mm_private_archive_reader(archive));
}

static void cache_copy_source(void *context, MMChunkFileCopy *binding, MMChunkEndpoint *out)
{
    *out = mm_private_archive_copy_endpoint(context, binding);
}

static void cache_copy_target(void *context, MMChunkFileCopy *binding, MMChunkEndpoint *out)
{
    *out = mm_private_archive_copy_target(context, binding);
}

static void cache_retain(void *context) { mm_private_archive_retain(context); }
static void cache_release(void *context) { mm_private_archive_close(context); }

static void archive_reloaded(void *context)
{
    MMPrivateArchive *archive = context;
    mm_movie_archive_file_clear(&archive->editor);
}

void mm_private_archive_cache_services(MMPrivateArchive *archive, const MMHandleServices *handles,
                                       MMResourceServices *services)
{
    const MMChunkOwnerRead *reader = mm_private_archive_reader(archive);
    const MMChunkPayloadServices *payloads = &reader->payloads;
    services->handles = handles ? handles : payloads->handles;
    services->block_files = payloads->files;
    services->retain_file = cache_retain;
    services->release_file = cache_release;
    services->live_copy_source = cache_copy_source;
    services->live_copy_target = cache_copy_target;
    if (!services->copy)
        services->copy = &archive->copy;
    services->live_reloaded = archive_reloaded;
}

MMResourceCache *mm_private_archive_cache_create(MMPrivateArchive *archive, MMI32 budget,
                                                 const MMResourceServices *profile,
                                                 const MMStorageAllocator *owner, const MMStorageAllocator *storage)
{
    MMResourceServices services = {0};
    MMResourceSource source = {0};
    if (!archive)
        return NULL;
    if (profile)
        services = *profile;
    source.live = archive;
    source.reader = mm_private_archive_reader(archive);
    mm_private_archive_cache_services(archive, services.handles, &services);
    return mm_resource_cache_create(&source, budget, &services, owner, storage);
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

static MMPrivateArchive *allocate_archive(void *file, const MMChunkOwnerFileIO *io,
                                          const MMBlockFileServices *blocks, const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive;
    MMChunkOwnerRead *reader;
    MMChunkIndexOptions index = {0};
    MMChunkWriteOptions write = {0};
    MMChunkPayloadServices payloads = {0};
    if (!file || !io || !io->release)
        return NULL;
    if (!profile || (profile->unit != 1 && profile->unit != 2) || !profile->convert || !io->read_stream || !blocks)
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
    index.legacy_name = profile->legacy_name;
    index.trash_unused = profile->trash_unused;
    index.restore_map_on_failure = 1;
    write.max_references = max_references(archive);
    write.system = profile->encoding;
    write.scan_all_references = profile->trash_unused;
    payloads.handles = mm_chunk_handle_services();
    payloads.files = blocks;
    archive->backing = mm_chunk_archive_new(profile->expanded, NULL, io, &payloads, max_title_bytes(archive),
                                            &index, &write);
    if (!archive->backing)
    {
        free(archive);
        io->release(file);
        return NULL;
    }
    mm_reference_init(&archive->references);
    reader = mm_private_archive_reader(archive);
    mm_chunk_link_init(&archive->link, archive, reader, &archive->references);
    reader->storage->owner->files.main_file = file;
    return archive;
}

static int bind_editor(MMPrivateArchive *archive)
{
    MMChunkOwnerRead *reader = mm_private_archive_reader(archive);
    const MMArchiveWriter writer = mm_private_archive_writer(archive);
    archive->titles.context = archive;
    archive->titles.encoding = archive->profile.encoding;
    archive->titles.swapped_unicode_encoding = archive->profile.swapped_unicode_encoding;
    archive->titles.convert = convert_title;
    archive->titles.read = mm_chunk_owner_title_read;
    archive->titles.write = mm_chunk_owner_title_write;
    archive->copy.titles = archive->titles;
    archive->copy.legacy_name = archive->profile.legacy_name;
    archive->copy.text_unit = archive->profile.unit;
    archive->editor_ready = mm_movie_archive_file_init(&archive->editor, reader, &writer, &archive->titles);
    return archive->editor_ready;
}

MMPrivateArchive *mm_private_archive_open(void *file, int mode, const MMChunkOwnerFileIO *io,
                                         const MMBlockFileServices *blocks, const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive = allocate_archive(file, io, blocks, profile);
    MMChunkOwnerRead *reader;
    if (!archive)
        return NULL;
    if ((mode < MM_PRIVATE_ARCHIVE_READ || mode > MM_PRIVATE_ARCHIVE_WORKING) ||
        (mode != MM_PRIVATE_ARCHIVE_READ && (!io->resize || !io->write_at || !io->size || !io->create_extra)))
        goto fail;
    reader = mm_private_archive_reader(archive);
    archive->writable = mode != MM_PRIVATE_ARCHIVE_READ;
    if (mode == MM_PRIVATE_ARCHIVE_CREATE)
    {
        if (mm_chunk_reader_create_index(reader) != MM_CHUNK_OK || !io->resize(file, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        reader->storage->owner->files.main_end = MM_CHUNK_PREFIX_BYTES;
    }
    else
    {
        if (mm_chunk_reader_read_index(reader) != MM_CHUNK_OK || mm_chunk_reader_validate_index(reader) != MM_CHUNK_OK)
            goto fail;
        if (mode == MM_PRIVATE_ARCHIVE_EDIT)
            mm_chunk_owner_read_free_map(reader->storage);
        else if (mode == MM_PRIVATE_ARCHIVE_WORKING)
            reader->storage->owner->add_to_extra = 1;
    }
    if (bind_editor(archive))
        return archive;
fail:
    mm_private_archive_close(archive);
    return NULL;
}

MMPrivateArchive *mm_private_archive_open_forest(const MMBlockFile *extent, const MMChunkOwnerFileIO *io,
                                               const MMBlockFileServices *blocks, const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive;
    MMChunkOwnerRead *reader;
    MMChunkOwnerStorage *storage;
    MMReadStream stream;
    if (!extent)
        return NULL;
    archive = allocate_archive(extent->file, io, blocks, profile);
    if (!archive)
        return NULL;
    reader = mm_private_archive_reader(archive);
    storage = reader->storage;
    stream = io->read_stream(extent->file);
    if (mm_chunk_reader_create_index(reader) == MM_CHUNK_OK)
    {
        storage->owner->files.invalid_main = 1;
        if (mm_chunk_reader_import_forest(reader, &stream, extent->file, extent->offset, extent->length, 0) ==
                MM_CHUNK_OK &&
            mm_chunk_reader_validate_index(reader) == MM_CHUNK_OK && bind_editor(archive))
        {
            storage->owner->files.main_end = extent->offset + extent->length;
            return archive;
        }
    }
    mm_private_archive_close(archive);
    return NULL;
}

MMPrivateArchive *mm_private_archive_open_resource_forest(MMResourceCache *source, MMU32 type, MMU32 number,
                                                         MMBlockTransform decode, void *decode_context,
                                                         const MMChunkOwnerFileIO *io, const MMBlockFileServices *files,
                                                         const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive = NULL;
    MMOwnedBlock block;
    MMBlockFile extent;
    if (!source || !io || !io->release || source->services.block_files != files ||
        mm_owned_block_init(&block, source->services.handles, files) != MM_DATA_OK)
        return NULL;
    if (mm_resource_cache_read_block(source, type, number, &block) == MM_CHUNK_OK &&
        mm_owned_block_code(&block, 0, decode_context, decode, NULL) == MM_DATA_OK &&
        mm_owned_block_materialize(&block, 0, &extent) == MM_DATA_OK)
        archive = mm_private_archive_open_forest(&extent, io, files, profile);
    mm_owned_block_clear(&block);
    return archive;
}

MMPrivateArchive *mm_private_archive_find(const MMChunkContext *registry, void *file)
{
    const MMChunkLink *link = mm_chunk_context_find(registry, file, NULL);
    return link ? link->owner : NULL;
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

static MMPrivateArchive *open_named_file(const MMNamedArchiveServices *s, void *name, int create)
{
    const MMU32 flags = mm_chunk_file_flags(create ? MM_CHUNK_WRITE | MM_CHUNK_TEMP : 0);
    const MMRegisteredFile *file = create ? mm_file_ops_create(s->file_ops, name, flags) :
                                           mm_file_ops_open(s->file_ops, name, flags);
    const int mode = create ? MM_PRIVATE_ARCHIVE_CREATE : MM_PRIVATE_ARCHIVE_WORKING;
    MMPrivateArchive *archive = mm_private_archive_open_registered(s->registry, file ? file->object : NULL,
                                                                   mode, s->io, s->blocks, s->profile);
    if (!archive && s->open_failed)
        s->open_failed(create);
    return archive;
}

MMPrivateArchive *mm_private_archive_open_named(const MMNamedArchiveServices *s, void *name, int create)
{
    MMPrivateArchive *archive = NULL;
    if (!create)
    {
        const MMRegisteredFile *file = mm_file_ops_find(s->file_ops, name);
        if (file)
            archive = mm_private_archive_find(s->registry, file->object);
    }
    if (!archive)
        return open_named_file(s, name, create);
    mm_private_archive_retain(archive);
    return archive;
}

static MMResourceCache *cache_opened_archive(MMPrivateArchive *archive, MMI32 budget,
                                            const MMResourceServices *profile,
                                            const MMStorageAllocator *owner, const MMStorageAllocator *storage)
{
    MMResourceCache *cache = mm_private_archive_cache_create(archive, budget, profile, owner, storage);
    mm_private_archive_close(archive);
    return cache;
}

MMResourceCache *mm_private_archive_open_named_cache(const MMNamedArchiveServices *s, void *name, int create,
                                                    MMI32 budget, const MMResourceServices *profile,
                                                    const MMStorageAllocator *owner, const MMStorageAllocator *storage)
{
    return cache_opened_archive(mm_private_archive_open_named(s, name, create), budget, profile, owner, storage);
}

MMResourceCache *mm_shared_archive_cache_open(const MMSharedArchiveCacheServices *s, void *name, int prefer_working,
                                              MMI32 budget, int *selected, MMChunkOwnerRead **reader)
{
    const MMNamedArchiveServices *files = s->files;
    const MMRegisteredFile *file = mm_file_ops_find(files->file_ops, name);
    void *backing = file ? file->object : NULL;
    MMPrivateArchive *archive = mm_private_archive_find(files->registry, backing);
    MMResourceCache *cache = NULL;
    MMResourceSource source = {0};
    *selected = 1;
    if (archive)
        mm_private_archive_retain(archive);
    else
    {
        const MMChunkLink *link = mm_chunk_context_find(s->legacy_registry, backing, s->extra_collision);
        if (link)
        {
            if (s->prepare_legacy(link->owner))
            {
                source.live = link->owner;
                source.reader = link->reader;
                cache = mm_resource_cache_create(&source, budget, s->legacy, s->owner_allocator,
                                                 s->storage_allocator);
            }
        }
        else
        {
            *selected = prefer_working || (file && (file->owner->flags & (MM_FILE_WRITE | MM_FILE_DENY_READ)));
            if (*selected)
                archive = open_named_file(files, name, 0);
        }
    }
    if (archive)
        cache = cache_opened_archive(archive, budget, s->working, s->owner_allocator, s->storage_allocator);
    if (reader && cache)
        *reader = mm_resource_cache_reader(cache);
    return cache;
}

int mm_private_archive_reopen(MMPrivateArchive *archive, int backing_writable)
{
    int result;
    if (!archive || backing_writable)
        return MM_CHUNK_BAD_FILE;
    result = mm_chunk_reader_reopen(mm_private_archive_reader(archive));
    if (result == MM_CHUNK_OK)
        archive_reloaded(archive);
    return result;
}

MMMovieArchiveEditor mm_private_archive_editor(MMPrivateArchive *archive)
{
    return mm_movie_archive_file_editor(&archive->editor, archive->profile.unit, convert_editor_text);
}

MMRichTextArchive mm_private_archive_rich_text(MMPrivateArchive *archive, const MMMovieArchiveEditor *editor)
{
    MMChunkOwnerRead *owner = archive ? mm_private_archive_reader(archive) : NULL;
    return mm_rich_text_file_archive(owner, NULL, editor,
                                     archive ? archive->profile.swapped_unicode_encoding : 0);
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

int mm_private_archive_store_text_draft(MMPrivateArchive *archive, const MMTextAuthorName *name,
                                        const MMTextDescriptor *text, MMU32 creator)
{
    unsigned short counted[MM_STRING_MAX + 2], converted[MM_TEXT_NAME_BYTES - 1];
    MMStringValue value = {0};
    MMArchiveWriter writer;
    MMU32 number;
    const char *end = memchr(name->legacy, '\0', sizeof(name->legacy));
    const void *characters = name->legacy;
    size_t count;
    if (!archive->writable || !end || end == name->legacy)
        return 0;
    count = (size_t)(end - name->legacy);
    value.buffer = counted;
    value.unit = archive->profile.unit;
    if (value.unit == 2)
    {
        count = mm_text_utf16(NULL, characters, count, MM_TEXT_WINDOWS, converted, MM_TEXT_NAME_BYTES - 1);
        if (!count)
            return 0;
        characters = converted;
    }
    mm_string_value_set(&value, characters, (MMI32)count);
    writer = mm_private_archive_writer(archive);
    return mm_text_write_draft(&value, text, &number, &writer) && number == 0 &&
           mm_private_archive_save(archive, creator);
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
           mm_chunk_owner_save(mm_private_archive_reader(archive)->storage, NULL, NULL, creator,
                               archive->profile.encoding) == MM_CHUNK_OK;
}
