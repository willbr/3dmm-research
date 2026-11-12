#include "three_mm/chunk_loading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "loading failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Memory
{
    int live, calls, fail, speed, after;
} Memory;
static void *resize(void *c, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
{
    Memory *m = (Memory *)c;
    void *p;
    ++m->calls;
    if (m->fail == m->calls || m->fail == -1 || (m->after && m->calls >= m->after) ||
        (m->speed && priority == THREE_MM_STORAGE_SPEED))
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
    (void)bytes;
    --((Memory *)c)->live;
    free(p);
}
static void put16(unsigned char *p, unsigned n, int big)
{
    p[big ? 1 : 0] = (unsigned char)n;
    p[big ? 0 : 1] = (unsigned char)(n >> 8);
}
static void put32(unsigned char *p, ThreeMMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (8 * i));
}
typedef struct IO
{
    unsigned char data[2048];
    int calls, fail;
    int *unread;
} IO;
static int read_at(void *c, ThreeMMU32 offset, void *p, size_t n)
{
    IO *io = (IO *)c;
    if (io->unread)
        CHECK(!*io->unread);
    if (++io->calls == io->fail)
        return 0;
    CHECK(offset + n <= sizeof(io->data));
    memcpy(p, io->data + offset, n);
    return 1;
}
static int write_at(void *c, ThreeMMU32 offset, const void *p, size_t n)
{
    IO *io = (IO *)c;
    if (++io->calls == io->fail)
        return 0;
    CHECK(offset + n <= sizeof(io->data));
    memcpy(io->data + offset, p, n);
    return 1;
}
static void maps(void)
{
    Memory m;
    ThreeMMStorageAllocator allocator;
    IO io;
    ThreeMMReadStream stream;
    ThreeMMStorageWriter writer;
    ThreeMMChunkFreeMap *map, *loaded, *sentinel;
    ThreeMMChunkFreeRange range;
    unsigned char expected[28];
    int big, unread, calls;
    unsigned i;
    memset(&m, 0, sizeof(m));
    memset(&io, 0, sizeof(io));
    allocator.context = &m;
    allocator.resize = resize;
    allocator.release = release;
    stream.context = &io;
    stream.length = 28;
    stream.read_at = read_at;
    writer.context = &io;
    writer.length = 28;
    writer.write_at = write_at;
    map = NULL;
    CHECK(three_mm_free_map_create(&allocator, 1, &map) == THREE_MM_CHUNK_OK && map && !map->count);
    range.offset = 128;
    range.bytes = 17;
    CHECK(three_mm_free_map_insert(map, 0, &range) == THREE_MM_CHUNK_OK);
    range.offset = 168;
    range.bytes = 29;
    CHECK(three_mm_free_map_insert(map, 1, &range) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_free_map_wire_size(map) == 28);
    for (big = 0; big < 2; ++big)
    {
        memset(expected, 0, sizeof(expected));
        put16(expected, 1, big);
        put16(expected + 2, 0x0303, big);
        put32(expected + 4, 8, big);
        put32(expected + 8, 2, big);
        put32(expected + 12, 128, big);
        put32(expected + 16, 17, big);
        put32(expected + 20, 168, big);
        put32(expected + 24, 29, big);
        io.calls = 0;
        CHECK(three_mm_free_map_write(map, &writer, big, 0x0303) == THREE_MM_STORAGE_OK && io.calls == 2 &&
              !memcmp(io.data, expected, 28));
        loaded = NULL;
        CHECK(three_mm_free_map_read(&stream, &allocator, &loaded) == THREE_MM_CHUNK_OK && loaded->count == 2);
        CHECK(three_mm_free_map_get(loaded, 1, &range) == THREE_MM_CHUNK_OK && range.offset == 168 &&
              range.bytes == 29);
        three_mm_free_map_close(loaded);
        for (i = 1; i <= 2; ++i)
        {
            io.calls = 0;
            io.fail = (int)i;
            CHECK(three_mm_free_map_write(map, &writer, big, 0x0303) == THREE_MM_STORAGE_IO && io.calls == (int)i);
        }
        io.fail = 0;
    }
    three_mm_free_map_close(map);
    CHECK(!m.live);
    /* Lazy state is cleared before read callbacks and after every failure. */
    put16(io.data, 1, 0);
    put16(io.data + 2, 0x0303, 0);
    put32(io.data + 4, 8, 0);
    put32(io.data + 8, 2, 0);
    put32(io.data + 12, 128, 0);
    put32(io.data + 16, 17, 0);
    put32(io.data + 20, 168, 0);
    put32(io.data + 24, 29, 0);
    io.unread = &unread;
    map = NULL;
    unread = 1;
    io.calls = 0;
    CHECK(three_mm_free_map_load(&map, &unread, &stream, 0, 28, &allocator) == THREE_MM_CHUNK_OK && !unread);
    calls = io.calls;
    unread = 1;
    CHECK(three_mm_free_map_load(&map, &unread, &stream, 0, 28, &allocator) == THREE_MM_CHUNK_BAD_FILE && !unread &&
          io.calls == calls);
    range.offset = 130;
    range.bytes = 15;
    CHECK(three_mm_free_map_put(map, 0, &range) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_free_map_erase(map, 1) == THREE_MM_CHUNK_OK && map->count == 1);
    CHECK(three_mm_free_map_get(map, 0, &range) == THREE_MM_CHUNK_OK && range.offset == 130);
    calls = m.calls;
    m.fail = -1;
    CHECK(three_mm_free_map_put(map, 0, &range) == THREE_MM_CHUNK_OK && m.calls == calls);
    while (map->count < map->storage.buffers[0].capacity / 8)
        CHECK(three_mm_free_map_insert(map, map->count, &range) == THREE_MM_CHUNK_OK);
    i = map->count;
    CHECK(three_mm_free_map_insert(map, map->count, &range) == THREE_MM_CHUNK_MEMORY && map->count == i);
    m.fail = 0;
    three_mm_free_map_close(map);
    map = NULL;
    CHECK(!m.live);
    unread = 1;
    io.calls = 0;
    CHECK(three_mm_free_map_load(&map, &unread, &stream, 0, 0, &allocator) == THREE_MM_CHUNK_OK && !map && !unread &&
          !io.calls);
    for (i = 1; i <= 2; ++i)
    {
        io.calls = 0;
        io.fail = (int)i;
        unread = 1;
        CHECK(three_mm_free_map_load(&map, &unread, &stream, 0, 28, &allocator) == THREE_MM_CHUNK_IO && !map &&
              !unread && !m.live);
    }
    io.fail = 0;
    io.unread = NULL;
    sentinel = (ThreeMMChunkFreeMap *)&io;
    for (i = 0; i < 3; ++i)
    {
        memset(io.data, 0, 12);
        put16(io.data, 1, 0);
        put32(io.data + 4, i == 0 ? 4 : 8, 0);
        put32(io.data + 8, i == 1 ? 3 : 2, 0);
        if (i == 2)
            io.data[0] = 0x7f;
        loaded = sentinel;
        CHECK(three_mm_free_map_read(&stream, &allocator, &loaded) == THREE_MM_CHUNK_BAD_FILE && loaded == sentinel &&
              !m.live);
    }
    put16(io.data, 1, 0);
    put32(io.data + 4, 8, 0);
    put32(io.data + 8, 2, 0);
    /* Fail the record, speculative body allocation and exact body fallback. */
    m.calls = 0;
    m.fail = 1;
    loaded = sentinel;
    CHECK(three_mm_free_map_read(&stream, &allocator, &loaded) == THREE_MM_CHUNK_MEMORY && loaded == sentinel &&
          !m.live);
    m.calls = 0;
    m.fail = -1;
    CHECK(three_mm_free_map_create(&allocator, 0, &loaded) == THREE_MM_CHUNK_MEMORY && !m.live);
    m.fail = 0;
    m.calls = 0;
    m.after = 2;
    CHECK(three_mm_free_map_read(&stream, &allocator, &loaded) == THREE_MM_CHUNK_MEMORY && loaded == sentinel && !m.live);
    m.after = 0;
    m.speed = 1;
    CHECK(three_mm_free_map_read(&stream, &allocator, &loaded) == THREE_MM_CHUNK_OK);
    three_mm_free_map_close(loaded);
    m.speed = 0;
    CHECK(!m.live);
    put32(io.data + 8, 0, 0);
    stream.length = 12;
    loaded = NULL;
    CHECK(three_mm_free_map_read(&stream, &allocator, &loaded) == THREE_MM_CHUNK_OK && !loaded->count);
    writer.length = 12;
    io.calls = 0;
    CHECK(three_mm_free_map_write(loaded, &writer, 0, 0x0303) == THREE_MM_STORAGE_OK && io.calls == 1);
    three_mm_free_map_close(loaded);
    CHECK(!m.live);
}
/* Independent compact/expanded GG fixture with a named root and one child. */
static ThreeMMU32 fixture(unsigned char *p, int big, int version, ThreeMMU32 name)
{
    ThreeMMU32 stride = version < 4 ? 32 : 20, root = stride + 12 + name, rounded = (root + 3) & ~3UL,
               data = rounded + stride, total = 136 + 20 + data + 16;
    unsigned char *g, *r;
    memset(p, 0, 2048);
    memcpy(p, "CHN2", 4);
    put16(p + 8, version, big);
    put16(p + 10, version < 4 ? 1 : 4, big);
    put16(p + 12, 1, big);
    put16(p + 14, 0x0303, big);
    put32(p + 16, total, big);
    put32(p + 20, 136, big);
    put32(p + 24, total - 136, big);
    put32(p + 28, total, big);
    g = p + 136;
    put16(g, 1, big);
    put16(g + 2, 0x0303, big);
    put32(g + 4, 2, big);
    put32(g + 8, data, big);
    put32(g + 12, 0xffffffffUL, big);
    put32(g + 16, stride, big);
    r = g + 20;
    put32(r, 0x41414141UL, big);
    put32(r + 4, 1, big);
    put32(r + 8, 128, big);
    if (stride == 20)
    {
        put32(r + 12, (8UL << 8) | 2, big);
        put16(r + 16, 1, big);
    }
    else
    {
        put32(r + 12, 8, big);
        put32(r + 16, 1, big);
        r[29] = 1;
    }
    put32(r + stride, 0x42424242UL, big);
    put32(r + stride + 4, 2, big);
    put32(r + stride + 8, 7, big);
    memset(r + stride + 12, 'N', name);
    r = g + 20 + rounded;
    put32(r, 0x42424242UL, big);
    put32(r + 4, 2, big);
    if (stride == 20)
        put16(r + 18, 1, big);
    else
        put32(r + 20, 1, big);
    put32(g + 20 + data, 0, big);
    put32(g + 24 + data, root, big);
    put32(g + 28 + data, rounded, big);
    put32(g + 32 + data, stride, big);
    return total;
}
typedef struct Material
{
    int inserts, converts, fail, fail_convert;
    ThreeMMU32 name, expected;
} Material;
static int convert(void *c, const char *text, void *wire, ThreeMMU32 capacity, ThreeMMU32 *bytes)
{
    Material *m = (Material *)c;
    ++m->converts;
    CHECK(strlen(text) == m->name && capacity >= 5);
    if (m->fail_convert)
        return 0;
    memcpy(wire, "abcde", 5);
    *bytes = 5;
    return 1;
}
static int insert(void *c, ThreeMMU32 position, const ThreeMMChunkEntry *entry, const void *variable, ThreeMMU32 bytes)
{
    Material *m = (Material *)c;
    ThreeMMChunkChild child;
    const unsigned char *p = (const unsigned char *)variable;
    CHECK(position == (ThreeMMU32)m->inserts);
    if (++m->inserts == m->fail)
        return 0;
    if (position == 0)
    {
        CHECK(entry->type == 0x41414141UL && entry->child_count == 1 && bytes == 12 + m->expected);
        memcpy(&child, p, 12);
        CHECK(child.type == 0x42424242UL && child.number == 2 && child.slot == 7);
        if (m->expected)
            CHECK(p[12] == (m->converts ? 'a' : 'N'));
    }
    else
        CHECK(entry->type == 0x42424242UL && !bytes && !entry->child_count);
    return 1;
}
static void materialize(void)
{
    unsigned char data[2048];
    ThreeMMU32 sizes[] = {0, 1, 255, 256, 516};
    unsigned i;
    int big, version;
    ThreeMMMemoryStream memory;
    ThreeMMChunkIndex *index;
    ThreeMMChunkPolicy policy;
    ThreeMMChunkMaterializer host;
    Material m;
    Memory allocation;
    ThreeMMStorageAllocator allocator;
    memset(&policy, 0, sizeof(policy));
    policy.max_name_bytes = 516;
    host.context = &m;
    host.legacy_name = convert;
    host.insert = insert;
    memset(&allocation, 0, sizeof(allocation));
    allocator.context = &allocation;
    allocator.resize = resize;
    allocator.release = release;
    for (version = 2; version <= 5; version += 3)
        for (big = 0; big < 2; ++big)
            for (i = 0; i < 5; ++i)
            {
                policy.native_big_endian = big;
                CHECK(three_mm_memory_stream_init(&memory, data, fixture(data, big, version, sizes[i])));
                CHECK(three_mm_chunk_index_open(&memory.stream, &policy, &index) == THREE_MM_CHUNK_OK);
                memset(&m, 0, sizeof(m));
                m.name = sizes[i];
                m.expected = version < 3 ? (sizes[i] && sizes[i] < 256 ? 5 : 0) : sizes[i];
                CHECK(three_mm_chunk_materialize(index, &host, &allocator) == THREE_MM_CHUNK_OK && m.inserts == 2 &&
                      !allocation.live);
                CHECK(m.converts == (version < 3 && sizes[i] && sizes[i] < 256));
                m.inserts = m.converts = 0;
                m.fail = 2;
                CHECK(three_mm_chunk_materialize(index, &host, &allocator) == THREE_MM_CHUNK_MEMORY &&
                      !allocation.live);
                m.fail = 0;
                m.inserts = m.converts = 0;
                allocation.fail = -1;
                CHECK(three_mm_chunk_materialize(index, &host, &allocator) == THREE_MM_CHUNK_MEMORY &&
                      !allocation.live && !m.inserts);
                allocation.fail = 0;
                if (version < 3 && sizes[i] && sizes[i] < 256)
                {
                    m.fail_convert = 1;
                    m.inserts = m.converts = 0;
                    CHECK(three_mm_chunk_materialize(index, &host, &allocator) == THREE_MM_CHUNK_BAD_FILE &&
                          !m.inserts && !allocation.live);
                }
                three_mm_chunk_index_close(index);
            }
}
int main(void)
{
    maps();
    materialize();
    return 0;
}
