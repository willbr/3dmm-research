/* Chunk graph operations converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_ops.h"
#include "mm/chunk_storage.h"
#include <string.h>
#define OK MM_CHUNK_OK
#define BAD MM_CHUNK_BAD_FILE
#define LONER ((MMU32)2)
#define FOREST ((MMU32)16)
#define DATA_FLAGS ((MMU32)21)
static MMI32 last_runtime = 0;
MMI32 mm_chunk_runtime_highest(void)
{
    return last_runtime;
}
static int readable(const MMChunkEdit *edit)
{
    return edit && edit->count && edit->entry;
}
static int lookup(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 *position,
                  MMChunkEntry *entry)
{
    const MMChunkAccess access = mm_chunk_edit_access(edit);
    return mm_chunk_find(&access, type, number, position) == OK &&
           (!entry || mm_chunk_at(&access, *position, entry) == OK);
}
int mm_chunk_swap_data(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 other_type,
                             MMU32 other_number)
{
    MMChunkEntry a, b, saved;
    MMU32 first, second;
    if (!readable(edit) || !edit->store || !edit->reset_runtime)
        return BAD;
    if (!lookup(edit, type, number, &first, &a) || !lookup(edit, other_type, other_number, &second, &b))
        return MM_CHUNK_NOT_FOUND;
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
int mm_chunk_swap_children(const MMChunkEdit *edit, MMU32 type, MMU32 number,
                                 MMU32 other_type, MMU32 other_number)
{
    MMChunkEntry a, b;
    MMU32 first, second, bytes_a, bytes_b;
    if (!readable(edit) || !edit->store || !edit->move_bytes)
        return BAD;
    if (!lookup(edit, type, number, &first, &a) || !lookup(edit, other_type, other_number, &second, &b))
        return MM_CHUNK_NOT_FOUND;
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
int mm_chunk_move(const MMChunkEdit *edit, MMU32 type, MMU32 number, MMU32 new_type,
                        MMU32 new_number)
{
    MMChunkEntry entry, parent;
    MMChunkChild child;
    MMU32 source, target, remaining, extra, i, k;
    MMI32 runtime;
    if (!readable(edit) || !edit->store || !edit->move_entry || !edit->child || !edit->put_child ||
        !edit->runtime || !edit->set_runtime)
        return BAD;
    if (type == new_type && number == new_number)
        return OK;
    if (!lookup(edit, type, number, &source, &entry) || lookup(edit, new_type, new_number, &target, NULL))
        return MM_CHUNK_NOT_FOUND;
    runtime = edit->runtime(edit->context, type, number);
    if (runtime)
        edit->set_runtime(edit->context, type, number, 0);
    remaining = entry.parent_count;
    extra = remaining && edit->scan_all_references ? 1U : 0U;
    entry.type = new_type;
    entry.number = new_number;
    edit->store(edit->context, source, &entry, 0);
    edit->move_entry(edit->context, source, target);
    remaining += extra;
    for (i = edit->count(edit->context); i-- > 0 && remaining;)
    {
        if (!edit->entry(edit->context, i, &parent))
            return MM_CHUNK_IO;
        for (k = 0; k < parent.child_count;)
        {
            if (!edit->child(edit->context, i, k, &child))
                return MM_CHUNK_IO;
            if (child.type != type || child.number != number)
            {
                ++k;
                continue;
            }
            child.type = new_type;
            child.number = new_number;
            if (mm_chunk_retarget(edit, i, k, &child) != OK)
                return BAD;
            --remaining;
        }
    }
    if (runtime && type == new_type)
        edit->set_runtime(edit->context, new_type, new_number, runtime);
    return remaining == extra ? OK : BAD;
}
static int candidate(const MMChunkEndpoint *endpoint, MMU32 type, MMI32 runtime, MMU32 minimum,
                     MMU32 *number)
{
    const MMChunkAccess access = mm_chunk_edit_access(&endpoint->edit);
    MMU32 position;
    MMChunkEntry entry;
    int status = mm_chunk_find(&access, type, minimum, &position);
    if (status != OK && status != MM_CHUNK_NOT_FOUND)
        return 0;
    for (; position < access.count; ++position)
    {
        if (mm_chunk_at(&access, position, &entry) != OK || entry.type != type)
            break;
        if (endpoint->edit.runtime(endpoint->edit.context, type, entry.number) == runtime)
        {
            *number = entry.number;
            return 1;
        }
    }
    return 0;
}
int mm_chunk_match(const MMChunkEndpoint *src, const MMChunkEndpoint *dst, MMU32 type,
                         MMU32 number, MMU32 *matched)
{
    MMChunkWalk a, b;
    MMChunkGraph source, destination;
    MMChunkChild node_a, node_b;
    MMU32 flags_a, flags_b, minimum = 0, target, children;
    MMI32 runtime, child_runtime;
    int has_a, has_b, result = 0;
    if (!src || !dst || !matched || !readable(&src->edit) || !readable(&dst->edit) || !src->edit.runtime ||
        !dst->edit.runtime || !src->identity || !dst->identity || !src->edit.child || !dst->edit.child ||
        src->identity == dst->identity)
        return 0;
    runtime = src->edit.runtime(src->edit.context, type, number);
    if (!runtime)
        return 0;
    if (mm_chunk_walk_init(&a, src->allocator) != OK)
        return 0;
    if (mm_chunk_walk_init(&b, dst->allocator) != OK)
    {
        mm_chunk_walk_dispose(&a);
        return 0;
    }
    source = mm_chunk_edit_graph(&src->edit);
    destination = mm_chunk_edit_graph(&dst->edit);
    children = source.children(source.context, type, number);
    while (candidate(dst, type, runtime, minimum, &target))
    {
        if (destination.children(destination.context, type, target) == children)
        {
            mm_chunk_walk_reset(&a, &source, type, number);
            mm_chunk_walk_reset(&b, &destination, type, target);
            for (;;)
            {
                has_a = mm_chunk_walk_next(&a, 0, &node_a, NULL, &flags_a);
                child_runtime = has_a ? src->edit.runtime(src->edit.context, node_a.type, node_a.number) : 0;
                if (has_a && !child_runtime)
                    goto done;
                has_b = mm_chunk_walk_next(&b, 0, &node_b, NULL, &flags_b);
                if (has_a != has_b)
                    break;
                if (!has_a)
                {
                    *matched = target;
                    result = 1;
                    goto done;
                }
                if ((flags_a | flags_b) & MM_WALK_ERROR)
                    goto done;
                if (((flags_a ^ flags_b) & (MM_WALK_PRE | MM_WALK_POST | MM_WALK_ROOT)) ||
                    node_a.type != node_b.type || (!(flags_a & MM_WALK_ROOT) && node_a.slot != node_b.slot) ||
                    child_runtime != dst->edit.runtime(dst->edit.context, node_b.type, node_b.number) ||
                    source.children(source.context, node_a.type, node_a.number) !=
                        destination.children(destination.context, node_b.type, node_b.number))
                    break;
            }
        }
        if (target == 0xffffffffUL)
            break;
        minimum = target + 1;
    }
done:
    mm_chunk_walk_dispose(&b);
    mm_chunk_walk_dispose(&a);
    return result;
}
typedef struct NumberMap
{
    MMU32 type, source, destination;
} NumberMap;
static int map_find(const MMArray *map, MMU32 type, MMU32 number, NumberMap *out, MMU32 *position)
{
    NumberMap item;
    MMU32 low = 0, high = map->count, middle;
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
static void loner(const MMChunkEndpoint *endpoint, MMU32 type, MMU32 number)
{
    MMChunkEntry entry;
    MMU32 position;
    if (lookup(&endpoint->edit, type, number, &position, &entry))
    {
        entry.flags |= LONER;
        endpoint->edit.store(endpoint->edit.context, position, &entry, 0);
    }
}
static int copy_lookup(const MMChunkEndpoint *source, MMU32 type, MMU32 number, MMU32 *position,
                       MMChunkEntry *entry)
{
    if (!source->stored)
        return lookup(&source->edit, type, number, position, entry);
    return mm_chunk_find(source->stored, type, number, position) == OK &&
           (!entry || mm_chunk_at(source->stored, *position, entry) == OK);
}
static MMU32 stored_children(void *context, MMU32 type, MMU32 number)
{
    const MMChunkAccess *chunks = (const MMChunkAccess *)context;
    MMU32 position;
    MMChunkEntry entry;
    if (mm_chunk_find(chunks, type, number, &position) != OK || mm_chunk_at(chunks, position, &entry) != OK)
        return 0;
    return entry.child_count;
}
static int stored_child(void *context, MMU32 type, MMU32 number, MMU32 ordinal, MMChunkChild *child)
{
    const MMChunkAccess *chunks = (const MMChunkAccess *)context;
    MMU32 position;
    return mm_chunk_find(chunks, type, number, &position) == OK &&
           mm_chunk_child_at(chunks, position, ordinal, child) == OK;
}

static int copy_payload(const MMChunkEndpoint *src, const MMChunkEndpoint *dst,
                        MMU32 position, const MMChunkEntry *entry, MMU32 *number)
{
    MMOwnedBlock block;
    MMU32 target;
    int result = MM_CHUNK_IO;
    if (mm_owned_block_init(&block, src->handles, src->files) != MM_DATA_OK)
        return result;
    if (src->bind_payload(src->context, position, entry, &block))
    {
        if (src->identity != dst->identity && !lookup(&dst->edit, entry->type, entry->number, &target, NULL))
        {
            result = mm_chunk_put_block(&dst->edit, dst->files, dst->allocator, entry->type, entry->number, &block);
            if (result == OK)
                *number = entry->number;
        }
        else
            result = mm_chunk_add_block_copy(&dst->edit, dst->files, dst->allocator, entry->type, &block, number);
    }
    mm_owned_block_clear(&block);
    return result == OK ? OK : MM_CHUNK_IO;
}

static int copy_chunk(const MMChunkEndpoint *src, const MMChunkEndpoint *dst, MMU32 type,
                      MMU32 number, int clone, int (*reuse)(MMU32), MMU32 *copied, int *created)
{
    MMCollectionStorage storage;
    MMArray map;
    MMChunkWalk walk;
    MMChunkGraph graph;
    MMChunkChild node, parent, link;
    MMChunkEntry entry, target_entry;
    NumberMap item, parent_item;
    const void *name;
    MMU32 position, target_position, map_position, flags, root = 0;
    MMI32 runtime;
    int skip = 0, free_root = 0, new_root = 0, result = OK;
    if (!src || !dst || !copied || !src->identity || !dst->identity || !readable(&dst->edit) ||
        !dst->edit.set_runtime || !dst->edit.store || !src->bind_payload ||
        !src->read_name || !dst->write_name)
        return BAD;
    if (src->stored)
    {
        if (!src->stored->entry || !src->stored->child || src->identity == dst->identity)
            return BAD;
        graph.context = (void *)src->stored;
        graph.children = stored_children;
        graph.child = stored_child;
    }
    else
    {
        if (!readable(&src->edit) || !src->edit.child || !src->edit.runtime || !src->edit.set_runtime)
            return BAD;
        graph = mm_chunk_edit_graph(&src->edit);
    }
    if (!copy_lookup(src, type, number, &position, NULL))
        return MM_CHUNK_NOT_FOUND;
    if (!clone && !reuse && src->identity == dst->identity)
    {
        if (created)
            *created = 0;
        else
            loner(dst, type, number);
        *copied = number;
        return OK;
    }
    if (mm_storage_init(&storage, src->allocator) != MM_STORAGE_OK)
        return BAD;
    storage.min_grow[0] = 128;
    memset(&map, 0, sizeof(map));
    map.storage = &storage;
    map.width = sizeof(NumberMap);
    map.free_count = -1;
    if (mm_chunk_walk_init(&walk, src->allocator) != OK)
    {
        mm_storage_dispose(&storage);
        return BAD;
    }
    if (mm_chunk_walk_reset(&walk, &graph, type, number) != OK)
    {
        result = BAD;
        goto done;
    }
    while (mm_chunk_walk_next(&walk, skip, &node, &parent, &flags))
    {
        skip = 0;
        if (flags & MM_WALK_ERROR)
        {
            result = MM_CHUNK_MEMORY;
            goto done;
        }
        if (!(flags & MM_WALK_PRE))
            continue;
        if (map_find(&map, node.type, node.number, &item, NULL))
            skip = 1;
        else
        {
            runtime = src->stored ? 0 : src->edit.runtime(src->edit.context, node.type, node.number);
            item.type = node.type;
            item.source = node.number;
            if (runtime && !clone && (!reuse || (!(flags & MM_WALK_ROOT) && reuse(node.type))) &&
                mm_chunk_match(src, dst, node.type, node.number, &item.destination))
                skip = 1;
            else
            {
                if (!src->stored && !runtime && last_runtime < 0x7fffffffL &&
                    src->edit.set_runtime(src->edit.context, node.type, node.number, last_runtime + 1))
                    runtime = ++last_runtime;
                if (!copy_lookup(src, node.type, node.number, &position, &entry))
                {
                    result = BAD;
                    goto done;
                }
                result = copy_payload(src, dst, position, &entry, &item.destination);
                if (result != OK)
                    goto done;
                /* Cloning into the source can move a live directory row. */
                if (!src->stored)
                    copy_lookup(src, node.type, node.number, &position, &entry);
                lookup(&dst->edit, node.type, item.destination, &target_position, &target_entry);
                if (entry.flags & FOREST)
                {
                    target_entry.flags |= FOREST;
                    dst->edit.store(dst->edit.context, target_position, &target_entry, 0);
                }
                name = src->read_name(src->context, position);
                if (name && !dst->write_name(dst->context, target_position, name))
                {
                    mm_chunk_delete(&dst->edit, node.type, item.destination);
                    result = MM_CHUNK_IO;
                    goto done;
                }
                if (runtime)
                    dst->edit.set_runtime(dst->edit.context, node.type, item.destination, runtime);
                map_find(&map, node.type, node.number, NULL, &map_position);
                if (mm_array_insert(&map, map_position, &item, 1) != MM_STORAGE_OK)
                {
                    if (created)
                        mm_chunk_delete(&dst->edit, node.type, item.destination);
                    result = MM_CHUNK_MEMORY;
                    goto done;
                }
                free_root = 1;
            }
        }
        if (flags & MM_WALK_ROOT)
        {
            root = item.destination;
            new_root = !skip;
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
        if (mm_chunk_adopt(&dst->edit, parent.type, parent_item.destination, &link, !skip) != OK)
        {
            if (!skip)
                mm_chunk_delete(&dst->edit, node.type, item.destination);
            result = MM_CHUNK_IO;
            goto done;
        }
    }
    if (created)
        *created = new_root;
    if (!created || new_root)
        loner(dst, type, root);
    *copied = root;
done:
    mm_storage_dispose(&storage);
    if (result != OK && free_root)
        mm_chunk_delete(&dst->edit, type, root);
    mm_chunk_walk_dispose(&walk);
    return result;
}

int mm_chunk_copy(const MMChunkEndpoint *src, const MMChunkEndpoint *dst, MMU32 type,
                  MMU32 number, int clone, MMU32 *copied)
{
    return copy_chunk(src, dst, type, number, clone, NULL, copied, NULL);
}

int mm_chunk_copy_reference(const MMChunkEndpoint *src, const MMChunkEndpoint *dst, MMU32 type,
                            MMU32 number, MMU32 *copied, int *created)
{
    if (!created)
        return BAD;
    return copy_chunk(src, dst, type, number, 0, NULL, copied, created);
}

int mm_chunk_copy_filtered(const MMChunkEndpoint *src, const MMChunkEndpoint *dst, MMU32 type,
                           MMU32 number, int (*reuse)(MMU32), MMU32 *copied)
{
    int created;
    return copy_chunk(src, dst, type, number, reuse == NULL, reuse, copied, &created);
}
