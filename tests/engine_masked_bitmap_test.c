#include "mm/masked_bitmap.h"
#include "mm/dib_frame.h"
#include "mm/damage_map.h"
#include "mm/ellipse_geometry.h"
#include "mm/graphics_transition.h"
#include "mm/graphics_view.h"
#include "mm/indexed_display.h"
#include "mm/pixel_paint.h"
#include "mm/playback.h"
#include "mm/region.h"
#include "engine_bitmap_block_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    MMMaskedOwnerHost host;
    MMStorageAllocator objects;
    ResourceTestBlock block;
    unsigned char input[4096], output[4096], bitmap[24];
    MMI32 size, packed, written;
    int phase, fail, owners, memory, upside_down, unpacks, reads, writes, diagnostic;
} Fixture;
static int fail(Fixture *f)
{
    return ++f->phase == f->fail;
}
static void *create(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Fixture *f = (Fixture *)p;
    void *o;
    assert(!old && !old_bytes && bytes && priority == MM_STORAGE_NORMAL);
    if (fail(f))
        return NULL;
    o = malloc(bytes);
    assert(o);
    ++f->owners;
    return o;
}
static int allocate(void *p, MMI32 n, void **out)
{
    Fixture *f = (Fixture *)p;
    if (fail(f))
        return 0;
    assert(n >= 0 && n <= 4096);
    *out = malloc(n ? (size_t)n : 1);
    assert(*out);
    ++f->memory;
    return 1;
}
static void free_memory(void *p, void **v)
{
    if (*v)
    {
        --((Fixture *)p)->memory;
        free(*v);
        *v = NULL;
    }
}
static void release(void *p, void *o, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    assert(o && bytes);
    free(o);
    --f->owners;
}
static void diagnostic(void *p, int reason)
{
    ((Fixture *)p)->diagnostic = reason + 1;
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
    assert(file == f && at == 3 && n <= (size_t)f->size);
    if (fail(f))
        return 0;
    memcpy(out, f->input, (size_t)n);
    return 1;
}
static int write(void *p, void *file, MMU32 at, const void *data, size_t n)
{
    Fixture *f = (Fixture *)p;
    ++f->writes;
    assert(file == f && at == 3 && n == (size_t)f->size);
    if (fail(f))
        return 0;
    memcpy(f->output, data, (size_t)n);
    f->written = (MMI32)n;
    return 1;
}
static int bitmap_read(void *p, void *name, unsigned char transparent, MMMaskedSource *out)
{
    Fixture *f = (Fixture *)p;
    (void)name;
    assert(transparent == 0);
    if (fail(f))
        return 0;
    if (!allocate(p, 24, &out->pixels))
        return 0;
    memcpy(out->pixels, f->bitmap, 24);
    out->width = 3;
    out->height = 6;
    out->upside_down = f->upside_down;
    return 1;
}
static void setup(Fixture *f, int big)
{
    memset(f, 0, sizeof(*f));
    f->host.bitmap.context = f;
    f->host.bitmap.host_big_endian = big;
    f->host.bitmap.system = 0x4321;
    f->objects.context = f;
    f->objects.resize = create;
    f->objects.release = release;
    f->host.objects = &f->objects;
    f->host.bitmap.allocate = allocate;
    f->host.bitmap.free_memory = free_memory;
    f->host.bitmap.diagnostic = diagnostic;
    f->host.bitmap.decode = decode;
    f->host.bitmap.bitmap_read = bitmap_read;
    resource_block_init(&f->block, f, read, write);
}
static void put(unsigned char *b, unsigned n, MMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        b[big ? n - i - 1 : i] = (unsigned char)v;
        v >>= 8;
    }
}
static void balanced(Fixture *f)
{
    assert(!f->memory && !f->owners);
    assert(mm_owned_block_clear(&f->block.owner) == MM_DATA_OK);
}
static void seed(Fixture *f, int source_big)
{
    unsigned char header[28];
    memset(header, 0, sizeof(header));
    put(header, 2, 1, source_big);
    put(header + 2, 2, 0x1234, source_big);
    put(header + 8, 4, 0xfffffffeUL, source_big);
    put(header + 12, 4, 3, source_big);
    put(header + 16, 4, 2, source_big);
    put(header + 20, 4, 5, source_big);
    put(header + 24, 4, 40, source_big);
    memcpy(f->input, header, 28);
    put(f->input + 28, 2, 4, source_big);
    put(f->input + 30, 2, 4, source_big);
    f->input[32] = 1;
    f->input[33] = 2;
    f->input[34] = 9;
    f->input[35] = 8;
    f->input[36] = 0;
    f->input[37] = 2;
    f->input[38] = 6;
    f->input[39] = 5;
    f->size = 40;
    f->packed = 17;
}
static void encode(void)
{
    Fixture f;
    MMMaskedBitmap m;
    MMMaskedRect r = {0, 0, 5, 4}, bounds;
    MMBlockIO io;
    MMStorageWriter writer;
    unsigned char pixels[20] = {0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0};
    int big, flags, x, y, n;
    for (big = 0; big < 2; ++big)
        for (flags = 0; flags < 4; ++flags)
        {
            setup(&f, big);
            memset(&m, 0, sizeof(m));
            assert(mm_masked_init(&m, pixels, 5, 4, &r, 7, -2, 0, (unsigned)flags, 37, &f.host.bitmap));
            assert(mm_masked_validate(&m, big));
            mm_masked_bounds(&m, big, &bounds);
            assert(bounds.left == -6 && bounds.right == -3);
            for (y = -1; y < 5; ++y)
                for (x = -1; x < 6; ++x)
                {
                    int expected = x >= 0 && x < 5 && y >= 0 && y < 4 && pixels[(flags & 1 ? 3 - y : y) * 5 + x] != 0;
                    assert(mm_masked_hit(&m, big, x - 7, y + 2) == expected);
                }
            f.size = m.bytes;
            bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
            assert(mm_owned_block_io(&f.block.owner, &io) == MM_DATA_OK &&
                   mm_block_io_writer(&io, &writer) == MM_DATA_OK);
            assert(mm_masked_write(&m, &writer, &f.host.bitmap) && f.written == m.bytes &&
                   !memcmp(f.output, m.data, (size_t)m.bytes));
            --writer.length;
            assert(!mm_masked_write(&m, &writer, &f.host.bitmap) && f.diagnostic == MM_MASKED_WRONG_SIZE + 1);
            mm_masked_dispose(&m, &f.host.bitmap);
            balanced(&f);
        }
    for (n = 0; n < 4; ++n)
    {
        MMMaskedOwner *o;
        setup(&f, 0);
        f.fail = n;
        o = mm_masked_owner_new(pixels, 5, 4, &r, 0, 0, 0, 0, 0, &f.host);
        if (o)
            mm_masked_owner_release(o);
        else
            assert(n == 1 || n == 2);
        balanced(&f);
    }
    setup(&f, 0);
    memset(&m, 0, sizeof(m));
    assert(mm_masked_init(&m, pixels, 5, 4, &r, 0, 0, 0, 0, 0, &f.host.bitmap));
    {
        void *old = m.data;
        f.fail = f.phase + 1;
        assert(!mm_masked_init(&m, pixels, 5, 4, &r, 0, 0, 0, 2, 0, &f.host.bitmap) && m.data == old &&
               mm_masked_hit(&m, 0, 1, 1));
    }
    mm_masked_dispose(&m, &f.host.bitmap);
    balanced(&f);
    setup(&f, 0);
    memset(&m, 0, sizeof(m));
    memset(pixels, 0, 20);
    assert(mm_masked_init(&m, pixels, 5, 4, &r, 7, -2, 0, 0, 0, &f.host.bitmap) && m.bytes == 28 && !m.row_bytes &&
           f.diagnostic == 1);
    mm_masked_bounds(&m, 0, &bounds);
    assert(bounds.left == -7 && bounds.right == -7 && bounds.top == 2 && bounds.bottom == 2);
    assert(!mm_masked_hit(&m, 0, -7, 2));
    mm_masked_dispose(&m, &f.host.bitmap);
    assert(!mm_masked_init(&m, pixels, 2147483647, 2, &r, 0, 0, 0, 0, 0, &f.host.bitmap));
    assert(!mm_masked_init(&m, pixels, 5, 4, &r, (-2147483647 - 1), 0, 0, 0, 0, &f.host.bitmap));
    balanced(&f);
}
static void long_runs(void)
{
    Fixture f;
    MMMaskedBitmap m;
    MMMaskedRect r = {0, 0, 700, 2};
    unsigned char pixels[1400];
    int big, mask, x, y;
    for (big = 0; big < 2; ++big)
        for (mask = 0; mask < 2; ++mask)
        {
            setup(&f, big);
            memset(&m, 0, sizeof(m));
            memset(pixels, 17, sizeof(pixels));
            memset(pixels, 0, 600);
            pixels[0] = 3;
            assert(mm_masked_init(&m, pixels, 700, 2, &r, 0, 0, 0, (unsigned)mask * 2, 37, &f.host.bitmap));
            assert(mm_masked_validate(&m, big));
            for (y = 0; y < 2; ++y)
                for (x = 0; x < 700; ++x)
                    assert(mm_masked_hit(&m, big, x, y) == (pixels[y * 700 + x] != 0));
            mm_masked_dispose(&m, &f.host.bitmap);
            balanced(&f);
        }
    {
        unsigned char *wide = (unsigned char *)malloc(40000);
        assert(wide);
        memset(wide, 1, 40000);
        r.right = 40000;
        r.bottom = 1;
        setup(&f, 0);
        memset(&m, 0, sizeof(m));
        assert(!mm_masked_init(&m, wide, 40000, 1, &r, 0, 0, 0, 0, 0, &f.host.bitmap) && !m.data);
        free(wide);
        balanced(&f);
    }
}
static void loading(void)
{
    const int failure_steps[] = {3, 4, 2};
    Fixture f;
    MMMaskedOwner *owner;
    const MMMaskedBitmap *m;
    void *result;
    MMI32 estimate;
    int big, foreign, storage, factory, n, ok;
    for (big = 0; big < 2; ++big)
        for (foreign = 0; foreign < 2; ++foreign)
            for (storage = 0; storage < 3; ++storage)
                for (factory = 0; factory < 2; ++factory)
                    for (n = 0; n <= failure_steps[storage]; ++n)
                    {
                        setup(&f, big);
                        seed(&f, big ^ foreign);
                        bitmap_block_source(&f.block, f.input, (MMU32)f.size, (MMU32)f.packed, storage);
                        f.fail = n;
                        result = NULL;
                        if (factory)
                        {
                            ok = mm_masked_owner_fetch(&f.block.owner, &result, &estimate, &f.host);
                            owner = result;
                        }
                        else
                        {
                            owner = mm_masked_owner_read_block(&f.block.owner, &f.host);
                            ok = owner != NULL;
                        }
                        assert(ok == !n);
                        if (ok)
                        {
                            assert(owner);
                            m = mm_masked_owner_bitmap(owner);
                            assert(mm_masked_hit(m, big, -1, 3) && !mm_masked_hit(m, big, -2, 3) &&
                                   m->row_bytes == 4);
                            assert(f.block.owner.view.kind == (storage == BITMAP_FILE ? MM_BLOCK_FILE : MM_BLOCK_EMPTY));
                            assert(f.unpacks == (storage == BITMAP_PACKED_FILE));
                            if (factory)
                                assert(estimate == 40);
                            mm_masked_owner_release(owner);
                        }
                        else if (storage == BITMAP_MEMORY)
                            assert(f.block.owner.view.kind == (n == 1 ? MM_BLOCK_MEMORY : MM_BLOCK_EMPTY));
                        balanced(&f);
                    }
    setup(&f, 0);
    seed(&f, 0);
    bitmap_block_file(&f.block, 3, (MMU32)f.packed, 1);
    assert(mm_masked_owner_fetch(&f.block.owner, NULL, &estimate, &f.host) && estimate == 17 && !f.reads && !f.unpacks);
    assert(f.block.owner.view.packed);
    balanced(&f);
    for (n = 0; n < 40; ++n)
    {
        setup(&f, 0);
        seed(&f, 0);
        f.size = n;
        bitmap_block_file(&f.block, 3, (MMU32)f.size, 0);
        assert(!mm_masked_owner_read_block(&f.block.owner, &f.host));
        balanced(&f);
    }
    setup(&f, 0);
    seed(&f, 0);
    f.input[6] = 1;
    bitmap_block_memory(&f.block, f.input, (MMU32)f.size);
    assert(!mm_masked_owner_read_block(&f.block.owner, &f.host) && f.block.owner.view.kind == MM_BLOCK_EMPTY);
    balanced(&f);
}
static void malformed(void)
{
    Fixture f;
    MMMaskedBitmap m;
    int variant, big;
    for (big = 0; big < 2; ++big)
        for (variant = 0; variant < 11; ++variant)
        {
            setup(&f, big);
            seed(&f, big);
            m.data = f.input;
            m.bytes = f.size;
            m.row_bytes = 0;
            if (variant == 0)
                put(f.input, 2, 0x1234, big);
            if (variant == 1)
                put(f.input + 6, 2, 1, big);
            if (variant == 2)
                put(f.input + 24, 4, 39, big);
            if (variant == 3)
                put(f.input + 28, 2, 0xffff, big);
            if (variant == 4)
                put(f.input + 28, 2, 30, big);
            if (variant == 5)
                f.input[32] = 7;
            if (variant == 6)
                f.input[33] = 7;
            if (variant == 7)
            {
                put(f.input + 28, 2, 3, big);
                put(f.input + 30, 2, 5, big);
            }
            if (variant == 8)
            {
                m.bytes = 41;
                put(f.input + 24, 4, 41, big);
            }
            if (variant == 9)
                put(f.input + 20, 4, 2, big);
            if (variant == 10)
            {
                put(f.input + 8, 4, 0x80000000UL, big);
                put(f.input + 16, 4, 0x7fffffffUL, big);
            }
            assert(!mm_masked_validate(&m, big));
            balanced(&f);
        }
    setup(&f, 0);
    seed(&f, 0);
    put(f.input + 20, 4, 3, 0);
    put(f.input + 24, 4, 28, 0);
    m.data = f.input;
    m.bytes = 28;
    assert(mm_masked_validate(&m, 0) && !m.row_bytes);
    {
        MMMaskedRect r;
        mm_masked_bounds(&m, 0, &r);
        assert(r.left == r.right && r.top == r.bottom);
    }
}
static void imports(void)
{
    Fixture f;
    MMMaskedOwner *owner;
    int up, flags, n;
    for (up = 0; up < 2; ++up)
        for (flags = 0; flags < 4; ++flags)
            for (n = 0; n < 7; ++n)
            {
                setup(&f, 0);
                f.upside_down = up;
                f.bitmap[0] = 9;
                f.bitmap[21] = 7;
                f.fail = n;
                owner = mm_masked_owner_import(&f, 0, 0, 0, (unsigned)flags, 37, &f.host);
                if (owner)
                {
                    int flip = up ? (flags & 1) : !(flags & 1);
                    assert(mm_masked_hit(mm_masked_owner_bitmap(owner), 0, 0, flip ? 5 : 0));
                    mm_masked_owner_release(owner);
                }
                balanced(&f);
            }
}
static void stream_blitting(void)
{
    Fixture f;
    MMMaskedBitmap m;
    MMMemoryStream stream;
    const MMMaskedRect clip = {1, 1, 2, 3};
    unsigned char pixels[12], expected[12], bits[2];
    const MMMaskedRect first = {0, 0, 1, 1}, second = {2, 0, 3, 1}, last = {1, 1, 4, 2};
    MMRegion region = {0};
    int big, source, mask;
    mm_region_set_rect(&region, &first);
    assert(mm_region_apply_rect(&region, MM_REGION_UNION, &second, NULL, 0, NULL) == MM_REGION_UPDATED);
    assert(mm_region_apply_rect(&region, MM_REGION_UNION, &last, NULL, 0, NULL) == MM_REGION_UPDATED);
    for (big = 0; big < 2; ++big)
        for (source = 0; source < 2; ++source)
            for (mask = 0; mask < 2; ++mask)
            {
                setup(&f, big);
                seed(&f, source);
                if (mask)
                {
                    f.size = 36;
                    f.input[4] = 1;
                    f.input[5] = 37;
                    put(f.input + 24, 4, 36, source);
                    put(f.input + 28, 2, 2, source);
                    put(f.input + 30, 2, 2, source);
                    f.input[34] = 0;
                    f.input[35] = 2;
                }
                memset(&m, 0, sizeof(m));
                assert(mm_memory_stream_init(&stream, f.input, (size_t)f.size));
                assert(mm_masked_read_stream(&stream.stream, &m, &f.host.bitmap));
                memset(f.input, 0, sizeof(f.input));
                memset(expected, 0xa5, sizeof(expected));
                expected[1] = mask ? 37 : 9;
                expected[2] = mask ? 37 : 8;
                expected[4] = mask ? 37 : 6;
                expected[5] = mask ? 37 : 5;
                memset(pixels, 0xa5, sizeof(pixels));
                mm_masked_blit8(&m, big, pixels, 4, 3, 0, 0, NULL, NULL);
                assert(!memcmp(pixels, expected, sizeof(pixels)));
                memset(expected, 0xa5, sizeof(expected));
                expected[5] = mask ? 37 : 8;
                memset(pixels, 0xa5, sizeof(pixels));
                mm_masked_blit8(&m, big, pixels, 4, 3, -1, 1, &clip, NULL);
                mm_masked_blit8(&m, big, pixels, 4, 3, (-2147483647 - 1), 0, NULL, NULL);
                assert(!memcmp(pixels, expected, sizeof(pixels)));
                memset(expected, 0xa5, sizeof(expected));
                expected[2] = mask ? 37 : 8;
                expected[5] = mask ? 37 : 5;
                memset(pixels, 0xa5, sizeof(pixels));
                mm_masked_blit8(&m, big, pixels, 4, 3, 0, 0, NULL, &region);
                assert(!memcmp(pixels, expected, sizeof(pixels)));
                memset(bits, 0xa5, sizeof(bits));
                mm_masked_blit_mask(&m, big, bits, 1, 2, 4, 0, NULL);
                assert(bits[0] == 0xa6 && bits[1] == 0xac);
                mm_masked_dispose(&m, &f.host.bitmap);
                assert(!mm_masked_read_stream(&stream.stream, &m, &f.host.bitmap) && !m.data);
                balanced(&f);
            }
    mm_region_set_rect(&region, NULL);
}
static void scaled_drawing(void)
{
    static const unsigned char encoded[] = {0, 4, 0x0f, 0x33, 0xa5, 0xf0, 0, 1, 0xff, 1, 2, 0xf7, 0xaf};
    static const unsigned short lengths[] = {6, 7};
    const MMMaskedRle source = {encoded, sizeof(encoded), lengths, 4, 2, 0, 0};
    MMMaskedRle invalid = source;
    MMMaskedRect rect = {0, 0, 3, 1};
    MMMaskedTransfer transfer;
    unsigned char pixels[4];
    MMPixelSurface surface;
    int i;
    surface.pixels = pixels;
    surface.width = surface.pitch = 4;
    surface.height = 1;
    surface.depth = 8;
    memset(pixels, 0x0f, sizeof(pixels));
    assert(mm_masked_stretch(&surface, &source, &rect, NULL, NULL, NULL, 0, NULL, 0));
    assert(pixels[0] == 0xff && pixels[1] == 0xf7 && pixels[2] == 0xaf && pixels[3] == 0x0f);
    transfer.mask_black = 0;
    transfer.mask_white = 255;
    transfer.background = 0x55;
    for (i = 0; i < 256; ++i)
        transfer.colours[i] = (MMU32)i;
    assert(mm_masked_stretch(&surface, &source, &rect, NULL, NULL, NULL, 0, &transfer, 1));
    assert(pixels[0] == 0x0f && pixels[1] == 0x33 && pixels[2] == 0xa5 && pixels[3] == 0x0f);
    rect.right = 2;
    memset(pixels, 0x0f, sizeof(pixels));
    assert(mm_masked_stretch(&surface, &source, &rect, NULL, NULL, NULL, 0, &transfer, 1));
    assert(pixels[0] == 1 && pixels[1] == 0xa0 && pixels[2] == 0x0f);
    invalid.bytes--;
    assert(!mm_masked_stretch(&surface, &invalid, &rect, NULL, NULL, NULL, 0, &transfer, 1));
    assert(pixels[0] == 1 && pixels[1] == 0xa0);
    rect.left = -2147483645L;
    assert(mm_masked_stretch(&surface, &source, &rect, NULL, NULL, NULL, 0, NULL, 0));
    assert(pixels[0] == 0xaf && pixels[1] == 0xaf && pixels[2] == 0x0f);
}

