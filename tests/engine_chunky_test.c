#include "three_mm/chunky_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 chunky check failed at line %d\n", line);
        exit(1);
    }
}
#define CHECK(value) check((value), __LINE__)
static void put16(unsigned char *bytes, unsigned int value, int big)
{
    bytes[big ? 1 : 0] = (unsigned char)value;
    bytes[big ? 0 : 1] = (unsigned char)(value >> 8);
}
static void put32(unsigned char *bytes, ThreeMMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
        bytes[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}
/* Three independently encoded records. Root owns the first child twice and
 * the second once; two different targets share a slot and type. */
static size_t fixture(unsigned char *bytes, int prefix_big, int big, int stride, int version)
{
    ThreeMMU32 sizes[3], offsets[3], data, total, i;
    unsigned char *group, *row, *child;
    sizes[0] = stride + 40;
    sizes[1] = sizes[2] = stride;
    offsets[0] = 0;
    offsets[1] = sizes[0];
    offsets[2] = sizes[0] + sizes[1];
    data = sizes[0] + sizes[1] + sizes[2];
    total = 136 + 20 + data + 24;
    memset(bytes, 0, 1024);
    memcpy(bytes, "CHN2", 4);
    put32(bytes + 4, 0x54455354UL, prefix_big);
    put16(bytes + 8, version, prefix_big);
    put16(bytes + 10, version < 4 ? 1 : 4, prefix_big);
    put16(bytes + 12, 1, prefix_big);
    put16(bytes + 14, 0x0303, prefix_big);
    put32(bytes + 16, total, prefix_big);
    put32(bytes + 20, 136, prefix_big);
    put32(bytes + 24, total - 136, prefix_big);
    put32(bytes + 28, total, prefix_big);
    group = bytes + 136;
    put16(group, 1, big);
    put16(group + 2, 0x0303, big);
    put32(group + 4, 3, big);
    put32(group + 8, data, big);
    put32(group + 12, 0xffffffffUL, big);
    put32(group + 16, stride, big);
    for (i = 0; i < 3; ++i)
    {
        row = group + 20 + offsets[i];
        put32(row, i ? 0x42424242UL : 0x41414141UL, big);
        put32(row + 4, i == 2 ? 0xffffffffUL : i + 1, big);
        put32(row + 8, i ? 0 : 128, big);
        if (stride == 20)
        {
            put32(row + 12, i ? 16 : (8U << 8) | 2U, big);
            put16(row + 16, i ? 0 : 3, big);
            put16(row + 18, i ? (i == 1 ? 2 : 1) : 0, big);
        }
        else
        {
            put32(row + 12, i ? 0 : 8, big);
            put32(row + 16, i ? 0 : 3, big);
            put32(row + 20, i ? (i == 1 ? 2 : 1) : 0, big);
            put32(row + 24, 1234, big); /* Runtime ID must not become persistent state. */
            if (version < 4)
            {
                row[28] = 1; /* On-extra is never retained when opening disk indexes. */
                row[29] = i == 0;
                row[30] = i != 0;
            }
            else
                put32(row + 28, i ? 17 : 3, big);
        }
        put32(group + 20 + data + i * 8, offsets[i], big);
        put32(group + 24 + data + i * 8, sizes[i], big);
    }
    row = group + 20;
    child = row + stride;
    for (i = 0; i < 3; ++i)
    {
        put32(child + i * 12, 0x42424242UL, big);
        put32(child + i * 12 + 4, i == 1 ? 0xffffffffUL : 2, big);
        put32(child + i * 12 + 8, i == 2 ? 1 : 0, big);
    }
    memcpy(child + 36, "name", 4);
    return (size_t)total;
}
static void wire_indexes(void)
{
    unsigned char bytes[1024];
    ThreeMMMemoryStream memory;
    ThreeMMChunkPolicy policy;
    ThreeMMChunkIndex *index, *saved;
    ThreeMMChunkAccess access;
    ThreeMMChunkEntry entry;
    ThreeMMChunkChild child;
    ThreeMMChunkHeader header;
    const unsigned char *name;
    ThreeMMU32 count, position;
    size_t length;
    int prefix_big, big, stride, version;
    policy.expanded_runtime = 1;
    policy.native_big_endian = 0;
    policy.max_name_bytes = 516;
    for (prefix_big = 0; prefix_big <= 1; ++prefix_big)
        for (big = 0; big <= 1; ++big)
            for (stride = 20; stride <= 32; stride += 12)
                for (version = 1; version <= 5; ++version)
                {
                    length = fixture(bytes, prefix_big, big, stride, version);
                    CHECK(three_mm_memory_stream_init(&memory, bytes, length));
                    if (version < 4 && stride == 20)
                    {
                        CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_BAD_FILE);
                        continue;
                    }
                    CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_OK);
                    access = three_mm_chunk_index_access(index);
                    CHECK(access.count == 3 && three_mm_chunk_index_header(index)->big_endian == prefix_big);
                    CHECK(three_mm_chunk_find(&access, 0x41414141UL, 1, &position) == THREE_MM_CHUNK_OK &&
                          position == 0);
                    CHECK(three_mm_chunk_at(&access, 0, &entry) == THREE_MM_CHUNK_OK && entry.bytes == 8 &&
                          entry.offset == 128 && entry.flags == THREE_MM_CHUNK_LONER && entry.child_count == 3);
                    CHECK(three_mm_chunk_find(&access, 0x42424242UL, 3, &position) == THREE_MM_CHUNK_NOT_FOUND &&
                          position == 2);
                    CHECK(three_mm_chunk_find(&access, 0xffffffffUL, 0, &position) == THREE_MM_CHUNK_NOT_FOUND &&
                          position == 3);
                    CHECK(three_mm_chunk_find_child(&access, 0, 0, 0x42424242UL, 3, &position) ==
                              THREE_MM_CHUNK_NOT_FOUND &&
                          position == 1);
                    CHECK(three_mm_chunk_child_type(&access, 0, 0, 0x42424242UL, &child) == THREE_MM_CHUNK_OK &&
                          child.number == 2);
                    CHECK(three_mm_chunk_child_at(&access, 0, 1, &child) == THREE_MM_CHUNK_OK &&
                          child.number == 0xffffffffUL);
                    child.slot = 123;
                    CHECK(three_mm_chunk_child_type(&access, 0, 2, 0, &child) == THREE_MM_CHUNK_NOT_FOUND &&
                          child.slot == 123);
                    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_OK);
                    CHECK(three_mm_chunk_type_at(&access, 0x42424242UL, 1, &entry, &position) == THREE_MM_CHUNK_OK &&
                          position == 2 && entry.number == 0xffffffffUL);
                    position = 123;
                    CHECK(three_mm_chunk_type_at(&access, 0x42424242UL, 0xffffffffUL, &entry, &position) ==
                              THREE_MM_CHUNK_NOT_FOUND &&
                          position == 123);
                    CHECK(three_mm_chunk_index_name(index, 0, &name, &count) == THREE_MM_CHUNK_OK && count == 4 &&
                          !memcmp(name, "name", 4));
                    /* Input is no longer borrowed once open returns. Failed opens
                     * leave the prior owning handle and its records intact. */
                    bytes[0] = 'X';
                    saved = index;
                    CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_BAD_FILE &&
                          index == saved);
                    CHECK(three_mm_chunk_at(&access, 0, &entry) == THREE_MM_CHUNK_OK && entry.number == 1);
                    three_mm_chunk_index_close(index);
                }
    length = fixture(bytes, 0, 1, 32, 5);
    CHECK(three_mm_memory_stream_init(&memory, bytes, length));
    policy.expanded_runtime = 0;
    /* Existing expanded-to-compact pre-swap check rejects this opposite-endian
     * row. A port must not silently repair the original reader's decision. */
    CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_BAD_FILE);
    length = fixture(bytes, 0, 0, 20, 5);
    CHECK(three_mm_memory_stream_init(&memory, bytes, length));
    header.creator = 123;
    memory.stream.length = (ThreeMMU32)length - 1;
    CHECK(three_mm_chunk_read_header(&memory.stream, &header) == THREE_MM_CHUNK_BAD_FILE && header.creator == 123);
    memory.stream.length = (ThreeMMU32)length;
    put32(bytes + 136 + 20 + 100, 0xffffffffUL, 0); /* Location points before the GG. */
    CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_BAD_FILE);
    fixture(bytes, 0, 0, 20, 5);
    put32(bytes + 140, 0xffffffffUL, 0); /* Count arithmetic must not wrap. */
    CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_BAD_FILE);
    fixture(bytes, 0, 0, 20, 5);
    put32(bytes + 24, 0xffffffffUL, 0); /* Index extent arithmetic must not wrap. */
    CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_BAD_FILE);
}

