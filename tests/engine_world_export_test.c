#include "three_mm/world_export.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    BITMAP = 100,
    SOURCE_Z,
    PALETTE
};
typedef struct Object
{
    int kind, refs, locks, width, height, row, bytes;
    unsigned char *pixels;
} Object;
typedef struct Actor
{
    struct Actor *child;
    int style;
} Actor;
typedef struct Fixture
{
    ThreeMMWorldBuffers b;
    ThreeMMWorldExportServices s;
    Object *working, *bitmap, *source, *rgb, *depth;
    ThreeMMWorldMaterial material, original;
    Actor root, model;
    int phase, fail, live, material_live, bounds, locks, options, late, flat, no_texture;
    int rasters, writes, width, height, current, bad_pixels, short_source, bad_depth_late;
    int events[256], n, cache;
} Fixture;
static void event(Fixture *f, int n)
{
    assert(f->n < 256);
    f->events[f->n++] = n;
}
static int phase(Fixture *f)
{
    return ++f->phase == f->fail;
}
static Object *object(Fixture *f, int kind, int width, int height)
{
    Object *o = (Object *)calloc(1, sizeof(*o));
    int depth = kind == SOURCE_Z || kind == THREE_MM_WORLD_DEPTH_WORKING;
    assert(o);
    o->kind = kind;
    o->refs = 1;
    o->width = width;
    o->height = height;
    o->row = depth ? width * 2 : (width + 3) & ~3;
    o->bytes = o->row * height;
    o->pixels = (unsigned char *)malloc((size_t)o->bytes + 8);
    assert(o->pixels);
    memset(o->pixels, depth ? 255 : 0, (size_t)o->bytes);
    memset(o->pixels + o->bytes, 0xcc, 8);
    ++f->live;
    return o;
}
static void release(void *context, void *p)
{
    Fixture *f = (Fixture *)context;
    Object *o = (Object *)p;
    int i;
    assert(o->refs > 0 && !o->locks);
    event(f, 1000 + o->kind);
    if (--o->refs)
        return;
    for (i = 0; i < 8; ++i)
        assert(o->pixels[o->bytes + i] == 0xcc);
    free(o->pixels);
    free(o);
    --f->live;
}
static void *create(void *p, int kind, ThreeMMI32 width, ThreeMMI32 height)
{
    Fixture *f = (Fixture *)p;
    Object *o;
    if (phase(f))
        return NULL;
    o = object(f, kind, width, height);
    if (kind == THREE_MM_WORLD_RGB_WORKING)
        f->rgb = o;
    if (kind == THREE_MM_WORLD_DEPTH_WORKING)
        f->depth = o;
    return o;
}
static void *fetch(void *p, void *cache, int kind, ThreeMMU32 type, ThreeMMU32 number)
{
    Fixture *f = (Fixture *)p;
    Object *o = kind == THREE_MM_WORLD_RGB_BACKGROUND ? f->bitmap : f->source;
    assert(cache == &f->cache && type == (kind == THREE_MM_WORLD_RGB_BACKGROUND ? 11U : 22U));
    assert(number == (kind == THREE_MM_WORLD_RGB_BACKGROUND ? 33U : 44U));
    if (phase(f))
        return NULL;
    ++o->refs;
    return o;
}
static void depth_span(void *p, void *depth, ThreeMMWorldDepthSpan *out)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)depth;
    out->pixels = o->pixels;
    out->row = o->row;
    out->bytes = o->bytes;
    if (f->short_source && o == f->source)
        --out->bytes;
    if (f->bad_depth_late && f->locks && o == f->depth)
        ++out->row;
}
static void draw_bitmap(void *p, void *port, void *bitmap)
{
    Object *o = (Object *)port, *source = (Object *)bitmap;
    (void)p;
    assert(o->width == source->width && o->height == source->height);
    memcpy(o->pixels, source->pixels, (size_t)o->bytes);
}
static void copy_rgb(void *p, void *source, void *destination, ThreeMMI32 sw, ThreeMMI32 sh, ThreeMMI32 dw,
                     ThreeMMI32 dh)
{
    Object *src = (Object *)source, *dst = (Object *)destination;
    int x, y;
    (void)p;
    assert(sw == src->width && sh == src->height && dw == dst->width && dh == dst->height);
    for (y = 0; y < dh; ++y)
        for (x = 0; x < dw; ++x)
            dst->pixels[y * dst->row + x] = src->pixels[(y * sh / dh) * src->row + x * sw / dw];
}
static int lock(void *p, void *port, ThreeMMWorldPixels *out)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)port;
    ++f->locks;
    if (phase(f))
        return 0;
    ++o->locks;
    out->pixels = o->pixels;
    out->row = o->row;
    out->bytes = f->bad_pixels == f->locks ? 1 : (size_t)o->bytes;
    event(f, 300);
    return 1;
}
static void unlock(void *p, void *port)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)port;
    assert(o->locks == (o == f->working ? 2 : 1));
    --o->locks;
    event((Fixture *)p, 301);
}
static void *palette(void *p)
{
    Fixture *f = (Fixture *)p;
    if (phase(f))
        return NULL;
    return object(f, PALETTE, 0, 0);
}
static size_t material_count(void *p)
{
    (void)p;
    return 1;
}
static void material_enum(void *p, int (*visit)(void *, void *), void *argument)
{
    Fixture *f = (Fixture *)p;
    visit(argument, &f->material);
}
static void material_read(void *p, void *m, ThreeMMWorldMaterial *out)
{
    (void)p;
    *out = *(ThreeMMWorldMaterial *)m;
}
static void material_write(void *p, const ThreeMMWorldMaterial *in)
{
    Fixture *f = (Fixture *)p;
    f->material = *in;
    event(f, 700 + (int)in->flags);
}
static void *material_allocate(void *p, size_t n)
{
    Fixture *f = (Fixture *)p;
    void *result;
    if (phase(f))
        return NULL;
    result = malloc(n);
    assert(result);
    ++f->material_live;
    return result;
}
static void material_free(void *p, void *m)
{
    Fixture *f = (Fixture *)p;
    assert(f->material_live == 1);
    --f->material_live;
    free(m);
}
static void *child(void *p, void *a)
{
    (void)p;
    return ((Actor *)a)->child;
}
static void *next(void *p, void *a)
{
    (void)p;
    (void)a;
    return NULL;
}
static int type(void *p, void *a)
{
    Fixture *f = (Fixture *)p;
    return a == &f->model;
}
static int style(void *p, void *a)
{
    (void)p;
    return ((Actor *)a)->style;
}
static void set_style(void *p, void *a, int value)
{
    (void)p;
    ((Actor *)a)->style = value;
}
static void options(void *p, int *wire, int *flat, int *no_texture)
{
    Fixture *f = (Fixture *)p;
    ++f->options;
    if (f->late && f->options == 2)
        f->flat = f->no_texture = 1;
    *wire = 1;
    *flat = f->flat;
    *no_texture = f->no_texture;
}
static void bounds_callback(void *p, int enabled)
{
    Fixture *f = (Fixture *)p;
    assert(f->bounds != enabled);
    f->bounds = enabled;
    event(f, 400 + enabled);
}
static void flush(void *p)
{
    Fixture *f = (Fixture *)p;
    assert(f->bounds && f->material.flags == 20 && !f->material_live);
    event(f, 402);
}
static void raster(void *p, const ThreeMMWorldPixelView *rgb, const ThreeMMWorldPixelView *z)
{
    Fixture *f = (Fixture *)p;
    int x, y, scale = rgb->width / f->b.width;
    assert(!f->bounds && rgb->width == f->b.width * scale && rgb->height == f->b.height * scale);
    assert(rgb->row == rgb->width && z->row == z->width * 2 && rgb->x == rgb->width / 2 && rgb->y == rgb->height / 2);
    assert(rgb->type == 42 && z->type == 84 && rgb->pixels == f->rgb->pixels && z->pixels == f->depth->pixels);
    assert(f->model.style == 7 && f->root.style == 99);
    assert(f->material.flags == (f->flat ? 16U : 20U));
    assert(f->material.texture == (f->no_texture ? NULL : f->original.texture));
    for (y = 0; y < rgb->height; ++y)
        for (x = 0; x < rgb->width; ++x)
        {
            int index = y * f->depth->row + x * 2;
            assert(f->rgb->pixels[y * f->rgb->row + x] ==
                   (f->b.owners[THREE_MM_WORLD_CACHE] ? 0x10 + (y / scale) * 3 + x / scale : 0));
            assert(f->depth->pixels[index] == (f->b.owners[THREE_MM_WORLD_CACHE] ? 0x20 + x / scale : 255));
            assert(f->depth->pixels[index + 1] == (f->b.owners[THREE_MM_WORLD_CACHE] ? 0x80 + y / scale : 255));
        }
    ++f->rasters;
    event(f, 500);
    f->rgb->pixels[0] = 99;
}
static int write(void *p, const ThreeMMWorldPixels *pixels, void *palette_object, ThreeMMI32 width, ThreeMMI32 height)
{
    Fixture *f = (Fixture *)p;
    Object *pal = (Object *)palette_object;
    assert(pal->kind == PALETTE && f->bounds && !f->material_live && f->material.flags == 20);
    assert(pixels->pixels[0] == (f->current ? 0x55 : 99));
    f->width = width;
    f->height = height;
    ++f->writes;
    event(f, 600);
    return !phase(f);
}
static void init(Fixture *f)
{
    ThreeMMWorldBufferServices *b;
    ThreeMMWorldRenderServices *r;
    ThreeMMWorldMaterialServices *m;
    int x, y;
    memset(f, 0, sizeof(*f));
    f->bounds = f->flat = f->no_texture = 1;
    f->working = object(f, THREE_MM_WORLD_RGB_WORKING, 2, 1);
    f->working->locks = 1; /* Retained renderer lock belongs to the world. */
    f->bitmap = object(f, BITMAP, 3, 2);
    f->source = object(f, SOURCE_Z, 3, 2);
    memset(f->working->pixels, 0x55, (size_t)f->working->bytes);
    for (y = 0; y < 2; ++y)
        for (x = 0; x < 3; ++x)
        {
            f->bitmap->pixels[y * f->bitmap->row + x] = (unsigned char)(0x10 + y * 3 + x);
            f->source->pixels[y * f->source->row + x * 2] = (unsigned char)(0x20 + x);
            f->source->pixels[y * f->source->row + x * 2 + 1] = (unsigned char)(0x80 + y);
        }
    f->b.width = 3;
    f->b.height = 2;
    f->b.buffer_width = 2;
    f->b.buffer_height = 1;
    f->b.half_x = f->b.half_y = 1;
    f->b.scale = 4;
    f->b.owners[THREE_MM_WORLD_CACHE] = &f->cache;
    f->b.owners[THREE_MM_WORLD_RGB_WORKING] = f->working;
    f->b.rgb_type = 11;
    f->b.z_type = 22;
    f->b.rgb_number = 33;
    f->b.z_number = 44;
    f->b.rgb.type = 42;
    f->b.z.type = 84;
    f->root.child = &f->model;
    f->root.style = 99;
    f->material.object = &f->material;
    f->material.texture = f->bitmap;
    f->material.flags = 20;
    f->material.base = 11;
    f->material.range = 100;
    f->original = f->material;
    f->s.context = f;
    f->s.palette = palette;
    f->s.lock = lock;
    f->s.bounds_callback = bounds_callback;
    f->s.raster = raster;
    f->s.write = write;
    b = &f->s.buffers;
    b->context = f;
    b->create = create;
    b->release = release;
    b->fetch = fetch;
    b->depth_span = depth_span;
    b->draw_bitmap = draw_bitmap;
    b->copy_rgb = copy_rgb;
    b->unlock = unlock;
    r = &f->s.render;
    r->context = f;
    r->root = &f->root;
    r->actor_model = 1;
    r->bounding_points = 10;
    r->bounding_edges = 11;
    r->bounding_faces = 12;
    r->edges = 7;
    r->faces = 8;
    r->child = child;
    r->next = next;
    r->type = type;
    r->style = style;
    r->set_style = set_style;
    r->options = options;
    r->flush = flush;
    m = &r->materials;
    m->context = f;
    m->gouraud = 4;
    m->count = material_count;
    m->enumerate = material_enum;
    m->read = material_read;
    m->write = material_write;
    m->allocate = material_allocate;
    m->free = material_free;
}
static void balanced(Fixture *f)
{
    assert(f->live == 3 && !f->material_live && f->bounds);
    assert(f->bitmap->refs == 1 && f->source->refs == 1 && f->working->refs == 1 && f->working->locks == 1);
    assert(f->material.texture == f->original.texture && f->material.flags == 20 && f->material.base == 11 &&
           f->material.range == 100);
    f->working->locks = 0; /* World disposal, after the export transaction. */
    release(f, f->working);
    release(f, f->bitmap);
    release(f, f->source);
    assert(!f->live);
}
static void transactions(void)
{
    Fixture f;
    ThreeMMWorldBuffers before;
    int fail, scale, scenario, i;
    for (scale = 1; scale <= 16; ++scale)
    {
        init(&f);
        before = f.b;
        assert(three_mm_world_export_scaled(&f.b, scale, &f.s));
        assert(f.rasters == 1 && f.writes == 1 && f.width == 3 * scale && f.height == 2 * scale);
        assert(f.phase == 10 && f.options == 2 && !memcmp(&before, &f.b, sizeof(before)));
        for (i = 0; i < f.working->bytes; ++i)
            assert(f.working->pixels[i] == 0x55);
        assert(f.events[f.n - 3] == 1000 + PALETTE && f.events[f.n - 2] == 1000 + THREE_MM_WORLD_RGB_WORKING &&
               f.events[f.n - 1] == 1000 + THREE_MM_WORLD_DEPTH_WORKING);
        balanced(&f);
    }
    for (fail = 1; fail <= 10; ++fail)
    {
        init(&f);
        f.fail = fail;
        assert(!three_mm_world_export_scaled(&f.b, 2, &f.s));
        assert(f.phase == fail && f.rasters == (fail >= 8) && f.writes == (fail == 10));
        balanced(&f);
    }
    for (fail = 0; fail <= 3; ++fail)
    {
        init(&f);
        f.fail = fail;
        f.current = 1;
        assert(three_mm_world_export_current(&f.b, &f.s) == (fail == 0));
        assert(!f.rasters && !f.options && (fail || (f.width == 2 && f.height == 1)));
        assert(f.locks == (fail == 1 ? 0 : 1));
        balanced(&f);
    }
    for (scenario = 0; scenario < 6; ++scenario)
    {
        init(&f);
        if (scenario == 0)
            f.b.owners[THREE_MM_WORLD_CACHE] = NULL;
        if (scenario == 1)
            f.short_source = 1;
        if (scenario == 2)
            f.bad_pixels = 1;
        if (scenario == 3)
            f.bad_pixels = 2;
        if (scenario == 4)
            f.bad_depth_late = 1;
        if (scenario == 5)
        {
            f.late = 1;
            f.flat = f.no_texture = 0;
        }
        assert(three_mm_world_export_scaled(&f.b, 3, &f.s) == (scenario == 0 || scenario == 5));
        balanced(&f);
    }
    init(&f);
    assert(!three_mm_world_export_scaled(&f.b, 17, &f.s));
    assert(!f.phase);
    balanced(&f);
}
static void dimensions(void)
{
    ThreeMMWorldBuffers b;
    ThreeMMI32 scale, width, height;
    int request;
    memset(&b, 0, sizeof(b));
    b.width = 640;
    b.height = 480;
    for (request = 1; request <= 16; ++request)
    {
        assert(three_mm_world_export_size(&b, request, &scale, &width, &height));
        assert(scale == (request > 6 ? 6 : request) && width == 640 * scale && height == 480 * scale);
    }
    scale = width = height = 123;
    assert(!three_mm_world_export_size(&b, 0, &scale, &width, &height));
    assert(!three_mm_world_export_size(&b, 17, &scale, &width, &height));
    b.height = 4097;
    assert(!three_mm_world_export_size(&b, 1, &scale, &width, &height));
    assert(scale == 123 && width == 123 && height == 123);
    b.height = b.width = 4096;
    assert(three_mm_world_export_size(&b, 16, &scale, &width, &height) && scale == 1 && width == 4096 &&
           height == 4096);
}
static void depth_copy(void)
{
    unsigned char src[16], dst[3200];
    ThreeMMWorldDepthSpan source, destination;
    int scale, x, y, row, height;
    memset(src, 0xcc, sizeof(src));
    for (y = 0; y < 2; ++y)
        for (x = 0; x < 3; ++x)
        {
            src[y * 8 + x * 2] = (unsigned char)(x + 1);
            src[y * 8 + x * 2 + 1] = (unsigned char)(y + 11);
        }
    source.pixels = src;
    source.row = 8;
    source.bytes = 16;
    destination.pixels = dst;
    for (scale = 1; scale <= 16; ++scale)
    {
        row = 3 * scale * 2 + 2;
        height = 2 * scale;
        memset(dst, 0xcc, sizeof(dst));
        destination.row = row;
        destination.bytes = row * height;
        assert(three_mm_world_depth_resample(&source, &destination, 3 * scale, height, scale, 0, 0));
        for (y = 0; y < height; ++y)
            for (x = 0; x < row; ++x)
                assert(dst[y * row + x] == (x < row - 2 ? src[(y / scale) * 8 + (x / 2 / scale) * 2 + x % 2] : 0xcc));
        assert(dst[row * height] == 0xcc);
    }
    source.bytes = 13;
    assert(!three_mm_world_depth_resample(&source, &destination, 48, 32, 16, 0, 0));
    assert(dst[3136] == 0xcc);
    assert(!three_mm_world_depth_resample(&source, &destination, 48, 32, 17, 0, 0));
}
int main(void)
{
    dimensions();
    depth_copy();
    transactions();
    puts("C89 World export: scaling, palette/lock/material ownership and transaction failures passed.");
    return 0;
}
