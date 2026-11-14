#include "three_mm/depth_bitmap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    ThreeMMDepthServices s;
    unsigned char input[128], output[128];
    ThreeMMI32 size, packed, output_size;
    int phase, fail, objects, memory, warnings, published, valid, unpacks, reads, writes, adds, scans, pair, rows,
        region_empty, scan_mode;
    void *result;
    ThreeMMI32 estimate;
    ThreeMMDepthRect bounds, region;
} Fixture;
static int fail(Fixture *f)
{
    return ++f->phase == f->fail;
}
static void *create(void *p)
{
    Fixture *f = (Fixture *)p;
    void *v;
    if (fail(f))
        return NULL;
    v = calloc(1, sizeof(ThreeMMDepthBitmap));
    assert(v);
    ++f->objects;
    return v;
}
static ThreeMMDepthBitmap *state(void *p, void *o)
{
    (void)p;
    return (ThreeMMDepthBitmap *)o;
}
static int allocate(void *p, ThreeMMI32 n, void **out)
{
    Fixture *f = (Fixture *)p;
    if (fail(f))
        return 0;
    assert(n >= 0 && n <= 128);
    *out = malloc(n ? (size_t)n : 1);
    assert(*out);
    ++f->memory;
    return 1;
}
static void free_memory(void *p, void **out)
{
    if (*out)
    {
        --((Fixture *)p)->memory;
        free(*out);
        *out = NULL;
    }
}
static void release(void *p, void *o)
{
    Fixture *f = (Fixture *)p;
    three_mm_depth_dispose(state(p, o), &f->s);
    free(o);
    --f->objects;
}
static void warning(void *p)
{
    ++((Fixture *)p)->warnings;
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
static int read(void *p, void *b, void *out, ThreeMMI32 n, ThreeMMI32 at)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    ++f->reads;
    assert(at >= 0 && n >= 0 && at <= f->size && n <= f->size - at);
    if (fail(f))
        return 0;
    if (n)
        memcpy(out, f->input + at, (size_t)n);
    return 1;
}
static int write(void *p, void *b, const void *in, ThreeMMI32 n, ThreeMMI32 at)
{
    Fixture *f = (Fixture *)p;
    (void)b;
    ++f->writes;
    assert(at >= 0 && n >= 0 && at + n <= f->output_size);
    if (fail(f))
        return 0;
    if (n)
        memcpy(f->output + at, in, (size_t)n);
    return 1;
}
static void *add(void *p, void *file, ThreeMMU32 type, ThreeMMI32 n, ThreeMMU32 *number)
{
    Fixture *f = (Fixture *)p;
    (void)file;
    assert(type == 0x12345678UL);
    ++f->adds;
    if (fail(f))
        return NULL;
    assert(n <= 128);
    f->output_size = n;
    *number = 99;
    memset(f->output, 0xcc, 128);
    return p;
}
static void publish(void *p, int valid, void *o, ThreeMMI32 n)
{
    Fixture *f = (Fixture *)p;
    ++f->published;
    f->valid = valid;
    f->result = o;
    f->estimate = n;
}
static int region_bounds(void *p, void *r, ThreeMMDepthRect *out)
{
    Fixture *f = (Fixture *)p;
    (void)r;
    *out = f->region;
    return !f->region_empty;
}
static void scan_init(void *p, void *region, const ThreeMMDepthRect *clip, const ThreeMMDepthRect *bounds)
{
    Fixture *f = (Fixture *)p;
    (void)region;
    (void)clip;
    f->bounds = *bounds;
    f->pair = 0;
    ++f->scans;
}
static int scan_pair(void *p, ThreeMMI32 *left, ThreeMMI32 *right)
{
    Fixture *f = (Fixture *)p;
    if (f->pair++)
        return 0;
    *left = 0;
    *right = f->bounds.right - f->bounds.left;
    if (f->scan_mode == 1)
        *left = -1;
    if (f->scan_mode == 2)
        ++*right;
    return 1;
}
static void scan_next(void *p, ThreeMMI32 rows)
{
    Fixture *f = (Fixture *)p;
    f->rows += rows;
    f->pair = 0;
}
static void put(unsigned char *p, ThreeMMU32 n, int big)
{
    p[big ? 1 : 0] = (unsigned char)n;
    p[big ? 0 : 1] = (unsigned char)(n >> 8);
}
static void setup(Fixture *f, int big)
{
    int i;
    memset(f, 0, sizeof(*f));
    f->s.context = f;
    f->s.host_big_endian = big;
    f->s.system = 0x4321;
    f->s.object_bytes = 100;
    f->s.create = create;
    f->s.state = state;
    f->s.release = release;
    f->s.allocate = allocate;
    f->s.free_memory = free_memory;
    f->s.warning = warning;
    f->s.unpack = unpack;
    f->s.size = size;
    f->s.read = read;
    f->s.write = write;
    f->s.add = add;
    f->s.publish_fetch = publish;
    f->s.region_bounds = region_bounds;
    f->s.scan_init = scan_init;
    f->s.scan_pair = scan_pair;
    f->s.scan_next = scan_next;
    f->size = 24;
    f->packed = 9;
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
}
static void constructors(void)
{
    Fixture f;
    ThreeMMDepthBitmap *m;
    unsigned char pixels[12];
    int n, i;
    memset(pixels, 0x35, 12);
    for (n = 0; n < 4; ++n)
    {
        setup(&f, 0);
        f.fail = n;
        m = (ThreeMMDepthBitmap *)three_mm_depth_new(3, 2, pixels, &f.s);
        if (m)
        {
            assert(m->bytes == 12 && m->row_bytes == 6 && !memcmp(m->pixels, pixels, 12));
            release(&f, m);
        }
        else
            assert(n == 1 || n == 2);
        balanced(&f);
    }
    setup(&f, 0);
    m = (ThreeMMDepthBitmap *)three_mm_depth_new(3, 2, NULL, &f.s);
    assert(m);
    for (i = 0; i < 12; ++i)
        assert(((unsigned char *)m->pixels)[i] == 255);
    release(&f, m);
    balanced(&f);
    assert(!three_mm_depth_new(0, 2, NULL, &f.s));
    assert(!three_mm_depth_new(-1, 2, NULL, &f.s));
    assert(!three_mm_depth_new(2147483647, 2, NULL, &f.s));
    assert(!three_mm_depth_new(0x40000000, 2, NULL, &f.s));
    assert(!three_mm_depth_new(32768, 32768, NULL, &f.s));
    balanced(&f);
}
static void loading(void)
{
    Fixture f;
    ThreeMMDepthBitmap *m;
    int big, factory, n, i, ok;
    for (big = 0; big < 2; ++big)
        for (factory = 0; factory < 2; ++factory)
            for (n = 0; n < 8; ++n)
            {
                setup(&f, big);
                f.fail = n;
                if (factory)
                {
                    ok = three_mm_depth_fetch(&f, 1, &f.s);
                    m = (ThreeMMDepthBitmap *)f.result;
                    assert(f.published == 1 && f.valid == ok);
                }
                else
                {
                    m = (ThreeMMDepthBitmap *)three_mm_depth_read(&f, &f.s);
                    ok = m != NULL;
                }
                if (ok)
                {
                    assert(m && m->bounds.left == 2 && m->bounds.top == -1 && m->bounds.right == 5 &&
                           m->bounds.bottom == 1 && m->bytes == 12 && !memcmp(m->pixels, f.input + 12, 12));
                    if (factory)
                        assert(f.estimate == 112 && f.unpacks == 2);
                    release(&f, m);
                }
                else
                {
                    assert(!m);
                    if (n == 1)
                        assert(!f.warnings);
                }
                balanced(&f);
            }
    setup(&f, 0);
    assert(three_mm_depth_fetch(&f, 0, &f.s) && f.estimate == 9 && !f.result && !f.unpacks && !f.reads);
    balanced(&f);
    for (i = 0; i < 24; ++i)
    {
        setup(&f, 0);
        f.size = i;
        assert(!three_mm_depth_read(&f, &f.s));
        balanced(&f);
    }
    setup(&f, 0);
    f.size = 25;
    assert(!three_mm_depth_read(&f, &f.s));
    balanced(&f);
    setup(&f, 0);
    put(f.input + 8, 0, 0);
    f.size = 12;
    m = (ThreeMMDepthBitmap *)three_mm_depth_read(&f, &f.s);
    assert(m && !m->bytes && m->bounds.right == 2);
    release(&f, m);
    balanced(&f);
    setup(&f, 0);
    put(f.input + 8, 0xffff, 0);
    assert(!three_mm_depth_read(&f, &f.s));
    balanced(&f);
    setup(&f, 0);
    f.s.object_bytes = 2147483647;
    assert(!three_mm_depth_fetch(&f, 1, &f.s) && !f.valid);
    balanced(&f);
}
static void writing(void)
{
    Fixture f;
    ThreeMMDepthBitmap m;
    unsigned char pixels[12];
    ThreeMMU32 number;
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
            ok = three_mm_depth_write(&m, &f, 0x12345678UL, &number, &f.s);
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
    Fixture f;
    ThreeMMDepthBitmap m;
    unsigned char source[64], dest[100];
    ThreeMMDepthRect clip;
    int i, half, n;
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
        setup(&f, 0);
        memset(dest, 0xcc, 100);
        clip.left = 1;
        clip.top = 1;
        clip.right = 4;
        clip.bottom = 7;
        assert(three_mm_depth_draw(&m, dest + 4, 10, 8, 1, 1, &clip, NULL, half, &f.s));
        for (i = 0; i < 80 / (half ? 2 : 1); ++i)
        {
            int y = i / 10, x = i % 10;
            int copied = y >= (half ? 0 : 1) && y < (half ? 3 : 7) && x >= 2 && x < 8;
            assert(dest[4 + i] == (copied ? source[y * 8 + x] : 0xcc));
        }
        assert(f.rows == 6);
        for (i = 0; i < 4; ++i)
            assert(dest[i] == 0xcc && dest[96 + i] == 0xcc);
        balanced(&f);
    }
    setup(&f, 0);
    memset(dest, 0xcc, 100);
    assert(three_mm_depth_draw(&m, dest, 10, 8, 0, 0, NULL, NULL, 0, &f.s) && !f.scans);
    f.region_empty = 1;
    assert(three_mm_depth_draw(&m, dest, 10, 8, 0, 0, NULL, &f, 0, &f.s) && !f.scans);
    clip.left = 0;
    clip.top = 0;
    clip.right = 4;
    clip.bottom = 8;
    for (n = 1; n <= 2; ++n)
    {
        setup(&f, 0);
        f.scan_mode = n;
        assert(!three_mm_depth_draw(&m, dest, 10, 8, 0, 0, &clip, NULL, 0, &f.s));
    }
    setup(&f, 0);
    assert(!three_mm_depth_draw(&m, dest, 10, 8, 2147483647, 0, &clip, NULL, 0, &f.s));
    assert(!three_mm_depth_draw(&m, dest, 2147483647, 8, 0, 0, &clip, NULL, 0, &f.s));
    clip.top = 7;
    assert(!three_mm_depth_draw(&m, dest, 10, 7, 0, 0, &clip, NULL, 1, &f.s));
    for (i = 0; i < 100; ++i)
        assert(dest[i] == 0xcc);
    setup(&f, 0);
    f.region.left = 1;
    f.region.top = 2;
    f.region.right = 3;
    f.region.bottom = 4;
    clip.top = 0;
    assert(three_mm_depth_draw(&m, dest, 10, 8, 0, 0, &clip, &f, 0, &f.s) && f.rows == 2);
    for (i = 0; i < 80; ++i)
    {
        int y = i / 10, x = i % 10;
        assert(dest[i] == (y >= 2 && y < 4 && x >= 2 && x < 6 ? source[y * 8 + x] : 0xcc));
    }
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