static void packed_copy(void)
{
    MMPixelImage source, target;
    const MMPixelRect row = {0, 0, 2, 1};
    const MMPixelRect wide = {-2147483645L, 0, 2, 1};
    unsigned char pixels[] = {1, 2, 3, 0xf0, 5, 6, 7, 0x80, 0, 0, 0, 0};
    unsigned char palette[] = {0, 0, 0, 0, 255, 255, 255, 0};
    unsigned char colours[] = {255, 255, 255, 128, 128, 128};

    source.surface.pixels = pixels;
    source.surface.width = 2;
    source.surface.height = 1;
    source.surface.pitch = 8;
    source.surface.depth = 32;
    source.palette = NULL;
    source.colours = 0;
    target = source;
    target.surface.pixels = pixels + 4;
    assert(mm_pixel_copy(&target, &source, &wide, &wide, &row, NULL, 0, NULL));
    assert(memcmp(pixels, pixels + 4, 4) == 0);
    assert(pixels[8] == 5 && pixels[9] == 6 && pixels[10] == 7 && pixels[11] == 0x80);

    source.surface.pixels = colours;
    source.surface.pitch = 6;
    source.surface.depth = 24;
    target.surface.pixels = palette;
    target.surface.pitch = 1;
    target.surface.depth = 4;
    target.palette = palette;
    target.colours = 2;
    assert(mm_pixel_copy(&target, &source, &row, &row, NULL, NULL, 0, NULL));
    assert(palette[0] == 0x11);
}

