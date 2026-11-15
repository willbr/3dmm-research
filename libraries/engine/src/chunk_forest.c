/* Embedded forests converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_forest.h"
#include <string.h>
#define FOREST_LIMIT 0x7fffffffUL
static MMU32 get_word(const unsigned char *p, unsigned width, int big)
{
    MMU32 value = 0;
    unsigned i;
    for (i = 0; i < width; ++i)
        value |= (MMU32)p[i] << (8 * (big ? width - 1 - i : i));
    return value;
}
static void put_word(unsigned char *p, unsigned width, int big, MMU32 value)
{
    unsigned i;
    for (i = 0; i < width; ++i)
        p[i] = (unsigned char)(value >> (8 * (big ? width - 1 - i : i)));
}
int mm_forest_write(const MMForestExport *output, const MMStorageAllocator *allocator, MMU32 type,
                          MMU32 number, MMU32 offset, MMU32 *bytes)
{
    MMChunkWalk walk;
    MMChunkChild node, parent;
    MMChunkEntry entry;
    MMU32 flags, total = 0, position = offset;
    unsigned char descriptor[24];
    int result;
    if (!output || !output->entry || (output->write && !output->copy) || offset > FOREST_LIMIT)
        return MM_CHUNK_BAD_FILE;
    result = mm_chunk_walk_init(&walk, allocator);
    if (result != MM_CHUNK_OK)
        return result;
    result = mm_chunk_walk_reset(&walk, &output->graph, type, number);
    if (result != MM_CHUNK_OK)
        goto done;
    while (mm_chunk_walk_next(&walk, 0, &node, &parent, &flags))
    {
        if (flags & MM_WALK_ERROR)
        {
            result = MM_CHUNK_MEMORY;
            goto done;
        }
        if (!(flags & MM_WALK_PRE))
            continue;
        if (!output->entry(output->context, node.type, node.number, &entry) || entry.bytes > FOREST_LIMIT - 24 ||
            total > FOREST_LIMIT - 24 - entry.bytes || (output->write && position > FOREST_LIMIT - 24 - entry.bytes))
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        total += 24 + entry.bytes;
        if (!output->write)
            continue;
        put_word(descriptor, 2, output->big_endian, 1);
        put_word(descriptor + 2, 2, output->big_endian, (MMU32)output->system);
        put_word(descriptor + 4, 4, output->big_endian, node.type);
        put_word(descriptor + 8, 4, output->big_endian, flags & MM_WALK_ROOT ? 0xffffffffUL : node.slot);
        put_word(descriptor + 12, 4, output->big_endian, entry.bytes);
        put_word(descriptor + 16, 4, output->big_endian, entry.child_count);
        put_word(descriptor + 20, 4, output->big_endian, entry.flags & (MM_CHUNK_PACKED | MM_CHUNK_FOREST));
        if (!output->write(output->context, position, descriptor, sizeof(descriptor)) ||
            !output->copy(output->context, node.type, node.number, position + 24, entry.bytes))
        {
            result = MM_CHUNK_IO;
            goto done;
        }
        position += 24 + entry.bytes;
    }
    if (bytes)
        *bytes = total;
done:
    mm_chunk_walk_dispose(&walk);
    return result;
}
typedef struct ForestParent
{
    MMU32 type, number, remaining;
} ForestParent;
int mm_forest_read(const MMReadStream *stream, MMU32 offset, MMU32 bytes,
                         const MMForestImport *destination, const MMStorageAllocator *allocator, int copy)
{
    MMCollectionStorage storage;
    MMArray stack;
    ForestParent current, node;
    unsigned char descriptor[24];
    MMU32 end, payload, children, flags, slot;
    int big, result = MM_CHUNK_OK;
    if (!stream || !destination || !destination->add || !destination->adopt ||
        (bytes && (offset > stream->length || bytes > stream->length - offset)) || offset > FOREST_LIMIT ||
        bytes > FOREST_LIMIT - offset)
        return MM_CHUNK_BAD_FILE;
    if (mm_storage_init(&storage, allocator) != MM_STORAGE_OK)
        return MM_CHUNK_BAD_FILE;
    storage.min_grow[0] = 128;
    memset(&stack, 0, sizeof(stack));
    stack.storage = &storage;
    stack.width = sizeof(ForestParent);
    stack.free_count = -1;
    memset(&current, 0, sizeof(current));
    end = offset + bytes;
    while (offset < end)
    {
        if (end - offset < 24)
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        if (!mm_read_exact(stream, offset, descriptor, 24))
        {
            result = MM_CHUNK_IO;
            goto done;
        }
        offset += 24;
        if (descriptor[0] == 1 && descriptor[1] == 0)
            big = 0;
        else if (descriptor[0] == 0 && descriptor[1] == 1)
            big = 1;
        else
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        payload = get_word(descriptor + 12, 4, big);
        if (payload > end - offset)
        {
            result = MM_CHUNK_BAD_FILE;
            goto done;
        }
        node.type = get_word(descriptor + 4, 4, big);
        children = get_word(descriptor + 16, 4, big);
        slot = get_word(descriptor + 8, 4, big);
        flags = get_word(descriptor + 20, 4, big) & (MM_CHUNK_PACKED | (copy ? 0 : MM_CHUNK_FOREST));
        if (!destination->add(destination->context, node.type, offset, payload, flags, copy != 0, &node.number))
        {
            result = MM_CHUNK_IO;
            goto done;
        }
        offset += payload;
        if (stack.count)
        {
            if (!destination->adopt(destination->context, current.type, current.number, node.type, node.number, slot))
            {
                result = MM_CHUNK_IO;
                goto done;
            }
            --current.remaining;
        }
        if (children && children <= FOREST_LIMIT)
        {
            node.remaining = children;
            if (mm_array_add(&stack, &current, NULL) != MM_STORAGE_OK)
            {
                result = MM_CHUNK_MEMORY;
                goto done;
            }
            current = node;
        }
        else
            while (!current.remaining && stack.count)
                mm_array_pop(&stack, &current);
    }
    if (stack.count || current.remaining)
        result = MM_CHUNK_BAD_FILE;
done:
    mm_storage_dispose(&storage);
    return result;
}
