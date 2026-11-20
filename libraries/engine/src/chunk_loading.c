/* Chunk loading converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_loading.h"
#include <string.h>
typedef char RangeMustBeEightBytes[sizeof(MMChunkFreeRange) == 8 ? 1 : -1];
typedef char ChildMustBeTwelveBytes[sizeof(MMChunkChild) == 12 ? 1 : -1];
static MMArray map_view(MMChunkFreeMap *map)
{
    MMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &map->storage;
    a.width = 8;
    a.count = map->count;
    a.free_count = -1;
    return a;
}
int mm_free_map_create(const MMStorageAllocator *allocator, MMU32 reserve, MMChunkFreeMap **output)
{
    MMCollectionStorage empty;
    MMChunkFreeMap *map;
    MMArray a;
    if (!output || reserve >= MM_STORAGE_LIMIT / 8 ||
        mm_storage_init(&empty, allocator) != MM_STORAGE_OK)
        return MM_CHUNK_BAD_FILE;
    map = (MMChunkFreeMap *)empty.allocator.resize(empty.allocator.context, NULL, 0, sizeof(*map),
                                                        MM_STORAGE_NORMAL);
    if (!map)
        return MM_CHUNK_MEMORY;
    map->storage = empty;
    map->count = 0;
    map->storage.min_grow[0] = 128;
    a = map_view(map);
    if (mm_array_reserve(&a, reserve, 0) != MM_STORAGE_OK)
    {
        mm_free_map_close(map);
        return MM_CHUNK_MEMORY;
    }
    *output = map;
    return MM_CHUNK_OK;
}
void mm_free_map_close(MMChunkFreeMap *map)
{
    MMStorageAllocator allocator;
    if (!map)
        return;
    allocator = map->storage.allocator;
    if (mm_storage_dispose(&map->storage) != MM_STORAGE_OK)
        return;
    allocator.release(allocator.context, map, sizeof(*map));
}
int mm_free_map_get(const MMChunkFreeMap *map, MMU32 position, MMChunkFreeRange *range)
{
    if (!map || !range || position >= map->count || map->count > map->storage.buffers[0].capacity / 8)
        return MM_CHUNK_BAD_FILE;
    memcpy(range, map->storage.buffers[0].data + position * 8, 8);
    return MM_CHUNK_OK;
}
int mm_free_map_put(MMChunkFreeMap *map, MMU32 position, const MMChunkFreeRange *range)
{
    if (!map || !range || position >= map->count || map->count > map->storage.buffers[0].capacity / 8)
        return MM_CHUNK_BAD_FILE;
    memcpy(map->storage.buffers[0].data + position * 8, range, 8);
    return MM_CHUNK_OK;
}
int mm_free_map_insert(MMChunkFreeMap *map, MMU32 position, const MMChunkFreeRange *range)
{
    MMArray a;
    int result;
    if (!map || !range)
        return MM_CHUNK_BAD_FILE;
    a = map_view(map);
    result = mm_array_insert(&a, position, range, 1);
    map->count = a.count;
    return result == MM_STORAGE_OK       ? MM_CHUNK_OK
           : result == MM_STORAGE_MEMORY ? MM_CHUNK_MEMORY
                                               : MM_CHUNK_BAD_FILE;
}
int mm_free_map_erase(MMChunkFreeMap *map, MMU32 position)
{
    MMArray a;
    int result;
    if (!map)
        return MM_CHUNK_BAD_FILE;
    a = map_view(map);
    result = mm_array_delete(&a, position, 1);
    map->count = a.count;
    return result == MM_STORAGE_OK ? MM_CHUNK_OK : MM_CHUNK_BAD_FILE;
}
static MMU32 word32(const unsigned char *p, int big)
{
    MMU32 value = 0;
    unsigned i;
    for (i = 0; i < 4; ++i)
        value |= (MMU32)p[i] << (8 * (big ? 3 - i : i));
    return value;
}
int mm_free_map_read(const MMReadStream *stream, const MMStorageAllocator *allocator,
                           MMChunkFreeMap **output)
{
    MMCollectionHeader header;
    MMChunkFreeMap *map;
    MMChunkFreeRange range;
    MMU32 i;
    int result;
    if (!output)
        return MM_CHUNK_BAD_FILE;
    result = mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, &header);
    if (result != MM_STORAGE_OK || header.width != 8)
        return result == MM_STORAGE_IO ? MM_CHUNK_IO : MM_CHUNK_BAD_FILE;
    result = mm_free_map_create(allocator, 0, &map);
    if (result != MM_CHUNK_OK)
        return result;
    result = mm_storage_read(&map->storage, stream, 12, header.first_bytes, 0);
    if (result != MM_STORAGE_OK)
    {
        mm_free_map_close(map);
        return result == MM_STORAGE_MEMORY ? MM_CHUNK_MEMORY : MM_CHUNK_IO;
    }
    map->count = header.count;
    for (i = 0; i < map->count; ++i)
    {
        const unsigned char *p = map->storage.buffers[0].data + i * 8;
        range.offset = word32(p, header.big_endian);
        range.bytes = word32(p + 4, header.big_endian);
        memcpy(map->storage.buffers[0].data + i * 8, &range, 8);
    }
    *output = map;
    return MM_CHUNK_OK;
}
int mm_free_map_load(MMChunkFreeMap **map, int *unread, const MMReadStream *stream, MMU32 offset,
                           MMU32 bytes, const MMStorageAllocator *allocator)
{
    MMReadSlice slice;
    if (!map || !unread)
        return MM_CHUNK_BAD_FILE;
    *unread = 0;
    if (*map)
        return MM_CHUNK_BAD_FILE;
    if (!bytes)
        return MM_CHUNK_OK;
    if (!mm_read_slice_init(&slice, stream, offset, bytes))
        return MM_CHUNK_BAD_FILE;
    return mm_free_map_read(&slice.stream, allocator, map);
}
MMU32 mm_free_map_wire_size(const MMChunkFreeMap *map)
{
    return map ? 12 + map->count * 8 : 0;
}
int mm_free_map_write(MMChunkFreeMap *map, const MMStorageWriter *writer, int big, int system)
{
    MMCollectionHeader header;
    MMCollectionStorage temporary;
    unsigned char prefix[12];
    const unsigned short one = 1;
    MMU32 i, n, value;
    int result;
    if (!map)
        return MM_STORAGE_ARGUMENT;
    memset(&header, 0, sizeof(header));
    header.kind = MM_COLLECTION_ARRAY;
    header.width = 8;
    header.count = map->count;
    header.big_endian = big;
    header.system = system;
    if (mm_collection_header_write(&header, prefix, sizeof(prefix)) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    if (big == (*(const unsigned char *)&one == 0))
        return mm_storage_write(&map->storage, writer, prefix, 12, map->count * 8, 0);
    mm_storage_init(&temporary, &map->storage.allocator);
    result = mm_storage_ensure(&temporary, map->count * 8, 0, 0);
    if (result == MM_STORAGE_OK)
    {
        for (i = 0; i < map->count * 2; ++i)
        {
            memcpy(&value, map->storage.buffers[0].data + i * 4, 4);
            for (n = 0; n < 4; ++n)
                temporary.buffers[0].data[i * 4 + n] = (unsigned char)(value >> (8 * (big ? 3 - n : n)));
        }
        result = mm_storage_write(&temporary, writer, prefix, 12, map->count * 8, 0);
    }
    mm_storage_dispose(&temporary);
    return result;
}
int mm_chunk_serialized_name(const MMChunkIndex *index, MMU32 position, MMChunkLegacyName convert, void *context,
                              unsigned char scratch[MM_CHUNK_NAME_WIRE_BYTES],
                              const unsigned char **out, MMU32 *count)
{
    const unsigned char *name;
    char legacy[256];
    MMU32 n;
    if (mm_chunk_index_name(index, position, &name, &n) != MM_CHUNK_OK)
        return MM_CHUNK_BAD_FILE;
    if (mm_chunk_index_header(index)->version < 3 && n)
    {
        if (n >= sizeof(legacy))
            n = 0;
        else
        {
            memcpy(legacy, name, n);
            legacy[n] = 0;
            if (!convert || !convert(context, legacy, scratch, MM_CHUNK_NAME_WIRE_BYTES, &n) ||
                n > MM_CHUNK_NAME_WIRE_BYTES)
                return MM_CHUNK_BAD_FILE;
            name = scratch;
        }
    }
    *out = name;
    *count = n;
    return MM_CHUNK_OK;
}

int mm_chunk_materialize(const MMChunkIndex *index, const MMChunkMaterializer *host,
                         const MMStorageAllocator *allocator)
{
    MMCollectionStorage scratch;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMChunkChild child;
    const unsigned char *name;
    unsigned char converted[MM_CHUNK_NAME_WIRE_BYTES];
    MMU32 i, j, n, bytes;
    int result = MM_CHUNK_OK;
    if (!index || !host || !host->insert)
        return MM_CHUNK_BAD_FILE;
    if (mm_storage_init(&scratch, allocator) != MM_STORAGE_OK)
        return MM_CHUNK_BAD_FILE;
    access = mm_chunk_index_access(index);
    for (i = 0; i < access.count; ++i)
    {
        if (mm_chunk_at(&access, i, &entry) != MM_CHUNK_OK ||
            mm_chunk_serialized_name(index, i, host->legacy_name, host->context, converted, &name, &n) != MM_CHUNK_OK)
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        bytes = entry.child_count * 12 + n;
        if (mm_storage_ensure(&scratch, bytes, 0, 0) != MM_STORAGE_OK)
        {
            result = MM_CHUNK_MEMORY;
            goto done;
        }
        for (j = 0; j < entry.child_count; ++j)
        {
            if (mm_chunk_child_at(&access, i, j, &child) != MM_CHUNK_OK)
            {
                result = MM_CHUNK_BAD_FILE;
                goto done;
            }
            memcpy(scratch.buffers[0].data + j * 12, &child, 12);
        }
        if (n)
            memcpy(scratch.buffers[0].data + entry.child_count * 12, name, n);
        if (!host->insert(host->context, i, &entry, scratch.buffers[0].data, bytes))
        {
            result = MM_CHUNK_MEMORY;
            goto done;
        }
    }
done:
    mm_storage_dispose(&scratch);
    return result;
}