static void dib_and_indexed(void)
{
    static const unsigned char dib[] = {
        40, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0x20, 1, 2, 0x21, 1, 2, 0x22, 1, 2, 0xda, 0xda, 0xda,
        0x10, 3, 4, 0x11, 3, 4, 0x12, 3, 4, 0xdb, 0xdb, 0xdb
    };
    static const unsigned char palette[] = {1, 2, 3, 255, 1, 2, 3, 0, 4, 5, 6, 31};
    const MMPixelRect wide = {-2147483644L, 0, 3, 2}, row = {0, 0, 4, 1}, valid = {0, 0, 3, 1};
    unsigned char decoded[28], drawn[26], indices[] = {1, 0, 2, 4};
    MMU32 native[4];
    MMPixelSurface frame, surface;
    MMPixelImage indexed;
    MMDibInfo info;
    int x;

    frame.pixels = decoded;
    frame.width = 3;
    frame.height = 2;
    frame.pitch = 14;
    frame.depth = 32;
    memset(decoded, 0xaa, sizeof(decoded));
    assert(mm_dib_info(dib, 40, &info) && info.bytes == sizeof(dib) && info.bottom_up && info.pitch == 12);
    assert(!mm_dib_decode(dib, sizeof(dib) - 1, &frame) && decoded[0] == 0xaa);
    assert(mm_dib_decode(dib, sizeof(dib), &frame));
    assert(decoded[0] == 0x10 && decoded[2] == 4 && decoded[3] == 0 && decoded[12] == 0xaa);
    assert(decoded[14] == 0x20 && decoded[16] == 2 && decoded[17] == 0 && decoded[26] == 0xaa);
    surface = frame;
    surface.pixels = drawn;
    surface.pitch = 13;
    memset(drawn, 0xbb, sizeof(drawn));
    assert(mm_dib_draw(&surface, &frame, sizeof(decoded), 24, &wide, NULL, NULL, NULL));
    for (x = 0; x < 3; ++x)
        assert(drawn[4 * x] == 0x12 && drawn[13 + 4 * x] == 0x22);
    assert(drawn[12] == 0xbb && drawn[25] == 0xbb);

    surface.pixels = native;
    surface.width = 4;
    surface.height = 1;
    surface.pitch = 16;
    indexed.surface.pixels = indices;
    indexed.surface.width = indexed.surface.pitch = 4;
    indexed.surface.height = 1;
    indexed.surface.depth = 8;
    indexed.palette = palette;
    indexed.colours = 3;
    native[3] = 0xabcdefUL;
    assert(mm_pixel_expand_indexed(&surface, &indexed, &row, &row, NULL, NULL));
    assert(native[0] == 0x030201UL && native[1] == native[0] && native[2] == 0x060504UL && native[3] == 0xabcdefUL);
    assert(!mm_pixel_snapshot_indexed(&indexed, &surface, &row, &row) && indices[0] == 1 && indices[3] == 4);
    indexed.surface.pixels = (unsigned char *)&native[1];
    assert(mm_pixel_snapshot_indexed(&indexed, &surface, &valid, &valid));
    assert(((unsigned char *)&native[1])[0] == 0 && ((unsigned char *)&native[1])[1] == 0 &&
           ((unsigned char *)&native[1])[2] == 2);
}

