/* Collection storage converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/collection_storage.h"
#include <stdlib.h>
#include <string.h>

static void *heap_resize(void *context, void *data, ThreeMMU32 old_bytes, ThreeMMU32 new_bytes, int priority)
{
    (void)context;
    (void)old_bytes;
    (void)priority;
    if ((size_t)new_bytes != new_bytes)
        return NULL;
    return realloc(data, (size_t)new_bytes);
}
static void heap_release(void *context, void *data, ThreeMMU32 bytes)
{
    (void)context;
    (void)bytes;
    free(data);
}
static int valid(const ThreeMMCollectionStorage *storage)
{
    unsigned i;
    if (!storage || !storage->allocator.resize || !storage->allocator.release)
        return 0;
    for (i = 0; i < 2; ++i)
        if (storage->buffers[i].capacity >= THREE_MM_STORAGE_LIMIT ||
            (!storage->buffers[i].data != !storage->buffers[i].capacity) ||
            (!storage->buffers[i].data && storage->buffers[i].locks))
            return 0;
    return 1;
}
int three_mm_storage_init(ThreeMMCollectionStorage *storage, const ThreeMMStorageAllocator *allocator)
{
    ThreeMMCollectionStorage fresh;
    if (!storage || (allocator && (!allocator->resize || !allocator->release)))
        return THREE_MM_STORAGE_ARGUMENT;
    memset(&fresh, 0, sizeof(fresh));
    if (allocator)
        fresh.allocator = *allocator;
    else
    {
        fresh.allocator.resize = heap_resize;
        fresh.allocator.release = heap_release;
    }
    *storage = fresh;
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_dispose(ThreeMMCollectionStorage *storage)
{
    unsigned i;
    if (!valid(storage))
        return THREE_MM_STORAGE_ARGUMENT;
    if (storage->buffers[0].locks || storage->buffers[1].locks)
        return THREE_MM_STORAGE_LOCKED;
    for (i = 0; i < 2; ++i)
    {
        ThreeMMStorageBuffer *buffer = &storage->buffers[i];
        if (buffer->data)
            storage->allocator.release(storage->allocator.context, buffer->data, buffer->capacity);
        memset(buffer, 0, sizeof(*buffer));
    }
    return THREE_MM_STORAGE_OK;
}
static int resize(ThreeMMCollectionStorage *storage, unsigned section, ThreeMMU32 bytes, int priority)
{
    ThreeMMStorageBuffer *buffer = &storage->buffers[section];
    void *replacement;
    if (buffer->locks)
        return THREE_MM_STORAGE_LOCKED;
    if (!bytes)
    {
        if (buffer->data)
            storage->allocator.release(storage->allocator.context, buffer->data, buffer->capacity);
        buffer->data = NULL;
        buffer->capacity = 0;
        return THREE_MM_STORAGE_OK;
    }
    replacement =
        storage->allocator.resize(storage->allocator.context, buffer->data, buffer->capacity, bytes, priority);
    if (!replacement)
        return THREE_MM_STORAGE_MEMORY;
    buffer->data = (unsigned char *)replacement;
    buffer->capacity = bytes;
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_ensure(ThreeMMCollectionStorage *storage, ThreeMMU32 first, ThreeMMU32 second, int shrink)
{
    ThreeMMU32 sizes[2], extra, candidate;
    unsigned i;
    int result;
    if (!valid(storage) || first >= THREE_MM_STORAGE_LIMIT || second >= THREE_MM_STORAGE_LIMIT)
        return THREE_MM_STORAGE_ARGUMENT;
    sizes[0] = first;
    sizes[1] = second;
    if (shrink)
        for (i = 0; i < 2; ++i)
            if (sizes[i] < storage->buffers[i].capacity)
            {
                result = resize(storage, i, sizes[i], THREE_MM_STORAGE_NORMAL);
                if (result == THREE_MM_STORAGE_LOCKED)
                    return result;
                /* A failed shrink leaves the old capacity authoritative. */
            }
    for (i = 0; i < 2; ++i)
    {
        ThreeMMStorageBuffer *buffer = &storage->buffers[i];
        if (sizes[i] <= buffer->capacity)
            continue;
        if (buffer->locks)
            return THREE_MM_STORAGE_LOCKED;
        extra = storage->min_grow[i];
        if (extra < THREE_MM_STORAGE_LIMIT - buffer->capacity)
        {
            candidate = buffer->capacity + extra;
            if (candidate > sizes[i] && resize(storage, i, candidate, THREE_MM_STORAGE_SPEED) == THREE_MM_STORAGE_OK)
                continue;
        }
        result = resize(storage, i, sizes[i], THREE_MM_STORAGE_NORMAL);
        if (result != THREE_MM_STORAGE_OK)
            return result;
    }
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_lock(ThreeMMCollectionStorage *storage, unsigned section)
{
    ThreeMMStorageBuffer *buffer;
    if (!valid(storage) || section > 1)
        return THREE_MM_STORAGE_ARGUMENT;
    buffer = &storage->buffers[section];
    if (!buffer->data)
        return THREE_MM_STORAGE_OK;
    if (buffer->locks == (ThreeMMU32)0xffffffffUL)
        return THREE_MM_STORAGE_LOCKED;
    ++buffer->locks;
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_unlock(ThreeMMCollectionStorage *storage, unsigned section)
{
    ThreeMMStorageBuffer *buffer;
    if (!valid(storage) || section > 1)
        return THREE_MM_STORAGE_ARGUMENT;
    buffer = &storage->buffers[section];
    if (!buffer->data)
        return THREE_MM_STORAGE_OK;
    if (!buffer->locks)
        return THREE_MM_STORAGE_LOCKED;
    --buffer->locks;
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_copy(ThreeMMCollectionStorage *destination, const ThreeMMCollectionStorage *source,
                          ThreeMMU32 first, ThreeMMU32 second)
{
    int result;
    if (!valid(source) || !valid(destination) || source == destination || first > source->buffers[0].capacity ||
        second > source->buffers[1].capacity)
        return THREE_MM_STORAGE_ARGUMENT;
    result = three_mm_storage_ensure(destination, first, second, 1);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    if (first)
        memcpy(destination->buffers[0].data, source->buffers[0].data, first);
    if (second)
        memcpy(destination->buffers[1].data, source->buffers[1].data, second);
    destination->min_grow[0] = source->min_grow[0];
    destination->min_grow[1] = source->min_grow[1];
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_write(ThreeMMCollectionStorage *storage, const ThreeMMStorageWriter *writer, const void *header,
                           ThreeMMU32 header_bytes, ThreeMMU32 first, ThreeMMU32 second)
{
    ThreeMMU32 sizes[2], offset;
    unsigned i;
    int result, ok;
    if (!valid(storage) || !writer || !writer->write_at || (!header && header_bytes) ||
        first > storage->buffers[0].capacity || second > storage->buffers[1].capacity ||
        header_bytes > writer->length || first > writer->length - header_bytes ||
        second != writer->length - header_bytes - first)
        return THREE_MM_STORAGE_ARGUMENT;
    if (header_bytes && !writer->write_at(writer->context, 0, header, header_bytes))
        return THREE_MM_STORAGE_IO;
    offset = header_bytes;
    sizes[0] = first;
    sizes[1] = second;
    for (i = 0; i < 2; ++i)
    {
        if (!sizes[i])
            continue;
        result = three_mm_storage_lock(storage, i);
        if (result != THREE_MM_STORAGE_OK)
            return result;
        ok = writer->write_at(writer->context, offset, storage->buffers[i].data, sizes[i]);
        three_mm_storage_unlock(storage, i);
        if (!ok)
            return THREE_MM_STORAGE_IO;
        offset += sizes[i];
    }
    return THREE_MM_STORAGE_OK;
}
int three_mm_storage_read(ThreeMMCollectionStorage *storage, const ThreeMMReadStream *reader, ThreeMMU32 offset,
                          ThreeMMU32 first, ThreeMMU32 second)
{
    ThreeMMU32 sizes[2];
    unsigned i;
    int result, ok;
    if (!valid(storage) || !reader || offset > reader->length || first > reader->length - offset ||
        second > reader->length - offset - first)
        return THREE_MM_STORAGE_ARGUMENT;
    if (!first && !second)
        return THREE_MM_STORAGE_OK;
    result = three_mm_storage_ensure(storage, first, second, 0);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    sizes[0] = first;
    sizes[1] = second;
    for (i = 0; i < 2; ++i)
    {
        if (!sizes[i])
            continue;
        result = three_mm_storage_lock(storage, i);
        if (result != THREE_MM_STORAGE_OK)
            return result;
        ok = three_mm_read_exact(reader, offset, storage->buffers[i].data, sizes[i]);
        three_mm_storage_unlock(storage, i);
        if (!ok)
            return THREE_MM_STORAGE_IO;
        offset += sizes[i];
    }
    return THREE_MM_STORAGE_OK;
}
