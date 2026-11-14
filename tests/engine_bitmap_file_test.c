#include "three_mm/bitmap_file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Palette
{
    int kind;
    unsigned char bytes[1024];
} Palette;
typedef struct Test
{
    ThreeMMBitmapServices s;
    unsigned char input[1200], output[1200];
    ThreeMMI32 bytes, written;
    void *pixels, *palette;
    int phase, fail, files, memory, palettes, locks, opened, created, closed, writes, warning, bad, temporary,
        dimensions, valid;
    ThreeMMI32 width, height;
    int upside;
} Test;
static Test *test(void *p)
{
    return (Test *)p;
}
static int fail(Test *t)
{
    return ++t->phase == t->fail;
}
static void put(unsigned char *p, ThreeMMU32 value, unsigned n)
{
    while (n--)
    {
        *p++ = (unsigned char)value;
        value >>= 8;
    }
}
static void *open(void *p, void *filename)
{
    Test *t = test(p);
    (void)filename;
    ++t->opened;
    if (fail(t))
        return NULL;
    ++t->files;
    return p;
}
static void *create(void *p, void *filename)
{
    ++test(p)->created;
    return open(p, filename);
}
static void release(void *p, void *o)
{
    Test *t = test(p);
    if (o == p)
    {
        --t->files;
        ++t->closed;
    }
    else
    {
        --t->palettes;
        free(o);
    }
}
static ThreeMMI32 size(void *p, void *f)
{
    (void)f;
    return test(p)->bytes;
}
static int read(void *p, void *f, void *out, ThreeMMI32 n, ThreeMMI32 at)
{
    Test *t = test(p);
    (void)f;
    assert(at >= 0 && n >= 0 && at <= t->bytes && n <= t->bytes - at);
    if (fail(t))
        return 0;
    if (n)
        memcpy(out, t->input + at, (size_t)n);
    return 1;
}
static int write(void *p, void *f, const void *data, ThreeMMI32 n, ThreeMMI32 at)
{
    Test *t = test(p);
    (void)f;
    ++t->writes;
    assert(at >= 0 && n >= 0 && at + n <= 1200);
    if (fail(t))
        return 0;
    if (n)
        memcpy(t->output + at, data, (size_t)n);
    if (at + n > t->written)
        t->written = at + n;
    return 1;
}
static int allocate(void *p, ThreeMMI32 n, void **out)
{
    Test *t = test(p);
    if (fail(t))
        return 0;
    *out = malloc(n ? (size_t)n : 1);
    assert(*out);
    ++t->memory;
    return 1;
}
static void free_memory(void *p, void **v)
{
    if (*v)
    {
        --test(p)->memory;
        free(*v);
        *v = NULL;
    }
}
static void *palette_create(void *p)
{
    Test *t = test(p);
    Palette *v;
    if (fail(t))
        return NULL;
    v = (Palette *)malloc(sizeof(*v));
    assert(v);
    v->kind = 1;
    ++t->palettes;
    return v;
}
static void *palette_lock(void *p, void *v)
{
    ++test(p)->locks;
    return ((Palette *)v)->bytes;
}
static void palette_unlock(void *p, void *v)
{
    (void)v;
    --test(p)->locks;
}
static void publish(void *p, int kind, void *v)
{
    if (kind == THREE_MM_BITMAP_PIXELS)
        test(p)->pixels = v;
    else
        test(p)->palette = v;
}
static void dimensions(void *p, int valid, ThreeMMI32 w, ThreeMMI32 h, int up)
{
    Test *t = test(p);
    ++t->dimensions;
    t->valid = valid;
    t->width = w;
    t->height = h;
    t->upside = up;
}
static void warning(void *p, int reason)
{
    test(p)->warning = reason + 1;
}
static void bad(void *p)
{
    ++test(p)->bad;
}
static void temporary(void *p, void *f)
{
    (void)f;
    ++test(p)->temporary;
}
static void setup(Test *t, int rle)
{
    int i;
    memset(t, 0, sizeof(*t));
    t->bytes = 1090;
    t->width = 123;
    t->height = 456;
    t->s.context = t;
    t->s.open = open;
    t->s.create = create;
    t->s.release = release;
    t->s.size = size;
    t->s.read = read;
    t->s.write = write;
    t->s.allocate = allocate;
    t->s.free_memory = free_memory;
    t->s.palette_create = palette_create;
    t->s.palette_lock = palette_lock;
    t->s.palette_unlock = palette_unlock;
    t->s.publish = publish;
    t->s.dimensions = dimensions;
    t->s.warning = warning;
    t->s.bad_bitmap = bad;
    t->s.make_temporary = temporary;
    put(t->input, 0x4d42, 2);
    put(t->input + 2, t->bytes, 4);
    put(t->input + 10, 1078, 4);
    put(t->input + 14, 40, 4);
    put(t->input + 18, 3, 4);
    put(t->input + 22, 3, 4);
    put(t->input + 26, 1, 2);
    put(t->input + 28, 8, 2);
    put(t->input + 46, 256, 4);
    for (i = 54; i < 1090; ++i)
        t->input[i] = (unsigned char)(i * 17);
    if (rle)
    {
        t->bytes = 1080;
        put(t->input + 2, t->bytes, 4);
        put(t->input + 30, 1, 4);
        put(t->input + 34, 2, 4);
        t->input[1078] = 0;
        t->input[1079] = 1;
    }
}
static void balanced(Test *t)
{
    assert(!t->files && !t->memory && !t->palettes && !t->locks);
}
static void cleanup(Test *t)
{
    free_memory(t, &t->pixels);
    if (t->palette)
    {
        release(t, t->palette);
        t->palette = NULL;
    }
    balanced(t);
}
static void read_cases(void)
{
    Test t;
    ThreeMMBitmapHeader h;
    int reason, n, rle, ok;
    for (rle = 0; rle < 2; ++rle)
        for (n = 0; n < 10; ++n)
        {
            setup(&t, rle);
            t.fail = n;
            ok = three_mm_bitmap_read(&t, 1, 1, 73, &t.s);
            if (ok)
            {
                assert(t.pixels && t.palette && t.valid && t.width == 3 && t.height == 3 && !t.upside);
                if (rle)
                {
                    int i;
                    for (i = 0; i < 12; ++i)
                        assert(((unsigned char *)t.pixels)[i] == 73);
                }
                assert(!memcmp(((Palette *)t.palette)->bytes, t.input + 54, 1024));
                cleanup(&t);
            }
            else
            {
                assert(!t.pixels && !t.palette);
                if (n == 1)
                    assert(!t.dimensions && !t.bad && t.width == 123);
                else
                    assert(t.dimensions == 1 && !t.valid && t.bad == 1);
                balanced(&t);
            }
        }
    setup(&t, 0);
    put(t.input + 46, 17, 4);
    assert(three_mm_bitmap_read(&t, 0, 0, 0, &t.s) && t.valid);
    balanced(&t);
    setup(&t, 0);
    put(t.input + 46, 17, 4);
    assert(!three_mm_bitmap_read(&t, 0, 1, 0, &t.s) && t.warning == THREE_MM_BITMAP_COLOURS + 1);
    balanced(&t);
    setup(&t, 0);
    put(t.input + 22, 0xfffffffdUL, 4);
    assert(three_mm_bitmap_read(&t, 1, 0, 0, &t.s) && t.height == 3 && t.upside);
    cleanup(&t);
    for (n = 0; n <= 54; ++n)
    {
        setup(&t, 0);
        t.bytes = n;
        assert(!three_mm_bitmap_read(&t, 1, 1, 0, &t.s));
        balanced(&t);
    }
    setup(&t, 0);
    t.bytes = 55;
    put(t.input + 2, 55, 4);
    put(t.input + 10, 54, 4);
    assert(three_mm_bitmap_read(&t, 0, 0, 0, &t.s));
    balanced(&t);
    assert(!three_mm_bitmap_read(&t, 1, 0, 0, &t.s));
    balanced(&t);
    setup(&t, 1);
    t.input[1078] = 4;
    t.input[1079] = 9; /* Full row without terminator: destination must be freed. */
    assert(!three_mm_bitmap_read(&t, 1, 1, 0, &t.s) && t.warning == THREE_MM_BITMAP_RLE + 1);
    balanced(&t);
    setup(&t, 0);
    put(t.input + 22, 0x80000000UL, 4);
    assert(!three_mm_bitmap_header(t.input, t.bytes, &h, &reason));
    setup(&t, 0);
    put(t.input + 18, 0x7fffffffUL, 4);
    assert(!three_mm_bitmap_header(t.input, t.bytes, &h, &reason));
    setup(&t, 0);
    put(t.input + 18, 0x40000000UL, 4);
    put(t.input + 22, 3, 4);
    assert(!three_mm_bitmap_header(t.input, t.bytes, &h, &reason));
}
static void rle_cases(void)
{
    unsigned char guarded[20], expected[12], *out = guarded + 4;
    const unsigned char encoded[] = {4, 7, 0, 0, 0, 3, 1, 2, 3, 0, 0, 0, 0, 1};
    const unsigned char delta[] = {1, 9, 0, 2, 1, 1, 2, 8, 0, 1};
    const unsigned char extra_eol[] = {4, 7, 0, 0, 0, 0, 0, 1};
    const unsigned char missing_pad[] = {0, 3, 1, 2, 3};
    const unsigned char too_long[] = {5, 7};
    int i;
    memset(guarded, 0xcc, sizeof(guarded));
    assert(three_mm_bitmap_rle8(encoded, sizeof(encoded), out, 12, 4, 73));
    memset(expected, 73, 12);
    memset(expected, 7, 4);
    expected[4] = 1;
    expected[5] = 2;
    expected[6] = 3;
    assert(!memcmp(out, expected, 12));
    for (i = 0; i < 4; ++i)
        assert(guarded[i] == 0xcc && guarded[i + 16] == 0xcc);
    assert(three_mm_bitmap_rle8(delta, sizeof(delta), out, 12, 4, 73));
    memset(expected, 73, 12);
    expected[0] = 9;
    expected[6] = expected[7] = 8;
    assert(!memcmp(out, expected, 12));
    memset(guarded, 0xcc, sizeof(guarded));
    assert(!three_mm_bitmap_rle8(extra_eol, sizeof(extra_eol), out, 4, 4, 73));
    for (i = 8; i < 20; ++i)
        assert(guarded[i] == 0xcc);
    memset(guarded, 0xcc, sizeof(guarded));
    assert(!three_mm_bitmap_rle8(missing_pad, sizeof(missing_pad), out, 12, 4, 73));
    assert(!three_mm_bitmap_rle8(too_long, sizeof(too_long), out, 12, 4, 73));
    for (i = 0; i < 20; ++i)
        assert(guarded[i] == 0xcc);
    for (i = 0; i < 2; ++i)
        assert(!three_mm_bitmap_rle8(encoded, i, out, 12, 4, 73));
}
static void write_cases(void)
{
    Test t;
    Palette pal;
    unsigned char pixels[12];
    int n, flip, ok, i;
    for (i = 0; i < 1024; ++i)
        pal.bytes[i] = (unsigned char)i;
    for (i = 0; i < 12; ++i)
        pixels[i] = (unsigned char)i;
    for (flip = 0; flip < 2; ++flip)
        for (n = 0; n < 8; ++n)
        {
            setup(&t, 0);
            t.fail = n;
            ok = three_mm_bitmap_write(&t, pixels, &pal, 3, 3, flip, &t.s);
            if (ok)
            {
                assert(t.written == 1090 && !t.temporary && !memcmp(t.output + 54, pal.bytes, 1024));
                for (i = 0; i < 3; ++i)
                    assert(!memcmp(t.output + 1078 + i * 4, pixels + (flip ? 2 - i : i) * 4, 4));
            }
            else if (n == 1)
                assert(!t.temporary);
            else
                assert(t.temporary == 1);
            balanced(&t);
        }
    setup(&t, 0);
    assert(three_mm_bitmap_write(&t, NULL, &pal, 0, 2, 1, &t.s) && t.writes == 2 && t.written == 1078);
    balanced(&t);
    setup(&t, 0);
    assert(!three_mm_bitmap_write(&t, pixels, &pal, 2147483647, 1, 0, &t.s) && !t.created);
}
int main(void)
{
    read_cases();
    rle_cases();
    write_cases();
    puts("C89 indexed BMP: conditional outputs/palettes, RLE bounds, ownership failures, row order and exact bytes "
         "passed.");
    return 0;
}
