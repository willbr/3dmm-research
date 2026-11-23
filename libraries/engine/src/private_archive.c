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

static MMChunkAccess cache_chunks(void *context)
{
    return mm_chunk_owner_access(mm_private_archive_reader(context)->storage);
}

static int cache_contains(void *context, MMU32 type, MMU32 number)
{
    const MMChunkAccess chunks = cache_chunks(context);
    MMU32 position;
    return mm_chunk_find(&chunks, type, number, &position) == MM_CHUNK_OK;
}

static int cache_locate(void *context, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    return mm_chunk_owner_locate(mm_private_archive_reader(context)->storage, type, number, file, packed);
}

static int cache_name(void *context, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    return mm_chunk_owner_title_read(mm_private_archive_reader(context), type, number, wire, capacity, written);
}

static void cache_copy_source(void *context, MMChunkFileCopy *binding, MMChunkEndpoint *out)
{
    *out = mm_private_archive_copy_endpoint(context, binding);
}

static void cache_copy_target(void *context, MMChunkFileCopy *binding, MMChunkEndpoint *out)
{
    *out = mm_private_archive_copy_target(context, binding);
}

static MMChunkOwnerRead *cache_reader(void *context) { return mm_private_archive_reader(context); }
static MMArchiveWriter cache_writer(void *context) { return mm_private_archive_writer(context); }

static void cache_retain(void *context) { mm_private_archive_retain(context); }
static void cache_release(void *context) { mm_private_archive_close(context); }

static void cache_reopen(void *context)
{
    MMChunkOwnerStorage *storage = mm_private_archive_reader(context)->storage;
    void *file = storage->owner->files.main_file;
    if (file)
        mm_private_archive_reopen(context, storage->io.is_writable(file));
}

void mm_private_archive_cache_services(MMPrivateArchive *archive, const MMHandleServices *handles,
                                       MMResourceServices *services)
{
    const MMChunkOwnerRead *reader = mm_private_archive_reader(archive);
    const MMChunkPayloadServices *payloads = &reader->payloads;
    services->handles = handles ? handles : payloads->handles;
    services->block_files = payloads->files;
    services->live_chunks = cache_chunks;
    services->contains = cache_contains;
    services->locate = cache_locate;
    services->retain_file = cache_retain;
    services->release_file = cache_release;
    services->live_name = cache_name;
    services->live_copy_source = cache_copy_source;
    services->live_copy_target = cache_copy_target;
    services->live_reader = cache_reader;
    services->live_writer = cache_writer;
    if (!services->copy)
        services->copy = &archive->copy;
    if (reader->storage->io.is_writable)
        services->live_reopen = cache_reopen;
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
    payloads.handles = mm_chunk_handle_services();
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
    mm_private_archive_reader(archive)->storage->owner->files.main_file = file;
    return archive;
}

static int create_index(MMPrivateArchive *archive)
{
    MMChunkTable *table;
    const MMU32 width = archive->profile.expanded ? sizeof(MMChunkRecordBig) : sizeof(MMChunkRecordSmall);
    if (mm_chunk_table_create(NULL, width, 0, archive->profile.trash_unused, &table) != MM_STORAGE_OK)
        return 0;
    mm_private_archive_reader(archive)->storage->owner->files.index = table;
    return 1;
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
        if (!create_index(archive) || !io->resize(file, MM_CHUNK_PREFIX_BYTES))
            goto fail;
        reader->storage->owner->files.main_end = MM_CHUNK_PREFIX_BYTES;
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
    if (bind_editor(archive))
        return archive;
fail:
    mm_private_archive_close(archive);
    return NULL;
}

static int add_forest_chunk(void *context, MMU32 type, MMU32 offset, MMU32 bytes, MMU32 flags,
                            int copy, MMU32 *number)
{
    const MMChunkEdit *edit = context;
    MMChunkEntry entry = {0};
    MMU32 position;
    (void)copy;
    if (mm_chunk_unique(edit, type, &position, &entry.number) != MM_CHUNK_OK || !edit->insert(edit->context, position))
        return 0;
    entry.type = type;
    entry.offset = bytes ? offset : 0;
    entry.bytes = bytes;
    entry.flags = MM_CHUNK_LONER | (flags & (MM_CHUNK_PACKED | MM_CHUNK_FOREST));
    edit->store(edit->context, position, &entry, 1);
    *number = entry.number;
    return 1;
}

static int adopt_forest_chunk(void *context, MMU32 parent, MMU32 number, MMU32 type, MMU32 child, MMU32 slot)
{
    MMChunkChild link;
    link.type = type;
    link.number = child;
    link.slot = slot;
    return mm_chunk_adopt(context, parent, number, &link, 1) == MM_CHUNK_OK;
}

MMPrivateArchive *mm_private_archive_open_forest(const MMBlockFile *extent, const MMChunkOwnerFileIO *io,
                                               const MMBlockFileServices *blocks, const MMPrivateArchiveProfile *profile)
{
    MMPrivateArchive *archive;
    MMChunkOwnerStorage *storage;
    MMChunkEdit edit;
    MMReadStream stream;
    MMForestImport importer = {NULL, add_forest_chunk, adopt_forest_chunk};
    if (!extent)
        return NULL;
    archive = allocate_archive(extent->file, io, blocks, profile);
    if (!archive)
        return NULL;
    storage = mm_private_archive_reader(archive)->storage;
    stream = io->read_stream(extent->file);
    if (create_index(archive))
    {
        storage->owner->files.invalid_main = 1;
        edit = mm_chunk_owner_editor(storage, max_references(archive), profile->trash_unused);
        importer.context = &edit;
        if (mm_forest_read(&stream, extent->offset, extent->length, &importer, NULL, 0) == MM_CHUNK_OK &&
            validate_index(archive) && bind_editor(archive))
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
    MMPrivateArchive *archive = file ? mm_private_archive_find(files->registry, file->object) : NULL;
    MMResourceCache *cache = NULL;
    MMResourceSource source = {0};
    *selected = 1;
    if (archive)
        mm_private_archive_retain(archive);
    else
    {
        source.live = s->find_legacy(name);
        if (source.live)
        {
            if (s->prepare_legacy(source.live))
                cache = mm_resource_cache_create(&source, budget, s->legacy, s->owner_allocator,
                                                 s->storage_allocator);
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
