#include "mm/texture_resource.h"
#include "chunk_edit_fixture.h"
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
    MMTextureResource texture;
} Object;
typedef struct Palette
{
    Ref ref;
    int count;
    MMTextureColour colours[4];
} Palette;
typedef struct Imported
{
    MMTextureDescription description;
    unsigned char pixels[8];
} Imported;
typedef struct File
{
    Ref ref;
    unsigned char *bytes;
    MMI32 *length;
} File;
typedef struct Test
{
    MMTextureServices s;
    MMChunkPayloadServices payloads;
    MMHandleServices handles;
    MMBlockFileServices io;
    ChunkIndexFixture index;
    File source_file, chunk_file, standalone_file, temp_file;
    int phase, fail, objects, memory, imports, files, handles_live, published, invalidated, diagnostic, temporary,
        reported, pack_ok, creates_temp;
    unsigned char input[128], output[128], physical[128], temp[128];
    MMI32 input_size, output_size, physical_size, temp_size;
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
static MMTextureResource *state(void *p, void *o)
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
        mm_texture_dispose(&((Object *)v)->texture, &t->s);
        --t->objects;
        free(v);
    }
    else if (r->kind == 3)
        --t->files;
}
static void publish(void *p, void *o, const MMTextureResource *m)
{
    (void)o;
    (void)m;
    ++test(p)->published;
}
static void describe_import(void *p, void *o, MMTextureDescription *d, void **pixels)
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
static int allocate(void *p, MMI32 n, int clear, void **out)
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
static int resize_handle(void *p, void **handle, MMU32 n)
{
    MMU32 *v = (MMU32 *)realloc(*handle, sizeof(*v) + n);
    (void)p;
    if (!v)
        return 0;
    *v = n;
    *handle = v;
    return 1;
}
static int allocate_handle(void *p, MMU32 n, void **handle)
{
    Test *t = test(p);
    *handle = NULL;
    if (fail(t) || !resize_handle(p, handle, n))
        return 0;
    ++t->handles_live;
    return 1;
}
static void release_handle(void *p, void *handle) { --test(p)->handles_live; free(handle); }
static MMU32 size_handle(void *p, void *handle) { (void)p; return *(MMU32 *)handle; }
static void *lock_handle(void *p, void *handle) { (void)p; return (MMU32 *)handle + 1; }
static void unlock_handle(void *p, void *handle) { (void)p; (void)handle; }