static void pixel_scroll(void)
{
    static const unsigned char before[] = {0xa5, 0x3c, 0x96, 0xf0, 0xda};
    static const unsigned char moved[] = {0xac, 0x96, 0xf6, 0xf0, 0xda};
    static const unsigned char legacy[] = {0xa6, 0x96, 0xf6, 0xf0, 0xda};
    unsigned char pixels[sizeof(before)];
    const MMPixelRect rect = {4, 0, 28, 1};
    const MMPixelRect extreme = {-2147483647L - 1, 0, 2147483647L, 1};
    MMPixelSurface surface;
    surface.pixels = pixels;
    surface.width = 32;
    surface.height = 1;
    surface.pitch = sizeof(pixels);
    surface.depth = 1;
    memcpy(pixels, before, sizeof(pixels));
    assert(mm_pixel_scroll(&surface, &rect, -8, 0, NULL, NULL, 0));
    assert(!memcmp(pixels, moved, sizeof(pixels)));
    memcpy(pixels, before, sizeof(pixels));
    assert(mm_pixel_scroll(&surface, &rect, -8, 0, NULL, NULL, 1));
    assert(!memcmp(pixels, legacy, sizeof(pixels)));
    assert(mm_pixel_scroll(&surface, &extreme, -2147483647L - 1, 0, NULL, NULL, 1));
    assert(!memcmp(pixels, legacy, sizeof(pixels)));
}

static void pixel_polygon(void)
{
    static const MMPixelPoint triangle[] = {
        {-2147483647L - 1, -2147483647L - 1}, {2147483647L, 2147483647L}, {-2147483647L - 1, 2147483647L}
    };
    const MMPixelBrush brush = {{0, 0x7e}, {0, 0}, {255, 255, 255, 255, 255, 255, 255, 255}};
    MMPixelSurface surface;
    unsigned char pixels[32];
    MMI32 x, y;
    surface.pixels = pixels;
    surface.width = 7;
    surface.height = 4;
    surface.pitch = 8;
    surface.depth = 8;
    memset(pixels, 0xe7, sizeof(pixels));
    assert(mm_pixel_fill_polygon(&surface, &brush, triangle, 3, NULL, NULL));
    for (y = 0; y < surface.height; ++y)
        for (x = 0; x < surface.pitch; ++x)
            assert(pixels[y * surface.pitch + x] == (x < y ? 0x7e : 0xe7));
}

static void packed_inversion(void)
{
    static const unsigned char palette[] = {0, 0, 0, 0};
    const MMPixelRect rect = {0, 0, 1, 1};
    unsigned char bits = 0x37;
    MMPixelImage image;
    image.surface.pixels = &bits;
    image.surface.width = image.surface.height = image.surface.pitch = 1;
    image.surface.depth = 4;
    image.palette = NULL;
    image.colours = 0;
    assert(mm_pixel_invert_packed_rect(&image, &rect, NULL, NULL) && bits == 0x37);
    image.palette = palette;
    image.colours = 1;
    assert(mm_pixel_invert_packed_rect(&image, &rect, NULL, NULL) && bits == 0xc7);
}

static void ellipse_rows(void)
{
    MMEllipseSpan spans[3] = {{19, 21}, {19, 21}, {19, 21}};
    assert(!mm_ellipse_rows(MM_ELLIPSE_MAX_EXTENT + 1, 3, 0, 3, spans));
    assert(spans[0].left == 19 && spans[2].right == 21);
    assert(mm_ellipse_rows(3, 3, -2147483647L - 1, 3, spans));
    assert(!spans[0].right && !spans[1].right && !spans[2].right);
    assert(mm_ellipse_rows(3, 3, 2147483647L, 3, spans));
    assert(!spans[0].right && !spans[1].right && !spans[2].right);
    assert(mm_ellipse_rows(MM_ELLIPSE_MAX_EXTENT, 3, 1, 3, spans));
    /* The previous large-outline path yields these spans for a very thin oval. */
    assert(spans[0].left == 10286 && spans[0].right == 67098578L);
    assert(spans[1].left == 14606011L && spans[1].right == 52502852L && !spans[2].right);
}

static void indexed_double(void)
{
    const MMPixelRect bounds = {0, 0, 5, 4}, request = {-2, -1, 3, 3}, destination = {1, 2, 11, 10};
    const MMPixelRect rect = {0, 0, 2, 1}, clip = {0, 0, 4, 1};
    unsigned char pixels[] = {13, 31}, output[10];
    MMPixelRect cropped, visible;
    MMPixelSurface source, target;
    const MMPixelRect first_band = {0, 0, 4, 2}, second_run = {6, 0, 7, 2};
    MMRegion region = {0};
    mm_region_set_rect(&region, &first_band);
    assert(mm_region_apply_rect(&region, MM_REGION_UNION, &second_run, NULL, 0, NULL) == MM_REGION_UPDATED);
    assert(mm_indexed_double_plan(&bounds, &request, &destination, NULL, &cropped, &visible) == MM_DOUBLE_BOTH);
    assert(cropped.left == 0 && cropped.top == 0 && cropped.right == 3 && cropped.bottom == 3);
    assert(visible.left == 5 && visible.top == 4 && visible.right == 11 && visible.bottom == 10);

    source.pixels = pixels;
    source.width = source.pitch = 2;
    source.height = 1;
    source.depth = 8;
    target.pixels = output;
    target.width = 4;
    target.height = 2;
    target.pitch = 5;
    target.depth = 8;
    memset(output, 0xee, sizeof(output));
    assert(mm_indexed_double_draw(&target, &source, &rect, 0, 0, 1, &clip, &region));
    assert(output[0] == 13 && output[1] == 13 && output[2] == 31 && output[3] == 31);
    assert(!memcmp(output, output + 5, 4) && output[4] == 0xee && output[9] == 0xee);
    memset(output, 0xee, sizeof(output));
    target.height = 1;
    assert(mm_indexed_double_draw(&target, &source, &rect, 0, 0, 1, &clip, &region));
    assert(output[0] == 13 && output[3] == 31 && output[4] == 0xee && output[5] == 0xee && output[8] == 0xee);
    mm_region_set_rect(&region, NULL);
}

typedef struct DamageVisit
{
    MMPixelRect runs[2];
    int used, limit;
} DamageVisit;

static int collect_damage(void *context, const MMPixelRect *rect)
{
    DamageVisit *visit = (DamageVisit *)context;
    if (visit->used >= visit->limit)
        return 0;
    visit->runs[visit->used++] = *rect;
    return 1;
}

static void pixel_storage_and_damage(void)
{
    MMPixelBuffer buffer = {{NULL, 0, 0, 0, 0}, 0};
    MMDamageMap damage = {0}, copy = {0};
    const MMPixelRect first = {1, 0, 3, 2}, last = {63, 0, 67, 2}, area = {0, 0, 67, 3};
    DamageVisit visit;
    MMPixelRect bounds;
    unsigned char *pixels;
    MMI32 pitch;
    size_t bytes;
    assert(mm_pixel_layout(2147483647L, 1, 1, 4, &pitch, &bytes));
    assert(pitch == 268435456L && bytes == (size_t)268435456L);
    assert(!mm_pixel_layout(2147483647L, 8, 1, 4, &pitch, &bytes));
    assert(pitch == 268435456L && bytes == (size_t)268435456L);
    assert(mm_pixel_buffer_init(&buffer, 5, 3, 24, 4));
    pixels = (unsigned char *)buffer.surface.pixels;
    assert(buffer.surface.pitch == 16 && buffer.bytes == 48 && pixels[47] == 0);
    pixels[47] = 0xa7;
    assert(!mm_pixel_buffer_init(&buffer, 5, 3, 2, 4));
    assert(buffer.surface.pixels == pixels && pixels[47] == 0xa7);
    mm_pixel_buffer_reset(&buffer);

    assert(mm_damage_init(&damage, 67, 3));
    mm_damage_set(&damage, &first, 1);
    mm_damage_set(&damage, &last, 1);
    assert(mm_damage_scroll(&damage, &area, 1, 1));
    assert(mm_damage_bounds(&damage, &bounds) && bounds.left == 2 && bounds.top == 1 &&
           bounds.right == 67 && bounds.bottom == 3);
    assert(mm_damage_clone(&copy, &damage));
    visit.used = 0;
    visit.limit = 1;
    assert(!mm_damage_take(&damage, collect_damage, &visit) && visit.used == 1);
    assert(mm_damage_bounds(&damage, &bounds) && bounds.left == 2 && bounds.right == 67);
    visit.used = 0;
    visit.limit = 2;
    assert(mm_damage_take(&damage, collect_damage, &visit) && visit.used == 2);
    assert(visit.runs[0].left == 2 && visit.runs[0].right == 4 && visit.runs[0].top == 1 && visit.runs[0].bottom == 3);
    assert(visit.runs[1].left == 64 && visit.runs[1].right == 67 && visit.runs[1].top == 1 && visit.runs[1].bottom == 3);
    assert(!mm_damage_bounds(&damage, &bounds) && mm_damage_bounds(&copy, &bounds));
    mm_damage_reset(&copy);
    mm_damage_reset(&damage);
}

