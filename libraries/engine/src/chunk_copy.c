#include "mm/chunk_copy.h"

static int bind_file_payload(void *p, MMU32 position, const MMChunkEntry *entry, MMOwnedBlock *block)
{
    const MMChunkFileCopy *binding = p;
    (void)position;
    return mm_chunk_payload_bind_block(binding->archive->payloads, binding->file,
                                       entry->type, entry->number, block) == MM_CHUNK_PAYLOAD_OK &&
           (binding->allow_unbacked_empty || block->view.kind == MM_BLOCK_FILE);
}

static const void *read_file_name(void *p, MMU32 position)
{
    MMChunkFileCopy *binding = p;
    const MMChunkAccess chunks = mm_chunk_edit_access(&binding->archive->edit);
    MMChunkEntry entry;
    if (mm_chunk_at(&chunks, position, &entry) != MM_CHUNK_OK)
        return NULL;
    mm_chunk_title_read(&binding->name, binding->file, entry.type, entry.number, binding->titles);
    return binding->name.count ? &binding->name : NULL;
}

static int write_file_name(void *p, MMU32 position, const void *name)
{
    const MMChunkFileCopy *binding = p;
    const MMChunkAccess chunks = mm_chunk_edit_access(&binding->archive->edit);
    MMChunkEntry entry;
    return mm_chunk_at(&chunks, position, &entry) == MM_CHUNK_OK &&
           mm_chunk_title_write(name, binding->file, entry.type, entry.number, binding->titles);
}

MMChunkEndpoint mm_chunk_file_copy_endpoint(MMChunkFileCopy *binding, void *file, const MMArchiveWriter *archive,
                                           const MMChunkTitleIO *titles, unsigned text_unit, int allow_unbacked_empty)
{
    MMChunkEndpoint endpoint = {0};
    if (!binding || !file || !archive || !archive->payloads || !titles || !titles->read || !titles->write ||
        !mm_movie_title_init(&binding->name, text_unit))
        return endpoint;
    binding->file = file;
    binding->archive = archive;
    binding->titles = titles;
    binding->allow_unbacked_empty = allow_unbacked_empty;
    endpoint.edit = archive->edit;
    endpoint.allocator = archive->transfer_allocator;
    endpoint.handles = archive->payloads->handles;
    endpoint.files = archive->payloads->files;
    endpoint.identity = file;
    endpoint.context = binding;
    endpoint.bind_payload = bind_file_payload;
    endpoint.read_name = read_file_name;
    endpoint.write_name = write_file_name;
    return endpoint;
}

static int bind_stored_payload(void *p, MMU32 position, const MMChunkEntry *entry, MMOwnedBlock *block)
{
    const MMStoredChunkCopy *binding = p;
    (void)position;
    return mm_resource_cache_read_block(binding->cache, entry->type, entry->number, block) == MM_CHUNK_OK;
}

static const void *read_stored_name(void *p, MMU32 position)
{
    MMStoredChunkCopy *binding = p;
    unsigned char scratch[MM_CHUNK_NAME_WIRE_BYTES];
    const unsigned char *bytes;
    MMU32 count;
    if (mm_chunk_serialized_name(binding->cache->wire_index, position, binding->legacy_name,
                                 binding->titles->context, scratch, &bytes, &count) != MM_CHUNK_OK || !count)
        return NULL;
    mm_movie_title_read_wire(&binding->name, bytes, count, binding->titles);
    return binding->name.count ? &binding->name : NULL;
}

MMChunkEndpoint mm_stored_chunk_copy_endpoint(MMStoredChunkCopy *binding, MMResourceCache *cache, void *identity,
                                             const MMStorageAllocator *allocator, const MMChunkTitleIO *titles,
                                             MMChunkLegacyName legacy_name, unsigned text_unit)
{
    MMChunkEndpoint endpoint = {0};
    if (!binding || !cache || !cache->wire_index || !titles || !mm_movie_title_init(&binding->name, text_unit))
        return endpoint;
    binding->cache = cache;
    binding->chunks = mm_resource_cache_chunks(cache);
    binding->titles = titles;
    binding->legacy_name = legacy_name;
    endpoint.stored = &binding->chunks;
    endpoint.allocator = allocator;
    endpoint.handles = cache->services.handles;
    endpoint.files = cache->services.block_files;
    endpoint.identity = identity;
    endpoint.context = binding;
    endpoint.bind_payload = bind_stored_payload;
    endpoint.read_name = read_stored_name;
    return endpoint;
}
