#include "three_mm/chunk_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 heap failed at line %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Heap
{
    ThreeMMChunkFreeRange ranges[128];
    ThreeMMU32 count, physical;
    int fail_resize, fail_ensure, fail_insert, resizes;
} Heap;
static ThreeMMU32 count(void *ctx)
{
    return ((Heap *)ctx)->count;
}
static void get(void *ctx, ThreeMMU32 i, ThreeMMChunkFreeRange *r)
{
    Heap *h = (Heap *)ctx;
    CHECK(i < h->count);
    *r = h->ranges[i];
}
static void put(void *ctx, ThreeMMU32 i, const ThreeMMChunkFreeRange *r)
{
    Heap *h = (Heap *)ctx;
    CHECK(i < h->count);
    h->ranges[i] = *r;
}
static void erase(void *ctx, ThreeMMU32 i)
{
    Heap *h = (Heap *)ctx;
    CHECK(i < h->count);
    --h->count;
    memmove(h->ranges + i, h->ranges + i + 1, (h->count - i) * sizeof(*h->ranges));
}
static int insert(void *ctx, ThreeMMU32 i, const ThreeMMChunkFreeRange *r)
{
    Heap *h = (Heap *)ctx;
    CHECK(i <= h->count && h->count < 128);
    if (h->fail_insert)
        return 0;
    memmove(h->ranges + i + 1, h->ranges + i, (h->count - i) * sizeof(*h->ranges));
    h->ranges[i] = *r;
    ++h->count;
    return 1;
}
static int ensure(void *ctx)
{
    Heap *h = (Heap *)ctx;
    return h->count || !h->fail_ensure;
}
static ThreeMMU32 physical(void *ctx)
{
    return ((Heap *)ctx)->physical;
}
static int resize(void *ctx, ThreeMMU32 bytes)
{
    Heap *h = (Heap *)ctx;
    ++h->resizes;
    CHECK(bytes > h->physical);
    if (h->fail_resize)
        return 0;
    h->physical = bytes;
    return 1;
}
static ThreeMMChunkHeap services(Heap *h)
{
    ThreeMMChunkHeap io = {NULL, count, get, put, erase, insert, ensure, physical, resize};
    io.context = h;
    return io;
}
static void random_edits(void)
{
    Heap heap;
    ThreeMMChunkHeap io;
    unsigned char used[1024], live[48];
    ThreeMMU32 locations[48], lengths[48], end = 128, seed = 0x12345678UL;
    ThreeMMU32 i, j, slot, bytes, expected, actual, n, old_physical;
    int step;
    memset(&heap, 0, sizeof(heap));
    heap.physical = 128;
    io = services(&heap);
    memset(used, 0, sizeof(used));
    memset(used, 1, 128);
    memset(live, 0, sizeof(live));
    for (step = 0; step < 12000; ++step)
    {
        seed = seed * 1664525UL + 1013904223UL;
        slot = (seed >> 16) % 48;
        old_physical = heap.physical;
        if (live[slot])
        {
            CHECK(three_mm_chunk_heap_free(&io, &end, locations[slot], lengths[slot]) == THREE_MM_CHUNK_OK);
            for (i = 0; i < lengths[slot]; ++i)
            {
                CHECK(used[locations[slot] + i]);
                used[locations[slot] + i] = 0;
            }
            live[slot] = 0;
            expected = 1024;
            while (expected > 128 && !used[expected - 1])
                --expected;
            CHECK(end == expected && heap.physical == old_physical);
        }
        else
        {
            bytes = (seed >> 24) % 25;
            expected = 0;
            if (bytes)
            {
                for (expected = 128; expected < end; ++expected)
                {
                    for (i = 0; i < bytes && expected + i < end && !used[expected + i]; ++i)
                    {
                    }
                    if (i == bytes)
                        break;
                }
            }
            if (expected + bytes > sizeof(used))
                continue;
            actual = 0xdeadbeefUL;
            CHECK(three_mm_chunk_heap_allocate(&io, &end, bytes, &actual) == THREE_MM_CHUNK_OK && actual == expected);
            locations[slot] = actual;
            lengths[slot] = bytes;
            live[slot] = 1;
            for (i = 0; i < bytes; ++i)
            {
                CHECK(!used[actual + i]);
                used[actual + i] = 1;
            }
            CHECK(heap.physical >= old_physical && heap.physical >= end);
        }
        /* Derive the entire canonical free map from independent occupied bytes. */
        n = 0;
        for (i = 128; i < end; ++i)
            if (!used[i])
            {
                for (j = i; j < end && !used[j]; ++j)
                {
                }
                CHECK(n < heap.count && heap.ranges[n].offset == i && heap.ranges[n].bytes == j - i);
                ++n;
                i = j - 1;
            }
        CHECK(n == heap.count);
    }
}
static void failures(void)
{
    Heap heap, before;
    ThreeMMChunkHeap io;
    ThreeMMU32 end, offset;
    int mode;
    for (mode = 0; mode < 2; ++mode)
    {
        memset(&heap, 0, sizeof(heap));
        heap.physical = 256;
        end = 256;
        io = services(&heap);
        heap.fail_ensure = mode == 0;
        heap.fail_insert = mode == 1;
        CHECK(three_mm_chunk_heap_free(&io, &end, 144, 8) == THREE_MM_CHUNK_MEMORY);
        CHECK(!heap.count && end == 256 && heap.physical == 256 && !heap.resizes);
    }
    memset(&heap, 0, sizeof(heap));
    heap.physical = 256;
    heap.fail_resize = 1;
    io = services(&heap);
    end = 256;
    offset = 77;
    CHECK(three_mm_chunk_heap_allocate(&io, &end, 3, &offset) == THREE_MM_CHUNK_IO);
    CHECK(end == 256 && offset == 77 && heap.physical == 256 && heap.resizes == 1);
    /* Logical tail shrink leaves physical bytes available for later append. */
    CHECK(three_mm_chunk_heap_free(&io, &end, 240, 16) == THREE_MM_CHUNK_OK && end == 240);
    CHECK(three_mm_chunk_heap_allocate(&io, &end, 16, &offset) == THREE_MM_CHUNK_OK && offset == 240);
    CHECK(heap.resizes == 1);
    heap.count = 2;
    heap.ranges[0].offset = 140;
    heap.ranges[0].bytes = 10;
    heap.ranges[1].offset = 170;
    heap.ranges[1].bytes = 10;
    before = heap;
    CHECK(three_mm_chunk_heap_free(&io, &end, 145, 4) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_heap_free(&io, &end, 160, 15) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(!memcmp(&heap, &before, sizeof(heap)));
    /* Both-neighbor merge must not allocate even under allocation failure. */
    heap.fail_ensure = heap.fail_insert = 1;
    CHECK(three_mm_chunk_heap_free(&io, &end, 150, 20) == THREE_MM_CHUNK_OK);
    CHECK(heap.count == 1 && heap.ranges[0].offset == 140 && heap.ranges[0].bytes == 40);
    heap.count = 0;
    end = 0x7ffffffeUL;
    heap.physical = 0x7fffffffUL;
    offset = 77;
    CHECK(three_mm_chunk_heap_allocate(&io, &end, 2, &offset) == THREE_MM_CHUNK_BAD_FILE && offset == 77);
    CHECK(three_mm_chunk_heap_allocate(&io, &end, 1, &offset) == THREE_MM_CHUNK_OK && end == 0x7fffffffUL);
    CHECK(three_mm_chunk_heap_free(&io, &end, 0x7ffffffeUL, 2) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_heap_free(&io, &end, 1, 0) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_heap_free(&io, &end, 0, 0) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_heap_allocate(&io, &end, 0, &offset) == THREE_MM_CHUNK_OK && offset == 0);
    CHECK(three_mm_chunk_heap_allocate(NULL, &end, 1, &offset) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_heap_free(&io, NULL, 0, 0) == THREE_MM_CHUNK_BAD_FILE);
}
int main(void)
{
    random_edits();
    failures();
    return 0;
}