typedef struct RegionVisit
{
    int bands, capacity, count;
    MMI32 values[16];
} RegionVisit;

static void collect_region_edge(void *context, MMI32 value)
{
    RegionVisit *visit = (RegionVisit *)context;
    assert(visit->count < 16);
    visit->values[visit->count++] = value;
}

static int start_region_band(void *context, MMI32 height, MMI32 max_edges)
{
    RegionVisit *visit = (RegionVisit *)context;
    if (visit->bands == visit->capacity)
        return 0;
    ++visit->bands;
    assert(max_edges >= 0 && max_edges % 2 == 0);
    collect_region_edge(context, height);
    return 1;
}

static void end_region_band(void *context)
{
    collect_region_edge(context, MM_REGION_END);
}

static void region_bands(void)
{
    const MMRegionWord data[] = {3, 0, 5, 7, 14, MM_REGION_END};
    const MMPixelRect bounds = {-2, 1, 12, 4}, subtract = {3, 2, 8, 5}, clip = {0, 0, 10, 5};
    const MMI32 expected[] = {1, MM_REGION_END, 1, 0, 3, 5, 10, MM_REGION_END,
                              2, 0, 3, 8, 10, MM_REGION_END, 1, MM_REGION_END};
    MMRegionScan first, second;
    RegionVisit visit = {0, 2, 0, {0}};
    MMRegionBandSink sink = {NULL, start_region_band, collect_region_edge, end_region_band};
    sink.context = &visit;
    mm_region_scan_init(&first, &bounds, 0, data, sizeof(data) / sizeof(data[0]), &clip);
    mm_region_scan_init(&second, &subtract, 0, NULL, 0, &clip);
    assert(!mm_region_combine(MM_REGION_DIFFERENCE, &first, &second, 5, &sink));
    assert(visit.count == 8 && !memcmp(visit.values, expected, 8 * sizeof(MMI32)));

    mm_region_scan_init(&first, &bounds, 0, data, sizeof(data) / sizeof(data[0]), &clip);
    mm_region_scan_init(&second, &subtract, 0, NULL, 0, &clip);
    visit.bands = visit.count = 0;
    visit.capacity = 4;
    assert(mm_region_combine(MM_REGION_DIFFERENCE, &first, &second, 5, &sink));
    assert(visit.count == sizeof(expected) / sizeof(expected[0]));
    assert(!memcmp(visit.values, expected, sizeof(expected)));
}

typedef struct RegionAllocation
{
    int blocks, growths, fail_at;
} RegionAllocation;

static void *resize_region(void *context, void *data, MMU32 old_bytes, MMU32 new_bytes, int priority)
{
    RegionAllocation *probe = context;
    void *replacement;
    (void)priority;
    if (new_bytes > old_bytes && ++probe->growths == probe->fail_at)
        return NULL;
    replacement = realloc(data, (size_t)new_bytes);
    if (replacement && !old_bytes)
        ++probe->blocks;
    return replacement;
}

static void release_region(void *context, void *data, MMU32 bytes)
{
    RegionAllocation *probe = context;
    (void)bytes;
    --probe->blocks;
    free(data);
}

static void region_ownership(void)
{
    const MMPixelRect left = {-8, 0, -2, 4}, right = {6, 0, 12, 4};
    MMRegion shape = {0};
    MMRegionCursor held = {0}, current = {0};
    MMRegionBands *original;
    RegionAllocation probe = {0, 0, 0};
    MMStorageAllocator allocator = {NULL, resize_region, release_region};
    DamageVisit clipped;
    int growths;
    allocator.context = &probe;
    mm_region_set_rect(&shape, &left);
    probe.fail_at = 2;
    assert(mm_region_apply_rect(&shape, MM_REGION_UNION, &right, NULL, 0, &allocator) == MM_REGION_FAILED);
    assert(!shape.bands && shape.bounds.left == -8 && shape.bounds.right == -2 && !probe.blocks);
    probe.fail_at = 0;
    assert(mm_region_apply_rect(&shape, MM_REGION_UNION, &right, NULL, 0, &allocator) == MM_REGION_UPDATED);
    original = shape.bands;
    assert(original && probe.blocks == 2);
    mm_region_cursor_open(&held, &shape, &shape.bounds);

    probe.fail_at = probe.growths + 1;
    assert(mm_region_scale(&shape, 1, 2, 1, 2) == MM_REGION_FAILED);
    assert(shape.bands == original && shape.bounds.right == 12 && probe.blocks == 2);
    probe.fail_at = probe.growths + 2;
    assert(mm_region_scale(&shape, 1, 2, 1, 2) == MM_REGION_FAILED);
    assert(shape.bands == original && shape.bounds.bottom == 4 && probe.blocks == 2);

    probe.fail_at = 0;
    assert(mm_region_scale(&shape, 1, 2, 1, 2) == MM_REGION_UPDATED);
    assert(shape.bands != original && shape.bounds.left == -4 &&
           shape.bounds.right == 6 && shape.bounds.bottom == 2 && probe.blocks == 4);
    assert(held.scan.rows == 4 && held.scan.x == 0 && mm_region_scan_fetch(&held.scan) == 6);

    clipped.used = 0;
    clipped.limit = 1;
    assert(!mm_region_visit_rectangles(&shape, &shape.bounds, collect_damage, &clipped));
    assert(clipped.used == 1 && clipped.runs[0].left == -4 && clipped.runs[0].right == -1 &&
           clipped.runs[0].top == 0 && clipped.runs[0].bottom == 2);
    growths = probe.growths;
    probe.fail_at = growths + 1;
    assert(mm_region_scale(&shape, 1, 2, 1, 1) == MM_REGION_UPDATED);
    assert(probe.growths == growths && shape.bounds.left == -2 && shape.bounds.right == 3);
    mm_region_cursor_open(&current, &shape, &shape.bounds);
    assert(current.scan.rows == 2 && current.scan.x == 0 && mm_region_scan_fetch(&current.scan) == 1);
    assert(mm_region_scan_fetch(&current.scan) == 3 && mm_region_scan_fetch(&current.scan) == 5);

    mm_region_set_rect(&shape, NULL);
    assert(probe.blocks == 4 && mm_region_scan_fetch(&held.scan) == 14 && mm_region_scan_fetch(&held.scan) == 20);
    mm_region_cursor_close(&current);
    mm_region_cursor_close(&held);
    assert(!probe.blocks);
}

static void graphics_view(void)
{
    const MMPixelPoint base = {23, -11}, before_source = {-11, 99}, size = {1, -1};
    const MMPixelRect source = {-10, 100, 10, 110}, destination = {5, 20, 35, 25};
    const MMPixelRect visible = {-5, 102, 6, 108}, clip = {-10, 104, 0, 112}, away = {50, 200, 60, 210};
    const MMPixelRect degenerate = {1, 4, 1, 58}, scroll = {5, 6, 10, 11};
    const MMPixelPoint start = {9, 3}, end = {3, 8}, pen = {2, 4};
    const MMPixelPoint expected[] = {{3, 8}, {3, 12}, {5, 12}, {11, 7}, {11, 3}, {9, 3}};
    MMGraphicsView view;
    MMPixelRect rect, first, second;
    MMPixelPoint mapped, polygon[6];
    size_t i;

    mm_graphics_view_init(&view, base);
    assert(!view.has_clip && view.destination.left == -23 && view.destination.top == 11);
    mm_graphics_view_set_source(&view, &source);
    mm_graphics_view_set_destination(&view, &destination, base);
    rect = mm_graphics_view_destination(&view, base);
    assert(rect.left == 5 && rect.top == 20 && rect.right == 35 && rect.bottom == 25);
    mapped = mm_graphics_view_map_point(&view, before_source);
    assert(mapped.x == -19 && mapped.y == 31);
    mapped = mm_graphics_view_scale_size(&view, size);
    assert(mapped.x == 2 && mapped.y == 1);
    rect = mm_graphics_view_map_rect(&view, view.destination, 1);
    assert(rect.left == -10 && rect.top == 100 && rect.right == 10 && rect.bottom == 110);
    mapped = mm_graphics_view_pattern_origin(&view);
    assert(mapped.x == -8 && mapped.y == -69);

    mm_graphics_view_set_visible(&view, &visible);
    assert(view.has_clip && view.clip.left == -11 && view.clip.top == 32 && view.clip.right == 6 &&
           view.clip.bottom == 35);
    mm_graphics_view_clip(&view, &clip);
    assert(view.has_clip && view.clip.left == -11 && view.clip.top == 33 && view.clip.right == -3 &&
           view.clip.bottom == 35);
    mm_graphics_view_clip(&view, NULL);
    assert(view.has_clip && view.clip.top == 32 && view.clip.right == 6);
    mm_graphics_view_intersect_visible(&view, &away);
    assert(view.has_clip && view.clip.left == 0 && view.clip.bottom == 0);
    mm_graphics_view_set_source(&view, &degenerate);
    assert(view.source.right == 2 && view.source.bottom == 5 && view.has_clip);
    mm_graphics_view_set_destination(&view, &degenerate, base);
    assert(!view.has_clip && view.destination.right - view.destination.left == 1 &&
           view.destination.bottom - view.destination.top == 1);

    mm_graphics_scroll_exposed(&scroll, -2, 3, &first, &second);
    assert(first.left == 8 && first.top == 6 && first.right == 10 && first.bottom == 11);
    assert(second.left == 5 && second.top == 6 && second.right == 8 && second.bottom == 9);
    mm_graphics_scroll_exposed(&scroll, 0, 0, &first, &second);
    assert(first.right == 0 && first.bottom == 0 && second.right == 0 && second.bottom == 0);
    mm_graphics_line_polygon(start, end, pen, polygon);
    for (i = 0; i < 6; ++i)
        assert(polygon[i].x == expected[i].x && polygon[i].y == expected[i].y);
}

