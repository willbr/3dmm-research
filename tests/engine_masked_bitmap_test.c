#include "three_mm/masked_bitmap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    ThreeMMMaskedServices s;
    unsigned char input[4096], output[4096], bitmap[24];
    ThreeMMI32 size, packed, written;
    int phase, fail, objects, memory, consumed, is_memory, unpacks, reads, writes, diagnostic, published, valid;
    void *result;
    ThreeMMI32 estimate;
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
    o = calloc(1, sizeof(ThreeMMMaskedBitmap));
    assert(o);
    ++f->objects;
    return o;
}
static ThreeMMMaskedBitmap *state(void *p, void *o)
{
    (void)p;
    return (ThreeMMMaskedBitmap *)o;
}
static int allocate(void *p, ThreeMMI32 n, void **out)
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
    three_mm_masked_dispose((ThreeMMMaskedBitmap *)o, &f->s);
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
static ThreeMMI32 size(void *p, void *b, int packed)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    return packed ? f->packed : f->size;
}
static int read(void *p, void *b, void *out, ThreeMMI32 n)
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
static int write(void *p, void *b, const void *data, ThreeMMI32 n)
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
static void publish(void *p, int valid, void *o, ThreeMMI32 n)
{
    Fixture *f = (Fixture *)p;
    ++f->published;
    f->valid = valid;
    f->result = o;
    f->estimate = n;
}
static int bitmap_read(void *p, void *name, unsigned char transparent, ThreeMMMaskedSource *out)
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
static void put(unsigned char *b, unsigned n, ThreeMMU32 v, int big)
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
    ThreeMMMaskedBitmap m;
    ThreeMMMaskedRect r = {0, 0, 5, 4}, bounds;
    unsigned char pixels[20] = {0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0};
    int big, flags, x, y, n;
    for (big = 0; big < 2; ++big)
        for (flags = 0; flags < 4; ++flags)
        {
            setup(&f, big);
            memset(&m, 0, sizeof(m));
            assert(three_mm_masked_init(&m, pixels, 5, 4, &r, 7, -2, 0, (unsigned)flags, 37, &f.s));
            assert(three_mm_masked_validate(&m, big));
            three_mm_masked_bounds(&m, big, &bounds);
            assert(bounds.left == -6 && bounds.right == -3);
            for (y = -1; y < 5; ++y)
                for (x = -1; x < 6; ++x)
                {
                    int expected = x >= 0 && x < 5 && y >= 0 && y < 4 && pixels[(flags & 1 ? 3 - y : y) * 5 + x] != 0;
                    assert(three_mm_masked_hit(&m, big, x - 7, y + 2) == expected);
                }
            f.size = m.bytes;
            assert(three_mm_masked_write(&m, &f, &f.s) && f.written == m.bytes &&
                   !memcmp(f.output, m.data, (size_t)m.bytes));
            --f.size;
            assert(!three_mm_masked_write(&m, &f, &f.s) && f.diagnostic == THREE_MM_MASKED_WRONG_SIZE + 1);
            three_mm_masked_dispose(&m, &f.s);
            balanced(&f);
        }
    for (n = 0; n < 4; ++n)
    {
        void *o;
        setup(&f, 0);
        f.fail = n;
        o = three_mm_masked_new(pixels, 5, 4, &r, 0, 0, 0, 0, 0, &f.s);
        if (o)
            release(&f, o);
        else
            assert(n == 1 || n == 2);
        balanced(&f);
    }
    setup(&f, 0);
    memset(&m, 0, sizeof(m));
    assert(three_mm_masked_init(&m, pixels, 5, 4, &r, 0, 0, 0, 0, 0, &f.s));
    {
        void *old = m.data;
        f.fail = f.phase + 1;
        assert(!three_mm_masked_init(&m, pixels, 5, 4, &r, 0, 0, 0, 2, 0, &f.s) && m.data == old &&
               three_mm_masked_hit(&m, 0, 1, 1));
    }
    three_mm_masked_dispose(&m, &f.s);
    balanced(&f);
    setup(&f, 0);
    memset(&m, 0, sizeof(m));
    memset(pixels, 0, 20);
    assert(three_mm_masked_init(&m, pixels, 5, 4, &r, 7, -2, 0, 0, 0, &f.s) && m.bytes == 28 && !m.row_bytes &&
           f.diagnostic == 1);
    three_mm_masked_bounds(&m, 0, &bounds);
    assert(bounds.left == -7 && bounds.right == -7 && bounds.top == 2 && bounds.bottom == 2);
    assert(!three_mm_masked_hit(&m, 0, -7, 2));
    three_mm_masked_dispose(&m, &f.s);
    assert(!three_mm_masked_init(&m, pixels, 2147483647, 2, &r, 0, 0, 0, 0, 0, &f.s));
    assert(!three_mm_masked_init(&m, pixels, 5, 4, &r, (-2147483647 - 1), 0, 0, 0, 0, &f.s));
    balanced(&f);
}
static void long_runs(void)
{
    Fixture f;
    ThreeMMMaskedBitmap m;
    ThreeMMMaskedRect r = {0, 0, 700, 2};
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
            assert(three_mm_masked_init(&m, pixels, 700, 2, &r, 0, 0, 0, (unsigned)mask * 2, 37, &f.s));
            assert(three_mm_masked_validate(&m, big));
            for (y = 0; y < 2; ++y)
                for (x = 0; x < 700; ++x)
                    assert(three_mm_masked_hit(&m, big, x, y) == (pixels[y * 700 + x] != 0));
            three_mm_masked_dispose(&m, &f.s);
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
        assert(!three_mm_masked_init(&m, wide, 40000, 1, &r, 0, 0, 0, 0, 0, &f.s) && !m.data);
        free(wide);
        balanced(&f);
    }
}
static void loading(void)
{
    Fixture f;
    ThreeMMMaskedBitmap *m;
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
                            ok = three_mm_masked_fetch(&f, 1, &f.s);
                            m = (ThreeMMMaskedBitmap *)f.result;
                            assert(f.published == 1 && f.valid == ok);
                        }
                        else
                        {
                            m = (ThreeMMMaskedBitmap *)three_mm_masked_read(&f, &f.s);
                            ok = m != NULL;
                        }
                        if (ok)
                        {
                            assert(three_mm_masked_hit(m, big, -1, 3) && !three_mm_masked_hit(m, big, -2, 3) &&
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
    assert(three_mm_masked_fetch(&f, 0, &f.s) && f.estimate == 17 && !f.reads && !f.unpacks);
    balanced(&f);
    for (n = 0; n < 40; ++n)
    {
        setup(&f, 0);
        seed(&f, 0);
        f.size = n;
        assert(!three_mm_masked_read(&f, &f.s));
        balanced(&f);
    }
    setup(&f, 0);
    seed(&f, 0);
    f.is_memory = 1;
    f.input[6] = 1;
    assert(!three_mm_masked_read(&f, &f.s) && f.consumed == 1);
    balanced(&f);
}
static void malformed(void)
{
    Fixture f;
    ThreeMMMaskedBitmap m;
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
            assert(!three_mm_masked_validate(&m, big));
            balanced(&f);
        }
    setup(&f, 0);
    seed(&f, 0);
    put(f.input + 20, 4, 3, 0);
    put(f.input + 24, 4, 28, 0);
    m.data = f.input;
    m.bytes = 28;
    assert(three_mm_masked_validate(&m, 0) && !m.row_bytes);
    {
        ThreeMMMaskedRect r;
        three_mm_masked_bounds(&m, 0, &r);
        assert(r.left == r.right && r.top == r.bottom);
    }
}
static void imports(void)
{
    Fixture f;
    ThreeMMMaskedBitmap *m;
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
                m = (ThreeMMMaskedBitmap *)three_mm_masked_import(&f, 0, 0, 0, (unsigned)flags, 37, &f.s);
                if (m)
                {
                    int flip = up ? (flags & 1) : !(flags & 1);
                    assert(three_mm_masked_hit(m, 0, 0, flip ? 5 : 0));
                    release(&f, m);
                }
                balanced(&f);
            }
}
int main(void)
{
    encode();
    long_runs();
    loading();
    malformed();
    imports();
    puts("C89 masked bitmap: cropping/RLE, endian normalization, hits, malformed rows, transactional ownership and "
         "source consumption passed.");
    return 0;
}
