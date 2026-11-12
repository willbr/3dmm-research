/* Graph traversal converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_graph.h"
#include <string.h>
typedef char FrameMustBe16Bytes[sizeof(ThreeMMChunkWalkFrame) == 16 ? 1 : -1];
static ThreeMMArray stack_view(ThreeMMChunkWalk *walk)
{
    ThreeMMArray view;
    memset(&view, 0, sizeof(view));
    view.storage = &walk->stack;
    view.width = sizeof(ThreeMMChunkWalkFrame);
    view.count = walk->depth;
    view.free_count = -1;
    return view;
}
static int push(ThreeMMChunkWalk *walk)
{
    ThreeMMArray view = stack_view(walk);
    if (!walk->created)
    {
        if (three_mm_array_reserve(&view, 10, 0) != THREE_MM_STORAGE_OK)
            return 0;
        walk->created = 1;
    }
    if (three_mm_array_add(&view, &walk->current, NULL) != THREE_MM_STORAGE_OK)
        return 0;
    walk->depth = view.count;
    return 1;
}
static int pop(ThreeMMChunkWalk *walk)
{
    ThreeMMArray view = stack_view(walk);
    if (!walk->depth || three_mm_array_pop(&view, &walk->current) != THREE_MM_STORAGE_OK)
        return 0;
    walk->depth = view.count;
    return 1;
}
int three_mm_chunk_walk_init(ThreeMMChunkWalk *walk, const ThreeMMStorageAllocator *allocator)
{
    ThreeMMChunkWalk fresh;
    if (!walk)
        return THREE_MM_CHUNK_BAD_FILE;
    memset(&fresh, 0, sizeof(fresh));
    if (three_mm_storage_init(&fresh.stack, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    fresh.stack.min_grow[0] = 128;
    fresh.stack.min_grow[1] = 16;
    fresh.phase = THREE_MM_WALK_DONE;
    *walk = fresh;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_walk_dispose(ThreeMMChunkWalk *walk)
{
    if (!walk || three_mm_storage_dispose(&walk->stack) != THREE_MM_STORAGE_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    walk->depth = 0;
    walk->created = 0;
    walk->phase = THREE_MM_WALK_DONE;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_walk_reset(ThreeMMChunkWalk *walk, const ThreeMMChunkGraph *graph, ThreeMMU32 type,
                              ThreeMMU32 number)
{
    if (!walk || !graph || !graph->children || !graph->child || three_mm_chunk_walk_dispose(walk) != THREE_MM_CHUNK_OK)
        return THREE_MM_CHUNK_BAD_FILE;
    walk->graph = *graph;
    memset(&walk->current, 0, sizeof(walk->current));
    walk->current.node.type = type;
    walk->current.node.number = number;
    walk->phase = THREE_MM_WALK_START;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_walk_next(ThreeMMChunkWalk *walk, int skip, ThreeMMChunkChild *node, ThreeMMChunkChild *parent,
                             ThreeMMU32 *flags)
{
    ThreeMMChunkChild result, ancestor;
    ThreeMMU32 output = 0;
    if (!walk || !node || !flags)
        return 0;
    switch (walk->phase)
    {
    case THREE_MM_WALK_START:
        output = THREE_MM_WALK_PRE | THREE_MM_WALK_ROOT;
        if (!walk->graph.children(walk->graph.context, walk->current.node.type, walk->current.node.number))
            goto post;
        result = walk->current.node;
        walk->current.next = 0;
        walk->phase = THREE_MM_WALK_GO;
        break;
    case THREE_MM_WALK_GO:
        if (skip && !pop(walk))
            goto done;
        /* fall through */
    case THREE_MM_WALK_NO_SKIP:
        if (!walk->graph.child(walk->graph.context, walk->current.node.type, walk->current.node.number,
                               walk->current.next++, &result))
        {
        post:
            output |= THREE_MM_WALK_POST;
            result = walk->current.node;
            if (!pop(walk))
            {
                output |= THREE_MM_WALK_ROOT;
                three_mm_chunk_walk_dispose(walk);
            }
            else
            {
                walk->phase = THREE_MM_WALK_NO_SKIP;
                ancestor = walk->current.node;
            }
            break;
        }
        ancestor = walk->current.node;
        if (walk->graph.children(walk->graph.context, result.type, result.number))
        {
            if (!push(walk))
            {
                output |= THREE_MM_WALK_ERROR;
                goto leaf;
            }
            walk->current.node = result;
            walk->current.next = 0;
            output |= THREE_MM_WALK_PRE;
            walk->phase = THREE_MM_WALK_GO;
        }
        else
        {
        leaf:
            output |= THREE_MM_WALK_PRE | THREE_MM_WALK_POST;
            walk->phase = THREE_MM_WALK_NO_SKIP;
        }
        break;
    default:
    done:
        three_mm_chunk_walk_dispose(walk);
        return 0;
    }
    *node = result;
    *flags = output;
    if (parent && !(output & THREE_MM_WALK_ROOT))
        *parent = ancestor;
    return 1;
}
int three_mm_chunk_reaches(const ThreeMMChunkGraph *graph, const ThreeMMStorageAllocator *allocator, ThreeMMU32 type,
                           ThreeMMU32 number, ThreeMMU32 target_type, ThreeMMU32 target_number, ThreeMMU32 references)
{
    ThreeMMChunkWalk walk;
    ThreeMMChunkChild node;
    ThreeMMU32 flags;
    int result = 0;
    if (!references)
        return 0;
    if (three_mm_chunk_walk_init(&walk, allocator) != THREE_MM_CHUNK_OK)
        return 2;
    if (three_mm_chunk_walk_reset(&walk, graph, type, number) != THREE_MM_CHUNK_OK)
        result = 2;
    else
        while (three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags))
        {
            if (node.type == target_type && node.number == target_number)
            {
                result = 1;
                break;
            }
            if (flags & THREE_MM_WALK_ERROR)
            {
                result = 2;
                break;
            }
        }
    three_mm_chunk_walk_dispose(&walk);
    return result;
}