typedef struct TransitionTrace
{
    MMU32 now;
    char log[80];
    size_t used;
    int fail_prepare, fail_palette, fail_iris;
    MMGraphicsDissolvePixels dissolve_pixels;
    int fail_lock;
} TransitionTrace;

static void transition_event(void *context, char event)
{
    TransitionTrace *trace = context;
    assert(trace->used + 1 < sizeof(trace->log));
    trace->log[trace->used++] = event;
    trace->log[trace->used] = '\0';
}

static MMU32 transition_ticks(void *context)
{
    TransitionTrace *trace = context;
    trace->now += 2;
    return trace->now;
}

static void transition_flush(void *context) { transition_event(context, 'F'); }
static void transition_present(void *context) { transition_event(context, 'V'); }
static int transition_depth(void *context)
{
    (void)context;
    return 32;
}
static int transition_prepare(void *context, MMGraphicsTransitionKind kind)
{
    TransitionTrace *trace = context;
    transition_event(context, 'P');
    assert(kind == MM_GRAPHICS_SLIDE || kind == MM_GRAPHICS_IRIS);
    return !trace->fail_prepare;
}
static int transition_start_palette(void *context, int depth)
{
    TransitionTrace *trace = context;
    assert(depth == 0 || depth == 8);
    transition_event(context, 'B');
    return !trace->fail_palette;
}
static void transition_palette_step(void *context, MMGraphicsPalettePhase phase, MMU32 step, MMU32 total)
{
    assert(step > 0 && step <= total);
    transition_event(context, phase == MM_GRAPHICS_PALETTE_BLEND ? 'A' :
                              phase == MM_GRAPHICS_PALETTE_TO_FILL ? 'T' : 'R');
}
static void transition_set_palette(void *context, int restore)
{
    transition_event(context, restore ? 'O' : 'C');
}
static void transition_draw(void *context, int snap, int fill, const MMPixelRect *src, const MMPixelRect *dst)
{
    assert(src->right - src->left == dst->right - dst->left);
    assert(src->bottom - src->top == dst->bottom - dst->top);
    transition_event(context, snap ? (fill ? 'd' : 'D') : 'I');
}
static void transition_scroll(void *context, MMPixelPoint move)
{
    assert(move.x != 0 || move.y != 0);
    transition_event(context, 'S');
}
static int transition_iris(void *context, int fill, const MMPixelRect *outer, const MMPixelRect *inner)
{
    TransitionTrace *trace = context;
    assert(outer->left <= inner->left && outer->top <= inner->top &&
           outer->right >= inner->right && outer->bottom >= inner->bottom);
    transition_event(context, fill ? 'e' : 'E');
    return !trace->fail_iris;
}

static void graphics_transitions(void)
{
    static const unsigned char old[] = {10, 200, 60, 9, 32, 45, 97, 7}, next[] = {17, 195, 10, 90};
    static const unsigned char fill[] = {5, 10, 15, 88};
    unsigned char palette[8];
    TransitionTrace trace;
    const MMPixelPoint base = {0, 0};
    MMGraphicsView view;
    MMGraphicsTransition t = {0};
    MMGraphicsTransitionServices s = {NULL, transition_ticks, transition_flush, transition_prepare, transition_depth,
                                        transition_start_palette, transition_palette_step, transition_set_palette,
                                        transition_draw, transition_scroll, transition_present, transition_iris,
                                        NULL, NULL, NULL, NULL};
    s.context = &trace;
    memset(palette, 0xa5, sizeof(palette));
    mm_graphics_transition_palette(palette, 2, old, 2, next, 1, NULL, 2, 3);
    assert(palette[0] == 14 && palette[1] == 197 && palette[2] == 27 && palette[3] == 9 && palette[4] == 0xa5);
    mm_graphics_transition_palette(palette, 2, NULL, 0, next, 1, fill, 1, 2);
    assert(palette[0] == 11 && palette[1] == 102 && palette[2] == 13 && palette[3] == 88);
    assert(mm_graphics_transition_duration(0) == 1000 && mm_graphics_transition_duration(30000) == 1000);

    mm_graphics_view_init(&view, base);
    t.source.right = t.destination.right = 6;
    t.source.bottom = t.destination.bottom = 6;
    t.view = &view;
    t.focus.x = t.focus.y = 3;
    t.duration = 6;
    t.kind = MM_GRAPHICS_WIPE;
    t.palette = 1;
    t.directions = 12;
    memset(&trace, 0, sizeof(trace));
    trace.now = 0xfffffffcUL;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "FBAIFAIFAIFC"));

    t.kind = MM_GRAPHICS_SLIDE;
    memset(&trace, 0, sizeof(trace));
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "PBSDAVFSDAVFSDAVFCVF"));
    memset(&trace, 0, sizeof(trace));
    trace.fail_prepare = 1;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "PCIF"));

    t.kind = MM_GRAPHICS_FADE;
    t.fade_steps = 3;
    t.palette = 0;
    memset(&trace, 0, sizeof(trace));
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "BFTTTIFRRRO"));
    memset(&trace, 0, sizeof(trace));
    trace.fail_palette = 1;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "BIF"));

    t.kind = MM_GRAPHICS_IRIS;
    t.palette = 1;
    t.directions = 2;
    memset(&trace, 0, sizeof(trace));
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "FPBAEAEAEC"));
    memset(&trace, 0, sizeof(trace));
    trace.fail_iris = 1;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "FPBAECI"));
}

static int dissolve_lock(void *context, MMGraphicsDissolvePixels *pixels)
{
    TransitionTrace *trace = context;
    transition_event(context, 'L');
    *pixels = trace->dissolve_pixels;
    return !trace->fail_lock;
}

static void dissolve_unlock(void *context) { transition_event(context, 'U'); }
static void dissolve_show(void *context) { transition_event(context, 'H'); }

static MMU32 dissolve_random(void *context, MMU32 limit)
{
    assert(limit == 7);
    transition_event(context, 'N');
    return 3;
}

static void dissolve_draw(void *context, int snapshot, int fill, const MMPixelRect *src, const MMPixelRect *dst)
{
    TransitionTrace *trace = context;
    (void)src;
    if (snapshot)
    {
        assert(fill && dst->left == 0 && dst->top == 0 && dst->right == 1 && dst->bottom == 1);
        *(unsigned char *)trace->dissolve_pixels.destination.pixels = 0xcc;
    }
    transition_event(context, snapshot ? 'd' : 'I');
}

