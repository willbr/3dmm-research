#include "mm/masked_bitmap.h"
#include "mm/dib_frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    MMMaskedServices s;
    unsigned char input[4096], output[4096], bitmap[24];
    MMI32 size, packed, written;
    int phase, fail, objects, memory, consumed, is_memory, unpacks, reads, writes, diagnostic, published, valid;
    void *result;
    MMI32 estimate;
} Fixture;
static int fail(Fixture *f)
{
    return ++f->phase == f->fail;
}
static void *create(void *p)
{
    Fixture *f = (Fixture *)p;
    void *o;
    if (fail(f))
        return NULL;
    o = calloc(1, sizeof(MMMaskedBitmap));
    assert(o);
    ++f->objects;
    return o;
}
static MMMaskedBitmap *state(void *p, void *o)
{
    (void)p;
    return (MMMaskedBitmap *)o;
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
static void release(void *p, void *o)
{
    Fixture *f = (Fixture *)p;
    mm_masked_dispose((MMMaskedBitmap *)o, &f->s);
    free(o);
    --f->objects;
}
static void diagnostic(void *p, int reason)
{
    ((Fixture *)p)->diagnostic = reason + 1;
}
static int unpack(void *p, void *b)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    ++f->unpacks;
    return !fail(f);
}
static MMI32 size(void *p, void *b, int packed)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    return packed ? f->packed : f->size;
}
static int read(void *p, void *b, void *out, MMI32 n)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    ++f->reads;
    assert(n == f->size);
    if (fail(f))
        return 0;
    memcpy(out, f->input, (size_t)n);
    return 1;
}
static int write(void *p, void *b, const void *data, MMI32 n)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    ++f->writes;
    assert(n == f->size);
    if (fail(f))
        return 0;
    memcpy(f->output, data, (size_t)n);
    f->written = n;
    return 1;
}
static int is_memory(void *p, void *b)
{
    (void)b;
    return ((Fixture *)p)->is_memory;
}
static void consume(void *p, void *b)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    ++f->consumed;
    f->size = 0;
}
static void publish(void *p, int valid, void *o, MMI32 n)
{
    Fixture *f = (Fixture *)p;
    ++f->published;
    f->valid = valid;
    f->result = o;
    f->estimate = n;
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
    out->upside_down = f->is_memory;
    return 1;
}
static void setup(Fixture *f, int big)
{
    memset(f, 0, sizeof(*f));
    f->s.context = f;
    f->s.host_big_endian = big;
    f->s.system = 0x4321;
    f->s.create = create;
    f->s.state = state;
    f->s.release = release;
    f->s.allocate = allocate;
    f->s.free_memory = free_memory;
    f->s.diagnostic = diagnostic;
    f->s.unpack = unpack;
    f->s.size = size;
    f->s.read = read;
    f->s.write = write;
    f->s.is_memory = is_memory;
    f->s.consume = consume;
    f->s.publish_fetch = publish;
    f->s.bitmap_read = bitmap_read;
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
    assert(!f->memory && !f->objects);
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
    unsigned char pixels[20] = {0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0};
    int big, flags, x, y, n;
    for (big = 0; big < 2; ++big)
        for (flags = 0; flags < 4; ++flags)
        {
            setup(&f, big);
            memset(&m, 0, sizeof(m));
            assert(mm_masked_init(&m, pixels, 5, 4, &r, 7, -2, 0, (unsigned)flags, 37, &f.s));
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
            assert(mm_masked_write(&m, &f, &f.s) && f.written == m.bytes &&
                   !memcmp(f.output, m.data, (size_t)m.bytes));
            --f.size;
            assert(!mm_masked_write(&m, &f, &f.s) && f.diagnostic == MM_MASKED_WRONG_SIZE + 1);
            mm_masked_dispose(&m, &f.s);
            balanced(&f);
        }
    for (n = 0; n < 4; ++n)
    {
        void *o;
        setup(&f, 0);
        f.fail = n;
        o = mm_masked_new(pixels, 5, 4, &r, 0, 0, 0, 0, 0, &f.s);
        if (o)
            release(&f, o);
        else
            assert(n == 1 || n == 2);
        balanced(&f);
    }
    setup(&f, 0);
    memset(&m, 0, sizeof(m));
    assert(mm_masked_init(&m, pixels, 5, 4, &r, 0, 0, 0, 0, 0, &f.s));
    {
        void *old = m.data;
        f.fail = f.phase + 1;
        assert(!mm_masked_init(&m, pixels, 5, 4, &r, 0, 0, 0, 2, 0, &f.s) && m.data == old &&
               mm_masked_hit(&m, 0, 1, 1));
    }
    mm_masked_dispose(&m, &f.s);
    balanced(&f);
    setup(&f, 0);
    memset(&m, 0, sizeof(m));
    memset(pixels, 0, 20);
    assert(mm_masked_init(&m, pixels, 5, 4, &r, 7, -2, 0, 0, 0, &f.s) && m.bytes == 28 && !m.row_bytes &&
           f.diagnostic == 1);
    mm_masked_bounds(&m, 0, &bounds);
    assert(bounds.left == -7 && bounds.right == -7 && bounds.top == 2 && bounds.bottom == 2);
    assert(!mm_masked_hit(&m, 0, -7, 2));
    mm_masked_dispose(&m, &f.s);
    assert(!mm_masked_init(&m, pixels, 2147483647, 2, &r, 0, 0, 0, 0, 0, &f.s));
    assert(!mm_masked_init(&m, pixels, 5, 4, &r, (-2147483647 - 1), 0, 0, 0, 0, &f.s));
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
            assert(mm_masked_init(&m, pixels, 700, 2, &r, 0, 0, 0, (unsigned)mask * 2, 37, &f.s));
            assert(mm_masked_validate(&m, big));
            for (y = 0; y < 2; ++y)
                for (x = 0; x < 700; ++x)
                    assert(mm_masked_hit(&m, big, x, y) == (pixels[y * 700 + x] != 0));
            mm_masked_dispose(&m, &f.s);
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
        assert(!mm_masked_init(&m, wide, 40000, 1, &r, 0, 0, 0, 0, 0, &f.s) && !m.data);
        free(wide);
        balanced(&f);
    }
}
static void loading(void)
{
    Fixture f;
    MMMaskedBitmap *m;
    int big, foreign, memory, factory, n, ok;
    for (big = 0; big < 2; ++big)
        for (foreign = 0; foreign < 2; ++foreign)
            for (memory = 0; memory < 2; ++memory)
                for (factory = 0; factory < 2; ++factory)
                    for (n = 0; n < 8; ++n)
                    {
                        setup(&f, big);
                        seed(&f, big ^ foreign);
                        f.is_memory = memory;
                        f.fail = n;
                        if (factory)
                        {
                            ok = mm_masked_fetch(&f, 1, &f.s);
                            m = (MMMaskedBitmap *)f.result;
                            assert(f.published == 1 && f.valid == ok);
                        }
                        else
                        {
                            m = (MMMaskedBitmap *)mm_masked_read(&f, &f.s);
                            ok = m != NULL;
                        }
                        if (ok)
                        {
                            assert(mm_masked_hit(m, big, -1, 3) && !mm_masked_hit(m, big, -2, 3) &&
                                   m->row_bytes == 4);
                            assert(f.consumed == memory);
                            if (factory)
                                assert(f.estimate == 40 && f.unpacks == 2);
                            release(&f, m);
                        }
                        balanced(&f);
                    }
    setup(&f, 0);
    seed(&f, 0);
    assert(mm_masked_fetch(&f, 0, &f.s) && f.estimate == 17 && !f.reads && !f.unpacks);
    balanced(&f);
    for (n = 0; n < 40; ++n)
    {
        setup(&f, 0);
        seed(&f, 0);
        f.size = n;
        assert(!mm_masked_read(&f, &f.s));
        balanced(&f);
    }
    setup(&f, 0);
    seed(&f, 0);
    f.is_memory = 1;
    f.input[6] = 1;
    assert(!mm_masked_read(&f, &f.s) && f.consumed == 1);
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
    MMMaskedBitmap *m;
    int up, flags, n;
    for (up = 0; up < 2; ++up)
        for (flags = 0; flags < 4; ++flags)
            for (n = 0; n < 7; ++n)
            {
                setup(&f, 0);
                f.is_memory = up;
                f.bitmap[0] = 9;
                f.bitmap[21] = 7;
                f.fail = n;
                m = (MMMaskedBitmap *)mm_masked_import(&f, 0, 0, 0, (unsigned)flags, 37, &f.s);
                if (m)
                {
                    int flip = up ? (flags & 1) : !(flags & 1);
                    assert(mm_masked_hit(m, 0, 0, flip ? 5 : 0));
                    release(&f, m);
                }
                balanced(&f);
            }
}
typedef struct Scan
{
    MMI32 row, span;
} Scan;
static void start_scan(void *context, const MMMaskedRect *bounds)
{
    Scan *scan = (Scan *)context;
    assert(bounds->left == 0 && bounds->top == 0 && bounds->right == 4 && bounds->bottom == 2);
    scan->row = scan->span = 0;
}
static int scan_span(void *context, MMI32 *left, MMI32 *right)
{
    Scan *scan = (Scan *)context;
    if (scan->row == 0 && scan->span < 2)
    {
        *left = scan->span++ * 2;
        *right = *left + 1;
        return 1;
    }
    if (scan->row == 1 && !scan->span)
    {
        ++scan->span;
        *left = 1;
        *right = 4;
        return 1;
    }
    return 0;
}
static void next_row(void *context, MMI32 rows)
{
    Scan *scan = (Scan *)context;
    scan->row += rows;
    scan->span = 0;
}

static void stream_blitting(void)
{
    Fixture f;
    MMMaskedBitmap m;
    MMMemoryStream stream;
    const MMMaskedRect clip = {1, 1, 2, 3};
    unsigned char pixels[12], expected[12], bits[2];
    Scan scan;
    MMMaskedSpans spans = {NULL, start_scan, scan_span, next_row, NULL};
    int big, source, mask;
    spans.context = &scan;
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
                assert(mm_masked_read_stream(&stream.stream, &m, &f.s));
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
                mm_masked_blit8(&m, big, pixels, 4, 3, 0, 0, NULL, &spans);
                assert(scan.row == 1 && !memcmp(pixels, expected, sizeof(pixels)));
                memset(bits, 0xa5, sizeof(bits));
                mm_masked_blit_mask(&m, big, bits, 1, 2, 4, 0, NULL);
                assert(bits[0] == 0xa6 && bits[1] == 0xac);
                mm_masked_dispose(&m, &f.s);
                assert(!mm_masked_read_stream(&stream.stream, &m, &f.s) && !m.data);
                balanced(&f);
            }
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
    puts("C89 masked bitmap: cropping/RLE, endian normalization, hits, malformed rows, transactional ownership and "
         "source consumption passed.");
    return 0;
}
