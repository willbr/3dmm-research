/* Graph traversal converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_graph.h"
#include <string.h>
typedef char FrameMustBe16Bytes[sizeof(MMChunkWalkFrame) == 16 ? 1 : -1];
static MMArray stack_view(MMChunkWalk *walk)
{
    MMArray view;
    memset(&view, 0, sizeof(view));
    view.storage = &walk->stack;
    view.width = sizeof(MMChunkWalkFrame);
    view.count = walk->depth;
    view.free_count = -1;
    return view;
}
static int push(MMChunkWalk *walk)
{
    MMArray view = stack_view(walk);
    if (!walk->created)
    {
        if (mm_array_reserve(&view, 10, 0) != MM_STORAGE_OK)
            return 0;
        walk->created = 1;
    }
    if (mm_array_add(&view, &walk->current, NULL) != MM_STORAGE_OK)
        return 0;
    walk->depth = view.count;
    return 1;
}
static int pop(MMChunkWalk *walk)
{
    MMArray view = stack_view(walk);
    if (!walk->depth || mm_array_pop(&view, &walk->current) != MM_STORAGE_OK)
        return 0;
    walk->depth = view.count;
    return 1;
}
int mm_chunk_walk_init(MMChunkWalk *walk, const MMStorageAllocator *allocator)
{
    MMChunkWalk fresh;
    if (!walk)
        return MM_CHUNK_BAD_FILE;
    memset(&fresh, 0, sizeof(fresh));
    if (mm_storage_init(&fresh.stack, allocator) != MM_STORAGE_OK)
        return MM_CHUNK_BAD_FILE;
    fresh.stack.min_grow[0] = 128;
    fresh.stack.min_grow[1] = 16;
    fresh.phase = MM_WALK_DONE;
    *walk = fresh;
    return MM_CHUNK_OK;
}
int mm_chunk_walk_dispose(MMChunkWalk *walk)
{
    if (!walk || mm_storage_dispose(&walk->stack) != MM_STORAGE_OK)
        return MM_CHUNK_BAD_FILE;
    walk->depth = 0;
    walk->created = 0;
    walk->phase = MM_WALK_DONE;
    return MM_CHUNK_OK;
}
int mm_chunk_walk_reset(MMChunkWalk *walk, const MMChunkGraph *graph, MMU32 type,
                              MMU32 number)
{
    if (!walk || !graph || !graph->children || !graph->child || mm_chunk_walk_dispose(walk) != MM_CHUNK_OK)
        return MM_CHUNK_BAD_FILE;
    walk->graph = *graph;
    memset(&walk->current, 0, sizeof(walk->current));
    walk->current.node.type = type;
    walk->current.node.number = number;
    walk->phase = MM_WALK_START;
    return MM_CHUNK_OK;
}
int mm_chunk_walk_next(MMChunkWalk *walk, int skip, MMChunkChild *node, MMChunkChild *parent,
                             MMU32 *flags)
{
    MMChunkChild result, ancestor;
    MMU32 output = 0;
    if (!walk || !node || !flags)
        return 0;
    switch (walk->phase)
    {
    case MM_WALK_START:
        output = MM_WALK_PRE | MM_WALK_ROOT;
        if (!walk->graph.children(walk->graph.context, walk->current.node.type, walk->current.node.number))
            goto post;
        result = walk->current.node;
        walk->current.next = 0;
        walk->phase = MM_WALK_GO;
        break;
    case MM_WALK_GO:
        if (skip && !pop(walk))
            goto done;
        /* fall through */
    case MM_WALK_NO_SKIP:
        if (!walk->graph.child(walk->graph.context, walk->current.node.type, walk->current.node.number,
                               walk->current.next++, &result))
        {
        post:
            output |= MM_WALK_POST;
            result = walk->current.node;
            if (!pop(walk))
            {
                output |= MM_WALK_ROOT;
                mm_chunk_walk_dispose(walk);
            }
            else
            {
                walk->phase = MM_WALK_NO_SKIP;
                ancestor = walk->current.node;
            }
            break;
        }
        ancestor = walk->current.node;
        if (walk->graph.children(walk->graph.context, result.type, result.number))
        {
            if (!push(walk))
            {
                output |= MM_WALK_ERROR;
                goto leaf;
            }
            walk->current.node = result;
            walk->current.next = 0;
            output |= MM_WALK_PRE;
            walk->phase = MM_WALK_GO;
        }
        else
        {
        leaf:
            output |= MM_WALK_PRE | MM_WALK_POST;
            walk->phase = MM_WALK_NO_SKIP;
        }
        break;
    default:
    done:
        mm_chunk_walk_dispose(walk);
        return 0;
    }
    *node = result;
    *flags = output;
    if (parent && !(output & MM_WALK_ROOT))
        *parent = ancestor;
    return 1;
}
int mm_chunk_reaches(const MMChunkGraph *graph, const MMStorageAllocator *allocator, MMU32 type,
                           MMU32 number, MMU32 target_type, MMU32 target_number, MMU32 references)
{
    MMChunkWalk walk;
    MMChunkChild node;
    MMU32 flags;
    int result = 0;
    if (!references)
        return 0;
    if (mm_chunk_walk_init(&walk, allocator) != MM_CHUNK_OK)
        return 2;
    if (mm_chunk_walk_reset(&walk, graph, type, number) != MM_CHUNK_OK)
        result = 2;
    else
        while (mm_chunk_walk_next(&walk, 0, &node, NULL, &flags))
        {
            if (node.type == target_type && node.number == target_number)
            {
                result = 1;
                break;
            }
            if (flags & MM_WALK_ERROR)
            {
                result = 2;
                break;
            }
        }
    mm_chunk_walk_dispose(&walk);
    return result;
}
