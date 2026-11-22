#include "mm/world_export.h"
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
    int kind, refs, width, height, row, bytes;
    unsigned char *pixels;
    MMDepthBitmap depth;
    MMMaskedOwner *mask;
} Object;
typedef struct Actor
{
    struct Actor *child;
    int style;
} Actor;
typedef struct Fixture
{
    MMWorldBuffers b;
    MMWorldExportServices s;
    Object *bitmap, *source;
    MMWorldMaterial material, original;
    Actor root, model;
    int phase, fail, live, material_live, bounds, options, late, flat, no_texture;
    int rasters, writes, width, height, current, short_source, missing_depth, missing_colour;
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
    int depth = kind == SOURCE_Z;
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
    assert(o->refs > 0);
    event(f, 1000 + o->kind);
    if (--o->refs)
        return;
    for (i = 0; i < 8; ++i)
        assert(o->pixels[o->bytes + i] == 0xcc);
    mm_masked_owner_release(o->mask);
    free(o->pixels);
    free(o);
    --f->live;
}
static void *fetch(void *p, void *cache, int kind, MMU32 type, MMU32 number)
{
    Fixture *f = (Fixture *)p;
    Object *o = kind == MM_WORLD_COLOR_ASSET ? f->bitmap : f->source;
    assert(cache == &f->cache && type == (kind == MM_WORLD_COLOR_ASSET ? 11U : 22U));
    assert(number == (kind == MM_WORLD_COLOR_ASSET ? 33U : 44U));
    if (phase(f))
        return NULL;
    ++o->refs;
    return o;
}
static const MMDepthBitmap *depth_bitmap(void *p, void *depth)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)depth;
    assert(o == f->source);
    if (f->missing_depth)
        return NULL;
    o->depth.bounds.right = o->width;
    o->depth.bounds.bottom = o->height;
    o->depth.pixels = o->pixels;
    o->depth.row_bytes = o->row;
    o->depth.bytes = o->bytes;
    if (f->short_source)
        --o->depth.bytes;
    return &o->depth;
}
static const MMMaskedBitmap *masked_bitmap(void *p, void *bitmap)
{
    Fixture *f = (Fixture *)p;
    assert(bitmap == f->bitmap);
    return f->missing_colour ? NULL : mm_masked_owner_bitmap(f->bitmap->mask);
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
static void material_read(void *p, void *m, MMWorldMaterial *out)
{
    (void)p;
    *out = *(MMWorldMaterial *)m;
}
static void material_write(void *p, const MMWorldMaterial *in)
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
static void raster(void *p, const MMRenderFrame *frame, const MMWorldPixelView *rgb, const MMWorldPixelView *z)
{
    Fixture *f = (Fixture *)p;
    int x, y, scale = rgb->width / f->b.width;
    int pitch = (rgb->width + 3) & ~3;
    unsigned char *indices = (unsigned char *)rgb->pixels;
    const unsigned char *depth = (const unsigned char *)z->pixels;
    assert(frame->world == &f->root && frame->camera == &f->model);
    assert(!f->bounds && rgb->width == f->b.width * scale && rgb->height == f->b.height * scale);
    assert(rgb->row == rgb->width && z->row == z->width * 2 && rgb->x == rgb->width / 2 && rgb->y == rgb->height / 2);
    assert(rgb->type == 42 && z->type == 84 && indices && depth && indices != f->b.working.surface.pixels);
    assert(f->model.style == 7 && f->root.style == 99);
    assert(f->material.flags == (f->flat ? 16U : 20U));
    assert(f->material.texture == (f->no_texture ? NULL : f->original.texture));
    for (y = 0; y < rgb->height; ++y)
    {
        for (x = 0; x < rgb->width; ++x)
        {
            int index = y * z->row + x * 2;
            assert(indices[y * pitch + x] == (f->b.owners[MM_WORLD_CACHE] ? 0x10 + (y / scale) * 3 + x / scale : 0));
            assert(depth[index] == (f->b.owners[MM_WORLD_CACHE] ? 0x20 + x / scale : 255));
            assert(depth[index + 1] == (f->b.owners[MM_WORLD_CACHE] ? 0x80 + y / scale : 255));
        }
        for (; x < pitch; ++x)
            assert(indices[y * pitch + x] == 0);
    }
    ++f->rasters;
    event(f, 500);
    indices[0] = 99;
}
static int write(void *p, const MMWorldPixels *pixels, void *palette_object, MMI32 width, MMI32 height)
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
    const MMMaskedRect rect = {0, 0, 3, 2};
    MMWorldBufferServices *b;
    MMWorldRenderServices *r;
    MMWorldMaterialServices *m;
    int x, y;
    memset(f, 0, sizeof(*f));
    f->bounds = f->flat = f->no_texture = 1;
    assert(mm_pixel_buffer_init(&f->b.working, 2, 1, 8, 4));
    f->bitmap = object(f, BITMAP, 3, 2);
    f->source = object(f, SOURCE_Z, 3, 2);
    memset(f->b.working.surface.pixels, 0x55, f->b.working.bytes);
    for (y = 0; y < 2; ++y)
        for (x = 0; x < 3; ++x)
        {
            f->bitmap->pixels[y * f->bitmap->row + x] = (unsigned char)(0x10 + y * 3 + x);
            f->source->pixels[y * f->source->row + x * 2] = (unsigned char)(0x20 + x);
            f->source->pixels[y * f->source->row + x * 2 + 1] = (unsigned char)(0x80 + y);
        }
    f->bitmap->mask = mm_masked_owner_new(f->bitmap->pixels, f->bitmap->row, f->bitmap->height, &rect,
                                         2, -3, 0, 0, 0, NULL);
    assert(f->bitmap->mask);
    f->b.width = 3;
    f->b.height = 2;
    f->b.buffer_width = 2;
    f->b.buffer_height = 1;
    f->b.half_x = f->b.half_y = 1;
    f->b.scale = 4;
    f->b.owners[MM_WORLD_CACHE] = &f->cache;
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
    f->s.release_palette = release;
    f->s.bounds_callback = bounds_callback;
    f->s.raster = raster;
    f->s.write = write;
    b = &f->s.buffers;
    b->context = f;
    b->release_depth = release;
    b->release_bitmap = release;
    b->fetch = fetch;
    b->depth_bitmap = depth_bitmap;
    b->masked_bitmap = masked_bitmap;
    r = &f->s.render;
    r->context = f;
    mm_render_frame_borrow(&r->frame, &f->root, &f->model, NULL, NULL);
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
    assert(f->live == 2 && !f->material_live && f->bounds);
    assert(f->bitmap->refs == 1 && f->source->refs == 1);
    assert(f->material.texture == f->original.texture && f->material.flags == 20 && f->material.base == 11 &&
           f->material.range == 100);
    mm_pixel_buffer_reset(&f->b.working);
    release(f, f->bitmap);
    release(f, f->source);
    assert(!f->live);
}
static void transactions(void)
{
    Fixture f;
    MMWorldBuffers before;
    int fail, scale, scenario;
    size_t i;
    MMWorldPixels working;
    for (scale = 1; scale <= 16; ++scale)
    {
        init(&f);
        before = f.b;
        assert(mm_world_export_scaled(&f.b, scale, &f.s));
        assert(f.rasters == 1 && f.writes == 1 && f.width == 3 * scale && f.height == 2 * scale);
        assert(f.phase == 5 && f.options == 2 && !memcmp(&before, &f.b, sizeof(before)));
        working = mm_world_buffer_pixels(&f.b.working);
        for (i = 0; i < working.bytes; ++i)
            assert(working.pixels[i] == 0x55);
        assert(f.events[f.n - 1] == 1000 + PALETTE);
        balanced(&f);
    }
    for (fail = 1; fail <= 5; ++fail)
    {
        init(&f);
        f.fail = fail;
        assert(!mm_world_export_scaled(&f.b, 2, &f.s));
        assert(f.phase == fail && f.rasters == (fail >= 4) && f.writes == (fail == 5));
        balanced(&f);
    }
    for (fail = 0; fail <= 2; ++fail)
    {
        init(&f);
        f.fail = fail;
        f.current = 1;
        assert(mm_world_export_current(&f.b, &f.s) == (fail == 0));
        assert(!f.rasters && !f.options && (fail || (f.width == 2 && f.height == 1)));
        assert(f.phase == (fail == 1 ? 1 : 2));
        balanced(&f);
    }
    init(&f);
    f.current = 1;
    --f.b.working.bytes;
    assert(!mm_world_export_current(&f.b, &f.s) && f.phase == 1 && !f.writes);
    balanced(&f);
    for (scenario = 0; scenario < 5; ++scenario)
    {
        init(&f);
        if (scenario == 0)
            f.b.owners[MM_WORLD_CACHE] = NULL;
        if (scenario == 1)
            f.short_source = 1;
        if (scenario == 2)
            f.missing_colour = 1;
        if (scenario == 3)
            f.missing_depth = 1;
        if (scenario == 4)
        {
            f.late = 1;
            f.flat = f.no_texture = 0;
        }
        assert(mm_world_export_scaled(&f.b, 3, &f.s) == (scenario == 0 || scenario == 4));
        balanced(&f);
    }
    init(&f);
    assert(!mm_world_export_scaled(&f.b, 17, &f.s));
    assert(!f.phase);
    balanced(&f);
}
static void dimensions(void)
{
    MMWorldBuffers b;
    MMI32 scale, width, height;
    int request;
    memset(&b, 0, sizeof(b));
    b.width = 640;
    b.height = 480;
    for (request = 1; request <= 16; ++request)
    {
        assert(mm_world_export_size(&b, request, &scale, &width, &height));
        assert(scale == (request > 6 ? 6 : request) && width == 640 * scale && height == 480 * scale);
    }
    scale = width = height = 123;
    assert(!mm_world_export_size(&b, 0, &scale, &width, &height));
    assert(!mm_world_export_size(&b, 17, &scale, &width, &height));
    b.height = 4097;
    assert(!mm_world_export_size(&b, 1, &scale, &width, &height));
    assert(scale == 123 && width == 123 && height == 123);
    b.height = b.width = 4096;
    assert(mm_world_export_size(&b, 16, &scale, &width, &height) && scale == 1 && width == 4096 &&
           height == 4096);
}
static void depth_copy(void)
{
    unsigned char src[16], dst[3200];
    MMDepthBitmap source = {0}, destination = {0};
    int scale, x, y, row, height;
    memset(src, 0xcc, sizeof(src));
    for (y = 0; y < 2; ++y)
        for (x = 0; x < 3; ++x)
        {
            src[y * 8 + x * 2] = (unsigned char)(x + 1);
            src[y * 8 + x * 2 + 1] = (unsigned char)(y + 11);
        }
    source.pixels = src;
    source.row_bytes = 8;
    source.bytes = 16;
    destination.pixels = dst;
    for (scale = 1; scale <= 16; ++scale)
    {
        row = 3 * scale * 2 + 2;
        height = 2 * scale;
        memset(dst, 0xcc, sizeof(dst));
        destination.row_bytes = row;
        destination.bytes = row * height;
        assert(mm_world_depth_resample(&source, &destination, 3 * scale, height, scale, 0, 0));
        for (y = 0; y < height; ++y)
            for (x = 0; x < row; ++x)
                assert(dst[y * row + x] == (x < row - 2 ? src[(y / scale) * 8 + (x / 2 / scale) * 2 + x % 2] : 0xcc));
        assert(dst[row * height] == 0xcc);
    }
    source.bytes = 13;
    assert(!mm_world_depth_resample(&source, &destination, 48, 32, 16, 0, 0));
    assert(dst[3136] == 0xcc);
    assert(!mm_world_depth_resample(&source, &destination, 48, 32, 17, 0, 0));
}

static void depth_placement(void)
{
    unsigned char src[32], dst[40];
    MMDepthBitmap source = {{1, 1, 4, 4}, 8, 16, NULL};
    MMDepthBitmap target = {{0, 0, 4, 4}, 10, 40, NULL};
    int x, y;

    for (x = 0; x < (int)sizeof(src); ++x)
        src[x] = (unsigned char)(x + 1);
    memset(dst, 0xff, sizeof(dst));
    source.pixels = src;
    target.pixels = dst;
    assert(mm_world_draw_depth(&source, &target, 4, 4));
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 10; ++x)
            assert(dst[y * 10 + x] == (y == 1 && x >= 2 && x < 8 ? src[y * 8 + x] : 0xff));
    target.bytes = 39;
    memset(dst, 0xff, sizeof(dst));
    assert(!mm_world_draw_depth(&source, &target, 4, 4));
    for (x = 0; x < (int)sizeof(dst); ++x)
        assert(dst[x] == 0xff);
}

int main(void)
{
    dimensions();
    depth_copy();
    depth_placement();
    transactions();
    puts("C89 World export: scaling, palette/material ownership and transaction failures passed.");
    return 0;
}