static void graphics_dissolve(void)
{
    static unsigned char source[] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    static const unsigned char copied[] = {16, 17, 18, 19, 21, 22, 23, 0xa5};
    unsigned char destination[8];
    MMGraphicsDissolvePixels pixels = {{NULL, 3, 2, 4, 8}, {source, 5, 3, 5, 8}, {1, 1}};
    TransitionTrace trace;
    MMGraphicsTransition t = {0};
    MMGraphicsTransitionServices s = {NULL, transition_ticks, transition_flush, NULL, transition_depth,
                                      transition_start_palette, transition_palette_step, transition_set_palette,
                                      dissolve_draw, NULL, transition_present, NULL,
                                      dissolve_lock, dissolve_unlock, dissolve_random, dissolve_show};
    pixels.destination.pixels = destination;
    s.context = &trace;
    t.kind = MM_GRAPHICS_DISSOLVE;
    t.duration = 6;
    t.destination.right = 3;
    t.destination.bottom = 2;
    t.palette = t.fill = t.has_source = 1;
    memset(&trace, 0, sizeof(trace));
    memset(destination, 0xa5, sizeof(destination));
    trace.dissolve_pixels = pixels;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "LdFNVFHVFHVFHCNVFHVFHVFHU"));
    assert(!memcmp(destination, copied, sizeof(destination)));

    t.fill = 0;
    memset(&trace, 0, sizeof(trace));
    trace.dissolve_pixels = pixels;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "LBNVFHAVFHAVFHACVFHU"));

    memset(&trace, 0, sizeof(trace));
    trace.fail_lock = 1;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "LUCIF"));

    t.fill = 1;
    t.has_source = 0;
    memset(&trace, 0, sizeof(trace));
    memset(destination, 0xa5, sizeof(destination));
    trace.dissolve_pixels = pixels;
    trace.dissolve_pixels.source.pixels = NULL;
    mm_graphics_transition_run(&t, &s);
    assert(!strcmp(trace.log, "LdFNVFHVFHVFHCU"));
    assert(!memcmp(destination, "\xcc\xcc\xcc\xcc\xcc\xcc\xcc\xa5", sizeof(destination)));
}

typedef struct MovieIndexedTrace
{
    MMGraphicsMovieIndexedOperation steps[3];
    MMGraphicsMovieTransition effect;
    int count, fail_at;
} MovieIndexedTrace;

static int movie_indexed_draw(void *context, MMGraphicsMovieIndexedOperation step,
                              const MMGraphicsMovieTransition *effect)
{
    MovieIndexedTrace *trace = (MovieIndexedTrace *)context;
    assert(trace->count < 3);
    trace->steps[trace->count++] = step;
    trace->effect = *effect;
    return trace->count == trace->fail_at ? -17 : 0;
}

static void movie_transition_indexed(void)
{
    MovieIndexedTrace trace;
    int kind;
    for (kind = MM_GRAPHICS_MOVIE_FADE_BLACK; kind <= MM_GRAPHICS_MOVIE_DISSOLVE; ++kind)
    {
        trace.count = trace.fail_at = 0;
        assert(mm_graphics_movie_transition_indexed(kind, 0, &trace, movie_indexed_draw) == 1);
        assert(trace.count == 1 && trace.steps[0] == MM_MOVIE_TRANSITION_DISSOLVE);
        assert(trace.effect.fill == (kind == MM_GRAPHICS_MOVIE_DISSOLVE ? -1 :
                                      kind == MM_GRAPHICS_MOVIE_FADE_WHITE ? 255 : 0));
        assert(trace.effect.dissolve_phase_ms == (kind == MM_GRAPHICS_MOVIE_DISSOLVE ? 4000 : 2000));
    }
    trace.count = 0;
    assert(!mm_graphics_movie_transition_indexed(-1, 0, &trace, movie_indexed_draw) && trace.count == 0);
    assert(mm_graphics_movie_transition_indexed(MM_GRAPHICS_MOVIE_BLACK, 0, &trace, movie_indexed_draw) == 1);
    assert(trace.count == 1 && trace.steps[0] == MM_MOVIE_TRANSITION_FILL_BLACK);
    trace.count = 0;
    assert(mm_graphics_movie_transition_indexed(MM_GRAPHICS_MOVIE_DISSOLVE, 1, &trace, movie_indexed_draw) == 1);
    assert(trace.count == 3 && trace.steps[0] == MM_MOVIE_TRANSITION_FILL_BLACK &&
           trace.steps[1] == MM_MOVIE_TRANSITION_SET_PALETTE && trace.steps[2] == MM_MOVIE_TRANSITION_COPY);
    trace.count = 0;
    trace.fail_at = 2;
    assert(mm_graphics_movie_transition_indexed(MM_GRAPHICS_MOVIE_CUT, 0, &trace, movie_indexed_draw) == -17);
    assert(trace.count == 2);
}

static void movie_transition_rgba(void)
{
    static unsigned char old[] = {10, 11, 12, 255, 20, 21, 22, 255, 30, 31, 32, 255, 40, 41, 42, 255};
    static const unsigned char expected[] = {10, 11, 12, 255, 60, 60, 60, 60, 30, 31, 32, 255, 40, 41, 42, 255};
    static const unsigned char resized[] = {10, 11, 12, 255, 10, 11, 12, 255, 20, 21, 22, 255, 20, 21, 22, 255};
    unsigned char next[20];
    MMPixelSurface outgoing = {old, 4, 1, 16, 32};
    MMPixelSurface incoming = {NULL, 4, 1, 20, 32};
    size_t i;
    incoming.pixels = next;
    memset(next, 60, sizeof(next));
    assert(mm_graphics_movie_transition_rgba(MM_GRAPHICS_MOVIE_DISSOLVE, 1000, 3, &outgoing, &incoming, NULL));
    assert(!memcmp(next, expected, sizeof(expected)) && next[16] == 60);
    outgoing.width = 2;
    assert(mm_graphics_movie_transition_rgba(MM_GRAPHICS_MOVIE_DISSOLVE, 0, 3, &outgoing, &incoming, NULL));
    assert(!memcmp(next, resized, sizeof(resized)));
    memset(next, 60, sizeof(next));
    assert(mm_graphics_movie_transition_rgba(MM_GRAPHICS_MOVIE_FADE_WHITE, 2000, 3, NULL, &incoming, NULL));
    for (i = 0; i < 16; ++i)
        assert(next[i] == 255);
    assert(next[16] == 60);
    assert(mm_graphics_movie_transition_rgba(MM_GRAPHICS_MOVIE_BLACK, 10, 3, NULL, &incoming, NULL));
    for (i = 0; i < 16; ++i)
        assert(next[i] == (i % 4 == 3 ? 255 : 0));
    memset(next, 60, sizeof(next));
    assert(mm_graphics_movie_transition_rgba(MM_GRAPHICS_MOVIE_DISSOLVE, 4000, 3, NULL, &incoming, NULL));
    assert(next[0] == 60);
}

typedef struct MoviePaletteFixture
{
    const unsigned char *incoming, *authored;
} MoviePaletteFixture;

static int movie_palette_read(const void *context, unsigned char *palette)
{
    const MoviePaletteFixture *data = (const MoviePaletteFixture *)context;
    memcpy(palette, data->incoming, 1024);
    return 1;
}

static void movie_palette_overlay(const void *context, unsigned char *palette)
{
    const MoviePaletteFixture *data = (const MoviePaletteFixture *)context;
    assert(!mm_display_palette_overlay(palette, 256, 255, data->authored, 2));
    assert(mm_display_palette_overlay(palette, 256, 42, data->authored, 2));
}

static MMU32 movie_fixed_ticks(void)
{
    return 3;
}

