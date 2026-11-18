/* Chunk storage policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_storage.h"
#include "mm/codec.h"
#include <stdlib.h>

static int locate_payload(const MMChunkPayloadServices *s, void *archive, MMU32 type,
                          MMU32 number, MMOwnedBlock *block)
{
    MMBlockFile file;
    int packed;
    if (!s->locate(archive, type, number, &file, &packed))
        return MM_CHUNK_PAYLOAD_MISSING;
    if ((file.file || file.offset || file.length) &&
        mm_owned_block_set_file(block, &file, packed) != MM_DATA_OK)
        return MM_CHUNK_PAYLOAD_INVALID;
    return MM_CHUNK_PAYLOAD_OK;
}

static int payload_stream(MMOwnedBlock *block, MMBlockIO *io, MMReadStream *stream)
{
    return mm_owned_block_io(block, io) == MM_DATA_OK &&
           mm_block_io_stream(io, stream) == MM_DATA_OK;
}

int mm_chunk_read_payload(const MMChunkPayloadServices *s, void *archive, MMU32 type,
                          MMU32 number, MMReadReceiver receive, void *context, int *result)
{
    MMOwnedBlock block;
    MMBlockIO io;
    MMReadStream stream;
    int opened, status = MM_CHUNK_BAD_FILE;
    if (!s || !s->locate || !s->decode || !archive || !receive || !result ||
        mm_owned_block_init(&block, s->handles, s->files) != MM_DATA_OK)
        return MM_CHUNK_BAD_FILE;
    opened = locate_payload(s, archive, type, number, &block);
    if (opened == MM_CHUNK_PAYLOAD_MISSING)
        return MM_CHUNK_NOT_FOUND;
    if (opened == MM_CHUNK_PAYLOAD_OK &&
        mm_owned_block_code(&block, 0, s->decode_context, s->decode, NULL) == MM_DATA_OK &&
        payload_stream(&block, &io, &stream))
    {
        *result = receive(context, &stream);
        status = MM_CHUNK_OK;
    }
    mm_owned_block_clear(&block);
    return status;
}

int mm_chunk_payload_cache_init(MMChunkPayloadCache *cache, const MMChunkPayloadServices *s)
{
    if (!cache || !s || !s->locate || !s->decode ||
        mm_owned_block_init(&cache->block, s->handles, s->files) != MM_DATA_OK)
        return MM_CHUNK_PAYLOAD_INVALID;
    cache->services = s;
    cache->position = (size_t)-1;
    return MM_CHUNK_PAYLOAD_OK;
}

int mm_chunk_payload_cache_clear(MMChunkPayloadCache *cache)
{
    if (!cache)
        return MM_CHUNK_PAYLOAD_INVALID;
    cache->position = (size_t)-1;
    return mm_owned_block_clear(&cache->block) == MM_DATA_OK ?
               MM_CHUNK_PAYLOAD_OK : MM_CHUNK_PAYLOAD_INVALID;
}

static int check_packed_length(MMOwnedBlock *block, MMU32 limit)
{
    MMBlockIO io;
    unsigned char header[8];
    MMU32 decoded;
    if (mm_owned_block_io(block, &io) != MM_DATA_OK ||
        mm_block_read(&io, 0, header, sizeof(header), 1) != MM_DATA_OK)
        return MM_CHUNK_PAYLOAD_BAD_HEADER;
    decoded = ((MMU32)header[4] << 24) | ((MMU32)header[5] << 16) |
              ((MMU32)header[6] << 8) | header[7];
    return decoded > limit || io.view.length > limit ? MM_CHUNK_PAYLOAD_LIMIT : MM_CHUNK_PAYLOAD_OK;
}

int mm_chunk_payload_cache_load(MMChunkPayloadCache *cache, void *archive, size_t position,
                                const MMChunkEntry *entry, MMU32 limit,
                                const MMChunkPayloadCacheCallbacks *callbacks)
{
    const MMChunkPayloadServices *s;
    int status;
    if (!cache || !cache->services || !archive || !entry || position == (size_t)-1)
        return MM_CHUNK_PAYLOAD_INVALID;
    if (cache->position == position)
        return MM_CHUNK_PAYLOAD_OK;

    s = cache->services;
    cache->position = (size_t)-1;
    if (callbacks && callbacks->invalidate)
        callbacks->invalidate(callbacks->context);
    if (mm_chunk_payload_cache_clear(cache) != MM_CHUNK_PAYLOAD_OK)
        return MM_CHUNK_PAYLOAD_INVALID;

    status = locate_payload(s, archive, entry->type, entry->number, &cache->block);
    if (status == MM_CHUNK_PAYLOAD_OK && (entry->flags & MM_CHUNK_PACKED))
        status = check_packed_length(&cache->block, limit);
    if (status == MM_CHUNK_PAYLOAD_OK &&
        mm_owned_block_code(&cache->block, 0, s->decode_context, s->decode, NULL) != MM_DATA_OK)
        status = MM_CHUNK_PAYLOAD_DECODE_FAILED;
    if (status == MM_CHUNK_PAYLOAD_OK && callbacks && callbacks->accept &&
        !callbacks->accept(callbacks->context))
        status = MM_CHUNK_PAYLOAD_INVALID;
    if (status == MM_CHUNK_PAYLOAD_OK)
        cache->position = position;
    else
        mm_chunk_payload_cache_clear(cache);
    return status;
}

int mm_chunk_payload_cache_stream(MMChunkPayloadCache *cache, MMBlockIO *io, MMReadStream *stream)
{
    return cache && cache->position != (size_t)-1 && payload_stream(&cache->block, io, stream);
}

static void *find_codec(void *context, MMI32 format)
{
    return format == MM_CODEC_KCDC || format == MM_CODEC_KCD2 ? context : NULL;
}

static int decode(void *codec, MMI32 format, const void *input, MMU32 bytes, void *output,
                  MMU32 capacity, MMI32 *written)
{
    (void)codec;
    return mm_codec_decode(format, input, bytes, output, capacity, written) == MM_DATA_OK;
}

int mm_chunk_payload_open(const MMReadStream *stream, const MMChunkHeader *header, const MMChunkEntry *entry,
                          MMU32 exact_size, MMU32 max_packed_bytes, MMChunkStreamPayload *payload)
{
    MMU32 start = entry->offset, bytes = entry->bytes;
    MMU32 end = header->index_offset;
    unsigned char *packed;
    MMI32 expected, written;
    MMPackedDecoder decoder;
    int status;
    payload->decoded = NULL;
    if ((start < MM_CHUNK_PREFIX_BYTES && (start || bytes)) || start > end || bytes > end - start ||
        !mm_read_slice_init(&payload->slice, stream, start, bytes))
        return MM_CHUNK_BAD_FILE;
    payload->stream = &payload->slice.stream;
    if (!(entry->flags & MM_CHUNK_PACKED))
        return !exact_size || bytes == exact_size ? MM_CHUNK_OK : MM_CHUNK_BAD_FILE;
    if (bytes <= 8 || bytes >= MM_STORAGE_LIMIT || bytes > max_packed_bytes || (size_t)bytes != bytes)
        return MM_CHUNK_BAD_FILE;
    packed = (unsigned char *)malloc((size_t)bytes);
    if (!packed)
        return MM_CHUNK_MEMORY;
    decoder.context = packed;
    decoder.find = find_codec;
    decoder.convert = decode;
    if (!mm_read_exact(payload->stream, 0, packed, (size_t)bytes))
        status = MM_CHUNK_IO;
    else if (mm_packed_decode(&decoder, packed, bytes, NULL, 0, &expected) != MM_DATA_OK ||
             (MMU32)(size_t)expected != (MMU32)expected || (MMU32)expected > max_packed_bytes ||
             (exact_size && (MMU32)expected != exact_size))
        status = MM_CHUNK_BAD_FILE;
    else if (!(payload->decoded = (unsigned char *)malloc((size_t)expected)))
        status = MM_CHUNK_MEMORY;
    else if (mm_packed_decode(&decoder, packed, bytes, payload->decoded, (MMU32)expected, &written) != MM_DATA_OK)
    {
        free(payload->decoded);
        payload->decoded = NULL;
        status = MM_CHUNK_BAD_FILE;
    }
    else
    {
        mm_memory_stream_init(&payload->memory, payload->decoded, (size_t)written);
        payload->stream = &payload->memory.stream;
        status = MM_CHUNK_OK;
    }
    free(packed);
    return status;
}

static int child_ordered(const MMChunkAccess *access, MMU32 parent, MMU32 count)
{
    MMChunkChild child, previous = {0};
    MMU32 i;
    for (i = 0; i < count; ++i)
    {
        if (mm_chunk_child_at(access, parent, i, &child) != MM_CHUNK_OK ||
            (i && (child.slot < previous.slot ||
                   (child.slot == previous.slot && (child.type < previous.type ||
                       (child.type == previous.type && child.number <= previous.number))))))
            return 0;
        previous = child;
    }
    return 1;
}

MMChunkArchiveResult mm_chunk_archive_validate(const MMReadStream *stream, const MMChunkPolicy *policy,
                                               MMU32 max_packed_bytes, int allow_empty)
{
    MMChunkIndex *index;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMChunkStreamPayload payload;
    const MMChunkHeader *header;
    MMChunkArchiveResult result = MM_CHUNK_ARCHIVE_INDEX;
    MMU32 position, remaining;
    unsigned char first;
    int readable;
    if (mm_chunk_index_open(stream, policy, &index) != MM_CHUNK_OK)
        return result;
    access = mm_chunk_index_access(index);
    header = mm_chunk_index_header(index);
    remaining = header->index_offset - MM_CHUNK_PREFIX_BYTES;
    if ((!access.count && !allow_empty) || !mm_chunk_keys_ordered(&access))
        goto done;
    if (mm_chunk_validate_graph(&access, policy->expanded_runtime ? MM_STORAGE_LIMIT : 65535U) != MM_CHUNK_OK)
    {
        result = MM_CHUNK_ARCHIVE_GRAPH;
        goto done;
    }
    for (position = 0; position < access.count; ++position)
    {
        if (mm_chunk_at(&access, position, &entry) != MM_CHUNK_OK)
        {
            result = MM_CHUNK_ARCHIVE_INDEX;
            goto done;
        }
        if (!child_ordered(&access, position, entry.child_count))
        {
            result = MM_CHUNK_ARCHIVE_GRAPH;
            goto done;
        }
        result = MM_CHUNK_ARCHIVE_PAYLOAD;
        if (entry.bytes > remaining ||
            mm_chunk_payload_open(stream, header, &entry, 0, max_packed_bytes, &payload) != MM_CHUNK_OK)
            goto done;
        remaining -= entry.bytes;
        readable = !payload.stream->length || mm_read_exact(payload.stream, 0, &first, 1);
        free(payload.decoded);
        if (!readable)
            goto done;
    }
    result = MM_CHUNK_ARCHIVE_OK;
done:
    mm_chunk_index_close(index);
    return result;
}

int mm_chunk_storage_allocate(const MMChunkStoragePolicy *policy,
                                    const MMChunkStorageServices *services, MMU32 bytes, int force_extra,
                                    MMChunkLocation *output)
{
    MMChunkLocation location;
    int extra;
    if (!policy || !services || !services->file || !services->create_extra || !services->reserve || !output ||
        bytes > policy->max_bytes)
        return MM_CHUNK_BAD_FILE;
    extra = bytes && (force_extra || policy->add_to_extra);
    if (extra && !services->file(services->context, 1) && !services->create_extra(services->context))
        return MM_CHUNK_IO;
    if (!extra && bytes && policy->free_map_unread)
        return MM_CHUNK_BAD_FILE;
    location.file = services->file(services->context, extra);
    location.bytes = bytes;
    location.flags = extra ? 1 : 0;
    location.offset = 0;
    if (bytes && !services->reserve(services->context, extra, bytes, &location.offset))
        return MM_CHUNK_IO;
    *output = location;
    return MM_CHUNK_OK;
}
int mm_chunk_storage_cache(const MMChunkStoragePolicy *policy, const MMChunkStorageServices *services,
                                 MMU32 position, MMChunkLocation *output)
{
    MMChunkEntry entry;
    MMChunkLocation source, destination;
    int result;
    if (!policy || !services || !services->file || !services->entry || !services->cache_place || !services->copy ||
        !services->release_space)
        return MM_CHUNK_BAD_FILE;
    services->entry(services->context, position, &entry);
    if ((entry.flags & 1) || !entry.bytes)
    {
        destination.file = services->file(services->context, 1);
        destination.offset = entry.offset;
        destination.bytes = entry.bytes;
        destination.flags = 1;
    }
    else
    {
        if (services->trace)
            services->trace(services->context, MM_STORAGE_CACHE, &entry);
        source.file = services->file(services->context, 0);
        source.offset = entry.offset;
        source.bytes = entry.bytes;
        source.flags = 0;
        result = mm_chunk_storage_allocate(policy, services, entry.bytes, 1, &destination);
        if (result != MM_CHUNK_OK)
            return result;
        if (!services->copy(services->context, &source, &destination))
        {
            services->release_space(services->context, 1, destination.offset, destination.bytes);
            return MM_CHUNK_IO;
        }
        services->cache_place(services->context, position, destination.offset);
    }
    if (output)
    {
        *output = destination;
        if (services->trace)
        {
            services->entry(services->context, position, &entry);
            services->trace(services->context, MM_STORAGE_FETCH_EXTRA, &entry);
        }
    }
    return MM_CHUNK_OK;
}
int mm_chunk_storage_fetch(const MMChunkStoragePolicy *policy, const MMChunkStorageServices *services,
                                 MMU32 position, MMChunkLocation *output)
{
    MMChunkEntry entry;
    MMChunkLocation location;
    if (!policy || !services || !services->entry || !services->file || !output)
        return MM_CHUNK_BAD_FILE;
    if (policy->read_from_extra &&
        mm_chunk_storage_cache(policy, services, position, output) == MM_CHUNK_OK)
        return MM_CHUNK_OK;
    services->entry(services->context, position, &entry);
    location.file = services->file(services->context, (entry.flags & 1) != 0);
    location.offset = entry.offset;
    location.bytes = entry.bytes;
    location.flags = entry.flags & 1;
    *output = location;
    if (services->trace)
        services->trace(services->context, MM_STORAGE_FETCH_DIRECT, &entry);
    return MM_CHUNK_OK;
}
