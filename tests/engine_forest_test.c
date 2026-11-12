#include "three_mm/chunk_forest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "forest check failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
#define ROOT 0x524f4f54UL
#define LEAF 0x4c454146UL
#define ZERO 0x5a45524fUL
static const unsigned char root_data[] = "root";
static const unsigned char leaf_data[] = "leaf-data";
typedef struct Host
{
    unsigned char wire[8192];
    ThreeMMU32 size, positions[256], flags[256], parents[256], slots[256], types[256];
    int added, adopted, reads, writes, copies, fail_read, fail_write, fail_copy, fail_add, fail_adopt;
} Host;
typedef struct Memory
{
    int live, calls, fail, speed;
    ThreeMMU32 limit;
} Memory;
static void *allocate(void *c, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
{
    Memory *m = (Memory *)c;
    void *p;
    ++m->calls;
    if (m->fail || (m->speed && priority == THREE_MM_STORAGE_SPEED) || (m->limit && bytes > m->limit))
        return NULL;
    p = malloc(bytes);
    CHECK(p != NULL);
    if (old)
    {
        memcpy(p, old, bytes < before ? bytes : before);
        free(old);
    }
    else
        ++m->live;
    return p;
}
static void release(void *c, void *p, ThreeMMU32 bytes)
{
    Memory *m = (Memory *)c;
    (void)bytes;
    --m->live;
    free(p);
}
static ThreeMMU32 children(void *c, ThreeMMU32 type, ThreeMMU32 number)
{
    (void)c;
    (void)number;
    return type == ROOT ? 2 : type == ZERO ? 1 : 0;
}
static int child(void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 position, ThreeMMChunkChild *out)
{
    (void)c;
    (void)number;
    if (type == ROOT && position < 2)
    {
        out->type = position ? ZERO : LEAF;
        out->number = position ? 2 : 9;
        out->slot = position ? 40 : 30;
        return 1;
    }
    if (type == ZERO && !position)
    {
        out->type = LEAF;
        out->number = 9;
        out->slot = 50;
        return 1;
    }
    return 0;
}
static int entry(void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMChunkEntry *out)
{
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->number = number;
    out->child_count = children(c, type, number);
    out->bytes = type == ROOT ? 4 : type == LEAF ? 9 : 0;
    out->flags = type == ROOT   ? THREE_MM_CHUNK_FOREST
                 : type == LEAF ? THREE_MM_CHUNK_PACKED | THREE_MM_CHUNK_FOREST
                                : 0;
    return 1;
}
static int write_wire(void *c, ThreeMMU32 offset, const void *p, size_t bytes)
{
    Host *h = (Host *)c;
    if (++h->writes == h->fail_write)
        return 0;
    CHECK(offset <= sizeof(h->wire) && bytes <= sizeof(h->wire) - offset);
    memcpy(h->wire + offset, p, bytes);
    if (offset + bytes > h->size)
        h->size = offset + (ThreeMMU32)bytes;
    return 1;
}
static int copy_data(void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    Host *h = (Host *)c;
    (void)number;
    if (++h->copies == h->fail_copy)
        return 0;
    CHECK(bytes == (type == ROOT ? 4UL : type == LEAF ? 9UL : 0UL));
    CHECK(offset <= sizeof(h->wire) && bytes <= sizeof(h->wire) - offset);
    if (bytes)
        memcpy(h->wire + offset, type == ROOT ? root_data : leaf_data, bytes);
    if (offset + bytes > h->size)
        h->size = offset + bytes;
    return 1;
}
static int read_wire(void *c, ThreeMMU32 offset, void *p, size_t bytes)
{
    Host *h = (Host *)c;
    if (++h->reads == h->fail_read)
        return 0;
    CHECK(offset <= h->size && bytes <= h->size - offset);
    memcpy(p, h->wire + offset, bytes);
    return 1;
}
static int add(void *c, ThreeMMU32 type, ThreeMMU32 offset, ThreeMMU32 bytes, ThreeMMU32 flags, int copy,
               ThreeMMU32 *number)
{
    Host *h = (Host *)c;
    (void)bytes;
    if (h->added + 1 == h->fail_add)
        return 0;
    CHECK(h->added < 256);
    CHECK(!copy || !(flags & THREE_MM_CHUNK_FOREST));
    *number = (ThreeMMU32)h->added;
    h->positions[h->added] = offset;
    h->flags[h->added] = flags;
    h->types[h->added] = type;
    ++h->added;
    return 1;
}
static int adopt(void *c, ThreeMMU32 parent_type, ThreeMMU32 parent_number, ThreeMMU32 type, ThreeMMU32 number,
                 ThreeMMU32 slot)
{
    Host *h = (Host *)c;
    (void)parent_type;
    (void)type;
    if (h->adopted + 1 == h->fail_adopt)
        return 0;
    CHECK(number < (ThreeMMU32)h->added && parent_number < number);
    h->parents[number] = parent_number;
    h->slots[number] = slot;
    ++h->adopted;
    return 1;
}
static void reset(Host *h)
{
    h->added = h->adopted = h->reads = h->writes = h->copies = 0;
    h->fail_read = h->fail_write = h->fail_copy = h->fail_add = h->fail_adopt = 0;
}
static void put32(unsigned char *p, ThreeMMU32 n)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (i * 8));
}
int main(void)
{
    Host h;
    Memory m;
    ThreeMMStorageAllocator memory;
    ThreeMMForestExport output;
    ThreeMMForestImport input;
    ThreeMMReadStream stream;
    ThreeMMU32 size, hash;
    unsigned i;
    int big, copy, failure;
    memset(&h, 0, sizeof(h));
    memset(&m, 0, sizeof(m));
    memset(&output, 0, sizeof(output));
    memory.context = &m;
    memory.resize = allocate;
    memory.release = release;
    output.context = &h;
    output.graph.context = &h;
    output.graph.children = children;
    output.graph.child = child;
    output.entry = entry;
    output.write = write_wire;
    output.copy = copy_data;
    output.system = 0x0303;
    input.context = &h;
    input.add = add;
    input.adopt = adopt;
    stream.context = &h;
    stream.read_at = read_wire;
    stream.length = sizeof(h.wire);
    for (big = 0; big < 2; ++big)
    {
        output.big_endian = big;
        reset(&h);
        CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 7, &size) == THREE_MM_CHUNK_OK && size == 118 &&
              h.size == 125 && !m.live);
        if (!big)
        {
            hash = 2166136261UL;
            for (i = 7; i < 125; ++i)
                hash = (hash ^ h.wire[i]) * 16777619UL;
            CHECK(hash == 0x2c02e752UL);
        }
        for (copy = 0; copy < 2; ++copy)
        {
            reset(&h);
            CHECK(three_mm_forest_read(&stream, 7, size, &input, &memory, copy) == THREE_MM_CHUNK_OK);
            CHECK(h.added == 4 && h.adopted == 3 && !m.live);
            CHECK(h.types[0] == ROOT && h.types[1] == LEAF && h.types[2] == ZERO && h.types[3] == LEAF);
            CHECK(h.positions[0] == 31 && h.positions[1] == 59 && h.positions[2] == 92 && h.positions[3] == 116);
            CHECK(h.flags[0] == (copy ? 0UL : THREE_MM_CHUNK_FOREST) && h.flags[1] == (copy ? 4UL : 20UL));
            CHECK(h.parents[1] == 0 && h.parents[2] == 0 && h.parents[3] == 2 && h.slots[1] == 30 && h.slots[3] == 50);
        }
    }
    output.big_endian = 0;
    reset(&h);
    output.write = NULL;
    CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 0, &size) == THREE_MM_CHUNK_OK && size == 118 && !h.writes &&
          !h.copies && !m.live);
    output.write = write_wire;
    for (failure = 1; failure <= 4; ++failure)
    {
        reset(&h);
        size = 999;
        h.fail_write = failure;
        CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 7, &size) == THREE_MM_CHUNK_IO && size == 999 &&
              !m.live && h.copies == failure - 1);
        reset(&h);
        size = 999;
        h.fail_copy = failure;
        CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 7, &size) == THREE_MM_CHUNK_IO && size == 999 &&
              !m.live && h.writes == failure);
    }
    reset(&h);
    CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 7, &size) == THREE_MM_CHUNK_OK);
    for (failure = 1; failure <= 4; ++failure)
    {
        reset(&h);
        h.fail_read = failure;
        CHECK(three_mm_forest_read(&stream, 7, size, &input, &memory, 0) == THREE_MM_CHUNK_IO && !m.live &&
              h.added == failure - 1);
        reset(&h);
        h.fail_add = failure;
        CHECK(three_mm_forest_read(&stream, 7, size, &input, &memory, 0) == THREE_MM_CHUNK_IO && !m.live &&
              h.added == failure - 1);
        if (failure < 4)
        {
            reset(&h);
            h.fail_adopt = failure;
            CHECK(three_mm_forest_read(&stream, 7, size, &input, &memory, 0) == THREE_MM_CHUNK_IO && !m.live &&
                  h.adopted == failure - 1);
        }
    }
    reset(&h);
    m.fail = 1;
    CHECK(three_mm_forest_read(&stream, 7, size, &input, &memory, 0) == THREE_MM_CHUNK_MEMORY && h.added == 1 &&
          !m.live);
    size = 999;
    CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 7, &size) == THREE_MM_CHUNK_MEMORY && size == 999 &&
          !m.live);
    m.fail = 0;
    m.speed = 1;
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 7, 118, &input, &memory, 0) == THREE_MM_CHUNK_OK && !m.live);
    m.speed = 0;
    for (i = 1; i < 24; ++i)
    {
        reset(&h);
        CHECK(three_mm_forest_read(&stream, 7, i, &input, &memory, 0) == THREE_MM_CHUNK_BAD_FILE && !h.added &&
              !m.live);
    }
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 7, 117, &input, &memory, 0) == THREE_MM_CHUNK_BAD_FILE && !m.live);
    h.wire[7] = 0x7f;
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 7, 118, &input, &memory, 0) == THREE_MM_CHUNK_BAD_FILE && !h.added);
    h.wire[7] = 1;
    put32(h.wire + 7 + 12, 0xffffffffUL);
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 7, 118, &input, &memory, 0) == THREE_MM_CHUNK_BAD_FILE && !h.added);
    put32(h.wire + 7 + 12, 4);
    put32(h.wire + 7 + 16, 0xffffffffUL);
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 7, 28, &input, &memory, 0) == THREE_MM_CHUNK_OK && h.added == 1 && !h.adopted);
    put32(h.wire + 7 + 16, 1);
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 7, 28, &input, &memory, 0) == THREE_MM_CHUNK_BAD_FILE && !m.live);
    reset(&h);
    stream.length = 0;
    CHECK(three_mm_forest_read(&stream, 900, 0, &input, &memory, 0) == THREE_MM_CHUNK_OK && !h.reads && !m.live);
    stream.length = sizeof(h.wire);
    reset(&h);
    size = 999;
    CHECK(three_mm_forest_write(&output, &memory, ROOT, 7, 0x7fffffffUL, &size) == THREE_MM_CHUNK_BAD_FILE &&
          size == 999 && !h.writes && !m.live);
    /* Deep nesting forces stack growth, relocation and failed growth cleanup. */
    memset(h.wire, 0, sizeof(h.wire));
    h.size = 200 * 24;
    for (i = 0; i < 200; ++i)
    {
        h.wire[i * 24] = 1;
        put32(h.wire + i * 24 + 4, ROOT);
        put32(h.wire + i * 24 + 8, i);
        put32(h.wire + i * 24 + 16, i < 199 ? 1 : 0);
    }
    reset(&h);
    CHECK(three_mm_forest_read(&stream, 0, h.size, &input, &memory, 0) == THREE_MM_CHUNK_OK && h.added == 200 &&
          h.adopted == 199 && !m.live);
    for (i = 1; i < 200; ++i)
        CHECK(h.parents[i] == i - 1 && h.slots[i] == i);
    reset(&h);
    m.limit = 128;
    CHECK(three_mm_forest_read(&stream, 0, h.size, &input, &memory, 0) == THREE_MM_CHUNK_MEMORY && h.added > 1 &&
          h.added < 200 && !m.live);
    return 0;
}