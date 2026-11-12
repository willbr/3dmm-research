/* Chunk graph operations converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_ops.h"
#include <string.h>
#define OK THREE_MM_CHUNK_OK
#define BAD THREE_MM_CHUNK_BAD_FILE
#define LONER ((ThreeMMU32)2)
#define FOREST ((ThreeMMU32)16)
#define DATA_FLAGS ((ThreeMMU32)21)
static ThreeMMI32 last_runtime = 0;
ThreeMMI32 three_mm_chunk_runtime_highest(void)
{
    return last_runtime;
}
static int readable(const ThreeMMChunkEdit *edit)
{
    return edit && edit->count && edit->entry;
}
static int access_entry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    const ThreeMMChunkEdit *edit = (const ThreeMMChunkEdit *)context;
    edit->entry(edit->context, position, entry);
    return 1;
}
static int lookup(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *position,
                  ThreeMMChunkEntry *entry)
{
    ThreeMMChunkAccess access;
    access.context = (void *)edit;
    access.count = edit->count(edit->context);
    access.entry = access_entry;
    access.child = NULL;
    if (three_mm_chunk_find(&access, type, number, position) != OK)
        return 0;
    if (entry)
        edit->entry(edit->context, *position, entry);
    return 1;
}
int three_mm_chunk_swap_data(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 other_type,
                             ThreeMMU32 other_number)
{
    ThreeMMChunkEntry a, b, saved;
    ThreeMMU32 first, second;
    if (!readable(edit) || !edit->store || !edit->reset_runtime)
        return BAD;
    if (!lookup(edit, type, number, &first, &a) || !lookup(edit, other_type, other_number, &second, &b))
        return THREE_MM_CHUNK_NOT_FOUND;
    saved = a;
    a.offset = b.offset;
    a.bytes = b.bytes;
    a.flags = (a.flags & ~DATA_FLAGS) | (b.flags & DATA_FLAGS);
    b.offset = saved.offset;
    b.bytes = saved.bytes;
    b.flags = (b.flags & ~DATA_FLAGS) | (saved.flags & DATA_FLAGS);
    edit->store(edit->context, first, &a, 0);
    edit->store(edit->context, second, &b, 0);
    edit->reset_runtime(edit->context, type, number);
    edit->reset_runtime(edit->context, other_type, other_number);
    return OK;
}
int three_mm_chunk_swap_children(const ThreeMMChunkEdit *edit, ThreeMMU32 type, ThreeMMU32 number,
                                 ThreeMMU32 other_type, ThreeMMU32 other_number)
{
    ThreeMMChunkEntry a, b;
    ThreeMMU32 first, second, bytes_a, bytes_b;
    if (!readable(edit) || !edit->store || !edit->move_bytes)
        return BAD;
    if (!lookup(edit, type, number, &first, &a) || !lookup(edit, other_type, other_number, &second, &b))
        return THREE_MM_CHUNK_NOT_FOUND;
    if (first == second || a.child_count > 0x7fffffffUL / 12 || b.child_count > 0x7fffffffUL / 12)
        return BAD;
    bytes_a = a.child_count * 12;
    bytes_b = b.child_count * 12;
    a.child_count = b.child_count;
    b.child_count = bytes_a / 12;
    edit->store(edit->context, first, &a, 0);
    edit->store(edit->context, second, &b, 0);
    if ((bytes_a && !edit->move_bytes(edit->context, first, 0, second, bytes_b, bytes_a)) ||
        (bytes_b && !edit->move_bytes(edit->context, second, 0, first, 0, bytes_b)))
        return BAD;
    return OK;
}
int three_mm_chunk_move(const ThreeMMChunkEndpoint *endpoint, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 new_type,
                        ThreeMMU32 new_number)
{
    const ThreeMMChunkEdit *edit;
    ThreeMMChunkEntry entry, parent;
    ThreeMMChunkChild child;
    ThreeMMU32 source, target, remaining, i, k;
    ThreeMMI32 runtime;
    if (!endpoint)
        return BAD;
    edit = &endpoint->edit;
    if (!readable(edit) || !edit->store || !edit->move_entry || !edit->child || !edit->put_child ||
        !endpoint->runtime || !endpoint->set_runtime)
        return BAD;
    if (type == new_type && number == new_number)
        return OK;
    if (!lookup(edit, type, number, &source, &entry) || lookup(edit, new_type, new_number, &target, NULL))
        return THREE_MM_CHUNK_NOT_FOUND;
    runtime = endpoint->runtime(endpoint->context, type, number);
    if (runtime)
        endpoint->set_runtime(endpoint->context, type, number, 0);
    remaining = entry.parent_count;
    entry.type = new_type;
    entry.number = new_number;
    edit->store(edit->context, source, &entry, 0);
    edit->move_entry(edit->context, source, target);
    if (remaining)
    {
        if (endpoint->scan_all_references)
            ++remaining;
        for (i = edit->count(edit->context); i-- > 0 && remaining;)
        {
            edit->entry(edit->context, i, &parent);
            for (k = 0; k < parent.child_count;)
            {
                edit->child(edit->context, i, k, &child);
                if (child.type != type || child.number != number)
                {
                    ++k;
                    continue;
                }
                child.type = new_type;
                child.number = new_number;
                if (three_mm_chunk_retarget(edit, i, k, &child) != OK)
                    return BAD;
                --remaining;
            }
        }
        if (runtime && type == new_type)
            endpoint->set_runtime(endpoint->context, new_type, new_number, runtime);
        return remaining == (endpoint->scan_all_references ? 1U : 0U) ? OK : BAD;
    }
    if (runtime && type == new_type)
        endpoint->set_runtime(endpoint->context, new_type, new_number, runtime);
    return OK;
}
static int candidate(const ThreeMMChunkEndpoint *endpoint, ThreeMMU32 type, ThreeMMI32 runtime, ThreeMMU32 minimum,
                     ThreeMMU32 *number)
{
    ThreeMMU32 position, count = endpoint->edit.count(endpoint->edit.context);
    ThreeMMChunkEntry entry;
    lookup(&endpoint->edit, type, minimum, &position, NULL);
    for (; position < count; ++position)
    {
        endpoint->edit.entry(endpoint->edit.context, position, &entry);
        if (entry.type != type)
            break;
        if (endpoint->runtime(endpoint->context, type, entry.number) == runtime)
        {
            *number = entry.number;
            return 1;
        }
    }
    return 0;
}
int three_mm_chunk_match(const ThreeMMChunkEndpoint *src, const ThreeMMChunkEndpoint *dst, ThreeMMU32 type,
                         ThreeMMU32 number, ThreeMMU32 *matched)
{
    ThreeMMChunkWalk a, b;
    ThreeMMChunkChild node_a, node_b;
    ThreeMMU32 flags_a, flags_b, minimum = 0, target, children;
    ThreeMMI32 runtime, child_runtime;
    int has_a, has_b, result = 0;
    if (!src || !dst || !matched || !readable(&src->edit) || !readable(&dst->edit) || !src->runtime || !dst->runtime ||
        !src->identity || !dst->identity || !src->graph.children || !src->graph.child || !dst->graph.children ||
        !dst->graph.child || src->identity == dst->identity)
        return 0;
    runtime = src->runtime(src->context, type, number);
    if (!runtime)
        return 0;
    if (three_mm_chunk_walk_init(&a, src->allocator) != OK)
        return 0;
    if (three_mm_chunk_walk_init(&b, dst->allocator) != OK)
    {
        three_mm_chunk_walk_dispose(&a);
        return 0;
    }
    children = src->graph.children(src->graph.context, type, number);
    while (candidate(dst, type, runtime, minimum, &target))
    {
        if (dst->graph.children(dst->graph.context, type, target) == children)
        {
            three_mm_chunk_walk_reset(&a, &src->graph, type, number);
            three_mm_chunk_walk_reset(&b, &dst->graph, type, target);
            for (;;)
            {
                has_a = three_mm_chunk_walk_next(&a, 0, &node_a, NULL, &flags_a);
                child_runtime = has_a ? src->runtime(src->context, node_a.type, node_a.number) : 0;
                if (has_a && !child_runtime)
                    goto done;
                has_b = three_mm_chunk_walk_next(&b, 0, &node_b, NULL, &flags_b);
                if (has_a != has_b)
                    break;
                if (!has_a)
                {
                    *matched = target;
                    result = 1;
                    goto done;
                }
                if ((flags_a | flags_b) & THREE_MM_WALK_ERROR)
                    goto done;
                if (((flags_a ^ flags_b) & (THREE_MM_WALK_PRE | THREE_MM_WALK_POST | THREE_MM_WALK_ROOT)) ||
                    node_a.type != node_b.type || (!(flags_a & THREE_MM_WALK_ROOT) && node_a.slot != node_b.slot) ||
                    child_runtime != dst->runtime(dst->context, node_b.type, node_b.number) ||
                    src->graph.children(src->graph.context, node_a.type, node_a.number) !=
                        dst->graph.children(dst->graph.context, node_b.type, node_b.number))
                    break;
            }
        }
        if (target == 0xffffffffUL)
            break;
        minimum = target + 1;
    }
done:
    three_mm_chunk_walk_dispose(&b);
    three_mm_chunk_walk_dispose(&a);
    return result;
}
typedef struct NumberMap
{
    ThreeMMU32 type, source, destination;
} NumberMap;
static int map_find(const ThreeMMArray *map, ThreeMMU32 type, ThreeMMU32 number, NumberMap *out, ThreeMMU32 *position)
{
    NumberMap item;
    ThreeMMU32 low = 0, high = map->count, middle;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        memcpy(&item, map->storage->buffers[0].data + middle * sizeof(item), sizeof(item));
        if (item.type < type || (item.type == type && item.source < number))
            low = middle + 1;
        else if (item.type > type || item.source > number)
            high = middle;
        else
        {
            if (out)
                *out = item;
            if (position)
                *position = middle;
            return 1;
        }
    }
    if (position)
        *position = low;
    return 0;
}
static void loner(const ThreeMMChunkEndpoint *endpoint, ThreeMMU32 type, ThreeMMU32 number)
{
    ThreeMMChunkEntry entry;
    ThreeMMU32 position;
    if (lookup(&endpoint->edit, type, number, &position, &entry))
    {
        entry.flags |= LONER;
        endpoint->edit.store(endpoint->edit.context, position, &entry, 0);
    }
}
int three_mm_chunk_copy(const ThreeMMChunkEndpoint *src, const ThreeMMChunkEndpoint *dst, ThreeMMU32 type,
                        ThreeMMU32 number, int clone, ThreeMMU32 *copied)
{
    ThreeMMCollectionStorage storage;
    ThreeMMArray map;
    ThreeMMChunkWalk walk;
    ThreeMMChunkChild node, parent, link;
    ThreeMMChunkEntry entry, target_entry;
    NumberMap item, parent_item;
    ThreeMMU32 position, target_position, map_position, flags, root = 0;
    ThreeMMI32 runtime;
    int skip = 0, free_root = 0, result = OK;
    if (!src || !dst || !copied || !src->identity || !dst->identity || !readable(&src->edit) || !readable(&dst->edit) ||
        !src->runtime || !src->set_runtime || !dst->set_runtime || !dst->edit.store || !src->read_payload ||
        !dst->put_payload || !dst->add_payload || !src->read_name || !dst->write_name)
        return BAD;
    if (!lookup(&src->edit, type, number, &position, NULL))
        return THREE_MM_CHUNK_NOT_FOUND;
    if (!clone && src->identity == dst->identity)
    {
        loner(dst, type, number);
        *copied = number;
        return OK;
    }
    if (three_mm_storage_init(&storage, src->allocator) != THREE_MM_STORAGE_OK)
        return BAD;
    storage.min_grow[0] = 128;
    memset(&map, 0, sizeof(map));
    map.storage = &storage;
    map.width = sizeof(NumberMap);
    map.free_count = -1;
    if (three_mm_chunk_walk_init(&walk, src->allocator) != OK)
    {
        three_mm_storage_dispose(&storage);
        return BAD;
    }
    if (three_mm_chunk_walk_reset(&walk, &src->graph, type, number) != OK)
    {
        result = BAD;
        goto done;
    }
    while (three_mm_chunk_walk_next(&walk, skip, &node, &parent, &flags))
    {
        skip = 0;
        if (flags & THREE_MM_WALK_ERROR)
        {
            result = THREE_MM_CHUNK_MEMORY;
            goto done;
        }
        if (!(flags & THREE_MM_WALK_PRE))
            continue;
        if (map_find(&map, node.type, node.number, &item, NULL))
            skip = 1;
        else
        {
            runtime = src->runtime(src->context, node.type, node.number);
            item.type = node.type;
            item.source = node.number;
            if (runtime && !clone && three_mm_chunk_match(src, dst, node.type, node.number, &item.destination))
                skip = 1;
            else
            {
                if (!runtime && last_runtime < 0x7fffffffL &&
                    src->set_runtime(src->context, node.type, node.number, last_runtime + 1))
                    runtime = ++last_runtime;
                lookup(&src->edit, node.type, node.number, &position, NULL);
                src->read_payload(src->context, position);
                if (src->identity != dst->identity &&
                    !lookup(&dst->edit, node.type, node.number, &target_position, NULL))
                {
                    item.destination = node.number;
                    if (!dst->put_payload(dst->context, node.type, item.destination, src->context))
                    {
                        result = THREE_MM_CHUNK_IO;
                        goto done;
                    }
                }
                else if (!dst->add_payload(dst->context, node.type, src->context, &item.destination))
                {
                    result = THREE_MM_CHUNK_IO;
                    goto done;
                }
                lookup(&src->edit, node.type, node.number, &position, &entry);
                lookup(&dst->edit, node.type, item.destination, &target_position, &target_entry);
                if (entry.flags & FOREST)
                {
                    target_entry.flags |= FOREST;
                    dst->edit.store(dst->edit.context, target_position, &target_entry, 0);
                }
                if (src->read_name(src->context, position) &&
                    !dst->write_name(dst->context, target_position, src->context))
                {
                    three_mm_chunk_delete(&dst->edit, node.type, item.destination);
                    result = THREE_MM_CHUNK_IO;
                    goto done;
                }
                if (runtime)
                    dst->set_runtime(dst->context, node.type, item.destination, runtime);
                map_find(&map, node.type, node.number, NULL, &map_position);
                if (three_mm_array_insert(&map, map_position, &item, 1) != THREE_MM_STORAGE_OK)
                {
                    result = THREE_MM_CHUNK_MEMORY;
                    goto done;
                }
                free_root = 1;
            }
        }
        if (flags & THREE_MM_WALK_ROOT)
        {
            root = item.destination;
            continue;
        }
        if (!map_find(&map, parent.type, parent.number, &parent_item, NULL))
        {
            result = BAD;
            goto done;
        }
        link.type = node.type;
        link.number = item.destination;
        link.slot = node.slot;
        if (three_mm_chunk_adopt(&dst->edit, parent.type, parent_item.destination, &link, !skip, dst->max_references) !=
            OK)
        {
            if (!skip)
                three_mm_chunk_delete(&dst->edit, node.type, item.destination);
            result = THREE_MM_CHUNK_IO;
            goto done;
        }
    }
    loner(dst, type, root);
    *copied = root;
done:
    three_mm_storage_dispose(&storage);
    if (result != OK && free_root)
        three_mm_chunk_delete(&dst->edit, type, root);
    three_mm_chunk_walk_dispose(&walk);
    return result;
}
