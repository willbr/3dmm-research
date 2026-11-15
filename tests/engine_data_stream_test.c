#include "mm/data_stream.h"
#include "mm/collections.h"
#include "mm/string_table.h"
#include "mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "data stream check %d: %s\n", __LINE__, #x);                                               \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
static void word(unsigned char *p, MMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (8 * i));
}
static void gst(void)
{
    unsigned char wire[64], out[20];
    MMMemoryStream stream;
    MMCollectionHeader h, before;
    int big, allocated, system;
    for (big = 0; big < 2; ++big)
        for (allocated = 0; allocated < 2; ++allocated)
        {
            memset(wire, 0, sizeof(wire));
            wire[big ? 1 : 0] = 1;
            wire[2] = wire[3] = 3;
            word(wire + 4, 8, big);
            word(wire + 8, 3, big);
            word(wire + 12, 6, big);
            word(wire + 16, allocated ? 1 : 0xffffffffUL, big);
            CHECK(mm_memory_stream_init(&stream, wire, 50));
            CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, allocated, &h) ==
                  MM_STORAGE_OK);
            CHECK(h.width == 8 && h.count == 3 && h.bytes == 6 && h.first_bytes == 6 && h.second_bytes == 24);
            CHECK(mm_collection_header_write(&h, out, sizeof(out)) == MM_STORAGE_OK &&
                  !memcmp(out, wire, 20));
            before = h;
            CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, !allocated, &h) !=
                  MM_STORAGE_OK);
            CHECK(!memcmp(&before, &h, sizeof(h)));
            word(wire + 4, 6, big);
            CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, allocated, &h) !=
                  MM_STORAGE_OK);
            word(wire + 4, 0x04000000UL, big);
            word(wire + 8, 0x04000000UL, big);
            CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, allocated, &h) !=
                  MM_STORAGE_OK);
        }
    memset(wire, 0, sizeof(wire));
    wire[0] = 1;
    wire[2] = wire[3] = 3;
    word(wire + 4, 4, 0);
    CHECK(mm_memory_stream_init(&stream, wire, 20));
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, 1, &h) == MM_STORAGE_OK);
    word(wire + 16, 0xffffffffUL, 0);
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, 0, &h) == MM_STORAGE_OK);
    CHECK(mm_strings_wire_system(MM_TEXT_WINDOWS, MM_TEXT_UTF16_BE, &system) &&
          system == MM_TEXT_MAC);
    CHECK(mm_strings_wire_system(MM_TEXT_UTF16_LE, MM_TEXT_MAC, &system) &&
          system == MM_TEXT_UTF16_BE);
    CHECK(mm_strings_wire_system(MM_TEXT_UTF16_BE, MM_TEXT_WINDOWS, &system) &&
          system == MM_TEXT_UTF16_LE);
    system = 123;
    CHECK(!mm_strings_wire_system(MM_TEXT_WINDOWS, 0, &system) && system == 123);
}
static void ranges(void)
{
    MMBlockView b, before;
    MMU32 physical = 999;
    unsigned char memory[16];
    memset(&b, 0, sizeof(b));
    CHECK(mm_block_check(&b, 0, 0, 0, &physical) == MM_DATA_OK && !physical);
    CHECK(mm_block_move_min(&b, 0) != MM_DATA_OK);
    b.kind = MM_BLOCK_MEMORY;
    b.memory = memory;
    b.start = 4;
    b.length = 8;
    b.capacity = 16;
    b.packed = 1;
    CHECK(mm_block_check(&b, 2, 6, 0, &physical) == MM_DATA_PACKED);
    CHECK(mm_block_check(&b, 2, 6, 1, &physical) == MM_DATA_OK && physical == 6);
    CHECK(mm_block_check(&b, 8, 0, 1, &physical) == MM_DATA_OK && physical == 12);
    CHECK(mm_block_check(&b, 8, 1, 1, &physical) != MM_DATA_OK);
    CHECK(mm_block_move_min(&b, 3) == MM_DATA_OK && b.start == 7 && b.length == 5);
    before = b;
    CHECK(mm_block_move_min(&b, -8) != MM_DATA_OK && !memcmp(&b, &before, sizeof(b)));
    CHECK(mm_block_move_lim(&b, 4) == MM_DATA_OK && b.length == 9);
    before = b;
    CHECK(mm_block_move_lim(&b, 1) != MM_DATA_OK && !memcmp(&b, &before, sizeof(b)));
    CHECK(mm_block_move_lim(&b, (-2147483647 - 1)) != MM_DATA_OK);
    b.kind = MM_BLOCK_FILE;
    b.start = 7;
    b.length = 4;
    b.memory = NULL;
    b.capacity = 0;
    CHECK(mm_block_move_min(&b, -7) == MM_DATA_OK && b.start == 0 && b.length == 11);
    CHECK(mm_block_move_lim(&b, -11) == MM_DATA_OK && b.length == 0);
    CHECK(mm_block_move_lim(&b, MM_STORAGE_LIMIT) != MM_DATA_OK);
    /* Retain the historical delta bound, not a new bound on the resulting end. */
    b.start = MM_STORAGE_LIMIT - 4;
    b.length = 4;
    CHECK(mm_block_move_lim(&b, 3) == MM_DATA_OK && b.length == 7);
    CHECK(mm_block_move_lim(&b, 4) != MM_DATA_OK);
    CHECK(!mm_range_contains(10, 0xffffffffUL, 2) && !mm_range_contains(10, 9, 0xffffffffUL));
}
typedef struct Decode
{
    int read_fail, decode_fail, read_calls, decode_calls, live;
} Decode;
typedef struct Handle
{
    MMU32 size;
} Handle;
static Handle *handle(Decode *d, MMU32 n)
{
    Handle *h = (Handle *)malloc(sizeof(*h));
    CHECK(h);
    h->size = n;
    ++d->live;
    return h;
}
static int load(void *context, void **out)
{
    Decode *d = (Decode *)context;
    ++d->read_calls;
    *out = handle(d, 12);
    return !d->read_fail;
}
static void release(void *context, void *data)
{
    Decode *d = (Decode *)context;
    --d->live;
    free(data);
}
static int decode(void *context, void **data)
{
    Decode *d = (Decode *)context;
    ++d->decode_calls;
    if (d->decode_fail)
        return 0;
    release(d, *data);
    *data = handle(d, 4);
    return 1;
}
static MMU32 size(void *context, void *data)
{
    (void)context;
    return ((Handle *)data)->size;
}
static void unpack(void)
{
    MMBlockView b, before;
    MMBlockDecoder ops;
    Decode d;
    memset(&d, 0, sizeof(d));
    memset(&b, 0, sizeof(b));
    ops.context = &d;
    ops.read_file = load;
    ops.decode = decode;
    ops.release = release;
    ops.size = size;
    CHECK(mm_block_unpack(&b, NULL) == MM_DATA_OK);
    b.kind = MM_BLOCK_FILE;
    b.length = 12;
    b.packed = 1;
    before = b;
    d.read_fail = 1;
    CHECK(mm_block_unpack(&b, &ops) == MM_DATA_IO && !d.live && !memcmp(&b, &before, sizeof(b)));
    d.read_fail = 0;
    d.decode_fail = 1;
    CHECK(mm_block_unpack(&b, &ops) == MM_DATA_CODEC && !d.live && !memcmp(&b, &before, sizeof(b)));
    d.decode_fail = 0;
    CHECK(mm_block_unpack(&b, &ops) == MM_DATA_OK && d.live == 1 && b.kind == MM_BLOCK_MEMORY &&
          !b.packed && b.length == 4);
    release(&d, b.memory);
    b.memory = handle(&d, 12);
    b.start = 0;
    b.length = b.capacity = 12;
    b.packed = 1;
    before = b;
    d.decode_fail = 1;
    CHECK(mm_block_unpack(&b, &ops) == MM_DATA_CODEC && d.live == 1 && !memcmp(&b, &before, sizeof(b)));
    b.start = 1;
    b.length = 11;
    before = b;
    CHECK(mm_block_unpack(&b, &ops) == MM_DATA_PACKED && !memcmp(&b, &before, sizeof(b)));
    b.start = 0;
    b.length = 12;
    d.decode_fail = 0;
    CHECK(mm_block_unpack(&b, &ops) == MM_DATA_OK && b.length == 4 && d.live == 1);
    release(&d, b.memory);
    CHECK(!d.live);
}
typedef struct Codec
{
    int finds, converts, missing, fail, mismatch;
} Codec;
static void *find_codec(void *context, MMI32 format)
{
    Codec *c = (Codec *)context;
    ++c->finds;
    CHECK(format == 0x41424344);
    return c->missing ? NULL : c;
}
static int convert(void *context, MMI32 format, const void *input, MMU32 bytes, void *output,
                   MMU32 capacity, MMI32 *written)
{
    Codec *c = (Codec *)context;
    ++c->converts;
    CHECK(format == 0x41424344 && bytes == 1 && *(const unsigned char *)input == 0xcc && capacity == 3);
    if (c->fail)
    {
        *written = 1;
        return 0;
    }
    memcpy(output, "XYZ", 3);
    *written = c->mismatch ? 2 : 3;
    return 1;
}
static void packed(void)
{
    unsigned char input[] = {0x41, 0x42, 0x43, 0x44, 0, 0, 0, 3, 0xcc}, output[8];
    MMPackedDecoder ops;
    Codec c;
    MMI32 n = 99;
    MMMemoryStream stream;
    memset(&c, 0, sizeof(c));
    ops.context = &c;
    ops.find = find_codec;
    ops.convert = convert;
    CHECK(mm_packed_decode(&ops, input, 8, NULL, 0, &n) == MM_DATA_ARGUMENT && n == 99);
    CHECK(mm_packed_decode(&ops, input, 9, NULL, 0, &n) == MM_DATA_OK && n == 3 && c.finds == 1 &&
          !c.converts);
    CHECK(mm_packed_decode(&ops, input, 9, output, 2, &n) == MM_DATA_CODEC && c.finds == 1);
    CHECK(mm_packed_decode(&ops, input, 9, output, 8, &n) == MM_DATA_OK && n == 3 &&
          !memcmp(output, "XYZ", 3) && c.converts == 1);
    c.fail = 1;
    CHECK(mm_packed_decode(&ops, input, 9, output, 8, &n) == MM_DATA_CODEC && n == 1);
    c.fail = 0;
    c.mismatch = 1;
    CHECK(mm_packed_decode(&ops, input, 9, output, 8, &n) == MM_DATA_WRONG_SIZE && n == 2);
    c.mismatch = 0;
    c.missing = 1;
    CHECK(mm_packed_decode(&ops, input, 9, NULL, 0, &n) == MM_DATA_CODEC && n == 3);
    memset(input + 4, 0xff, 4);
    CHECK(mm_packed_decode(&ops, input, 9, NULL, 0, &n) == MM_DATA_CODEC && n == -1);
    CHECK(mm_memory_stream_init(&stream, input, 8));
    CHECK(mm_packed_format(&stream.stream, &n) == MM_DATA_OK && n == 0x41424344);
}
typedef struct Heap
{
    int calls, fail, fail_once, live;
} Heap;
static void *resize_heap(void *context, void *data, MMU32 old_bytes, MMU32 new_bytes, int priority)
{
    Heap *h = (Heap *)context;
    void *p;
    ++h->calls;
    CHECK(priority == MM_STORAGE_NORMAL);
    if (h->fail || h->fail_once)
    {
        h->fail_once = 0;
        return NULL;
    }
    p = malloc(new_bytes);
    CHECK(p);
    if (data)
    {
        memcpy(p, data, old_bytes < new_bytes ? old_bytes : new_bytes);
        free(data);
    }
    else
        ++h->live;
    return p;
}
static void release_heap(void *context, void *data, MMU32 bytes)
{
    Heap *h = (Heap *)context;
    (void)bytes;
    --h->live;
    free(data);
}
static MMU32 seed = 0xdeadbeefUL;
static MMU32 pick(MMU32 n)
{
    seed = seed * 1664525UL + 1013904223UL;
    return n ? seed % n : 0;
}
typedef struct Output
{
    MMByteBuffer *buffer;
    const unsigned char *expected;
    int fail, calls;
} Output;
static int write_buffer(void *context, MMU32 offset, const void *bytes, size_t count)
{
    Output *out = (Output *)context;
    ++out->calls;
    CHECK(!offset && out->buffer->storage.buffers[0].locks == 1);
    CHECK(!memcmp(bytes, out->expected, count));
    CHECK(mm_byte_buffer_reserve(out->buffer, 10000, 0) == MM_STORAGE_LOCKED);
    return !out->fail;
}
static void buffers(void)
{
    MMByteBuffer b;
    MMStorageAllocator allocator;
    Heap heap;
    unsigned char model[256], actual[256], input[8];
    MMU32 length = 0, offset, remove, n, i, step;
    MMStorageWriter writer;
    Output out;
    memset(&heap, 0, sizeof(heap));
    allocator.context = &heap;
    allocator.resize = resize_heap;
    allocator.release = release_heap;
    CHECK(mm_byte_buffer_init(&b, &allocator) == MM_STORAGE_OK);
    b.min_grow = 16;
    heap.fail_once = 1;
    CHECK(mm_byte_buffer_reserve(&b, 4, 0) == MM_STORAGE_OK && b.storage.buffers[0].capacity == 4 &&
          heap.calls == 2);
    CHECK(mm_byte_buffer_reserve(&b, 1, 1) == MM_STORAGE_OK && b.storage.buffers[0].capacity == 20);
    for (step = 0; step < 3000; ++step)
    {
        offset = pick(length + 1);
        remove = pick(length - offset + 1);
        n = pick(8);
        if (length - remove + n > 250)
            continue;
        for (i = 0; i < n; ++i)
            input[i] = (unsigned char)pick(256);
        CHECK(mm_byte_buffer_replace(&b, offset, remove, input, n) == MM_STORAGE_OK);
        memmove(model + offset + n, model + offset + remove, length - offset - remove);
        memcpy(model + offset, input, n);
        length = length - remove + n;
        CHECK(b.length == length && mm_byte_buffer_read(&b, 0, actual, length) == MM_STORAGE_OK &&
              !memcmp(actual, model, length));
    }
    heap.fail = 1;
    CHECK(mm_byte_buffer_reserve(&b, 10000, 0) != MM_STORAGE_OK && b.length == length);
    CHECK(mm_byte_buffer_read(&b, 0, actual, length) == MM_STORAGE_OK && !memcmp(actual, model, length));
    heap.fail = 0;
    CHECK(length > 1);
    out.buffer = &b;
    out.expected = model + 1;
    out.calls = out.fail = 0;
    writer.context = &out;
    writer.length = length - 1;
    writer.write_at = write_buffer;
    CHECK(mm_byte_buffer_write(&b, 1, &writer) == MM_STORAGE_OK && out.calls == 1);
    out.fail = 1;
    CHECK(mm_byte_buffer_write(&b, 1, &writer) == MM_STORAGE_IO && out.calls == 2);
    CHECK(!b.storage.buffers[0].locks);
    CHECK(mm_storage_dispose(&b.storage) == MM_STORAGE_OK && !heap.live);
}
typedef struct Pieces
{
    MMU32 sizes[3];
    int fail_index, calls;
} Pieces;
static int piece_size(void *context, MMU32 i, MMU32 *bytes)
{
    Pieces *p = (Pieces *)context;
    *bytes = p->sizes[i];
    return 1;
}
static int piece_read(void *context, MMU32 i, MMU32 offset, void *out, MMU32 bytes)
{
    const char *data[] = {"abc", "defg", "hi"};
    Pieces *p = (Pieces *)context;
    ++p->calls;
    if ((int)i == p->fail_index)
        return 0;
    memcpy(out, data[i] + offset, bytes);
    return 1;
}
static void pieces(void)
{
    Pieces p = {{3, 4, 2}, -1, 0};
    MMPieceReader reader;
    MMU32 i, start, n;
    unsigned char output[8];
    reader.context = &p;
    reader.count = 3;
    reader.length = 9;
    reader.piece_size = piece_size;
    reader.read_piece = piece_read;
    CHECK(mm_piece_find(&reader, 6, &i, &start, &n) == MM_DATA_OK && i == 1 && start == 3 && n == 4);
    CHECK(mm_piece_find(&reader, 9, &i, &start, &n) == MM_DATA_OK && i == 3 && start == 9 && !n);
    CHECK(mm_piece_read(&reader, 2, output, 6) == MM_DATA_OK && !memcmp(output, "cdefgh", 6));
    p.fail_index = 1;
    p.calls = 0;
    CHECK(mm_piece_read(&reader, 2, output, 6) == MM_DATA_IO && p.calls == 3);
    CHECK(output[0] == 'c' && output[5] == 'h' && output[1] == 0xa3 && output[4] == 0xa3);
    p.sizes[0] = 2;
    reader.length = 8;
    CHECK(mm_piece_find(&reader, 2, &i, &start, &n) == MM_DATA_OK && i == 1 && start == 2);
    reader.length = 0;
    CHECK(mm_piece_find(&reader, 0, &i, &start, &n) == MM_DATA_OK && !i && !start && !n);
}
int main(void)
{
    gst();
    ranges();
    unpack();
    packed();
    buffers();
    pieces();
    return 0;
}