static int locate(void *p, MMU32 type, MMU32 number, MMBlockFile *extent, int *packed)
{
    Test *t = test(p);
    (void)type;
    (void)number;
    if (fail(t))
        return 0;
    extent->file = &t->source_file;
    extent->offset = 0;
    extent->length = (MMU32)t->input_size;
    *packed = 0;
    return 1;
}
static int decode(void *p, void **handle)
{
    Test *t = test(p);
    assert(size_handle(p, *handle) == (MMU32)t->packed_size);
    if (fail(t) || !resize_handle(p, handle, (MMU32)t->input_size))
        return 0;
    memcpy(lock_handle(p, *handle), t->input, (size_t)t->input_size);
    return 1;
}
static int encode(void *p, void **handle)
{
    Test *t = test(p);
    if (!t->pack_ok || fail(t))
        return 0;
    return resize_handle(p, handle, (MMU32)t->packed_size);
}
static void retain_file(void *p, void *file) { (void)p; ++((File *)file)->ref.refs; }
static int read_io(void *p, void *file, MMU32 at, void *out, size_t n)
{
    File *f = file;
    assert(at <= (MMU32)*f->length && n <= (MMU32)*f->length - at);
    if (fail(test(p)))
        return 0;
    memcpy(out, f->bytes + at, n);
    return 1;
}
static int write_io(void *p, void *file, MMU32 at, const void *data, size_t n)
{
    File *f = file;
    assert(at <= 128 && n <= 128 - at);
    if (fail(test(p)))
        return 0;
    memcpy(f->bytes + at, data, n);
    if (at + n > (MMU32)*f->length)
        *f->length = (MMI32)(at + n);
    return 1;
}
static int allocate_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    Test *t = ((ChunkIndexFixture *)p)->observer;
    if (fail(t))
        return 0;
    extent->file = &t->chunk_file;
    extent->offset = 0;
    extent->bytes = bytes;
    extent->flags = 0;
    return 1;
}
static int read_bitmap(void *p, void *filename, MMTextureBitmap *out)
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
static MMI32 palette_count(void *p, void *palette)
{
    (void)p;
    return ((Palette *)palette)->count;
}
static void palette_colour(void *p, void *palette, MMI32 i, MMTextureColour *out)
{
    Palette *v = (Palette *)palette;
    (void)p;
    assert(i >= 0 && i < v->count);
    *out = v->colours[i];
}
static void *create_file(void *p, void *filename)
{
    Test *t = test(p);
    (void)filename;
    if (fail(t))
        return NULL;
    t->standalone_file.ref.refs = 1;
    ++t->files;
    return &t->standalone_file;
}
static void *create_temp(void *p)
{
    Test *t = test(p);
    if (!t->creates_temp)
        return NULL;
    t->temp_file.ref.refs = 1;
    ++t->files;
    return &t->temp_file;
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
    MMTextureDescription d;
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
    t->s.payloads = &t->payloads;
    t->s.encode = encode;
    t->payloads.handles = &t->handles;
    t->payloads.files = &t->io;
    t->payloads.locate = locate;
    t->payloads.decode_context = t;
    t->payloads.decode = decode;
    t->handles.context = t;
    t->handles.allocate = allocate_handle;
    t->handles.release = release_handle;
    t->handles.size = size_handle;
    t->handles.lock = lock_handle;
    t->handles.unlock = unlock_handle;
    t->handles.resize = resize_handle;
    t->io.context = t;
    t->io.retain = retain_file;
    t->io.release = release;
    t->io.create_temp = create_temp;
    t->io.read = read_io;
    t->io.write = write_io;
    t->s.read_bitmap = read_bitmap;
    t->s.palette_count = palette_count;
    t->s.palette_colour = palette_colour;
    t->s.create_file = create_file;
    t->s.make_temporary = make_temporary;
    t->s.report = report;
    d.row_bytes = 4;
    d.width = 3;
    d.height = 2;
    d.base_x = 65535;
    d.origin_x = -7;
    d.type = 3;
    d.flags = 1;
    mm_texture_header_write(t->input, 0, 0x0303, &d);
    memset(t->input + 20, 17, 8);
    t->input_size = 28;
    t->packed_size = 19;
    t->source_file.bytes = t->input;
    t->source_file.length = &t->input_size;
    t->chunk_file.bytes = t->output;
    t->chunk_file.length = &t->output_size;
    t->standalone_file.ref.kind = t->temp_file.ref.kind = 3;
    t->standalone_file.bytes = t->physical;
    t->standalone_file.length = &t->physical_size;
    t->temp_file.bytes = t->temp;
    t->temp_file.length = &t->temp_size;
    t->source.ref.kind = t->target.ref.kind = 2;
    t->source.count = t->target.count = 4;
    t->source.colours[1].red = 77;
    t->source.colours[1].green = 45;
    t->source.colours[1].blue = 33;
    t->target.colours[0] = t->target.colours[3] = t->source.colours[1];
}
static void balanced(Test *t)
{
    assert(!t->objects && !t->memory && !t->imports && !t->files && !t->source.ref.refs && !t->handles_live);
    assert(!t->source_file.ref.refs && !t->chunk_file.ref.refs && !t->standalone_file.ref.refs && !t->temp_file.ref.refs);
}
static void codecs_and_loads(void)
{
    Test t;
    MMOwnedBlock block;
    MMBlockFile extent;
    MMTextureDescription d;
    unsigned char b[20];
    MMI32 n, estimate;
    int bad, i, big, ok;
    Object *o;
    setup(&t);
    for (big = 0; big < 2; ++big)
    {
        assert(mm_texture_header_read(t.input, 0, &d, &bad) && !bad);
        mm_texture_header_write(b, big, 0x0303, &d);
        assert(mm_texture_header_read(b, 0, &d, &bad) && !bad && d.base_x == 65535 && d.origin_x == -7 &&
               d.width == 3);
    }
    d.row_bytes = -1;
    assert(!mm_texture_payload(&d, &n));
    d.row_bytes = 32767;
    d.height = 65535;
    assert(mm_texture_payload(&d, &n) && n == 2147385345);
    for (i = 0; i < 10; ++i)
    {
        setup(&t);
        assert(mm_owned_block_init(&block, &t.handles, &t.io) == MM_DATA_OK);
        extent.file = &t.source_file;
        extent.offset = 0;
        extent.length = (MMU32)t.packed_size;
        assert(mm_owned_block_set_file(&block, &extent, 1) == MM_DATA_OK);
        assert(mm_texture_fetch(&block, NULL, &estimate, &t.s) && estimate == 19 && !t.phase && block.view.packed);
        t.fail = i;
        o = NULL;
        ok = mm_texture_fetch(&block, &o, &estimate, &t.s);
        if (ok)
        {
            assert(estimate == 19 && o->texture.description.row_bytes == 4 &&
                   ((unsigned char *)o->texture.pixels)[7] == 17);
            assert(block.view.length == 28 && !block.view.packed && !block.file && t.handles_live == 1);
            release(&t, o);
        }
        else
            assert(t.invalidated == 2);
        assert(mm_owned_block_clear(&block) == MM_DATA_OK);
        balanced(&t);
    }
    for (i = 0; i < 28; ++i)
    {
        setup(&t);
        t.input_size = i;
        assert(!mm_texture_read(&t, 1, 0, &t.s));
        balanced(&t);
    }
    setup(&t);
    t.input[4] = t.input[5] = 255;
    assert(!mm_texture_read(&t, 1, 0, &t.s));
    balanced(&t);
    setup(&t);
    t.input_size = 29;
    o = (Object *)mm_texture_read(&t, 1, 0, &t.s);
    assert(o);
    release(&t, o);
    balanced(&t);
    {
        MMMemoryStream stream;
        setup(&t);
        assert(mm_memory_stream_init(&stream, t.input, 27));
        assert(!mm_texture_read_stream(&stream.stream, &t.s));
        balanced(&t);
        assert(mm_memory_stream_init(&stream, t.input, 28));
        o = (Object *)mm_texture_read_stream(&stream.stream, &t.s);
        assert(o && o->texture.description.row_bytes == 4 && ((unsigned char *)o->texture.pixels)[7] == 17);
        assert(!t.source_file.ref.refs && !t.handles_live);
        release(&t, o);
        balanced(&t);
    }
}
static void constructors_and_import(void)
{
    Test t;
    Object *o;
    void *pixels = NULL;
    Imported *owner;
    int n, cache;
    setup(&t);
    assert(!mm_texture_new(NULL, 4, 1, &t.s) && !mm_texture_wrap(NULL, &t.s));
    assert(allocate(&t, 8, 1, &pixels));
    t.object_fail = 1;
    assert(!mm_texture_new(pixels, 4, 2, &t.s) && t.memory == 1);
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
    assert(!mm_texture_wrap(owner, &t.s) && t.imports == 1);
    t.object_fail = 0;
    o = (Object *)mm_texture_wrap(owner, &t.s);
    assert(o && o->texture.imported_owner == owner);
    release(&t, o);
    balanced(&t);
    for (cache = 0; cache < 2; ++cache)
        for (n = 0; n < 7; ++n)
        {
            setup(&t);
            t.fail = n;
            t.cache_fail = cache;
            o = (Object *)mm_texture_bitmap(&t, &t.target, &t.s);
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
    o = (Object *)mm_texture_bitmap(&t, &t.target, &t.s);
    assert(o && ((unsigned char *)o->texture.pixels)[0] == 1);
    release(&t, o);
    balanced(&t);
    setup(&t);
    t.object_fail = 1;
    assert(!mm_texture_bitmap(&t, NULL, &t.s));
    balanced(&t);
    setup(&t);
    t.source.count = 1;
    assert(!mm_texture_bitmap(&t, &t.target, &t.s));
    balanced(&t);
    setup(&t);
    t.bitmap_invalid = 1;
    o = (Object *)mm_texture_bitmap(&t, NULL, &t.s);
    assert(o && t.diagnostic == MM_TEXTURE_UPSIDE_DOWN + 1);
    release(&t, o);
    balanced(&t);
}
static void writes(void)
{
    Test t;
    MMChunkEdit edit;
    MMTextureResource m;
    MMOwnedBlock block;
    MMBlockFile extent;
    unsigned char pixels[8] = {0};
    MMU32 number;
    int n, compress, ok;
    memset(&m, 0, sizeof(m));
    m.pixels = pixels;
    m.description.width = m.description.row_bytes = 4;
    m.description.height = 2;
    for (n = 0; n < 4; ++n)
    {
        setup(&t);
        fixture_chunk_add(&t.index, 1, 0);
        t.index.observer = &t;
        edit = fixture_chunk_edit(&t.index);
        edit.allocate = allocate_extent;
        t.fail = n;
        number = 99;
        ok = mm_texture_write_chunk(&m, &edit, 1, &number, &t.s);
        assert(ok == !n);
        if (n == 1)
            assert(t.invalidated == 1 && t.index.count == 1);
        else
            assert(number == 1 && t.index.count == 2 && t.index.rows[1].entry.bytes == 28);
        assert(t.output_size == (!n ? 28 : n == 3 ? 20 : 0));
        balanced(&t);
    }
    setup(&t);
    assert(mm_owned_block_init(&block, &t.handles, &t.io) == MM_DATA_OK);
    extent.file = &t.chunk_file;
    extent.offset = 0;
    extent.length = 24;
    assert(mm_owned_block_set_file(&block, &extent, 0) == MM_DATA_OK);
    assert(!mm_texture_write(&m, &block, &t.s) && t.output_size == 20);
    assert(mm_owned_block_clear(&block) == MM_DATA_OK);
    balanced(&t);
    for (compress = 0; compress < 2; ++compress)
        for (n = 0; n < 10; ++n)
        {
            setup(&t);
            t.fail = n;
            t.pack_ok = 1;
            ok = mm_texture_standalone(&m, &t, compress, &t, &t.s);
            if (ok)
                assert(!t.temporary && !t.reported && t.physical_size > 4);
            else
                assert(t.reported && ((n == 1 && !t.temporary) || (n != 1 && t.temporary)));
            balanced(&t);
        }
    setup(&t);
    t.pack_ok = 0;
    assert(mm_texture_standalone(&m, &t, 1, NULL, &t.s));
    assert(t.physical[0] == 'p' && t.physical[1] == 'u' && t.physical_size == 32);
    assert(!memcmp(t.physical + 24, pixels, sizeof(pixels)));
    balanced(&t);
    setup(&t);
    t.pack_ok = 1;
    assert(mm_texture_standalone(&m, &t, 1, NULL, &t.s));
    assert(t.physical[0] == 'a' && t.physical[1] == 'p' && t.physical_size == 23);
    balanced(&t);
    setup(&t);
    t.fail = 2;
    t.creates_temp = 1;
    assert(mm_texture_standalone(&m, &t, 1, NULL, &t.s));
    assert(t.temp_size == 28 && t.physical_size == 32 && !memcmp(t.physical + 4, t.temp, 28));
    balanced(&t);
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
