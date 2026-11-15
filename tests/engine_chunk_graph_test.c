#include "mm/chunk_graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 graph failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Graph
{
    MMU32 counts[80];
    MMChunkChild children[80][4];
} Graph;
typedef struct Allocator
{
    int calls, live, releases, fail_all, fail_speed;
    MMU32 limit;
} Allocator;
typedef struct Event
{
    MMChunkChild node, parent;
    MMU32 flags;
} Event;
typedef struct Reference
{
    Graph *graph;
    Event events[256];
    int count, mode;
} Reference;
static MMU32 children(void *context, MMU32 type, MMU32 number)
{
    Graph *g = (Graph *)context;
    CHECK(type == 10 && number < 80);
    return g->counts[number];
}
static int child(void *context, MMU32 type, MMU32 number, MMU32 i, MMChunkChild *out)
{
    Graph *g = (Graph *)context;
    CHECK(type == 10 && number < 80);
    if (i >= g->counts[number])
        return 0;
    *out = g->children[number][i];
    return 1;
}
static void edge(Graph *g, MMU32 parent, MMU32 target)
{
    MMChunkChild *out = &g->children[parent][g->counts[parent]];
    out->type = 10;
    out->number = target;
    out->slot = 100 + g->counts[parent]++;
}
static void *resize(void *context, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Allocator *a = (Allocator *)context;
    void *fresh;
    ++a->calls;
    if (a->fail_all || (a->limit && bytes > a->limit) || (a->fail_speed && priority == MM_STORAGE_SPEED))
        return NULL;
    fresh = malloc(bytes);
    CHECK(fresh != NULL);
    if (old)
    {
        memcpy(fresh, old, bytes < old_bytes ? bytes : old_bytes);
        memset(old, 0xa3, old_bytes);
        free(old);
    }
    else
        ++a->live;
    return fresh;
}
static void release(void *context, void *data, MMU32 bytes)
{
    Allocator *a = (Allocator *)context;
    CHECK(data && a->live == 1);
    memset(data, 0xa3, bytes);
    free(data);
    --a->live;
    ++a->releases;
}
static int emit(Reference *r, MMChunkChild node, MMChunkChild parent, MMU32 flags)
{
    int skip;
    Event *e;
    CHECK(r->count < 256);
    e = r->events + r->count;
    memset(e, 0, sizeof(*e));
    e->node = node;
    e->flags = flags;
    if (!(flags & MM_WALK_ROOT))
        e->parent = parent;
    skip = r->mode && (r->count + r->mode) % (r->mode + 1) == 0;
    ++r->count;
    return skip;
}
static void reference_visit(Reference *r, MMChunkChild node, MMChunkChild parent, int root)
{
    MMU32 i, flags = root ? MM_WALK_ROOT : 0;
    if (!r->graph->counts[node.number])
    {
        emit(r, node, parent, flags | MM_WALK_PRE | MM_WALK_POST);
        return;
    }
    if (emit(r, node, parent, flags | MM_WALK_PRE))
        return;
    for (i = 0; i < r->graph->counts[node.number]; ++i)
        reference_visit(r, r->graph->children[node.number][i], node, 0);
    emit(r, node, parent, flags | MM_WALK_POST);
}
static void comparisons(void)
{
    Graph graph;
    Allocator allocator;
    Reference reference;
    MMChunkGraph io;
    MMStorageAllocator memory;
    MMChunkWalk walk;
    MMChunkChild root, node, parent, sentinel;
    MMU32 flags;
    int mode, i, skip;
    memset(&graph, 0, sizeof(graph));
    memset(&allocator, 0, sizeof(allocator));
    edge(&graph, 0, 1);
    edge(&graph, 0, 2);
    edge(&graph, 1, 3);
    edge(&graph, 1, 4);
    edge(&graph, 2, 3);
    edge(&graph, 4, 5);
    io.context = &graph;
    io.children = children;
    io.child = child;
    memory.context = &allocator;
    memory.resize = resize;
    memory.release = release;
    root.type = 10;
    root.number = root.slot = 0;
    memset(&sentinel, 0x6b, sizeof(sentinel));
    CHECK(mm_chunk_walk_init(&walk, &memory) == MM_CHUNK_OK);
    for (mode = 0; mode < 8; ++mode)
    {
        memset(&reference, 0, sizeof(reference));
        reference.graph = &graph;
        reference.mode = mode;
        reference_visit(&reference, root, sentinel, 1);
        CHECK(mm_chunk_walk_reset(&walk, &io, 10, 0) == MM_CHUNK_OK);
        i = 0;
        skip = 0;
        while ((parent = sentinel), mm_chunk_walk_next(&walk, skip, &node, &parent, &flags))
        {
            CHECK(i < reference.count && !memcmp(&node, &reference.events[i].node, sizeof(node)) &&
                  flags == reference.events[i].flags);
            if (flags & MM_WALK_ROOT)
                CHECK(!memcmp(&parent, &sentinel, sizeof(parent)));
            else
                CHECK(!memcmp(&parent, &reference.events[i].parent, sizeof(parent)));
            skip = mode && (i + mode) % (mode + 1) == 0;
            ++i;
        }
        CHECK(i == reference.count && !walk.depth && !allocator.live && !walk.stack.buffers[0].data);
        node = sentinel;
        flags = 123;
        CHECK(!mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && flags == 123 &&
              !memcmp(&node, &sentinel, sizeof(node)));
    }
    CHECK(mm_chunk_reaches(&io, &memory, 10, 0, 10, 3, 2) == 1 && !allocator.live);
    CHECK(mm_chunk_reaches(&io, &memory, 10, 0, 10, 79, 1) == 0 && !allocator.live);
    i = allocator.calls;
    CHECK(mm_chunk_reaches(&io, &memory, 10, 0, 10, 0, 0) == 0 && allocator.calls == i);
    CHECK(mm_chunk_walk_dispose(&walk) == MM_CHUNK_OK);
}
static void failures(void)
{
    Graph graph;
    Allocator allocator;
    MMStorageAllocator memory;
    MMChunkGraph io;
    MMChunkWalk walk;
    MMChunkChild node;
    MMU32 flags;
    int events, errors, i;
    memset(&graph, 0, sizeof(graph));
    memset(&allocator, 0, sizeof(allocator));
    edge(&graph, 0, 1);
    edge(&graph, 0, 3);
    edge(&graph, 1, 2);
    io.context = &graph;
    io.children = children;
    io.child = child;
    memory.context = &allocator;
    memory.resize = resize;
    memory.release = release;
    CHECK(mm_chunk_walk_init(&walk, &memory) == MM_CHUNK_OK);
    allocator.fail_all = 1;
    CHECK(mm_chunk_walk_reset(&walk, &io, 10, 0) == MM_CHUNK_OK);
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && flags == (MM_WALK_PRE | MM_WALK_ROOT));
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && node.number == 1 &&
          flags == (MM_WALK_PRE | MM_WALK_POST | MM_WALK_ERROR));
    CHECK(mm_chunk_walk_next(&walk, 1, &node, NULL, &flags) && node.number == 3 &&
          flags == (MM_WALK_PRE | MM_WALK_POST));
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) &&
          flags == (MM_WALK_POST | MM_WALK_ROOT));
    CHECK(!allocator.live);
    CHECK(mm_chunk_reaches(&io, &memory, 10, 0, 10, 1, 1) == 1); /* Match precedes ERROR. */
    CHECK(mm_chunk_reaches(&io, &memory, 10, 0, 10, 2, 1) == 2);
    memset(&graph, 0, sizeof(graph));
    for (i = 0; i < 63; ++i)
        edge(&graph, (MMU32)i, (MMU32)i + 1);
    allocator.fail_all = 0;
    allocator.limit = 288;
    CHECK(mm_chunk_walk_reset(&walk, &io, 10, 0) == MM_CHUNK_OK);
    events = errors = 0;
    while (mm_chunk_walk_next(&walk, 0, &node, NULL, &flags))
    {
        CHECK(node.number <= 19);
        ++events;
        if (flags & MM_WALK_ERROR)
            ++errors;
    }
    CHECK(events == 39 && errors == 1 && !allocator.live);
    allocator.limit = 0;
    allocator.fail_speed = 1;
    CHECK(mm_chunk_walk_reset(&walk, &io, 10, 0) == MM_CHUNK_OK);
    events = 0;
    while (mm_chunk_walk_next(&walk, 0, &node, NULL, &flags))
    {
        CHECK(!(flags & MM_WALK_ERROR));
        ++events;
    }
    CHECK(events == 127 && !allocator.live);
    allocator.fail_speed = 0;
    CHECK(mm_chunk_walk_reset(&walk, &io, 10, 0) == MM_CHUNK_OK);
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags));
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && allocator.live == 1);
    CHECK(mm_chunk_walk_reset(&walk, &io, 10, 63) == MM_CHUNK_OK && !allocator.live);
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) &&
          flags == (MM_WALK_PRE | MM_WALK_POST | MM_WALK_ROOT));
    CHECK(mm_chunk_walk_reset(&walk, &io, 10, 0) == MM_CHUNK_OK);
    CHECK(mm_chunk_walk_next(&walk, 0, &node, NULL, &flags));
    CHECK(!mm_chunk_walk_next(&walk, 1, &node, NULL, &flags) && !allocator.live);
    CHECK(mm_chunk_walk_dispose(&walk) == MM_CHUNK_OK);
    CHECK(mm_chunk_walk_dispose(&walk) == MM_CHUNK_OK);
}
int main(void)
{
    comparisons();
    failures();
    return 0;
}
