#include "three_mm/collection_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "storage check %d: %s\n", __LINE__, #x);                                                   \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Request
{
    ThreeMMU32 old_bytes, new_bytes;
    int priority;
} Request;
typedef struct Heap
{
    Request requests[64];
    int calls, live, frees, fail_at, fail_speed, fail_normal, fail_shrink;
} Heap;
static void *resize_test(void *context, void *data, ThreeMMU32 old_bytes, ThreeMMU32 new_bytes, int priority)
{
    Heap *heap = (Heap *)context;
    unsigned char *replacement;
    Request *request;
    CHECK(heap->calls < 64 && new_bytes && new_bytes < THREE_MM_STORAGE_LIMIT);
    request = &heap->requests[heap->calls++];
    request->old_bytes = old_bytes;
    request->new_bytes = new_bytes;
    request->priority = priority;
    if (heap->calls == heap->fail_at || (heap->fail_speed && priority == THREE_MM_STORAGE_SPEED) ||
        (heap->fail_normal && priority == THREE_MM_STORAGE_NORMAL) || (heap->fail_shrink && new_bytes < old_bytes))
        return NULL;
    replacement = (unsigned char *)malloc(new_bytes);
    CHECK(replacement != NULL);
    memset(replacement, 0xa3, new_bytes);
    if (data)
    {
        memcpy(replacement, data, old_bytes < new_bytes ? old_bytes : new_bytes);
        free(data);
    }
    else
        ++heap->live;
    return replacement;
}
static void release_test(void *context, void *data, ThreeMMU32 bytes)
{
    Heap *heap = (Heap *)context;
    CHECK(data && bytes && heap->live > 0);
    --heap->live;
    ++heap->frees;
    free(data);
}
static void init(ThreeMMCollectionStorage *storage, Heap *heap)
{
    ThreeMMStorageAllocator allocator;
    memset(heap, 0, sizeof(*heap));
    allocator.context = heap;
    allocator.resize = resize_test;
    allocator.release = release_test;
    CHECK(three_mm_storage_init(storage, &allocator) == THREE_MM_STORAGE_OK);
}
static void growth(void)
{
    ThreeMMCollectionStorage storage;
    Heap heap;
    unsigned char *first, *second;
    init(&storage, &heap);
    CHECK(three_mm_storage_ensure(&storage, 0, 0, 0) == THREE_MM_STORAGE_OK && heap.calls == 0);
    storage.min_grow[0] = 16;
    storage.min_grow[1] = 8;
    CHECK(three_mm_storage_ensure(&storage, 4, 2, 0) == THREE_MM_STORAGE_OK);
    CHECK(storage.buffers[0].capacity == 16 && storage.buffers[1].capacity == 8 && heap.live == 2);
    CHECK(heap.requests[0].priority == THREE_MM_STORAGE_SPEED && heap.requests[1].priority == THREE_MM_STORAGE_SPEED);
    memcpy(storage.buffers[0].data, "abcd", 4);
    memcpy(storage.buffers[1].data, "XY", 2);
    first = storage.buffers[0].data;
    heap.fail_speed = 1;
    CHECK(three_mm_storage_ensure(&storage, 20, 2, 0) == THREE_MM_STORAGE_OK);
    CHECK(heap.calls == 4 && heap.requests[2].new_bytes == 32 && heap.requests[3].new_bytes == 20);
    CHECK(heap.requests[3].priority == THREE_MM_STORAGE_NORMAL && storage.buffers[0].capacity == 20);
    CHECK(first != storage.buffers[0].data && !memcmp(storage.buffers[0].data, "abcd", 4));
    first = storage.buffers[0].data;
    second = storage.buffers[1].data;
    heap.fail_normal = 1;
    CHECK(three_mm_storage_ensure(&storage, 24, 10, 0) == THREE_MM_STORAGE_MEMORY);
    CHECK(first == storage.buffers[0].data && second == storage.buffers[1].data);
    CHECK(storage.buffers[0].capacity == 20 && storage.buffers[1].capacity == 8);
    heap.fail_normal = heap.fail_speed = 0;
    heap.fail_at = heap.calls + 2;
    storage.min_grow[0] = storage.min_grow[1] = 0;
    CHECK(three_mm_storage_ensure(&storage, 40, 12, 0) == THREE_MM_STORAGE_MEMORY);
    CHECK(storage.buffers[0].capacity == 40 && storage.buffers[1].capacity == 8);
    CHECK(!memcmp(storage.buffers[0].data, "abcd", 4) && !memcmp(storage.buffers[1].data, "XY", 2));
    heap.fail_at = 0;
    heap.fail_shrink = 1;
    first = storage.buffers[0].data;
    CHECK(three_mm_storage_ensure(&storage, 4, 2, 1) == THREE_MM_STORAGE_OK);
    CHECK(storage.buffers[0].data == first && storage.buffers[0].capacity == 40 && storage.buffers[1].capacity == 8);
    heap.fail_shrink = 0;
    CHECK(three_mm_storage_ensure(&storage, 4, 2, 1) == THREE_MM_STORAGE_OK);
    CHECK(storage.buffers[0].capacity == 4 && storage.buffers[1].capacity == 2);
    CHECK(three_mm_storage_ensure(&storage, 0, 0, 1) == THREE_MM_STORAGE_OK && heap.live == 0);
    CHECK(!storage.buffers[0].data && !storage.buffers[1].data);
    storage.min_grow[0] = 0xffffffffUL;
    CHECK(three_mm_storage_ensure(&storage, 4, 0, 0) == THREE_MM_STORAGE_OK);
    CHECK(heap.requests[heap.calls - 1].new_bytes == 4 &&
          heap.requests[heap.calls - 1].priority == THREE_MM_STORAGE_NORMAL);
    CHECK(three_mm_storage_ensure(&storage, THREE_MM_STORAGE_LIMIT, 0, 0) == THREE_MM_STORAGE_ARGUMENT);
    CHECK(three_mm_storage_dispose(&storage) == THREE_MM_STORAGE_OK && heap.live == 0);
    CHECK(three_mm_storage_dispose(&storage) == THREE_MM_STORAGE_OK);
}
static void locks_and_copy(void)
{
    ThreeMMCollectionStorage source, copy;
    Heap a, b;
    init(&source, &a);
    init(&copy, &b);
    CHECK(three_mm_storage_lock(&source, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_unlock(&source, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_ensure(&source, 8, 4, 0) == THREE_MM_STORAGE_OK);
    memcpy(source.buffers[0].data, "abcdefgh", 8);
    memcpy(source.buffers[1].data, "1234", 4);
    source.min_grow[0] = 32;
    source.min_grow[1] = 16;
    CHECK(three_mm_storage_lock(&source, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_lock(&source, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_ensure(&source, 9, 4, 0) == THREE_MM_STORAGE_LOCKED);
    CHECK(three_mm_storage_ensure(&source, 0, 4, 1) == THREE_MM_STORAGE_LOCKED);
    CHECK(three_mm_storage_dispose(&source) == THREE_MM_STORAGE_LOCKED && a.live == 2);
    CHECK(three_mm_storage_copy(&copy, &source, 4, 2) == THREE_MM_STORAGE_OK);
    CHECK(copy.buffers[0].capacity == 4 && copy.buffers[1].capacity == 2);
    CHECK(copy.min_grow[0] == 32 && copy.min_grow[1] == 16);
    CHECK(!memcmp(copy.buffers[0].data, "abcd", 4) && !memcmp(copy.buffers[1].data, "12", 2));
    copy.buffers[0].data[0] = 'X';
    CHECK(source.buffers[0].data[0] == 'a');
    b.fail_speed = b.fail_normal = 1;
    CHECK(three_mm_storage_copy(&copy, &source, 8, 4) == THREE_MM_STORAGE_MEMORY);
    CHECK(copy.buffers[0].data[0] == 'X' && copy.buffers[1].data[0] == '1');
    CHECK(three_mm_storage_unlock(&source, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_unlock(&source, 0) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_unlock(&source, 0) == THREE_MM_STORAGE_LOCKED);
    CHECK(three_mm_storage_lock(&source, 1) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_ensure(&source, 8, 5, 0) == THREE_MM_STORAGE_LOCKED);
    CHECK(three_mm_storage_unlock(&source, 1) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_dispose(&source) == THREE_MM_STORAGE_OK && a.live == 0);
    CHECK(three_mm_storage_dispose(&copy) == THREE_MM_STORAGE_OK && b.live == 0);
}
typedef struct IO
{
    unsigned char bytes[32];
    int calls, fail_at;
    unsigned locks[4];
    ThreeMMCollectionStorage *storage;
} IO;
static int observe(IO *io)
{
    CHECK(io->calls < 4);
    io->locks[io->calls++] = (io->storage->buffers[0].locks ? 1U : 0U) | (io->storage->buffers[1].locks ? 2U : 0U);
    return io->calls != io->fail_at;
}
static int write_at(void *context, ThreeMMU32 offset, const void *bytes, size_t count)
{
    IO *io = (IO *)context;
    CHECK(offset <= sizeof(io->bytes) && count <= sizeof(io->bytes) - offset);
    if (!observe(io))
        return 0;
    memcpy(io->bytes + offset, bytes, count);
    return 1;
}
static int read_at(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    IO *io = (IO *)context;
    CHECK(offset <= sizeof(io->bytes) && count <= sizeof(io->bytes) - offset);
    if (!observe(io))
        return 0;
    memcpy(bytes, io->bytes + offset, count);
    return 1;
}
static void streams(void)
{
    ThreeMMCollectionStorage storage;
    ThreeMMReadStream reader;
    ThreeMMStorageWriter writer;
    IO io;
    unsigned fail;
    CHECK(three_mm_storage_init(&storage, NULL) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_storage_ensure(&storage, 4, 2, 0) == THREE_MM_STORAGE_OK);
    memcpy(storage.buffers[0].data, "abcd", 4);
    memcpy(storage.buffers[1].data, "XY", 2);
    memset(&io, 0, sizeof(io));
    io.storage = &storage;
    writer.context = &io;
    writer.length = 9;
    writer.write_at = write_at;
    CHECK(three_mm_storage_write(&storage, &writer, "HDR", 3, 4, 2) == THREE_MM_STORAGE_OK);
    CHECK(!memcmp(io.bytes, "HDRabcdXY", 9) && io.calls == 3);
    CHECK(io.locks[0] == 0 && io.locks[1] == 1 && io.locks[2] == 2);
    for (fail = 1; fail <= 3; ++fail)
    {
        io.calls = 0;
        io.fail_at = fail;
        CHECK(three_mm_storage_write(&storage, &writer, "HDR", 3, 4, 2) == THREE_MM_STORAGE_IO);
        CHECK(io.calls == (int)fail && !storage.buffers[0].locks && !storage.buffers[1].locks);
    }
    io.calls = io.fail_at = 0;
    writer.length = 8;
    CHECK(three_mm_storage_write(&storage, &writer, "HDR", 3, 4, 2) == THREE_MM_STORAGE_ARGUMENT && !io.calls);
    writer.length = 9;
    CHECK(three_mm_storage_write(&storage, &writer, "H", 0xffffffffUL, 4, 2) == THREE_MM_STORAGE_ARGUMENT && !io.calls);
    reader.context = &io;
    reader.length = 9;
    reader.read_at = read_at;
    for (fail = 1; fail <= 2; ++fail)
    {
        io.calls = 0;
        io.fail_at = fail;
        memset(storage.buffers[0].data, '-', 4);
        memset(storage.buffers[1].data, '-', 2);
        CHECK(three_mm_storage_read(&storage, &reader, 3, 4, 2) == THREE_MM_STORAGE_IO);
        CHECK(!storage.buffers[0].locks && !storage.buffers[1].locks);
        CHECK(storage.buffers[0].data[0] == (fail == 2 ? 'a' : '-'));
        CHECK(storage.buffers[1].data[0] == '-');
    }
    io.calls = io.fail_at = 0;
    CHECK(three_mm_storage_read(&storage, &reader, 3, 4, 2) == THREE_MM_STORAGE_OK);
    CHECK(io.locks[0] == 1 && io.locks[1] == 2 && !memcmp(storage.buffers[1].data, "XY", 2));
    io.calls = 0;
    CHECK(three_mm_storage_read(&storage, &reader, 0xffffffffUL, 4, 2) == THREE_MM_STORAGE_ARGUMENT && !io.calls);
    CHECK(three_mm_storage_read(&storage, &reader, 8, 4, 2) == THREE_MM_STORAGE_ARGUMENT && !io.calls);
    CHECK(three_mm_storage_dispose(&storage) == THREE_MM_STORAGE_OK);
}
int main(void)
{
    growth();
    locks_and_copy();
    streams();
    return 0;
}