typedef struct LiveGraph
{
    ThreeMMChunkEntry entries[4];
    ThreeMMChunkChild children[4][2];
    int fail;
} LiveGraph;

typedef struct FailingInput
{
    unsigned char bytes[1024];
    int calls, fail_at;
} FailingInput;
static int failing_read(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    FailingInput *input = (FailingInput *)context;
    if (++input->calls == input->fail_at)
        return 0;
    memcpy(bytes, input->bytes + offset, count);
    return 1;
}
static void io_failures(void)
{
    FailingInput input;
    ThreeMMReadStream stream;
    ThreeMMChunkPolicy policy = {0, 0, 516};
    ThreeMMChunkIndex *index, *saved;
    ThreeMMChunkAccess access;
    ThreeMMChunkEntry entry;
    int step;
    stream.length = (ThreeMMU32)fixture(input.bytes, 0, 0, 20, 5);
    stream.context = &input;
    stream.read_at = failing_read;
    input.calls = input.fail_at = 0;
    CHECK(three_mm_chunk_index_open(&stream, &policy, &index) == THREE_MM_CHUNK_OK);
    saved = index;
    access = three_mm_chunk_index_access(index);
    for (step = 1; step <= 4; ++step)
    {
        input.calls = 0;
        input.fail_at = step;
        CHECK(three_mm_chunk_index_open(&stream, &policy, &index) == THREE_MM_CHUNK_IO && index == saved);
        CHECK(three_mm_chunk_at(&access, 0, &entry) == THREE_MM_CHUNK_OK && entry.number == 1);
    }
    three_mm_chunk_index_close(index);
}
static int live_entry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    LiveGraph *graph = (LiveGraph *)context;
    if (graph->fail)
        return 0;
    *entry = graph->entries[position];
    return 1;
}
static int live_child(void *context, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *child)
{
    LiveGraph *graph = (LiveGraph *)context;
    *child = graph->children[parent][position];
    return 1;
}
static void diamond(LiveGraph *graph)
{
    int i;
    memset(graph, 0, sizeof(*graph));
    for (i = 0; i < 4; ++i)
    {
        graph->entries[i].type = 10;
        graph->entries[i].number = i + 1;
        graph->entries[i].parent_count = i ? (i == 3 ? 2 : 1) : 0;
        graph->entries[i].child_count = i == 0 ? 2 : i == 3 ? 0 : 1;
        graph->entries[i].variable_bytes = graph->entries[i].child_count * 12;
        graph->children[i][0].type = graph->children[i][1].type = 10;
        graph->children[i][0].number = i == 0 ? 2 : 4;
    }
    graph->children[0][1].number = 3;
    graph->children[0][1].slot = 1;
}
static void graphs(void)
{
    LiveGraph graph;
    ThreeMMChunkAccess access;
    ThreeMMChunkEntry entry;
    ThreeMMU32 position;
    diamond(&graph);
    access.context = &graph;
    access.count = 4;
    access.entry = live_entry;
    access.child = live_child;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_OK);
    graph.entries[1].parent_count = 2;
    graph.entries[3].parent_count = 1;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_OK); /* Retail checks aggregate totals. */
    diamond(&graph);
    graph.entries[3].child_count = 1;
    graph.entries[3].variable_bytes = 12;
    graph.entries[1].parent_count = 2;
    graph.children[3][0].number = 2;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_BAD_FILE); /* Reachable cycle. */
    diamond(&graph);
    graph.children[1][0].number = 99;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_BAD_FILE); /* Missing target. */
    diamond(&graph);
    graph.entries[0].child_count = 0;
    graph.entries[3].parent_count = 0;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_BAD_FILE); /* Orphan subgraph. */
    diamond(&graph);
    graph.entries[0].child_count = 0;
    graph.entries[3].parent_count = 0;
    graph.children[1][0].number = 3;
    graph.children[2][0].number = 2;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_BAD_FILE); /* Unrooted cycle. */
    diamond(&graph);
    graph.entries[3].number = 9;
    CHECK(three_mm_chunk_find(&access, 10, 9, &position) == THREE_MM_CHUNK_OK && position == 3);
    CHECK(three_mm_chunk_find(&access, 10, 4, &position) == THREE_MM_CHUNK_NOT_FOUND && position == 3);
    graph.fail = 1;
    entry.number = 123;
    position = 123;
    CHECK(three_mm_chunk_at(&access, 0, &entry) == THREE_MM_CHUNK_IO && entry.number == 123);
    CHECK(three_mm_chunk_find(&access, 10, 9, &position) == THREE_MM_CHUNK_IO && position == 123);
    access.count = 0;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_OK);
}
static int chain_entry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry)
{
    ThreeMMU32 count = *(ThreeMMU32 *)context;
    memset(entry, 0, sizeof(*entry));
    entry->number = position;
    entry->parent_count = position ? 1 : 0;
    entry->child_count = position + 1 < count ? 1 : 0;
    entry->variable_bytes = entry->child_count * 12;
    return 1;
}
static int chain_child(void *context, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *child)
{
    (void)context;
    (void)position;
    child->type = child->slot = 0;
    child->number = parent + 1;
    return 1;
}
static void deep_graph(void)
{
    ThreeMMU32 count = 10000;
    ThreeMMChunkAccess access;
    access.context = &count;
    access.count = count;
    access.entry = chain_entry;
    access.child = chain_child;
    CHECK(three_mm_chunk_validate_graph(&access, 65535) == THREE_MM_CHUNK_OK);
}
int main(void)
{
    wire_indexes();
    graphs();
    io_failures();
    deep_graph();
    return 0;
}
