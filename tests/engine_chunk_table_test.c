#include "three_mm/chunk_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "chunk table failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Output
{
    unsigned char bytes[2048];
    ThreeMMChunkTable *table;
    int calls, fail;
} Output;
static int write_at(void *context, ThreeMMU32 offset, const void *data, size_t count)
{
    Output *o = (Output *)context;
    CHECK(offset <= sizeof(o->bytes) && count <= sizeof(o->bytes) - offset);
    ++o->calls;
    if (o->table)
    {
        CHECK(o->table->storage.buffers[0].locks == (ThreeMMU32)(o->calls == 2));
        CHECK(o->table->storage.buffers[1].locks == (ThreeMMU32)(o->calls == 3));
    }
    if (o->fail == o->calls)
        return 0;
    memcpy(o->bytes + offset, data, count);
    return 1;
}
static ThreeMMU32 observe(ThreeMMChunkTable *t, ThreeMMU32 hash)
{
    Output out;
    ThreeMMStorageWriter w;
    ThreeMMU32 i;
    memset(&out, 0, sizeof(out));
    out.table = t;
    w.context = &out;
    w.length = three_mm_chunk_table_wire_size(t);
    w.write_at = write_at;
    CHECK(three_mm_chunk_table_write(t, &w, 0, 0x0303) == THREE_MM_STORAGE_OK);
    for (i = 0; i < w.length; ++i)
        hash = (hash ^ out.bytes[i]) * 16777619U;
    return hash;
}
static void frozen_wire(void)
{
    ThreeMMChunkTable *t;
    ThreeMMU32 hash;
    unsigned char fixed[32], variable[32], *p;
    int width, trash, i, j;
    for (trash = 0; trash < 2; ++trash)
    {
        hash = 2166136261UL;
        for (width = 20; width <= 32; width += 12)
        {
            CHECK(three_mm_chunk_table_create(NULL, (ThreeMMU32)width, 0, trash, &t) == THREE_MM_STORAGE_OK);
            for (i = 0; i < 6; ++i)
            {
                for (j = 0; j < width; ++j)
                    fixed[j] = (unsigned char)(j * 7 + i);
                for (j = 0; j < 32; ++j)
                    variable[j] = (unsigned char)(j + i * 11);
                CHECK(three_mm_chunk_table_insert(t, 0, (ThreeMMU32)i * 4, variable, fixed) == THREE_MM_STORAGE_OK);
            }
            hash = observe(t, hash);
            CHECK(three_mm_chunk_table_insert_bytes(t, 2, 4, 4, "ABCD") == THREE_MM_STORAGE_OK);
            CHECK(three_mm_chunk_table_delete_bytes(t, 1, 0, 4) == THREE_MM_STORAGE_OK);
            CHECK(three_mm_chunk_table_move(t, 0, 6) == THREE_MM_STORAGE_OK);
            CHECK(three_mm_chunk_table_move_bytes(t, 0, 0, 3, 4, 4) == THREE_MM_STORAGE_OK);
            CHECK(three_mm_chunk_table_delete(t, 2) == THREE_MM_STORAGE_OK);
            hash = observe(t, hash);
            for (j = 0; j < width; ++j)
                fixed[j] = (unsigned char)(200 + j);
            CHECK(three_mm_chunk_table_insert(t, 1, 8, "12345678", fixed) == THREE_MM_STORAGE_OK);
            hash = observe(t, hash);
            CHECK(three_mm_chunk_table_lock(t) == THREE_MM_STORAGE_OK);
            p = (unsigned char *)three_mm_chunk_table_variable(t, 0, NULL);
            p[0] ^= 0x5a;
            CHECK(three_mm_chunk_table_unlock(t) == THREE_MM_STORAGE_OK);
            hash = observe(t, hash);
            CHECK(three_mm_chunk_table_close(t) == THREE_MM_STORAGE_OK);
        }
        /* Frozen from 2,188 native GeneralGroup bytes before conversion. */
        CHECK(hash == 0xccb73ad9UL);
    }
}
typedef struct Heap
{
    int calls, fail_after, speed_fail, live;
} Heap;
static void *resize(void *context, void *old, ThreeMMU32 before, ThreeMMU32 bytes, int priority)
{
    Heap *h = (Heap *)context;
    void *p;
    ++h->calls;
    if ((h->fail_after && h->calls >= h->fail_after) || (h->speed_fail && priority == THREE_MM_STORAGE_SPEED))
        return NULL;
    p = malloc(bytes);
    CHECK(p != NULL);
    if (old)
    {
        memcpy(p, old, before < bytes ? before : bytes);
        free(old);
    }
    else
        ++h->live;
    return p;
}
static void release(void *context, void *p, ThreeMMU32 bytes)
{
    Heap *h = (Heap *)context;
    (void)bytes;
    CHECK(h->live > 0);
    --h->live;
    free(p);
}
static void allocation_cases(void)
{
    Heap h;
    ThreeMMStorageAllocator a;
    ThreeMMChunkTable *t, *sentinel, sentinel_table;
    ThreeMMChunkRecordSmall fixed;
    ThreeMMU32 count, bytes, hash;
    unsigned char data[1024];
    int cut, result;
    memset(&fixed, 0, sizeof(fixed));
    memset(data, 0x42, sizeof(data));
    for (cut = 1; cut < 5; ++cut)
    {
        memset(&h, 0, sizeof(h));
        h.fail_after = cut;
        a.context = &h;
        a.resize = resize;
        a.release = release;
        sentinel = &sentinel_table;
        t = sentinel;
        result = three_mm_chunk_table_create(&a, 20, 5, 1, &t);
        if (cut < 4)
            CHECK(result == THREE_MM_STORAGE_MEMORY && t == sentinel);
        else
        {
            CHECK(result == THREE_MM_STORAGE_OK);
            CHECK(three_mm_chunk_table_close(t) == THREE_MM_STORAGE_OK);
        }
        CHECK(!h.live);
    }
    memset(&h, 0, sizeof(h));
    h.speed_fail = 1;
    CHECK(three_mm_chunk_table_create(&a, 20, 0, 1, &t) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_insert(t, 0, 4, "data", &fixed) == THREE_MM_STORAGE_OK);
    count = t->count;
    bytes = t->bytes;
    hash = observe(t, 2166136261UL);
    h.fail_after = h.calls + 1;
    CHECK(three_mm_chunk_table_insert(t, 1, sizeof(data), data, &fixed) == THREE_MM_STORAGE_MEMORY);
    CHECK(t->count == count && t->bytes == bytes && observe(t, 2166136261UL) == hash);
    h.fail_after = 0;
    CHECK(three_mm_chunk_table_lock(t) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_close(t) == THREE_MM_STORAGE_LOCKED);
    CHECK(three_mm_chunk_table_insert(t, 1, sizeof(data), data, &fixed) == THREE_MM_STORAGE_LOCKED);
    CHECK(three_mm_chunk_table_unlock(t) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_insert(t, 1, sizeof(data), data, &fixed) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_put(t, 0, 0, 4, "edit") == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_read(t, 0, 0, 4, data) == THREE_MM_STORAGE_OK && !memcmp(data, "edit", 4));
    CHECK(three_mm_chunk_table_read(t, 0, 4, 1, data) == THREE_MM_STORAGE_ARGUMENT);
    CHECK(three_mm_chunk_table_close(t) == THREE_MM_STORAGE_OK && !h.live);
}
static void write_failures(void)
{
    ThreeMMChunkTable *t;
    ThreeMMChunkRecordSmall fixed;
    ThreeMMStorageWriter w;
    ThreeMMCollectionHeader header;
    ThreeMMReadStream stream;
    ThreeMMMemoryStream memory;
    Output out;
    ThreeMMU32 before, i;
    int fail;
    memset(&fixed, 0, sizeof(fixed));
    CHECK(three_mm_chunk_table_create(NULL, 20, 0, 1, &t) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_insert(t, 0, 4, "wire", &fixed) == THREE_MM_STORAGE_OK);
    before = observe(t, 2166136261UL);
    for (fail = 0; fail <= 3; ++fail)
    {
        memset(&out, 0, sizeof(out));
        out.table = t;
        out.fail = fail;
        w.context = &out;
        w.length = three_mm_chunk_table_wire_size(t);
        w.write_at = write_at;
        CHECK(three_mm_chunk_table_write(t, &w, 1, 0x0303) == (fail ? THREE_MM_STORAGE_IO : THREE_MM_STORAGE_OK));
        CHECK(!t->storage.buffers[0].locks && !t->storage.buffers[1].locks);
        CHECK(observe(t, 2166136261UL) == before);
        if (!fail)
        {
            CHECK(three_mm_memory_stream_init(&memory, out.bytes, w.length));
            stream = memory.stream;
            CHECK(three_mm_collection_header_read(&stream, THREE_MM_COLLECTION_GROUP, 0, &header) ==
                  THREE_MM_STORAGE_OK);
            CHECK(header.big_endian && header.width == 20 && header.count == 1 && header.bytes == 24);
            for (i = 0; i < 4; ++i)
                CHECK(!out.bytes[44 + i]);
            CHECK(out.bytes[51] == 24);
        }
    }
    CHECK(three_mm_chunk_table_close(t) == THREE_MM_STORAGE_OK);
}
static void record_cases(void)
{
    ThreeMMChunkRecordSmall small;
    ThreeMMChunkRecordBig big;
    ThreeMMChunkEntry entry, actual;
    ThreeMMChunkTable *t;
    ThreeMMChunkChild child, value;
    ThreeMMChunkAccess access;
    unsigned char payload[16];
    ThreeMMU32 old;
    int expanded;
    memset(&small, 0, sizeof(small));
    memset(&big, 0, sizeof(big));
    memset(&entry, 0, sizeof(entry));
    big.rti = 99;
    entry.type = 0x4d4f5649UL;
    entry.number = 0xffffffffUL;
    entry.offset = 0x7fffffffUL;
    entry.bytes = 0x00ffffffUL;
    entry.flags = 0xabcd1234UL;
    entry.child_count = 0x12345;
    entry.parent_count = 0x23456;
    for (expanded = 0; expanded < 2; ++expanded)
    {
        void *p = expanded ? (void *)&big : &small;
        three_mm_chunk_record_set(p, expanded, &entry);
        three_mm_chunk_record_get(p, expanded, 100, &actual);
        CHECK(actual.type == entry.type && actual.number == entry.number && actual.offset == entry.offset &&
              actual.bytes == entry.bytes);
        CHECK(actual.flags == (expanded ? entry.flags : 0x34UL));
        CHECK(actual.child_count == (expanded ? entry.child_count : 0x2345UL));
        CHECK(actual.parent_count == (expanded ? entry.parent_count : 0x3456UL) && actual.variable_bytes == 100);
        old = actual.flags;
        three_mm_chunk_record_set_flags(p, expanded, 0x12345678UL, 0x00ff0f0fUL);
        CHECK(three_mm_chunk_record_flags(p, expanded, 0xffffffffUL) ==
              (((old & ~0x00ff0f0fUL) | (0x12345678UL & 0x00ff0f0fUL)) & (expanded ? 0xffffffffUL : 255UL)));
        CHECK(three_mm_chunk_record_bytes(p, expanded) == 0x00ffffffL);
        CHECK(three_mm_chunk_record_name_bytes(p, expanded, (ThreeMMI32)(actual.child_count * 12 + 17)) == 17);
    }
    CHECK(big.rti == 99);
    three_mm_chunk_record_set_bytes(&small, 0, 0x01000001L);
    CHECK(three_mm_chunk_record_bytes(&small, 0) == 1);
    entry.child_count = 1;
    entry.parent_count = 0;
    entry.type = 1;
    entry.number = 2;
    three_mm_chunk_record_set(&small, 0, &entry);
    child.type = 3;
    child.number = 4;
    child.slot = 5;
    memcpy(payload, &child, 12);
    memcpy(payload + 12, "name", 4);
    CHECK(three_mm_chunk_table_create(NULL, 20, 0, 1, &t) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_insert(t, 0, 16, payload, &small) == THREE_MM_STORAGE_OK);
    access = three_mm_chunk_table_access(t);
    CHECK(three_mm_chunk_table_clear_flags(t, 0x30) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_chunk_table_entry(t, 0, &actual) && actual.flags == (entry.flags & 0xcfU));
    CHECK(actual.bytes == entry.bytes && actual.child_count == 1);
    CHECK(three_mm_chunk_find(&access, 1, 2, &old) == THREE_MM_CHUNK_OK && old == 0);
    CHECK(three_mm_chunk_table_child(t, 0, 0, &value) && value.type == 3 && value.number == 4 && value.slot == 5);
    CHECK(!three_mm_chunk_table_child(t, 0, 1, &value) && value.type == 3);
    ((ThreeMMChunkRecordSmall *)three_mm_chunk_table_fixed(t, 0, NULL))->ckid = 9;
    CHECK(!three_mm_chunk_table_child(t, 0, 1, &value));
    CHECK(three_mm_chunk_table_close(t) == THREE_MM_STORAGE_OK);
}
int main(void)
{
    frozen_wire();
    allocation_cases();
    write_failures();
    record_cases();
    puts("chunk table C89 checks passed");
    return 0;
}
