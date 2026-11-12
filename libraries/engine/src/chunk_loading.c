/* Chunk loading converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_loading.h"
#include <string.h>
typedef char RangeMustBeEightBytes[sizeof(ThreeMMChunkFreeRange) == 8 ? 1 : -1];
typedef char ChildMustBeTwelveBytes[sizeof(ThreeMMChunkChild) == 12 ? 1 : -1];
static ThreeMMArray map_view(ThreeMMChunkFreeMap *map)
{
    ThreeMMArray a;
    memset(&a, 0, sizeof(a));
    a.storage = &map->storage;
    a.width = 8;
    a.count = map->count;
    a.free_count = -1;
    return a;
}
int three_mm_free_map_create(const ThreeMMStorageAllocator *allocator, ThreeMMU32 reserve, ThreeMMChunkFreeMap **output)
{
    ThreeMMCollectionStorage empty;
    ThreeMMChunkFreeMap *map;
    ThreeMMArray a;
    if (!output || reserve >= THREE_MM_STORAGE_LIMIT / 8 ||
        three_mm_storage_init(&empty, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    map = (ThreeMMChunkFreeMap *)empty.allocator.resize(empty.allocator.context, NULL, 0, sizeof(*map),
                                                        THREE_MM_STORAGE_NORMAL);
    if (!map)
        return THREE_MM_CHUNK_MEMORY;
    map->storage = empty;
    map->count = 0;
    map->storage.min_grow[0] = 128;
    a = map_view(map);
    if (three_mm_array_reserve(&a, reserve, 0) != THREE_MM_STORAGE_OK)
    {
        three_mm_free_map_close(map);
        return THREE_MM_CHUNK_MEMORY;
    }
    *output = map;
    return THREE_MM_CHUNK_OK;
}
void three_mm_free_map_close(ThreeMMChunkFreeMap *map)
{
    ThreeMMStorageAllocator allocator;
    if (!map)
        return;
    allocator = map->storage.allocator;
    if (three_mm_storage_dispose(&map->storage) != THREE_MM_STORAGE_OK)
        return;
    allocator.release(allocator.context, map, sizeof(*map));
}
int three_mm_free_map_get(const ThreeMMChunkFreeMap *map, ThreeMMU32 position, ThreeMMChunkFreeRange *range)
{
    if (!map || !range || position >= map->count || map->count > map->storage.buffers[0].capacity / 8)
        return THREE_MM_CHUNK_BAD_FILE;
    memcpy(range, map->storage.buffers[0].data + position * 8, 8);
    return THREE_MM_CHUNK_OK;
}
int three_mm_free_map_put(ThreeMMChunkFreeMap *map, ThreeMMU32 position, const ThreeMMChunkFreeRange *range)
{
    if (!map || !range || position >= map->count || map->count > map->storage.buffers[0].capacity / 8)
        return THREE_MM_CHUNK_BAD_FILE;
    memcpy(map->storage.buffers[0].data + position * 8, range, 8);
    return THREE_MM_CHUNK_OK;
}
int three_mm_free_map_insert(ThreeMMChunkFreeMap *map, ThreeMMU32 position, const ThreeMMChunkFreeRange *range)
{
    ThreeMMArray a;
    int result;
    if (!map || !range)
        return THREE_MM_CHUNK_BAD_FILE;
    a = map_view(map);
    result = three_mm_array_insert(&a, position, range, 1);
    map->count = a.count;
    return result == THREE_MM_STORAGE_OK       ? THREE_MM_CHUNK_OK
           : result == THREE_MM_STORAGE_MEMORY ? THREE_MM_CHUNK_MEMORY
                                               : THREE_MM_CHUNK_BAD_FILE;
}
int three_mm_free_map_erase(ThreeMMChunkFreeMap *map, ThreeMMU32 position)
{
    ThreeMMArray a;
    int result;
    if (!map)
        return THREE_MM_CHUNK_BAD_FILE;
    a = map_view(map);
    result = three_mm_array_delete(&a, position, 1);
    map->count = a.count;
    return result == THREE_MM_STORAGE_OK ? THREE_MM_CHUNK_OK : THREE_MM_CHUNK_BAD_FILE;
}
static ThreeMMU32 word32(const unsigned char *p, int big)
{
    ThreeMMU32 value = 0;
    unsigned i;
    for (i = 0; i < 4; ++i)
        value |= (ThreeMMU32)p[i] << (8 * (big ? 3 - i : i));
    return value;
}
int three_mm_free_map_read(const ThreeMMReadStream *stream, const ThreeMMStorageAllocator *allocator,
                           ThreeMMChunkFreeMap **output)
{
    ThreeMMCollectionHeader header;
    ThreeMMChunkFreeMap *map;
    ThreeMMChunkFreeRange range;
    ThreeMMU32 i;
    int result;
    if (!output)
        return THREE_MM_CHUNK_BAD_FILE;
    result = three_mm_collection_header_read(stream, THREE_MM_COLLECTION_ARRAY, 0, &header);
    if (result != THREE_MM_STORAGE_OK || header.width != 8)
        return result == THREE_MM_STORAGE_IO ? THREE_MM_CHUNK_IO : THREE_MM_CHUNK_BAD_FILE;
    result = three_mm_free_map_create(allocator, 0, &map);
    if (result != THREE_MM_CHUNK_OK)
        return result;
    result = three_mm_storage_read(&map->storage, stream, 12, header.first_bytes, 0);
    if (result != THREE_MM_STORAGE_OK)
    {
        three_mm_free_map_close(map);
        return result == THREE_MM_STORAGE_MEMORY ? THREE_MM_CHUNK_MEMORY : THREE_MM_CHUNK_IO;
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
    return THREE_MM_CHUNK_OK;
}
int three_mm_free_map_load(ThreeMMChunkFreeMap **map, int *unread, const ThreeMMReadStream *stream, ThreeMMU32 offset,
                           ThreeMMU32 bytes, const ThreeMMStorageAllocator *allocator)
{
    ThreeMMReadSlice slice;
    if (!map || !unread)
        return THREE_MM_CHUNK_BAD_FILE;
    *unread = 0;
    if (*map)
        return THREE_MM_CHUNK_BAD_FILE;
    if (!bytes)
        return THREE_MM_CHUNK_OK;
    if (!three_mm_read_slice_init(&slice, stream, offset, bytes))
        return THREE_MM_CHUNK_BAD_FILE;
    return three_mm_free_map_read(&slice.stream, allocator, map);
}
ThreeMMU32 three_mm_free_map_wire_size(const ThreeMMChunkFreeMap *map)
{
    return map ? 12 + map->count * 8 : 0;
}
int three_mm_free_map_write(ThreeMMChunkFreeMap *map, const ThreeMMStorageWriter *writer, int big, int system)
{
    ThreeMMCollectionHeader header;
    ThreeMMCollectionStorage temporary;
    unsigned char prefix[12];
    const unsigned short one = 1;
    ThreeMMU32 i, n, value;
    int result;
    if (!map)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(&header, 0, sizeof(header));
    header.kind = THREE_MM_COLLECTION_ARRAY;
    header.width = 8;
    header.count = map->count;
    header.big_endian = big;
    header.system = system;
    if (three_mm_collection_header_write(&header, prefix, sizeof(prefix)) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    if (big == (*(const unsigned char *)&one == 0))
        return three_mm_storage_write(&map->storage, writer, prefix, 12, map->count * 8, 0);
    three_mm_storage_init(&temporary, &map->storage.allocator);
    result = three_mm_storage_ensure(&temporary, map->count * 8, 0, 0);
    if (result == THREE_MM_STORAGE_OK)
    {
        for (i = 0; i < map->count * 2; ++i)
        {
            memcpy(&value, map->storage.buffers[0].data + i * 4, 4);
            for (n = 0; n < 4; ++n)
                temporary.buffers[0].data[i * 4 + n] = (unsigned char)(value >> (8 * (big ? 3 - n : n)));
        }
        result = three_mm_storage_write(&temporary, writer, prefix, 12, map->count * 8, 0);
    }
    three_mm_storage_dispose(&temporary);
    return result;
}
int three_mm_chunk_materialize(ThreeMMChunkIndex *index, const ThreeMMChunkMaterializer *host,
                               const ThreeMMStorageAllocator *allocator)
{
    ThreeMMCollectionStorage scratch;
    ThreeMMChunkAccess access;
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild child;
    const ThreeMMChunkHeader *header;
    const unsigned char *name;
    unsigned char converted[516];
    char legacy[256];
    ThreeMMU32 i, j, n, bytes;
    int result = THREE_MM_CHUNK_OK;
    if (!index || !host || !host->insert)
        return THREE_MM_CHUNK_BAD_FILE;
    if (three_mm_storage_init(&scratch, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    access = three_mm_chunk_index_access(index);
    header = three_mm_chunk_index_header(index);
    for (i = 0; i < access.count; ++i)
    {
        if (three_mm_chunk_at(&access, i, &entry) != THREE_MM_CHUNK_OK ||
            three_mm_chunk_index_name(index, i, &name, &n) != THREE_MM_CHUNK_OK)
        {
            result = THREE_MM_CHUNK_BAD_FILE;
            goto done;
        }
        if (header->version < 3 && n)
        {
            if (n >= sizeof(legacy))
                n = 0;
            else
            {
                memcpy(legacy, name, n);
                legacy[n] = 0;
                if (!host->legacy_name || !host->legacy_name(host->context, legacy, converted, sizeof(converted), &n) ||
                    n > sizeof(converted))
                {
                    result = THREE_MM_CHUNK_BAD_FILE;
                    goto done;
                }
                name = converted;
            }
        }
        bytes = entry.child_count * 12 + n;
        if (three_mm_storage_ensure(&scratch, bytes, 0, 0) != THREE_MM_STORAGE_OK)
        {
            result = THREE_MM_CHUNK_MEMORY;
            goto done;
        }
        for (j = 0; j < entry.child_count; ++j)
        {
            if (three_mm_chunk_child_at(&access, i, j, &child) != THREE_MM_CHUNK_OK)
            {
                result = THREE_MM_CHUNK_BAD_FILE;
                goto done;
            }
            memcpy(scratch.buffers[0].data + j * 12, &child, 12);
        }
        if (n)
            memcpy(scratch.buffers[0].data + entry.child_count * 12, name, n);
        if (!host->insert(host->context, i, &entry, scratch.buffers[0].data, bytes))
        {
            result = THREE_MM_CHUNK_MEMORY;
            goto done;
        }
    }
done:
    three_mm_storage_dispose(&scratch);
    return result;
}
