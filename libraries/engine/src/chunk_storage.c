/* Chunk storage policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_storage.h"
int three_mm_chunk_storage_allocate(const ThreeMMChunkStoragePolicy *policy,
                                    const ThreeMMChunkStorageServices *services, ThreeMMU32 bytes, int force_extra,
                                    ThreeMMChunkLocation *output)
{
    ThreeMMChunkLocation location;
    int extra;
    if (!policy || !services || !services->file || !services->create_extra || !services->reserve || !output ||
        bytes > policy->max_bytes)
        return THREE_MM_CHUNK_BAD_FILE;
    extra = bytes && (force_extra || policy->add_to_extra);
    if (extra && !services->file(services->context, 1) && !services->create_extra(services->context))
        return THREE_MM_CHUNK_IO;
    if (!extra && bytes && policy->free_map_unread)
        return THREE_MM_CHUNK_BAD_FILE;
    location.file = services->file(services->context, extra);
    location.bytes = bytes;
    location.flags = extra ? 1 : 0;
    location.offset = 0;
    if (bytes && !services->reserve(services->context, extra, bytes, &location.offset))
        return THREE_MM_CHUNK_IO;
    *output = location;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_storage_cache(const ThreeMMChunkStoragePolicy *policy, const ThreeMMChunkStorageServices *services,
                                 ThreeMMU32 position, ThreeMMChunkLocation *output)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkLocation source, destination;
    int result;
    if (!policy || !services || !services->file || !services->entry || !services->cache_place || !services->copy ||
        !services->release_space)
        return THREE_MM_CHUNK_BAD_FILE;
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
            services->trace(services->context, THREE_MM_STORAGE_CACHE, &entry);
        source.file = services->file(services->context, 0);
        source.offset = entry.offset;
        source.bytes = entry.bytes;
        source.flags = 0;
        result = three_mm_chunk_storage_allocate(policy, services, entry.bytes, 1, &destination);
        if (result != THREE_MM_CHUNK_OK)
            return result;
        if (!services->copy(services->context, &source, &destination))
        {
            services->release_space(services->context, 1, destination.offset, destination.bytes);
            return THREE_MM_CHUNK_IO;
        }
        services->cache_place(services->context, position, destination.offset);
    }
    if (output)
    {
        *output = destination;
        if (services->trace)
        {
            services->entry(services->context, position, &entry);
            services->trace(services->context, THREE_MM_STORAGE_FETCH_EXTRA, &entry);
        }
    }
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_storage_fetch(const ThreeMMChunkStoragePolicy *policy, const ThreeMMChunkStorageServices *services,
                                 ThreeMMU32 position, ThreeMMChunkLocation *output)
{
    ThreeMMChunkEntry entry;
    ThreeMMChunkLocation location;
    if (!policy || !services || !services->entry || !services->file || !output)
        return THREE_MM_CHUNK_BAD_FILE;
    if (policy->read_from_extra &&
        three_mm_chunk_storage_cache(policy, services, position, output) == THREE_MM_CHUNK_OK)
        return THREE_MM_CHUNK_OK;
    services->entry(services->context, position, &entry);
    location.file = services->file(services->context, (entry.flags & 1) != 0);
    location.offset = entry.offset;
    location.bytes = entry.bytes;
    location.flags = entry.flags & 1;
    *output = location;
    if (services->trace)
        services->trace(services->context, THREE_MM_STORAGE_FETCH_DIRECT, &entry);
    return THREE_MM_CHUNK_OK;
}
