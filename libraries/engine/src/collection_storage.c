/* Collection storage converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/collection_storage.h"
#include <stdlib.h>
#include <string.h>

static void *heap_resize(void *context, void *data, MMU32 old_bytes, MMU32 new_bytes, int priority)
{
    (void)context;
    (void)old_bytes;
    (void)priority;
    if ((size_t)new_bytes != new_bytes)
        return NULL;
    return realloc(data, (size_t)new_bytes);
}
static void heap_release(void *context, void *data, MMU32 bytes)
{
    (void)context;
    (void)bytes;
    free(data);
}
static int valid(const MMCollectionStorage *storage)
{
    unsigned i;
    if (!storage || !storage->allocator.resize || !storage->allocator.release)
        return 0;
    for (i = 0; i < 2; ++i)
        if (storage->buffers[i].capacity >= MM_STORAGE_LIMIT ||
            (!storage->buffers[i].data != !storage->buffers[i].capacity) ||
            (!storage->buffers[i].data && storage->buffers[i].locks))
            return 0;
    return 1;
}
int mm_storage_init(MMCollectionStorage *storage, const MMStorageAllocator *allocator)
{
    MMCollectionStorage fresh;
    if (!storage || (allocator && (!allocator->resize || !allocator->release)))
        return MM_STORAGE_ARGUMENT;
    memset(&fresh, 0, sizeof(fresh));
    if (allocator)
        fresh.allocator = *allocator;
    else
    {
        fresh.allocator.resize = heap_resize;
        fresh.allocator.release = heap_release;
    }
    *storage = fresh;
    return MM_STORAGE_OK;
}
int mm_storage_dispose(MMCollectionStorage *storage)
{
    unsigned i;
    if (!valid(storage))
        return MM_STORAGE_ARGUMENT;
    if (storage->buffers[0].locks || storage->buffers[1].locks)
        return MM_STORAGE_LOCKED;
    for (i = 0; i < 2; ++i)
    {
        MMStorageBuffer *buffer = &storage->buffers[i];
        if (buffer->data)
            storage->allocator.release(storage->allocator.context, buffer->data, buffer->capacity);
        memset(buffer, 0, sizeof(*buffer));
    }
    return MM_STORAGE_OK;
}
static int resize(MMCollectionStorage *storage, unsigned section, MMU32 bytes, int priority)
{
    MMStorageBuffer *buffer = &storage->buffers[section];
    void *replacement;
    if (buffer->locks)
        return MM_STORAGE_LOCKED;
    if (!bytes)
    {
        if (buffer->data)
            storage->allocator.release(storage->allocator.context, buffer->data, buffer->capacity);
        buffer->data = NULL;
        buffer->capacity = 0;
        return MM_STORAGE_OK;
    }
    replacement =
        storage->allocator.resize(storage->allocator.context, buffer->data, buffer->capacity, bytes, priority);
    if (!replacement)
        return MM_STORAGE_MEMORY;
    buffer->data = (unsigned char *)replacement;
    buffer->capacity = bytes;
    return MM_STORAGE_OK;
}
int mm_storage_resize_exact(MMCollectionStorage *storage, unsigned section, MMU32 bytes, int priority)
{
    if (!valid(storage) || section > 1 || bytes >= MM_STORAGE_LIMIT ||
        (priority != MM_STORAGE_SPEED && priority != MM_STORAGE_NORMAL))
        return MM_STORAGE_ARGUMENT;
    return resize(storage, section, bytes, priority);
}
int mm_storage_ensure(MMCollectionStorage *storage, MMU32 first, MMU32 second, int shrink)
{
    MMU32 sizes[2], extra, candidate;
    unsigned i;
    int result;
    if (!valid(storage) || first >= MM_STORAGE_LIMIT || second >= MM_STORAGE_LIMIT)
        return MM_STORAGE_ARGUMENT;
    sizes[0] = first;
    sizes[1] = second;
    if (shrink)
        for (i = 0; i < 2; ++i)
            if (sizes[i] < storage->buffers[i].capacity)
            {
                result = resize(storage, i, sizes[i], MM_STORAGE_NORMAL);
                if (result == MM_STORAGE_LOCKED)
                    return result;
                /* A failed shrink leaves the old capacity authoritative. */
            }
    for (i = 0; i < 2; ++i)
    {
        MMStorageBuffer *buffer = &storage->buffers[i];
        if (sizes[i] <= buffer->capacity)
            continue;
        if (buffer->locks)
            return MM_STORAGE_LOCKED;
        extra = storage->min_grow[i];
        if (extra < MM_STORAGE_LIMIT - buffer->capacity)
        {
            candidate = buffer->capacity + extra;
            if (candidate > sizes[i] && resize(storage, i, candidate, MM_STORAGE_SPEED) == MM_STORAGE_OK)
                continue;
        }
        result = resize(storage, i, sizes[i], MM_STORAGE_NORMAL);
        if (result != MM_STORAGE_OK)
            return result;
    }
    return MM_STORAGE_OK;
}
int mm_storage_lock(MMCollectionStorage *storage, unsigned section)
{
    MMStorageBuffer *buffer;
    if (!valid(storage) || section > 1)
        return MM_STORAGE_ARGUMENT;
    buffer = &storage->buffers[section];
    if (!buffer->data)
        return MM_STORAGE_OK;
    if (buffer->locks == (MMU32)0xffffffffUL)
        return MM_STORAGE_LOCKED;
    ++buffer->locks;
    return MM_STORAGE_OK;
}
int mm_storage_unlock(MMCollectionStorage *storage, unsigned section)
{
    MMStorageBuffer *buffer;
    if (!valid(storage) || section > 1)
        return MM_STORAGE_ARGUMENT;
    buffer = &storage->buffers[section];
    if (!buffer->data)
        return MM_STORAGE_OK;
    if (!buffer->locks)
        return MM_STORAGE_LOCKED;
    --buffer->locks;
    return MM_STORAGE_OK;
}
int mm_storage_copy(MMCollectionStorage *destination, const MMCollectionStorage *source,
                          MMU32 first, MMU32 second)
{
    int result;
    if (!valid(source) || !valid(destination) || source == destination || first > source->buffers[0].capacity ||
        second > source->buffers[1].capacity)
        return MM_STORAGE_ARGUMENT;
    result = mm_storage_ensure(destination, first, second, 1);
    if (result != MM_STORAGE_OK)
        return result;
    if (first)
        memcpy(destination->buffers[0].data, source->buffers[0].data, first);
    if (second)
        memcpy(destination->buffers[1].data, source->buffers[1].data, second);
    destination->min_grow[0] = source->min_grow[0];
    destination->min_grow[1] = source->min_grow[1];
    return MM_STORAGE_OK;
}
int mm_storage_write(MMCollectionStorage *storage, const MMStorageWriter *writer, const void *header,
                           MMU32 header_bytes, MMU32 first, MMU32 second)
{
    MMU32 sizes[2], offset;
    unsigned i;
    int result, ok;
    if (!valid(storage) || !writer || !writer->write_at || (!header && header_bytes) ||
        first > storage->buffers[0].capacity || second > storage->buffers[1].capacity ||
        header_bytes > writer->length || first > writer->length - header_bytes ||
        second != writer->length - header_bytes - first)
        return MM_STORAGE_ARGUMENT;
    if (header_bytes && !writer->write_at(writer->context, 0, header, header_bytes))
        return MM_STORAGE_IO;
    offset = header_bytes;
    sizes[0] = first;
    sizes[1] = second;
    for (i = 0; i < 2; ++i)
    {
        if (!sizes[i])
            continue;
        result = mm_storage_lock(storage, i);
        if (result != MM_STORAGE_OK)
            return result;
        ok = writer->write_at(writer->context, offset, storage->buffers[i].data, sizes[i]);
        mm_storage_unlock(storage, i);
        if (!ok)
            return MM_STORAGE_IO;
        offset += sizes[i];
    }
    return MM_STORAGE_OK;
}
int mm_storage_read(MMCollectionStorage *storage, const MMReadStream *reader, MMU32 offset,
                          MMU32 first, MMU32 second)
{
    MMU32 sizes[2];
    unsigned i;
    int result, ok;
    if (!valid(storage) || !reader || offset > reader->length || first > reader->length - offset ||
        second > reader->length - offset - first)
        return MM_STORAGE_ARGUMENT;
    if (!first && !second)
        return MM_STORAGE_OK;
    result = mm_storage_ensure(storage, first, second, 0);
    if (result != MM_STORAGE_OK)
        return result;
    sizes[0] = first;
    sizes[1] = second;
    for (i = 0; i < 2; ++i)
    {
        if (!sizes[i])
            continue;
        result = mm_storage_lock(storage, i);
        if (result != MM_STORAGE_OK)
            return result;
        ok = mm_read_exact(reader, offset, storage->buffers[i].data, sizes[i]);
        mm_storage_unlock(storage, i);
        if (!ok)
            return MM_STORAGE_IO;
        offset += sizes[i];
    }
    return MM_STORAGE_OK;
}
