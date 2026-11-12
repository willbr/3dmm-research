#include "three_mm/chunk_graph.h"
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
    ThreeMMU32 counts[80];
    ThreeMMChunkChild children[80][4];
} Graph;
typedef struct Allocator
{
    int calls, live, releases, fail_all, fail_speed;
    ThreeMMU32 limit;
} Allocator;
typedef struct Event
{
    ThreeMMChunkChild node, parent;
    ThreeMMU32 flags;
} Event;
typedef struct Reference
{
    Graph *graph;
    Event events[256];
    int count, mode;
} Reference;
static ThreeMMU32 children(void *context, ThreeMMU32 type, ThreeMMU32 number)
{
    Graph *g = (Graph *)context;
    CHECK(type == 10 && number < 80);
    return g->counts[number];
}
static int child(void *context, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 i, ThreeMMChunkChild *out)
{
    Graph *g = (Graph *)context;
    CHECK(type == 10 && number < 80);
    if (i >= g->counts[number])
        return 0;
    *out = g->children[number][i];
    return 1;
}
static void edge(Graph *g, ThreeMMU32 parent, ThreeMMU32 target)
{
    ThreeMMChunkChild *out = &g->children[parent][g->counts[parent]];
    out->type = 10;
    out->number = target;
    out->slot = 100 + g->counts[parent]++;
}
static void *resize(void *context, void *old, ThreeMMU32 old_bytes, ThreeMMU32 bytes, int priority)
{
    Allocator *a = (Allocator *)context;
    void *fresh;
    ++a->calls;
    if (a->fail_all || (a->limit && bytes > a->limit) || (a->fail_speed && priority == THREE_MM_STORAGE_SPEED))
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
static void release(void *context, void *data, ThreeMMU32 bytes)
{
    Allocator *a = (Allocator *)context;
    CHECK(data && a->live == 1);
    memset(data, 0xa3, bytes);
    free(data);
    --a->live;
    ++a->releases;
}
static int emit(Reference *r, ThreeMMChunkChild node, ThreeMMChunkChild parent, ThreeMMU32 flags)
{
    int skip;
    Event *e;
    CHECK(r->count < 256);
    e = r->events + r->count;
    memset(e, 0, sizeof(*e));
    e->node = node;
    e->flags = flags;
    if (!(flags & THREE_MM_WALK_ROOT))
        e->parent = parent;
    skip = r->mode && (r->count + r->mode) % (r->mode + 1) == 0;
    ++r->count;
    return skip;
}
static void reference_visit(Reference *r, ThreeMMChunkChild node, ThreeMMChunkChild parent, int root)
{
    ThreeMMU32 i, flags = root ? THREE_MM_WALK_ROOT : 0;
    if (!r->graph->counts[node.number])
    {
        emit(r, node, parent, flags | THREE_MM_WALK_PRE | THREE_MM_WALK_POST);
        return;
    }
    if (emit(r, node, parent, flags | THREE_MM_WALK_PRE))
        return;
    for (i = 0; i < r->graph->counts[node.number]; ++i)
        reference_visit(r, r->graph->children[node.number][i], node, 0);
    emit(r, node, parent, flags | THREE_MM_WALK_POST);
}
static void comparisons(void)
{
    Graph graph;
    Allocator allocator;
    Reference reference;
    ThreeMMChunkGraph io;
    ThreeMMStorageAllocator memory;
    ThreeMMChunkWalk walk;
    ThreeMMChunkChild root, node, parent, sentinel;
    ThreeMMU32 flags;
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
    CHECK(three_mm_chunk_walk_init(&walk, &memory) == THREE_MM_CHUNK_OK);
    for (mode = 0; mode < 8; ++mode)
    {
        memset(&reference, 0, sizeof(reference));
        reference.graph = &graph;
        reference.mode = mode;
        reference_visit(&reference, root, sentinel, 1);
        CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 0) == THREE_MM_CHUNK_OK);
        i = 0;
        skip = 0;
        while ((parent = sentinel), three_mm_chunk_walk_next(&walk, skip, &node, &parent, &flags))
        {
            CHECK(i < reference.count && !memcmp(&node, &reference.events[i].node, sizeof(node)) &&
                  flags == reference.events[i].flags);
            if (flags & THREE_MM_WALK_ROOT)
                CHECK(!memcmp(&parent, &sentinel, sizeof(parent)));
            else
                CHECK(!memcmp(&parent, &reference.events[i].parent, sizeof(parent)));
            skip = mode && (i + mode) % (mode + 1) == 0;
            ++i;
        }
        CHECK(i == reference.count && !walk.depth && !allocator.live && !walk.stack.buffers[0].data);
        node = sentinel;
        flags = 123;
        CHECK(!three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && flags == 123 &&
              !memcmp(&node, &sentinel, sizeof(node)));
    }
    CHECK(three_mm_chunk_reaches(&io, &memory, 10, 0, 10, 3, 2) == 1 && !allocator.live);
    CHECK(three_mm_chunk_reaches(&io, &memory, 10, 0, 10, 79, 1) == 0 && !allocator.live);
    i = allocator.calls;
    CHECK(three_mm_chunk_reaches(&io, &memory, 10, 0, 10, 0, 0) == 0 && allocator.calls == i);
    CHECK(three_mm_chunk_walk_dispose(&walk) == THREE_MM_CHUNK_OK);
}
static void failures(void)
{
    Graph graph;
    Allocator allocator;
    ThreeMMStorageAllocator memory;
    ThreeMMChunkGraph io;
    ThreeMMChunkWalk walk;
    ThreeMMChunkChild node;
    ThreeMMU32 flags;
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
    CHECK(three_mm_chunk_walk_init(&walk, &memory) == THREE_MM_CHUNK_OK);
    allocator.fail_all = 1;
    CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 0) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && flags == (THREE_MM_WALK_PRE | THREE_MM_WALK_ROOT));
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && node.number == 1 &&
          flags == (THREE_MM_WALK_PRE | THREE_MM_WALK_POST | THREE_MM_WALK_ERROR));
    CHECK(three_mm_chunk_walk_next(&walk, 1, &node, NULL, &flags) && node.number == 3 &&
          flags == (THREE_MM_WALK_PRE | THREE_MM_WALK_POST));
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) &&
          flags == (THREE_MM_WALK_POST | THREE_MM_WALK_ROOT));
    CHECK(!allocator.live);
    CHECK(three_mm_chunk_reaches(&io, &memory, 10, 0, 10, 1, 1) == 1); /* Match precedes ERROR. */
    CHECK(three_mm_chunk_reaches(&io, &memory, 10, 0, 10, 2, 1) == 2);
    memset(&graph, 0, sizeof(graph));
    for (i = 0; i < 63; ++i)
        edge(&graph, (ThreeMMU32)i, (ThreeMMU32)i + 1);
    allocator.fail_all = 0;
    allocator.limit = 288;
    CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 0) == THREE_MM_CHUNK_OK);
    events = errors = 0;
    while (three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags))
    {
        CHECK(node.number <= 19);
        ++events;
        if (flags & THREE_MM_WALK_ERROR)
            ++errors;
    }
    CHECK(events == 39 && errors == 1 && !allocator.live);
    allocator.limit = 0;
    allocator.fail_speed = 1;
    CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 0) == THREE_MM_CHUNK_OK);
    events = 0;
    while (three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags))
    {
        CHECK(!(flags & THREE_MM_WALK_ERROR));
        ++events;
    }
    CHECK(events == 127 && !allocator.live);
    allocator.fail_speed = 0;
    CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 0) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags));
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) && allocator.live == 1);
    CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 63) == THREE_MM_CHUNK_OK && !allocator.live);
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags) &&
          flags == (THREE_MM_WALK_PRE | THREE_MM_WALK_POST | THREE_MM_WALK_ROOT));
    CHECK(three_mm_chunk_walk_reset(&walk, &io, 10, 0) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_walk_next(&walk, 0, &node, NULL, &flags));
    CHECK(!three_mm_chunk_walk_next(&walk, 1, &node, NULL, &flags) && !allocator.live);
    CHECK(three_mm_chunk_walk_dispose(&walk) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_walk_dispose(&walk) == THREE_MM_CHUNK_OK);
}
int main(void)
{
    comparisons();
    failures();
    return 0;
}
