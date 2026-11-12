#include "three_mm/block_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "block check %d: %s\n", __LINE__, #x);                                                     \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Host
{
    int live, locks, alloc_fail, resize_fail, query_fail, convert_fail, read_fail, write_fail, buffer_fail;
    unsigned allocs, releases, resizes, reads, writes, buffers, codes;
    ThreeMMU32 capacity, written;
    unsigned char file[10000];
} Host;
typedef struct Handle
{
    ThreeMMU32 size;
    int locks;
    unsigned char bytes[1];
} Handle;
static int allocate(void *context, ThreeMMU32 n, void **out)
{
    Host *h = (Host *)context;
    Handle *p;
    ++h->allocs;
    *out = NULL;
    if (h->alloc_fail)
        return 0;
    p = (Handle *)malloc(sizeof(*p) + n);
    CHECK(p);
    p->size = n;
    p->locks = 0;
    memset(p->bytes, 0xcc, n);
    ++h->live;
    *out = p;
    return 1;
}
static void release(void *context, void *owned)
{
    Host *h = (Host *)context;
    Handle *p = (Handle *)owned;
    CHECK(p && !p->locks);
    --h->live;
    ++h->releases;
    free(p);
}
static ThreeMMU32 size(void *context, void *owned)
{
    (void)context;
    return ((Handle *)owned)->size;
}
static void *lock(void *context, void *owned)
{
    Host *h = (Host *)context;
    Handle *p = (Handle *)owned;
    ++h->locks;
    ++p->locks;
    return p->bytes;
}
static void unlock(void *context, void *owned)
{
    Host *h = (Host *)context;
    Handle *p = (Handle *)owned;
    CHECK(p->locks && h->locks);
    --p->locks;
    --h->locks;
}
static int resize_handle(void *context, void **owned, ThreeMMU32 n)
{
    Host *h = (Host *)context;
    Handle *p = (Handle *)*owned, *next;
    ++h->resizes;
    CHECK(!p->locks && !h->locks);
    if (h->resize_fail)
        return 0;
    next = (Handle *)malloc(sizeof(*p) + n);
    CHECK(next);
    next->size = n;
    next->locks = 0;
    memcpy(next->bytes, p->bytes, n < p->size ? n : p->size);
    free(p);
    *owned = next;
    return 1;
}
static ThreeMMHandleServices services(Host *h)
{
    ThreeMMHandleServices result;
    result.context = h;
    result.allocate = allocate;
    result.release = release;
    result.size = size;
    result.lock = lock;
    result.unlock = unlock;
    result.resize = resize_handle;
    return result;
}
static int read_file(void *context, ThreeMMU32 offset, void *output, size_t n)
{
    Host *h = (Host *)context;
    ++h->reads;
    if (h->read_fail && h->reads == (unsigned)h->read_fail)
        return 0;
    CHECK(three_mm_range_contains(sizeof(h->file), offset, (ThreeMMU32)n));
    memcpy(output, h->file + offset, n);
    return 1;
}
static int write_file(void *context, ThreeMMU32 offset, const void *input, size_t n)
{
    Host *h = (Host *)context;
    ++h->writes;
    if (h->write_fail && h->writes == (unsigned)h->write_fail)
        return 0;
    CHECK(three_mm_range_contains(sizeof(h->file), offset, (ThreeMMU32)n));
    memcpy(h->file + offset, input, n);
    return 1;
}
static ThreeMMBlockIO block(Host *h, const ThreeMMHandleServices *ops, int kind, ThreeMMU32 start, ThreeMMU32 n)
{
    ThreeMMBlockIO b;
    void *owned;
    memset(&b, 0, sizeof(b));
    b.handles = ops;
    b.context = h;
    b.read_file = read_file;
    b.write_file = write_file;
    b.view.kind = kind;
    b.view.start = start;
    b.view.length = n;
    if (kind == THREE_MM_BLOCK_MEMORY)
    {
        CHECK(allocate(h, start + n + 7, &owned));
        b.view.memory = owned;
        b.view.capacity = start + n + 7;
    }
    return b;
}
static void matrix(void)
{
    static const ThreeMMU32 lengths[] = {0, 1, 1024, 1025, 4096};
    Host source, dest;
    ThreeMMHandleServices a, b;
    ThreeMMBlockIO from, to, before;
    void *owned;
    unsigned char bytes[5000], actual[5000];
    unsigned i, s, shape, flags;
    int shrunk;
    for (i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)(i * 29 + i / 256);
    for (s = 0; s < 5; ++s)
        for (shape = 0; shape < 4; ++shape)
            for (flags = 0; flags < 4; ++flags)
            {
                memset(&source, 0, sizeof(source));
                memset(&dest, 0, sizeof(dest));
                a = services(&source);
                b = services(&dest);
                from = block(&source, &a, shape & 1 ? THREE_MM_BLOCK_FILE : THREE_MM_BLOCK_MEMORY, 3, lengths[s]);
                to = block(&dest, &b, shape & 2 ? THREE_MM_BLOCK_FILE : THREE_MM_BLOCK_MEMORY, 5, lengths[s]);
                from.view.packed = flags & 1;
                to.view.packed = flags & 2;
                if (shape & 1)
                    memcpy(source.file, bytes, lengths[s] + 10);
                else
                    memcpy(((Handle *)from.view.memory)->bytes, bytes, lengths[s] + 10);
                memset(dest.file, 0xcc, sizeof(dest.file));
                before = from;
                CHECK(three_mm_block_copy(&from, &to, 1, NULL) == THREE_MM_DATA_OK);
                CHECK(from.view.packed == (int)(flags & 1) && to.view.packed == (int)(flags & 2));
                CHECK(three_mm_block_read_handle(&from, 0, lengths[s], 1, &owned) == THREE_MM_DATA_OK &&
                      size(&source, owned) == lengths[s]);
                CHECK(three_mm_block_write_handle(&to, 0, owned, 1) == THREE_MM_DATA_OK);
                release(&source, owned);
                if (shape & 2)
                    memcpy(actual, dest.file, lengths[s] + 12);
                else
                    memcpy(actual, ((Handle *)to.view.memory)->bytes, lengths[s] + 12);
                for (i = 0; i < 5; ++i)
                    CHECK(actual[i] == 0xcc);
                CHECK(!memcmp(actual + 5, bytes + 3, lengths[s]));
                for (i = 0; i < 7; ++i)
                    CHECK(actual[5 + lengths[s] + i] == 0xcc);
                if (flags & 1)
                    CHECK(three_mm_block_copy(&from, &to, 0, NULL) == THREE_MM_DATA_PACKED);
                CHECK(three_mm_block_take_handle(&from, 1, &owned, &shrunk) == THREE_MM_DATA_OK && !shrunk);
                CHECK(size(&source, owned) == lengths[s] && !memcmp(((Handle *)owned)->bytes, bytes + 3, lengths[s]));
                if (shape & 1)
                    CHECK(!memcmp(&from, &before, sizeof(from)));
                else
                    CHECK(from.view.kind == THREE_MM_BLOCK_EMPTY && !from.view.packed && !from.view.memory);
                release(&source, owned);
                if (to.view.memory)
                    release(&dest, to.view.memory);
                CHECK(!source.live && !dest.live && !source.locks && !dest.locks);
            }
}
static int code(void *context, const void *input, ThreeMMU32 bytes, void *output, ThreeMMU32 capacity,
                ThreeMMI32 *written)
{
    Host *h = (Host *)context;
    CHECK(input && bytes == 16 && h->locks == (output ? 2 : 1));
    ++h->codes;
    if (!output)
    {
        *written = (ThreeMMI32)h->capacity;
        return !h->query_fail;
    }
    CHECK(capacity == h->capacity);
    if (h->convert_fail)
        return 0;
    *written = (ThreeMMI32)h->written;
    if (h->written <= capacity)
        memset(output, 0x42, h->written);
    return 1;
}
static void transactions(void)
{
    Host h;
    ThreeMMHandleServices ops;
    void *owned, *original;
    int failure, result, shrunk;
    for (failure = 0; failure < 6; ++failure)
    {
        memset(&h, 0, sizeof(h));
        ops = services(&h);
        CHECK(allocate(&h, 16, &owned));
        original = owned;
        h.capacity = 8;
        h.written = 4;
        h.query_fail = failure == 1;
        h.alloc_fail = failure == 2;
        h.convert_fail = failure == 3;
        h.resize_fail = failure == 4;
        if (failure == 5)
            h.written = 9;
        result = three_mm_handle_code(&ops, &owned, &h, code, &shrunk);
        CHECK(!h.locks && h.live == 1);
        if (failure == 0 || failure == 4)
        {
            CHECK(result == THREE_MM_DATA_OK && shrunk == (failure == 4));
            CHECK(size(&h, owned) == (failure == 4 ? 8u : 4u) && ((Handle *)owned)->bytes[0] == 0x42);
        }
        else
        {
            CHECK(result != THREE_MM_DATA_OK && owned == original && size(&h, owned) == 16);
            CHECK(((Handle *)owned)->bytes[0] == 0xcc);
        }
        release(&h, owned);
        CHECK(!h.live);
    }
    memset(&h, 0, sizeof(h));
    ops = services(&h);
    {
        ThreeMMBlockIO view = block(&h, &ops, THREE_MM_BLOCK_MEMORY, 3, 5);
        memcpy(((Handle *)view.view.memory)->bytes + 3, "slice", 5);
        h.resize_fail = 1;
        CHECK(three_mm_block_take_handle(&view, 1, &owned, &shrunk) == THREE_MM_DATA_OK && shrunk);
        CHECK(size(&h, owned) == 15 && !memcmp(((Handle *)owned)->bytes, "slice", 5));
        release(&h, owned);
        CHECK(!h.live && !h.locks);
    }
}
static void *buffer_allocate(void *context, void *old, ThreeMMU32 old_bytes, ThreeMMU32 n, int priority)
{
    Host *h = (Host *)context;
    CHECK(!old && !old_bytes && priority == THREE_MM_STORAGE_SPEED);
    if (h->buffer_fail)
        return NULL;
    ++h->buffers;
    return malloc(n);
}
static void buffer_release(void *context, void *buffer, ThreeMMU32 n)
{
    Host *h = (Host *)context;
    (void)n;
    --h->buffers;
    free(buffer);
}
static void copies(void)
{
    Host h, dest;
    ThreeMMHandleServices ops;
    ThreeMMBlockIO from, to;
    ThreeMMStorageAllocator allocator;
    unsigned char original[5000], expected[5000];
    unsigned i, mode, offset, n;
    int result;
    for (mode = 0; mode < 4; ++mode)
    {
        memset(&h, 0, sizeof(h));
        memset(&dest, 0, sizeof(dest));
        ops = services(&h);
        for (i = 0; i < sizeof(original); ++i)
            original[i] = (unsigned char)(i * 7 + i / 256);
        memcpy(h.file, original, sizeof(original));
        memcpy(expected, original, sizeof(expected));
        allocator.context = &h;
        allocator.resize = buffer_allocate;
        allocator.release = buffer_release;
        from = block(&h, &ops, THREE_MM_BLOCK_FILE, 0, 4096);
        to = block(&h, &ops, THREE_MM_BLOCK_FILE, 512, 4096);
        h.buffer_fail = mode != 0;
        h.read_fail = mode == 2 ? 2 : 0;
        h.write_fail = mode == 3 ? 2 : 0;
        result = three_mm_block_copy(&from, &to, 1, &allocator);
        CHECK(!h.buffers && result == (mode >= 2 ? THREE_MM_DATA_IO : THREE_MM_DATA_OK));
        if (!mode)
        {
            memmove(expected + 512, expected, 4096);
            CHECK(h.reads == 1 && h.writes == 1);
        }
        else
        {
            n = mode >= 2 ? 1024 : 4096;
            for (offset = 0; offset < n; offset += 1024)
                memmove(expected + 512 + offset, expected + offset, 1024);
            CHECK(h.reads == (mode >= 2 ? 2u : 4u));
        }
        CHECK(!memcmp(h.file, expected, sizeof(expected)));
    }
}
static int read_pack(void *context, void **handle)
{
    Host *h = (Host *)context;
    if (h->read_fail)
    {
        *handle = NULL;
        return 0;
    }
    return allocate(context, 16, handle);
}
static int pack(void *context, void **owned)
{
    Host *h = (Host *)context;
    void *next;
    if (h->convert_fail)
        return 0;
    if (!allocate(context, 4, &next))
        return 0;
    memset(((Handle *)next)->bytes, 0x42, 4);
    release(context, *owned);
    *owned = next;
    return 1;
}
static void packing(void)
{
    Host h;
    ThreeMMHandleServices ops;
    ThreeMMBlockDecoder coder;
    ThreeMMBlockView view, before;
    ThreeMMBlockIO io;
    int failure, shrunk;
    for (failure = 0; failure < 3; ++failure)
    {
        memset(&h, 0, sizeof(h));
        ops = services(&h);
        memset(&view, 0, sizeof(view));
        view.kind = THREE_MM_BLOCK_FILE;
        view.start = 7;
        view.length = 16;
        before = view;
        coder.context = &h;
        coder.read_file = read_pack;
        coder.decode = pack;
        coder.release = release;
        coder.size = size;
        h.read_fail = failure == 1;
        h.convert_fail = failure == 2;
        if (!failure)
        {
            CHECK(three_mm_block_pack(&view, &coder, &ops, &shrunk) == THREE_MM_DATA_OK && view.packed &&
                  view.length == 4);
            release(&h, view.memory);
        }
        else
            CHECK(three_mm_block_pack(&view, &coder, &ops, &shrunk) != THREE_MM_DATA_OK &&
                  !memcmp(&view, &before, sizeof(view)));
        CHECK(!h.live && !h.locks);
    }
    memset(&h, 0, sizeof(h));
    ops = services(&h);
    io = block(&h, &ops, THREE_MM_BLOCK_MEMORY, 3, 5);
    memcpy(((Handle *)io.view.memory)->bytes + 3, "slice", 5);
    coder.context = &h;
    h.convert_fail = 1;
    CHECK(three_mm_block_pack(&io.view, &coder, &ops, &shrunk) == THREE_MM_DATA_CODEC);
    CHECK(io.view.start == 0 && io.view.length == 5 && !io.view.packed &&
          !memcmp(((Handle *)io.view.memory)->bytes, "slice", 5));
    release(&h, io.view.memory);
    CHECK(!h.live && !h.locks);
}
static void read_failures(void)
{
    Host h;
    ThreeMMHandleServices ops;
    ThreeMMBlockIO b;
    void *owned = (void *)1;
    memset(&h, 0, sizeof(h));
    ops = services(&h);
    b = block(&h, &ops, THREE_MM_BLOCK_FILE, 3, 17);
    h.read_fail = 1;
    CHECK(three_mm_block_read_handle(&b, 0, 17, 1, &owned) == THREE_MM_DATA_IO && !owned && !h.live && !h.locks);
    h.read_fail = 0;
    h.alloc_fail = 1;
    CHECK(three_mm_block_read_handle(&b, 0, 17, 1, &owned) == THREE_MM_DATA_MEMORY && !owned && !h.live);
    h.alloc_fail = 0;
    b.view.packed = 1;
    CHECK(three_mm_block_read_handle(&b, 0, 17, 0, &owned) == THREE_MM_DATA_PACKED && !owned && !h.live);
    memset(&b.view, 0, sizeof(b.view));
    CHECK(three_mm_block_read_handle(&b, 0, 0, 1, &owned) == THREE_MM_DATA_OK && !owned && !h.live);
    {
        ThreeMMBlockIO memory = block(&h, &ops, THREE_MM_BLOCK_MEMORY, 3, 17);
        ThreeMMBlockIO file = block(&h, &ops, THREE_MM_BLOCK_FILE, 3, 17);
        h.reads = h.writes = 0;
        h.write_fail = 1;
        CHECK(three_mm_block_copy(&memory, &file, 1, NULL) == THREE_MM_DATA_IO && !h.locks);
        h.write_fail = 0;
        h.read_fail = 1;
        CHECK(three_mm_block_copy(&file, &memory, 1, NULL) == THREE_MM_DATA_IO && !h.locks);
        h.read_fail = 0;
        file.view.length = 16;
        CHECK(three_mm_block_copy(&memory, &file, 1, NULL) == THREE_MM_DATA_WRONG_SIZE && !h.locks);
        release(&h, memory.view.memory);
        CHECK(!h.live);
    }
}
int main(void)
{
    matrix();
    transactions();
    copies();
    packing();
    read_failures();
    return 0;
}
