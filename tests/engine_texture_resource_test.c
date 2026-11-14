#include "three_mm/texture_resource.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Ref
{
    int kind, refs;
} Ref;
typedef struct Object
{
    Ref ref;
    ThreeMMTextureResource texture;
} Object;
typedef struct Palette
{
    Ref ref;
    int count;
    ThreeMMTextureColour colours[4];
} Palette;
typedef struct Imported
{
    ThreeMMTextureDescription description;
    unsigned char pixels[8];
} Imported;
typedef struct Test
{
    ThreeMMTextureServices s;
    int phase, fail, objects, memory, imports, files, closed, published, invalidated, diagnostic, temporary, reported,
        pack_ok;
    unsigned char input[128], output[128], physical[128];
    ThreeMMI32 input_size, output_size, physical_size;
    int packed_size, bitmap_invalid, cache_fail, object_fail;
    Palette source, target;
} Test;
static Test *test(void *p)
{
    return (Test *)p;
}
static int fail(Test *t)
{
    return ++t->phase == t->fail;
}
static void *create(void *p)
{
    Test *t = test(p);
    Object *o;
    if (fail(t) || t->object_fail)
        return NULL;
    o = (Object *)calloc(1, sizeof(*o));
    assert(o);
    o->ref.kind = 1;
    o->ref.refs = 1;
    ++t->objects;
    return o;
}
static ThreeMMTextureResource *state(void *p, void *o)
{
    (void)p;
    return &((Object *)o)->texture;
}
static void release(void *p, void *v)
{
    Test *t = test(p);
    Ref *r = (Ref *)v;
    assert(r && r->refs > 0);
    if (--r->refs)
        return;
    if (r->kind == 1)
    {
        three_mm_texture_dispose(&((Object *)v)->texture, &t->s);
        --t->objects;
        free(v);
    }
    else if (r->kind == 3)
        --t->files;
}
static void publish(void *p, void *o, const ThreeMMTextureResource *m)
{
    (void)o;
    (void)m;
    ++test(p)->published;
}
static void describe_import(void *p, void *o, ThreeMMTextureDescription *d, void **pixels)
{
    (void)p;
    *d = ((Imported *)o)->description;
    *pixels = ((Imported *)o)->pixels;
}
static void free_import(void *p, void *o)
{
    --test(p)->imports;
    free(o);
}
static int allocate(void *p, ThreeMMI32 n, int clear, void **out)
{
    Test *t = test(p);
    (void)clear;
    if (fail(t) || (!clear && t->cache_fail))
        return 0;
    *out = calloc(1, n ? (size_t)n : 1);
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
static void invalidate(void *p, void *out, size_t n)
{
    Test *t = test(p);
    assert(!t->objects && !t->memory);
    ++t->invalidated;
    memset(out, 0xa3, n);
}
static void diagnostic(void *p, int n)
{
    test(p)->diagnostic = n + 1;
}
static void *file(void *p, void *c)
{
    (void)c;
    return p;
}
static void *find(void *p, void *f, ThreeMMU32 type, ThreeMMU32 number)
{
    (void)f;
    (void)type;
    (void)number;
    return fail(test(p)) ? NULL : p;
}
static int unpack(void *p, void *b)
{
    (void)b;
    return !fail(test(p));
}
static ThreeMMI32 size(void *p, void *b, int packed)
{
    (void)b;
    return packed ? test(p)->packed_size : test(p)->input_size;
}
static int read(void *p, void *b, void *out, ThreeMMI32 n, ThreeMMI32 at)
{
    Test *t = test(p);
    (void)b;
    assert(n >= 0 && at >= 0 && at <= t->input_size && n <= t->input_size - at);
    if (fail(t))
        return 0;
    if (n)
        memcpy(out, t->input + at, n);
    return 1;
}
static int write(void *p, void *b, const void *data, ThreeMMI32 n, ThreeMMI32 at)
{
    Test *t = test(p);
    (void)b;
    assert(at >= 0 && n >= 0 && at <= t->input_size && n <= t->input_size - at);
    if (fail(t))
        return 0;
    if (n)
        memcpy(t->output + at, data, n);
    if (at + n > t->output_size)
        t->output_size = at + n;
    return 1;
}
static void close_block(void *p)
{
    ++test(p)->closed;
}
static void *add(void *p, void *f, ThreeMMU32 type, ThreeMMI32 n, ThreeMMU32 *number)
{
    Test *t = test(p);
    (void)f;
    (void)type;
    if (fail(t))
        return NULL;
    t->input_size = n;
    *number = 7;
    return p;
}
static int read_bitmap(void *p, void *filename, ThreeMMTextureBitmap *out)
{
    Test *t = test(p);
    void *pixels = NULL;
    (void)filename;
    if (fail(t) || !allocate(p, 8, 1, &pixels))
        return 0;
    memset(pixels, 1, 8);
    out->pixels = pixels;
    out->palette = &t->source;
    ++t->source.ref.refs;
    out->width = 3;
    out->height = 2;
    out->upside_down = t->bitmap_invalid;
    return 1;
}
static ThreeMMI32 palette_count(void *p, void *palette)
{
    (void)p;
    return ((Palette *)palette)->count;
}
static void palette_colour(void *p, void *palette, ThreeMMI32 i, ThreeMMTextureColour *out)
{
    Palette *v = (Palette *)palette;
    (void)p;
    assert(i >= 0 && i < v->count);
    *out = v->colours[i];
}
static Ref fake_file;
static void *create_file(void *p, void *filename)
{
    Test *t = test(p);
    (void)filename;
    if (fail(t))
        return NULL;
    fake_file.kind = 3;
    fake_file.refs = 1;
    ++t->files;
    return &fake_file;
}
static void *temp_block(void *p, ThreeMMI32 n)
{
    if (fail(test(p)))
        return NULL;
    test(p)->input_size = n;
    return p;
}
static int pack(void *p, void *b)
{
    (void)b;
    return test(p)->pack_ok && !fail(test(p));
}
static int write_file(void *p, void *f, const void *data, ThreeMMI32 n, ThreeMMI32 at)
{
    Test *t = test(p);
    (void)f;
    assert(n >= 0 && at >= 0 && at + n <= 128);
    if (fail(t))
        return 0;
    if (n)
        memcpy(t->physical + at, data, n);
    t->physical_size = at + n;
    return 1;
}
static int copy_to_file(void *p, void *b, void *f, ThreeMMI32 at, ThreeMMI32 n)
{
    (void)b;
    return write_file(p, f, test(p)->output, n, at);
}
static void make_temporary(void *p, void *f)
{
    (void)f;
    ++test(p)->temporary;
}
static void report(void *p, void *sink, int error)
{
    (void)sink;
    test(p)->reported = error + 1;
}
static void setup(Test *t)
{
    ThreeMMTextureDescription d;
    memset(t, 0, sizeof(*t));
    memset(&d, 0, sizeof(d));
    t->s.context = t;
    t->s.system = 0x0303;
    t->s.packed_signature = 0x6b617061UL;
    t->s.unpacked_signature = 0x6b617570UL;
    t->s.indexed_type = 3;
    t->s.linear_flag = 1;
    t->s.create = create;
    t->s.release = release;
    t->s.state = state;
    t->s.publish = publish;
    t->s.describe_import = describe_import;
    t->s.free_import = free_import;
    t->s.allocate = allocate;
    t->s.free_memory = free_memory;
    t->s.invalidate = invalidate;
    t->s.diagnostic = diagnostic;
    t->s.file = file;
    t->s.find = find;
    t->s.unpack = unpack;
    t->s.size = size;
    t->s.read = read;
    t->s.write = write;
    t->s.close_block = close_block;
    t->s.add = add;
    t->s.read_bitmap = read_bitmap;
    t->s.palette_count = palette_count;
    t->s.palette_colour = palette_colour;
    t->s.create_file = create_file;
    t->s.temp_block = temp_block;
    t->s.pack = pack;
    t->s.write_file = write_file;
    t->s.copy_to_file = copy_to_file;
    t->s.make_temporary = make_temporary;
    t->s.report = report;
    d.row_bytes = 4;
    d.width = 3;
    d.height = 2;
    d.base_x = 65535;
    d.origin_x = -7;
    d.type = 3;
    d.flags = 1;
    three_mm_texture_header_write(t->input, 0, 0x0303, &d);
    memset(t->input + 20, 17, 8);
    t->input_size = 28;
    t->packed_size = 19;
    t->source.ref.kind = t->target.ref.kind = 2;
    t->source.count = t->target.count = 4;
    t->source.colours[1].red = 77;
    t->source.colours[1].green = 45;
    t->source.colours[1].blue = 33;
    t->target.colours[0] = t->target.colours[3] = t->source.colours[1];
}
static void balanced(Test *t)
{
    assert(!t->objects && !t->memory && !t->imports && !t->files && !t->source.ref.refs);
}
static void codecs_and_loads(void)
{
    Test t;
    ThreeMMTextureDescription d;
    unsigned char b[20];
    ThreeMMI32 n, estimate;
    int bad, i, big, ok;
    Object *o;
    setup(&t);
    for (big = 0; big < 2; ++big)
    {
        assert(three_mm_texture_header_read(t.input, 0, &d, &bad) && !bad);
        three_mm_texture_header_write(b, big, 0x0303, &d);
        assert(three_mm_texture_header_read(b, 0, &d, &bad) && !bad && d.base_x == 65535 && d.origin_x == -7 &&
               d.width == 3);
    }
    d.row_bytes = -1;
    assert(!three_mm_texture_payload(&d, &n));
    d.row_bytes = 32767;
    d.height = 65535;
    assert(three_mm_texture_payload(&d, &n) && n == 2147385345);
    for (i = 0; i < 10; ++i)
    {
        setup(&t);
        t.fail = i;
        o = NULL;
        ok = three_mm_texture_fetch(&t, 1, 0, &t, &o, &estimate, &t.s);
        if (ok)
        {
            assert(estimate == 19 && o->texture.description.row_bytes == 4 &&
                   ((unsigned char *)o->texture.pixels)[7] == 17);
            release(&t, o);
        }
        else
            assert(t.invalidated == 2);
        assert(t.closed == 1);
        balanced(&t);
    }
    for (i = 0; i < 28; ++i)
    {
        setup(&t);
        t.input_size = i;
        assert(!three_mm_texture_read(&t, 1, 0, &t.s));
        balanced(&t);
    }
    setup(&t);
    t.input[4] = t.input[5] = 255;
    assert(!three_mm_texture_read(&t, 1, 0, &t.s));
    balanced(&t);
    setup(&t);
    t.input_size = 29;
    o = (Object *)three_mm_texture_read(&t, 1, 0, &t.s);
    assert(o);
    release(&t, o);
    balanced(&t);
}
static void constructors_and_import(void)
{
    Test t;
    Object *o;
    void *pixels = NULL;
    Imported *owner;
    int n, cache;
    setup(&t);
    assert(!three_mm_texture_new(NULL, 4, 1, &t.s) && !three_mm_texture_wrap(NULL, &t.s));
    assert(allocate(&t, 8, 1, &pixels));
    t.object_fail = 1;
    assert(!three_mm_texture_new(pixels, 4, 2, &t.s) && t.memory == 1);
    free_memory(&t, &pixels);
    balanced(&t);
    setup(&t);
    owner = (Imported *)calloc(1, sizeof(*owner));
    assert(owner);
    ++t.imports;
    owner->description.row_bytes = 4;
    owner->description.width = 4;
    owner->description.height = 2;
    t.object_fail = 1;
    assert(!three_mm_texture_wrap(owner, &t.s) && t.imports == 1);
    t.object_fail = 0;
    o = (Object *)three_mm_texture_wrap(owner, &t.s);
    assert(o && o->texture.imported_owner == owner);
    release(&t, o);
    balanced(&t);
    for (cache = 0; cache < 2; ++cache)
        for (n = 0; n < 7; ++n)
        {
            setup(&t);
            t.fail = n;
            t.cache_fail = cache;
            o = (Object *)three_mm_texture_bitmap(&t, &t.target, &t.s);
            if (o)
            {
                assert(o->texture.description.row_bytes == 3);
                assert(!memcmp(o->texture.pixels, "\3\3\3\3\3\3", 6));
                release(&t, o);
            }
            balanced(&t);
        }
    setup(&t);
    t.target.count = 0;
    o = (Object *)three_mm_texture_bitmap(&t, &t.target, &t.s);
    assert(o && ((unsigned char *)o->texture.pixels)[0] == 1);
    release(&t, o);
    balanced(&t);
    setup(&t);
    t.object_fail = 1;
    assert(!three_mm_texture_bitmap(&t, NULL, &t.s));
    balanced(&t);
    setup(&t);
    t.source.count = 1;
    assert(!three_mm_texture_bitmap(&t, &t.target, &t.s));
    balanced(&t);
    setup(&t);
    t.bitmap_invalid = 1;
    o = (Object *)three_mm_texture_bitmap(&t, NULL, &t.s);
    assert(o && t.diagnostic == THREE_MM_TEXTURE_UPSIDE_DOWN + 1);
    release(&t, o);
    balanced(&t);
}
static void writes(void)
{
    Test t;
    ThreeMMTextureResource m;
    unsigned char pixels[8] = {0};
    ThreeMMU32 number;
    int n, compress, ok;
    memset(&m, 0, sizeof(m));
    m.pixels = pixels;
    m.description.width = m.description.row_bytes = 4;
    m.description.height = 2;
    for (n = 0; n < 4; ++n)
    {
        setup(&t);
        t.fail = n;
        number = 99;
        ok = three_mm_texture_write_chunk(&m, &t, 1, &number, &t.s);
        if (ok)
            assert(number == 7 && t.output_size == 28);
        else if (n > 1)
            assert(number == 7);
        assert(t.closed == 1);
    }
    setup(&t);
    t.input_size = 24;
    assert(!three_mm_texture_write(&m, &t, &t.s) && t.output_size == 20);
    for (compress = 0; compress < 2; ++compress)
        for (n = 0; n < 10; ++n)
        {
            setup(&t);
            t.fail = n;
            t.pack_ok = 1;
            ok = three_mm_texture_standalone(&m, &t, compress, &t, &t.s);
            if (ok)
                assert(!t.temporary && !t.reported && t.physical_size > 4);
            else
                assert(t.reported && ((n == 1 && !t.temporary) || (n != 1 && t.temporary)));
            assert(t.closed == 1);
            balanced(&t);
        }
    setup(&t);
    t.pack_ok = 0;
    assert(three_mm_texture_standalone(&m, &t, 1, NULL, &t.s));
    assert(t.physical[0] == 'p' && t.physical[1] == 'u');
    setup(&t);
    t.pack_ok = 1;
    assert(three_mm_texture_standalone(&m, &t, 1, NULL, &t.s));
    assert(t.physical[0] == 'a' && t.physical[1] == 'p');
}
int main(void)
{
    codecs_and_loads();
    constructors_and_import();
    writes();
    puts("C89 texture resources: wire I/O, bounds, palette ties/cache fallback, imports, ownership and standalone "
         "failures passed.");
    return 0;
}
