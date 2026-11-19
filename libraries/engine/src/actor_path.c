/* Actor path storage converted from the engine's native array adapters.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_path.h"
#include "mm/movie_data.h"
#include <string.h>

MMActorPath *mm_actor_path_create(const MMStorageAllocator *allocator, MMU32 reserve, int trash_unused)
{
    MMCollectionStorage storage;
    MMActorPath *path;
    if (mm_storage_init(&storage, allocator) != MM_STORAGE_OK)
        return NULL;
    path = (MMActorPath *)storage.allocator.resize(storage.allocator.context, NULL, 0,
                                                   sizeof(*path), MM_STORAGE_NORMAL);
    if (!path)
        return NULL;
    path->storage = storage;
    path->storage.min_grow[0] = 128;
    path->rows.storage = &path->storage;
    path->rows.width = MM_ACTOR_ROUTE_BYTES;
    path->rows.count = 0;
    path->rows.free_count = 0;
    path->rows.allocated = 0;
    path->rows.trash_unused = trash_unused;
    if (reserve && mm_array_reserve(&path->rows, reserve, 0) != MM_STORAGE_OK)
    {
        mm_actor_path_release(path);
        return NULL;
    }
    return path;
}

void mm_actor_path_release(MMActorPath *path)
{
    MMStorageAllocator allocator = path->storage.allocator;
    if (mm_storage_dispose(&path->storage) == MM_STORAGE_OK)
        allocator.release(allocator.context, path, sizeof(*path));
}

MMActorPath *mm_actor_path_clone(const MMActorPath *source)
{
    MMActorPath *clone = mm_actor_path_create(&source->storage.allocator, 0, source->rows.trash_unused);
    if (!clone)
        return NULL;
    if (mm_storage_copy(&clone->storage, &source->storage, source->rows.count * MM_ACTOR_ROUTE_BYTES, 0) !=
        MM_STORAGE_OK)
    {
        mm_actor_path_release(clone);
        return NULL;
    }
    clone->rows.count = source->rows.count;
    return clone;
}

int mm_actor_path_get(const MMActorPath *path, MMU32 index, void *row)
{
    if (index >= path->rows.count)
        return 0;
    memcpy(row, path->storage.buffers[0].data + index * MM_ACTOR_ROUTE_BYTES, MM_ACTOR_ROUTE_BYTES);
    return 1;
}

MMI32 mm_actor_path_count(const MMActorPath *path)
{
    return (MMI32)path->rows.count;
}

int mm_actor_path_insert(MMActorPath *path, MMU32 index, const void *row)
{
    return mm_array_insert(&path->rows, index, row, 1) == MM_STORAGE_OK;
}

int mm_actor_path_put(MMActorPath *path, MMU32 index, const void *row)
{
    if (index >= path->rows.count)
        return 0;
    memmove(path->storage.buffers[0].data + index * MM_ACTOR_ROUTE_BYTES, row, MM_ACTOR_ROUTE_BYTES);
    return 1;
}

int mm_actor_path_copy(MMActorPath *destination, MMU32 to, const MMActorPath *source, MMU32 first, MMU32 count)
{
    if (to > destination->rows.count || count > destination->rows.count - to ||
        first > source->rows.count || count > source->rows.count - first)
        return 0;
    if (count)
        memmove(destination->storage.buffers[0].data + to * MM_ACTOR_ROUTE_BYTES,
                source->storage.buffers[0].data + first * MM_ACTOR_ROUTE_BYTES, count * MM_ACTOR_ROUTE_BYTES);
    return 1;
}

int mm_actor_path_layout(MMU32 count, int system, MMCollectionHeader *header)
{
    const unsigned short one = 1;
    memset(header, 0, sizeof(*header));
    header->kind = MM_COLLECTION_ARRAY;
    header->system = system;
    header->big_endian = *(const unsigned char *)&one == 0;
    header->count = count;
    header->width = MM_ACTOR_ROUTE_BYTES;
    return mm_collection_layout(header) == MM_STORAGE_OK;
}

MMU32 mm_actor_path_wire_size(const MMActorPath *path)
{
    MMCollectionHeader header;
    if (!mm_actor_path_layout(path->rows.count, 0, &header))
        return 0;
    return header.header_bytes + header.first_bytes;
}

int mm_actor_path_write(MMActorPath *path, int system, const MMStorageWriter *writer)
{
    MMCollectionHeader header;
    unsigned char bytes[12];
    return mm_actor_path_layout(path->rows.count, system, &header) &&
           mm_collection_header_write(&header, bytes, sizeof(bytes)) == MM_STORAGE_OK &&
           mm_storage_write(&path->storage, writer, bytes, header.header_bytes, header.first_bytes, 0) == MM_STORAGE_OK;
}

static void *create_read_path(void *context, MMU32 count, void **rows)
{
    const MMActorPathFactory *factory = (const MMActorPathFactory *)context;
    MMActorPath *path = mm_actor_path_create(factory->allocator, 0, factory->trash_unused);
    if (!path)
        return NULL;
    if (mm_array_set_count(&path->rows, count) != MM_STORAGE_OK)
    {
        mm_actor_path_release(path);
        return NULL;
    }
    *rows = path->storage.buffers[0].data;
    return path;
}

static void release_read_path(void *path)
{
    mm_actor_path_release((MMActorPath *)path);
}

MMActorRouteReader mm_actor_path_reader(const MMActorPathFactory *factory)
{
    MMActorRouteReader reader = {NULL, create_read_path, release_read_path};
    reader.context = (void *)factory;
    return reader;
}

MMActorPath *mm_actor_path_read(const MMReadStream *stream, const MMActorPathFactory *factory)
{
    MMActorRouteReader reader = mm_actor_path_reader(factory);
    void *path;
    return mm_actor_read_route_stream(stream, &reader, &path) == MM_MOVIE_OK ? (MMActorPath *)path : NULL;
}
