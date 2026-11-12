/* Chunk metadata converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_metadata.h"
#include <string.h>
static ThreeMMArray runtime_view(ThreeMMChunkRuntime *runtime)
{
    ThreeMMArray view;
    memset(&view, 0, sizeof(view));
    view.storage = &runtime->storage;
    view.width = sizeof(ThreeMMChunkRuntimeEntry);
    view.count = runtime->count;
    view.free_count = -1;
    return view;
}
int three_mm_chunk_runtime_init(ThreeMMChunkRuntime *runtime, const ThreeMMStorageAllocator *allocator)
{
    if (!runtime || three_mm_storage_init(&runtime->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    runtime->storage.min_grow[0] = 128;
    runtime->count = 0;
    runtime->created = 0;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_runtime_dispose(ThreeMMChunkRuntime *runtime)
{
    if (!runtime || three_mm_storage_dispose(&runtime->storage) != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    runtime->count = 0;
    runtime->created = 0;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_runtime_swap(ThreeMMChunkRuntime *a, ThreeMMChunkRuntime *b)
{
    ThreeMMChunkRuntime saved;
    if (!a || !b || a->storage.buffers[0].locks || a->storage.buffers[1].locks || b->storage.buffers[0].locks ||
        b->storage.buffers[1].locks)
        return THREE_MM_CHUNK_BAD_FILE;
    saved = *a;
    *a = *b;
    *b = saved;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_runtime_at(const ThreeMMChunkRuntime *runtime, ThreeMMU32 position, ThreeMMChunkRuntimeEntry *entry)
{
    if (!runtime || !entry || position >= runtime->count || !runtime->storage.buffers[0].data ||
        runtime->count > runtime->storage.buffers[0].capacity / sizeof(*entry))
        return THREE_MM_CHUNK_BAD_FILE;
    memcpy(entry, runtime->storage.buffers[0].data + position * sizeof(*entry), sizeof(*entry));
    return THREE_MM_CHUNK_OK;
}
static int runtime_find(const ThreeMMChunkRuntime *runtime, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *position,
                        ThreeMMChunkRuntimeEntry *out)
{
    ThreeMMU32 low = 0, high = runtime->count, middle;
    ThreeMMChunkRuntimeEntry entry;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        if (three_mm_chunk_runtime_at(runtime, middle, &entry) != THREE_MM_CHUNK_OK)
            return -1;
        if (entry.type < type || (entry.type == type && entry.number < number))
            low = middle + 1;
        else if (entry.type > type || entry.number > number)
            high = middle;
        else
        {
            *position = middle;
            if (out)
                *out = entry;
            return 1;
        }
    }
    *position = low;
    return 0;
}
ThreeMMI32 three_mm_chunk_runtime_get(const ThreeMMChunkRuntime *runtime, ThreeMMU32 type, ThreeMMU32 number)
{
    ThreeMMChunkRuntimeEntry entry;
    ThreeMMU32 position;
    return runtime && runtime_find(runtime, type, number, &position, &entry) == 1 ? entry.runtime : 0;
}
int three_mm_chunk_runtime_set(ThreeMMChunkRuntime *runtime, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 value)
{
    ThreeMMArray view;
    ThreeMMChunkRuntimeEntry entry;
    ThreeMMU32 position;
    int found, result;
    if (!runtime)
        return THREE_MM_CHUNK_BAD_FILE;
    found = runtime_find(runtime, type, number, &position, &entry);
    if (found < 0)
        return THREE_MM_CHUNK_BAD_FILE;
    view = runtime_view(runtime);
    if (found)
    {
        if (!value)
        {
            three_mm_array_delete(&view, position, 1);
            runtime->count = view.count;
        }
        else
        {
            entry.runtime = value;
            memcpy(runtime->storage.buffers[0].data + position * sizeof(entry), &entry, sizeof(entry));
        }
        return THREE_MM_CHUNK_OK;
    }
    if (!value)
        return THREE_MM_CHUNK_OK;
    if (!runtime->created)
    {
        if (three_mm_array_reserve(&view, 1, 0) != THREE_MM_STORAGE_OK)
            return THREE_MM_CHUNK_MEMORY;
        runtime->created = 1;
    }
    entry.type = type;
    entry.number = number;
    entry.runtime = value;
    result = three_mm_array_insert(&view, position, &entry, 1);
    if (result != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_MEMORY;
    runtime->count = view.count;
    return THREE_MM_CHUNK_OK;
}
static int name_range(const ThreeMMChunkNames *names, ThreeMMU32 position, ThreeMMU32 *offset, ThreeMMU32 *bytes)
{
    ThreeMMChunkEntry entry;
    if (!names || !names->count || !names->entry || !names->pin || !names->unpin ||
        position >= names->count(names->context) || !names->entry(names->context, position, &entry) ||
        entry.child_count > entry.variable_bytes / 12)
        return 0;
    *offset = entry.child_count * 12;
    *bytes = entry.variable_bytes - *offset;
    return *bytes <= names->max_bytes;
}
int three_mm_chunk_name_set(const ThreeMMChunkNames *names, ThreeMMU32 position, const ThreeMMChunkNameValue *value)
{
    ThreeMMU32 offset, old_bytes, bytes;
    unsigned char *data;
    if (!name_range(names, position, &offset, &old_bytes) || !names->insert || !names->erase)
        return THREE_MM_CHUNK_BAD_FILE;
    bytes = value && value->characters ? value->bytes : 0;
    if (bytes > names->max_bytes || (bytes && !value->wire) || (value && value->characters && !bytes))
        return THREE_MM_CHUNK_BAD_FILE;
    if (old_bytes > bytes)
        names->erase(names->context, position, offset, old_bytes - bytes);
    else if (old_bytes < bytes && !names->insert(names->context, position, offset, bytes - old_bytes))
        return THREE_MM_CHUNK_MEMORY;
    if (bytes)
    {
        data = (unsigned char *)names->pin(names->context, position);
        if (!data)
            return THREE_MM_CHUNK_BAD_FILE;
        memcpy(data + offset, value->wire, bytes);
        names->unpin(names->context);
    }
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_name_get(const ThreeMMChunkNames *names, ThreeMMU32 position, const ThreeMMChunkNameDecoder *decoder)
{
    ThreeMMU32 offset, bytes;
    const unsigned char *data;
    if (!decoder || !decoder->decode || !decoder->clear || !decoder->characters)
        return 0;
    if (!name_range(names, position, &offset, &bytes) || !bytes)
    {
        decoder->clear(decoder->context);
        return 0;
    }
    data = (const unsigned char *)names->pin(names->context, position);
    if (!data)
    {
        decoder->clear(decoder->context);
        return 0;
    }
    if (!decoder->decode(decoder->context, data + offset, bytes))
        decoder->clear(decoder->context);
    names->unpin(names->context);
    return decoder->characters(decoder->context) > 0;
}
