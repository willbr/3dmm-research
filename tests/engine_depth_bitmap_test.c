#include "mm/depth_bitmap.h"
#include "engine_bitmap_block_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    MMDepthOwnerHost host;
    MMStorageAllocator object_allocator, pixel_allocator;
    MMDepthWriteServices writer;
    MMI32 overhead;
    ResourceTestBlock block;
    MMBlockIO output_io;
    unsigned char input[128], output[128];
    MMI32 size, packed, output_size;
    int phase, fail, objects, memory, warnings, unpacks, reads, writes, adds;
} Fixture;
static int fail(Fixture *f)
{
    return ++f->phase == f->fail;
}
static void *create(void *p, void *old, MMU32 old_bytes, MMU32 n, int priority)
{
    Fixture *f = (Fixture *)p;
    void *v;
    assert(!old && !old_bytes && n && priority == MM_STORAGE_NORMAL);
    if (fail(f))
        return NULL;
    v = malloc(n);
    assert(v);
    ++f->objects;
    return v;
}
static void *allocate(void *p, void *old, MMU32 old_bytes, MMU32 n, int priority)
{
    Fixture *f = (Fixture *)p;
    void *pixels;
    assert(!old && !old_bytes && n <= 128 && priority == MM_STORAGE_NORMAL);
    if (fail(f))
        return NULL;
    pixels = malloc(n ? n : 1);
    assert(pixels);
    ++f->memory;
    return pixels;
}
static void free_memory(void *p, void *pixels, MMU32 n)
{
    assert(pixels && n <= 128);
    --((Fixture *)p)->memory;
    free(pixels);
}
static void release(void *p, void *o, MMU32 n)
{
    assert(o && n);
    free(o);
    --((Fixture *)p)->objects;
}
static void warning(void *p)
{
    ++((Fixture *)p)->warnings;
}
static int decode(void *p, void **handle)
{
    Fixture *f = (Fixture *)p;
    assert(resource_handle_size(NULL, *handle) == (MMU32)f->packed);
    ++f->unpacks;
    if (fail(f))
        return 0;
    resource_block_decode(handle, f->input, (MMU32)f->size);
    return 1;
}
static int read(void *p, void *file, MMU32 at, void *out, size_t n)
{
    Fixture *f = (Fixture *)p;
    ++f->reads;
    assert(file == f && at >= 3 && at - 3 + n <= (size_t)f->size);
    if (fail(f))
        return 0;
    memcpy(out, f->input + (at - 3), n);
    return 1;
}
static int write(void *p, void *file, MMU32 at, const void *in, size_t n)
{
    Fixture *f = (Fixture *)p;
    ++f->writes;
    assert(file == f && at >= 3 && at - 3 + n <= (size_t)f->output_size);
    if (fail(f))
        return 0;
    memcpy(f->output + (at - 3), in, n);
    return 1;
}
static int add(void *p, void *file, MMU32 type, MMU32 n, MMU32 *number, MMStorageWriter *writer)
{
    Fixture *f = (Fixture *)p;
    assert(file == f && type == 0x12345678UL);
    ++f->adds;
    if (fail(f))
        return 0;
    assert(n <= 128);
    f->output_size = n;
    *number = 99;
    memset(f->output, 0xcc, 128);
    bitmap_block_file(&f->block, 3, n, 0);
    return mm_owned_block_io(&f->block.owner, &f->output_io) == MM_DATA_OK &&
           mm_block_io_writer(&f->output_io, writer) == MM_DATA_OK;
}
static void put(unsigned char *p, MMU32 n, int big)
{
    p[big ? 1 : 0] = (unsigned char)n;
    p[big ? 0 : 1] = (unsigned char)(n >> 8);
}
static void setup(Fixture *f, int big)
{
    int i;
    memset(f, 0, sizeof(*f));
    f->object_allocator.context = f;
    f->object_allocator.resize = create;
    f->object_allocator.release = release;
    f->pixel_allocator.context = f;
    f->pixel_allocator.resize = allocate;
    f->pixel_allocator.release = free_memory;
    f->host.objects = &f->object_allocator;
    f->host.pixels = &f->pixel_allocator;
    f->host.context = f;
    f->host.warning = warning;
    f->host.decode = decode;
    f->overhead = 100;
    f->writer.context = f;
    f->writer.host_big_endian = big;
    f->writer.system = 0x4321;
    f->writer.add = add;
    f->size = 24;
    f->packed = 9;
    resource_block_init(&f->block, f, read, write);
    put(f->input, 0x1234, big);
    put(f->input + 2, 0xffff, big);
    put(f->input + 4, 2, big);
    put(f->input + 6, 0xffff, big);
    put(f->input + 8, 3, big);
    put(f->input + 10, 2, big);
    for (i = 12; i < 24; ++i)
        f->input[i] = (unsigned char)(i * 11);
}
static void balanced(Fixture *f)
{
    assert(!f->objects && !f->memory);
    assert(mm_owned_block_clear(&f->block.owner) == MM_DATA_OK);
}
static void constructors(void)
{
    Fixture f;
    MMDepthOwner *owner;
    const MMDepthBitmap *m;
    unsigned char pixels[12];
    int n, i;
    memset(pixels, 0x35, 12);
    for (n = 0; n < 4; ++n)
    {
        setup(&f, 0);
        f.fail = n;
        owner = mm_depth_owner_new(3, 2, pixels, &f.host);
        if (owner)
        {
            m = mm_depth_owner_bitmap(owner);
            assert(m->bytes == 12 && m->row_bytes == 6 && !memcmp(m->pixels, pixels, 12));
            mm_depth_owner_release(owner);
        }
        else
            assert(n == 1 || n == 2);
        balanced(&f);
    }
    setup(&f, 0);
    owner = mm_depth_owner_new(3, 2, NULL, &f.host);
    assert(owner);
    m = mm_depth_owner_bitmap(owner);
    for (i = 0; i < 12; ++i)
        assert(((unsigned char *)m->pixels)[i] == 255);
    mm_depth_owner_release(owner);
    balanced(&f);
    assert(!mm_depth_owner_new(0, 2, NULL, &f.host));
    assert(!mm_depth_owner_new(-1, 2, NULL, &f.host));
    assert(!mm_depth_owner_new(2147483647, 2, NULL, &f.host));
    assert(!mm_depth_owner_new(0x40000000, 2, NULL, &f.host));
    assert(!mm_depth_owner_new(32768, 32768, NULL, &f.host));
    balanced(&f);
}
static void loading(void)
{
    const int failure_steps[] = {4, 4, 2};
    Fixture f;
    MMDepthOwner *owner;
    const MMDepthBitmap *m;
    void *result;
    MMI32 estimate;
    MMMemoryStream stream;
    int big, factory, storage, n, i, ok;
    for (big = 0; big < 2; ++big)
        for (factory = 0; factory < 2; ++factory)
            for (storage = 0; storage < 3; ++storage)
                for (n = 0; n <= failure_steps[storage]; ++n)
                {
                    setup(&f, big);
                    f.fail = n;
                    bitmap_block_source(&f.block, f.input, (MMU32)f.size, (MMU32)f.packed, storage);
                    result = NULL;
                    if (factory)
                    {
                        ok = mm_depth_owner_fetch(&f.block.owner, &result, &estimate, f.overhead, big, &f.host);
                        owner = result;
                    }
                    else
                    {
                        owner = mm_depth_owner_read_block(&f.block.owner, big, &f.host);
                        ok = owner != NULL;
                    }
                    assert(ok == !n);
                    if (ok)
                    {
                        assert(owner);
                        m = mm_depth_owner_bitmap(owner);
                        assert(m->bounds.left == 2 && m->bounds.top == -1 && m->bounds.right == 5 &&
                               m->bounds.bottom == 1 && m->bytes == 12 && !memcmp(m->pixels, f.input + 12, 12));
                        assert(f.unpacks == (storage == BITMAP_PACKED_FILE));
                        assert(f.block.owner.view.kind == (storage == BITMAP_FILE ? MM_BLOCK_FILE : MM_BLOCK_MEMORY));
                        if (factory)
                            assert(estimate == 112);
                        mm_depth_owner_release(owner);
                    }
                    else
                    {
                        assert(!owner);
                        assert(f.warnings == !(storage == BITMAP_PACKED_FILE && n <= 2));
                    }
                    balanced(&f);
                }
    for (big = 0; big < 2; ++big)
    {
        setup(&f, big);
        assert(mm_memory_stream_init(&stream, f.input, 23));
        assert(!mm_depth_owner_read_stream(&stream.stream, big, &f.host));
        assert(mm_memory_stream_init(&stream, f.input, 24));
        f.fail = f.phase + 1;
        assert(!mm_depth_owner_read_stream(&stream.stream, big, &f.host));
        f.fail = f.phase + 2;
        assert(!mm_depth_owner_read_stream(&stream.stream, big, &f.host));
        owner = mm_depth_owner_read_stream(&stream.stream, big, &f.host);
        assert(owner);
        m = mm_depth_owner_bitmap(owner);
        assert(m->bounds.left == 2 && m->bounds.top == -1 && m->bytes == 12 &&
               !memcmp(m->pixels, f.input + 12, 12));
        mm_depth_owner_release(owner);
        balanced(&f);
    }
    setup(&f, 0);
    bitmap_block_file(&f.block, 3, (MMU32)f.packed, 1);
    assert(mm_depth_owner_fetch(&f.block.owner, NULL, &estimate, f.overhead, 0, &f.host) &&
           estimate == 9 && !f.unpacks && !f.reads);
    assert(f.block.owner.view.packed);
    balanced(&f);
    for (i = 0; i < 24; ++i)
    {
        setup(&f, 0);
        f.size = i;
        bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
        assert(!mm_depth_owner_read_block(&f.block.owner, 0, &f.host));
        balanced(&f);
    }
    setup(&f, 0);
    f.size = 25;
    bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
    assert(!mm_depth_owner_read_block(&f.block.owner, 0, &f.host));
    balanced(&f);
    setup(&f, 0);
    put(f.input + 8, 0, 0);
    f.size = 12;
    bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
    owner = mm_depth_owner_read_block(&f.block.owner, 0, &f.host);
    assert(owner);
    m = mm_depth_owner_bitmap(owner);
    assert(!m->bytes && m->bounds.right == 2);
    mm_depth_owner_release(owner);
    balanced(&f);
    setup(&f, 0);
    put(f.input + 8, 0xffff, 0);
    bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
    assert(!mm_depth_owner_read_block(&f.block.owner, 0, &f.host));
    balanced(&f);
    setup(&f, 0);
    f.overhead = 2147483647;
    result = NULL;
    bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
    assert(!mm_depth_owner_fetch(&f.block.owner, &result, &estimate, f.overhead, 0, &f.host) && !result);
    balanced(&f);
}
static void writing(void)
{
    Fixture f;
    MMDepthBitmap m;
    unsigned char pixels[12];
    MMU32 number;
    int big, n, ok;
    memset(pixels, 0xab, 12);
    memset(&m, 0, sizeof(m));
    m.bounds.left = -2;
    m.bounds.top = 3;
    m.bounds.right = 1;
    m.bounds.bottom = 5;
    m.row_bytes = 6;
    m.bytes = 12;
    m.pixels = pixels;
    for (big = 0; big < 2; ++big)
        for (n = 0; n < 5; ++n)
        {
            setup(&f, big);
            f.fail = n;
            number = 123;
            ok = mm_depth_write(&m, &f, 0x12345678UL, &number, &f.writer);
            if (ok)
            {
                unsigned char header[12];
                put(header, 1, big);
                put(header + 2, 0x4321, big);
                put(header + 4, 0xfffe, big);
                put(header + 6, 3, big);
                put(header + 8, 3, big);
                put(header + 10, 2, big);
                assert(number == 99 && f.output_size == 24 && !memcmp(header, f.output, 12) &&
                       !memcmp(pixels, f.output + 12, 12));
            }
            else
            {
                assert(n >= 1 && n <= 3);
                assert(number == (n == 1 ? 123 : 99));
                if (n >= 2)
                    assert(f.output_size == 24);
                if (n == 3)
                    assert(f.output[12] == 0xcc);
            }
            balanced(&f);
        }
}
static void drawing(void)
{
    MMDepthBitmap m;
    unsigned char source[64], dest[100];
    MMDepthRect clip = {1, 1, 4, 7};
    const MMDepthRect shape = {1, 2, 4, 5}, hole = {2, 2, 3, 5};
    MMRegion region = {0};
    int i, half;
    memset(&m, 0, sizeof(m));
    m.bounds.right = 4;
    m.bounds.bottom = 8;
    m.row_bytes = 8;
    m.bytes = 64;
    m.pixels = source;
    for (i = 0; i < 64; ++i)
        source[i] = (unsigned char)i;
    for (half = 0; half < 2; ++half)
    {
        memset(dest, 0xcc, 100);
        assert(mm_depth_draw(&m, dest + 4, 10, 8, 1, 1, &clip, NULL, half));
        for (i = 0; i < 80 / (half ? 2 : 1); ++i)
        {
            int y = i / 10, x = i % 10;
            int copied = y >= (half ? 0 : 1) && y < (half ? 3 : 7) && x >= 2 && x < 8;
            assert(dest[4 + i] == (copied ? source[y * 8 + x] : 0xcc));
        }
        for (i = 0; i < 4; ++i)
            assert(dest[i] == 0xcc && dest[96 + i] == 0xcc);
    }
    memset(dest, 0xcc, 100);
    assert(mm_depth_draw(&m, dest, 10, 8, 0, 0, NULL, NULL, 0));
    assert(mm_depth_draw(&m, dest, 10, 8, 0, 0, NULL, &region, 0));
    assert(!mm_depth_draw(&m, dest, 10, 8, 2147483647, 0, &clip, NULL, 0));
    assert(!mm_depth_draw(&m, dest, 2147483647, 8, 0, 0, &clip, NULL, 0));
    clip.top = 7;
    clip.bottom = 8;
    assert(!mm_depth_draw(&m, dest, 10, 7, 0, 0, &clip, NULL, 1));
    for (i = 0; i < 100; ++i)
        assert(dest[i] == 0xcc);
    clip.top = 0;
    clip.bottom = 4;
    mm_region_set_rect(&region, &shape);
    assert(mm_region_apply_rect(&region, MM_REGION_DIFFERENCE, &hole, NULL, 0, NULL) == MM_REGION_UPDATED);
    assert(region.bands);
    assert(mm_depth_draw(&m, dest, 10, 8, 0, 0, &clip, &region, 0));
    for (i = 0; i < 80; ++i)
    {
        int y = i / 10, x = i % 10;
        int copied = y >= 2 && y < 4 && (x == 2 || x == 3 || x == 6 || x == 7);
        assert(dest[i] == (copied ? source[y * 8 + x] : 0xcc));
    }
    memset(dest, 0xcc, 100);
    assert(!mm_depth_draw(&m, dest, 6, 8, 0, 0, &clip, &region, 0));
    for (i = 0; i < 100; ++i)
        assert(dest[i] == (i == 14 ? source[18] : i == 15 ? source[19] : 0xcc));
    mm_region_set_rect(&region, NULL);
}
int main(void)
{
    constructors();
    loading();
    writing();
    drawing();
    puts("C89 depth bitmap: native-order headers, ownership/failures, partial writes and bounded full/half clipping "
         "passed.");
    return 0;
}
