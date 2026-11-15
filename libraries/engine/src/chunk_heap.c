/* Chunk heap policy converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_heap.h"
#define MAX_FILE 0x7fffffffUL
static int range_valid(const MMChunkFreeRange *r, MMU32 end)
{
    return r->bytes && r->offset <= end && r->bytes <= end - r->offset;
}
int mm_chunk_heap_allocate(const MMChunkHeap *heap, MMU32 *end, MMU32 bytes, MMU32 *offset)
{
    MMChunkFreeRange range;
    MMU32 i, count, physical, position;
    if (!heap || !end || !offset || *end > MAX_FILE || bytes > MAX_FILE || !heap->count || !heap->get || !heap->put ||
        !heap->erase)
        return MM_CHUNK_BAD_FILE;
    if (!bytes)
    {
        *offset = 0;
        return MM_CHUNK_OK;
    }
    count = heap->count(heap->context);
    for (i = 0; i < count; ++i)
    {
        heap->get(heap->context, i, &range);
        if (!range_valid(&range, *end))
            return MM_CHUNK_BAD_FILE;
        if (range.bytes < bytes)
            continue;
        position = range.offset;
        if (range.bytes == bytes)
            heap->erase(heap->context, i);
        else
        {
            range.offset += bytes;
            range.bytes -= bytes;
            heap->put(heap->context, i, &range);
        }
        *offset = position;
        return MM_CHUNK_OK;
    }
    if (bytes > MAX_FILE - *end || !heap->physical_end || !heap->resize_file)
        return MM_CHUNK_BAD_FILE;
    physical = heap->physical_end(heap->context);
    if (physical > MAX_FILE)
        return MM_CHUNK_BAD_FILE;
    if (*end + bytes > physical && !heap->resize_file(heap->context, *end + bytes))
        return MM_CHUNK_IO;
    *offset = *end;
    *end += bytes;
    return MM_CHUNK_OK;
}
int mm_chunk_heap_free(const MMChunkHeap *heap, MMU32 *end, MMU32 offset, MMU32 bytes)
{
    MMChunkFreeRange previous, next, range;
    MMU32 count, low, high, middle;
    int has_previous, has_next;
    if (!heap || !end || *end > MAX_FILE || offset > *end || bytes > *end - offset || !heap->count || !heap->get ||
        !heap->put || !heap->erase)
        return MM_CHUNK_BAD_FILE;
    if (!bytes)
        return offset ? MM_CHUNK_BAD_FILE : MM_CHUNK_OK;
    count = heap->count(heap->context);
    if (offset + bytes == *end)
    {
        if (count)
        {
            heap->get(heap->context, count - 1, &previous);
            if (!range_valid(&previous, *end) || previous.offset + previous.bytes > offset)
                return MM_CHUNK_BAD_FILE;
            if (previous.offset + previous.bytes == offset)
            {
                heap->erase(heap->context, count - 1);
                offset = previous.offset;
            }
        }
        *end = offset;
        return MM_CHUNK_OK;
    }
    if (!heap->ensure_map || !heap->insert)
        return MM_CHUNK_BAD_FILE;
    if (!heap->ensure_map(heap->context))
        return MM_CHUNK_MEMORY;
    low = 0;
    high = count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        heap->get(heap->context, middle, &range);
        if (!range_valid(&range, *end))
            return MM_CHUNK_BAD_FILE;
        if (range.offset < offset)
            low = middle + 1;
        else
            high = middle;
    }
    has_previous = low > 0;
    has_next = low < count;
    if (has_previous)
    {
        heap->get(heap->context, low - 1, &previous);
        if (!range_valid(&previous, *end) || previous.offset + previous.bytes > offset)
            return MM_CHUNK_BAD_FILE;
    }
    if (has_next)
    {
        heap->get(heap->context, low, &next);
        if (!range_valid(&next, *end) || offset + bytes > next.offset)
            return MM_CHUNK_BAD_FILE;
    }
    if (has_previous && previous.offset + previous.bytes == offset)
    {
        previous.bytes = offset + bytes - previous.offset;
        if (has_next && next.offset == offset + bytes)
        {
            previous.bytes = next.offset + next.bytes - previous.offset;
            heap->erase(heap->context, low);
        }
        heap->put(heap->context, low - 1, &previous);
    }
    else if (has_next && next.offset == offset + bytes)
    {
        next.bytes += next.offset - offset;
        next.offset = offset;
        heap->put(heap->context, low, &next);
    }
    else
    {
        range.offset = offset;
        range.bytes = bytes;
        if (!heap->insert(heap->context, low, &range))
            return MM_CHUNK_MEMORY;
    }
    return MM_CHUNK_OK;
}