static void movie_transition_palette(void)
{
    static unsigned char old[] = {10, 20, 30, 255, 10, 20, 30, 255, 1, 2, 3, 128, 10, 20, 30, 255};
    static const unsigned char source[] = {127, 40, 20, 255, 127, 40, 20, 255, 127, 40, 20, 255, 127, 40, 20, 255};
    static const unsigned char expected[] = {69, 30, 25, 255, 89, 45, 50, 255, 1, 2, 3, 128, 69, 30, 25, 255};
    static const unsigned short old_indices[] = {42, 42, MM_PIXEL_NO_PALETTE_INDEX, 42};
    static const unsigned char new_indices[] = {0, 43, 42, 42};
    unsigned char prior[1024] = {0}, next[1024] = {0}, target[1024], rendered[sizeof(source)];
    MMPixelSurface incoming = {NULL, 4, 1, 16, 32};
    const MMPixelSurface outgoing = {old, 4, 1, 16, 32};
    MMGraphicsMovieDissolvePalette palette = {NULL, NULL, NULL, old_indices, new_indices};
    MMGraphicsMovieSnapshot snapshot = {0};
    MoviePaletteFixture fixture;
    MMGraphicsMoviePaletteSource palette_source = {NULL, movie_palette_read, movie_palette_overlay};
    palette.outgoing = prior;
    palette.incoming = next;
    palette.target = target;
    incoming.pixels = rendered;
    memcpy(prior + 4 * 42, "\x1e\x14\x0a\x00", 4);
    memcpy(prior + 4 * 43, "\x50\x32\x32\x00", 4);
    memcpy(next + 4 * 42, "\x14\x28\x7f\x00", 4);
    memcpy(next + 4 * 43, next + 4 * 42, 4);
    memcpy(target, next, sizeof(target));
    target[4 * 42 + 2] = target[4 * 43 + 2] = 128;
    memcpy(rendered, source, sizeof(rendered));
    assert(mm_graphics_movie_transition_rgba(MM_GRAPHICS_MOVIE_DISSOLVE, 2000, 3,
                                            &outgoing, &incoming, &palette));
    /* The same incoming RGB can animate differently at two palette indices.
     * Pixel 0 has a stale hint. Pixel 2 retains its unlisted RGBA colour. */
    assert(!memcmp(rendered, expected, sizeof(expected)));

    fixture.incoming = next;
    fixture.authored = target + 4 * 42;
    palette_source.context = &fixture;
    assert(mm_graphics_movie_snapshot_prepare(&snapshot, MM_GRAPHICS_MOVIE_DISSOLVE, &outgoing, prior,
                                              &palette_source) == 1);
    assert(snapshot.indices && !memcmp(snapshot.indices, old_indices, sizeof(old_indices)));
    assert(!memcmp(snapshot.target, target, sizeof(target)) && !memcmp(snapshot.incoming, next, sizeof(next)));
    assert(!mm_graphics_movie_snapshot_prepare(&snapshot, MM_GRAPHICS_MOVIE_CUT, &outgoing, prior, &palette_source));
    assert(snapshot.indices != NULL);
    memcpy(rendered, source, sizeof(rendered));
    assert(mm_graphics_movie_snapshot_render(&snapshot, MM_GRAPHICS_MOVIE_DISSOLVE, 2000, 3,
                                            &outgoing, &incoming, new_indices));
    assert(!memcmp(rendered, expected, sizeof(expected)));
    fixture.incoming = prior;
    fixture.authored = prior + 4 * 42;
    assert(mm_graphics_movie_snapshot_prepare(&snapshot, MM_GRAPHICS_MOVIE_DISSOLVE, &outgoing, prior,
                                              &palette_source) == 1);
    assert(snapshot.indices == NULL);
    fixture.incoming = next;
    assert(mm_graphics_movie_snapshot_prepare(&snapshot, MM_GRAPHICS_MOVIE_FADE_WHITE, &outgoing, prior,
                                              &palette_source) == 1);
    assert(snapshot.indices == NULL);
    mm_graphics_movie_snapshot_dispose(&snapshot);

    {
        unsigned char stored[sizeof(old) + 4];
        MMPixelSurface authored = {NULL, 4, 1, sizeof(stored), 32};
        MMGraphicsMovieFrames frames;
        MMGraphicsMovieCapture capture = {0};
        MMPolledPlayback clock;
        void *pixels;
        mm_graphics_movie_frames_init(&frames);
        mm_polled_playback_reset(&clock, -1, 0);
        memcpy(stored, old, sizeof(old));
        authored.pixels = stored;
        assert(mm_graphics_movie_frames_present(&frames, &clock, 7, &authored, NULL, 0));
        assert(frames.presented.pitch == sizeof(old) && !memcmp(frames.presented.pixels, old, sizeof(old)));
        pixels = frames.presented.pixels;
        stored[0] = 253;
        authored.pitch = 1;
        assert(!mm_graphics_movie_frames_present(&frames, &clock, 8, &authored, NULL, 0));
        assert(frames.presented_scene == 7 && !memcmp(frames.presented.pixels, old, sizeof(old)));
        mm_graphics_movie_frames_invalidate(&frames);
        mm_graphics_movie_frames_capture(&frames, 7, &capture, &palette_source);
        assert(!capture.surface.pixels && frames.presented.pixels == pixels);

        assert(mm_graphics_movie_frames_present(&frames, &clock, 8, &outgoing, NULL, 0));
        fixture.incoming = prior;
        mm_graphics_movie_frames_capture(&frames, 8, &capture, &palette_source);
        assert(capture.surface.pixels == pixels && capture.has_palette && !frames.presented.pixels &&
               !memcmp(capture.palette, prior, sizeof(prior)));
        mm_graphics_movie_frames_clear(&frames);
        assert(!mm_graphics_movie_frames_begin(&frames, &clock, MM_GRAPHICS_MOVIE_CUT, &capture,
                                               &palette_source, movie_fixed_ticks));
        assert(capture.surface.pixels == pixels && !clock.transition);
        fixture.incoming = next;
        fixture.authored = target + 4 * 42;
        assert(mm_graphics_movie_frames_begin(&frames, &clock, MM_GRAPHICS_MOVIE_DISSOLVE, &capture,
                                              &palette_source, movie_fixed_ticks) == 1);
        assert(clock.transition == MM_GRAPHICS_MOVIE_DISSOLVE && frames.seed == 3 &&
               mm_graphics_movie_frames_has_palette(&frames) && !capture.surface.pixels);
        clock.transition_ms = 2000;
        memcpy(rendered, source, sizeof(rendered));
        assert(mm_graphics_movie_frames_present(&frames, &clock, 9, &incoming, new_indices, sizeof(new_indices)));
        assert(!memcmp(rendered, expected, sizeof(expected)) &&
               !memcmp(frames.presented.pixels, expected, sizeof(expected)));
        mm_graphics_movie_frames_clear(&frames);
        mm_graphics_movie_frames_capture(&frames, 9, &capture, NULL);
        assert(capture.surface.pixels && !capture.has_palette &&
               !memcmp(capture.surface.pixels, expected, sizeof(expected)));
        mm_graphics_movie_capture_dispose(&capture);
        mm_graphics_movie_frames_dispose(&frames);
    }
}

static void dissolve_snapshot(void)
{
    static const unsigned char palette[] = {
        90, 91, 94, 0, 92, 92, 92, 0, 32, 32, 32, 0, 36, 36, 31, 0, 67, 68, 68, 0, 69, 68, 68, 0
    };
    static MMU32 xrgb[] = {0x995e5b5aUL, 0x005c5c5cUL, 0x00202020UL, 0x001f2424UL, 0x00444445UL, 0};
    static const unsigned char expected[] = {1, 1, 2, 3, 4, 0xa5, 0xa5, 0xa5};
    static const unsigned char remapped[] = {
        92, 92, 92, 255, 32, 32, 32, 255, 31, 36, 36, 128, 68, 68, 67, 255, 94, 91, 90, 0, 99, 98, 97, 255
    };
    unsigned char rgba[] = {
        94, 91, 90, 255, 32, 32, 32, 255, 31, 36, 36, 128, 68, 68, 69, 255, 94, 91, 90, 0, 99, 98, 97, 255
    };
    unsigned char dst[8];
    unsigned short indices[6];
    static const unsigned short expected_indices[] = {1, 2, 3, 4, MM_PIXEL_NO_PALETTE_INDEX, MM_PIXEL_NO_PALETTE_INDEX};
    const MMPixelRect full = {0, 0, 6, 1}, valid = {0, 0, 5, 1};
    const MMPixelSurface input = {xrgb, 6, 1, 24, 32};
    MMPixelSurface frame = {NULL, 6, 1, 24, 32};
    MMPixelImage snapshot = {{NULL, 6, 1, 8, 8}, palette, 6};
    snapshot.surface.pixels = dst;
    frame.pixels = rgba;

    memset(dst, 0xa5, sizeof(dst));
    assert(!mm_pixel_snapshot_dissolve(&snapshot, &input, &full, &full));
    assert(!memcmp(dst, "\xa5\xa5\xa5\xa5\xa5\xa5\xa5\xa5", sizeof(dst)));
    assert(mm_pixel_snapshot_dissolve(&snapshot, &input, &valid, &valid));
    assert(!memcmp(dst, expected, sizeof(expected)));
    assert(mm_pixel_remap_dissolve_rgba(&frame, palette, 6, indices));
    assert(!memcmp(rgba, remapped, sizeof(rgba)));
    assert(!memcmp(indices, expected_indices, sizeof(indices)));
}

int main(void)
{
    encode();
    long_runs();
    loading();
    malformed();
    imports();
    stream_blitting();
    scaled_drawing();
    packed_copy();
    dib_and_indexed();
    pixel_scroll();
    pixel_polygon();
    packed_inversion();
    ellipse_rows();
    indexed_double();
    pixel_storage_and_damage();
    region_bands();
    region_ownership();
    graphics_view();
    graphics_transitions();
    graphics_dissolve();
    movie_transition_indexed();
    movie_transition_rgba();
    movie_transition_palette();
    dissolve_snapshot();
    puts("C89 masked bitmap: cropping/RLE, endian normalization, hits, malformed rows, transactional ownership and "
         "source consumption passed.");
    return 0;
}
