#include "three_mm/world_render.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WORK THREE_MM_WORLD_DIRTY_WORKING
#define SCREEN THREE_MM_WORLD_DIRTY_SCREEN
#define BG THREE_MM_WORLD_RGB_BACKGROUND
#define RGB THREE_MM_WORLD_RGB_WORKING
typedef struct Actor
{
    struct Actor *child, *next;
    int id, type, style;
} Actor;
typedef struct Fixture
{
    ThreeMMWorldRender r;
    ThreeMMWorldBuffers b;
    ThreeMMWorldRenderServices s;
    ThreeMMWorldMaterial m[4], original[4];
    Actor a[7];
    unsigned char background[64], working[64];
    int events[512], n, live, allocations, fail_alloc, fail_lock, invalid_pixels, bad_span;
    int empty, spans, scanner_live, wire, flat, no_texture, begin_options, rect_enabled, rect_disable, rect_overflow;
    int change_in_render, scan_y, scan[4], draw_source[4], draw_destination[4], scale[4], offset[2];
    size_t reported;
} Fixture;
static void event(Fixture *f, int e)
{
    assert(f->n < 512);
    f->events[f->n++] = e;
}
static int position(Fixture *f, int e)
{
    int i;
    for (i = 0; i < f->n; ++i)
        if (f->events[i] == e)
            return i;
    return -1;
}
static int equal_material(const ThreeMMWorldMaterial *a, const ThreeMMWorldMaterial *b)
{
    return a->object == b->object && a->texture == b->texture && a->flags == b->flags && a->base == b->base &&
           a->range == b->range;
}
static size_t count(void *p)
{
    return ((Fixture *)p)->reported;
}
static void enumerate(void *p, int (*visit)(void *, void *), void *argument)
{
    Fixture *f = (Fixture *)p;
    int i;
    event(f, 9);
    for (i = 0; i < 4; ++i)
        if (visit(argument, &f->m[i]))
            break;
}
static void read_material(void *p, void *object, ThreeMMWorldMaterial *out)
{
    (void)p;
    *out = *(ThreeMMWorldMaterial *)object;
}
static void write_material(void *p, const ThreeMMWorldMaterial *in)
{
    Fixture *f = (Fixture *)p;
    ThreeMMWorldMaterial *out = (ThreeMMWorldMaterial *)in->object;
    event(f, 200 + (int)(out - f->m));
    *out = *in;
}
static void *allocate(void *p, size_t bytes)
{
    Fixture *f = (Fixture *)p;
    void *result;
    ++f->allocations;
    if (f->fail_alloc)
        return NULL;
    result = malloc(bytes);
    assert(result);
    ++f->live;
    return result;
}
static void free_memory(void *p, void *memory)
{
    Fixture *f = (Fixture *)p;
    assert(memory);
    --f->live;
    free(memory);
}
static void *child(void *p, void *actor)
{
    (void)p;
    return ((Actor *)actor)->child;
}
static void *next(void *p, void *actor)
{
    (void)p;
    return ((Actor *)actor)->next;
}
static int type(void *p, void *actor)
{
    (void)p;
    return ((Actor *)actor)->type;
}
static int style(void *p, void *actor)
{
    (void)p;
    return ((Actor *)actor)->style;
}
static void set_style(void *p, void *actor, int value)
{
    Actor *a = (Actor *)actor;
    a->style = value;
    event((Fixture *)p, 40 + a->id);
}
static int has(void *p, int callback)
{
    return callback == THREE_MM_WORLD_BEGIN || ((Fixture *)p)->rect_enabled;
}
static void begin(void *p, void *actor)
{
    Fixture *f = (Fixture *)p;
    event(f, 100 + ((Actor *)actor)->id);
    if (f->begin_options)
        f->wire = f->flat = f->no_texture = 1;
}
static void rect(void *p, void *actor, ThreeMMI32 out[4])
{
    Fixture *f = (Fixture *)p;
    event(f, 150 + ((Actor *)actor)->id);
    out[0] = -1;
    out[1] = 1;
    out[2] = 3;
    out[3] = 2;
    if (f->rect_disable)
        f->rect_enabled = 0;
    if (f->rect_overflow)
        out[2] = 2147483647;
}
static void options(void *p, int *wire, int *flat, int *no_texture)
{
    Fixture *f = (Fixture *)p;
    *wire = f->wire;
    *flat = f->flat;
    *no_texture = f->no_texture;
}
static int bounds(void *p, int region, ThreeMMI32 out[4])
{
    Fixture *f = (Fixture *)p;
    assert(region == WORK);
    out[0] = -3;
    out[1] = 1;
    out[2] = 6;
    out[3] = 9;
    return !f->empty;
}
static void clear(void *p, int region)
{
    event((Fixture *)p, region == WORK ? 31 : 93);
}
static void region_union(void *p, int destination, int source)
{
    assert(destination == SCREEN && source == WORK);
    assert(!((Fixture *)p)->scanner_live);
    event((Fixture *)p, 30);
}
static void add(void *p, int region, const ThreeMMI32 r[4])
{
    Fixture *f = (Fixture *)p;
    assert(region == WORK && r[0] == -f->b.scale && r[2] == 3 * f->b.scale);
    event(f, 32);
}
static void depth(void *p, const ThreeMMI32 r[4])
{
    assert(r[0] == 0 && r[1] == 1 && r[2] == 6 && r[3] == 4);
    event((Fixture *)p, 10);
}
static int lock(void *p, int kind, ThreeMMWorldPixels *out)
{
    Fixture *f = (Fixture *)p;
    event(f, kind == BG ? 20 : 21);
    if (f->fail_lock == kind)
        return 0;
    out->pixels = kind == BG ? f->background : f->working;
    out->row = kind == BG ? 8 : 9;
    out->bytes = f->invalid_pixels ? 1 : 64;
    return 1;
}
static void unlock(void *p, int kind)
{
    event((Fixture *)p, kind == BG ? 23 : 22);
}
static void scan_init(void *p, const ThreeMMI32 r[4])
{
    Fixture *f = (Fixture *)p;
    assert(!f->scanner_live++);
    memcpy(f->scan, r, sizeof(f->scan));
    f->scan_y = r[1];
    f->spans = 0;
    event(f, 11);
}
static int scan_span(void *p, ThreeMMI32 *left, ThreeMMI32 *right)
{
    Fixture *f = (Fixture *)p;
    if (f->spans++)
        return 0;
    *left = f->bad_span ? -1 : 1;
    *right = 3;
    return 1;
}
static void scan_next(void *p)
{
    Fixture *f = (Fixture *)p;
    ++f->scan_y;
    f->spans = 0;
}
static void scan_end(void *p)
{
    Fixture *f = (Fixture *)p;
    assert(f->scanner_live-- == 1);
    event(f, 24);
}
static void render(void *p)
{
    Fixture *f = (Fixture *)p;
    event(f, 50);
    if (f->change_in_render)
        three_mm_world_render_dirty(&f->r);
}
static void publish(void *p, const ThreeMMWorldRender *r)
{
    assert(!r->changed);
    event((Fixture *)p, 60);
}
static void snapshot_depth(void *p)
{
    event((Fixture *)p, 80);
}
static void detach(void *p)
{
    event((Fixture *)p, 81);
}
static void snapshot_rgb(void *p)
{
    event((Fixture *)p, 82);
}
static void flush(void *p)
{
    event((Fixture *)p, 83);
}
static void restore(void *p)
{
    event((Fixture *)p, 84);
}
static void scale(void *p, ThreeMMI32 nx, ThreeMMI32 dx, ThreeMMI32 ny, ThreeMMI32 dy)
{
    Fixture *f = (Fixture *)p;
    f->scale[0] = nx;
    f->scale[1] = dx;
    f->scale[2] = ny;
    f->scale[3] = dy;
    event(f, 90);
}
static void offset(void *p, ThreeMMI32 x, ThreeMMI32 y)
{
    Fixture *f = (Fixture *)p;
    f->offset[0] = x;
    f->offset[1] = y;
    event(f, 91);
}
static void mark(void *p)
{
    event((Fixture *)p, 92);
}
static void draw(void *p, const ThreeMMI32 src[4], const ThreeMMI32 dst[4])
{
    Fixture *f = (Fixture *)p;
    memcpy(f->draw_source, src, sizeof(f->draw_source));
    memcpy(f->draw_destination, dst, sizeof(f->draw_destination));
    event(f, 94);
}
static void init(Fixture *f)
{
    ThreeMMWorldRenderServices *s;
    ThreeMMWorldMaterialServices *m;
    int i;
    memset(f, 0, sizeof(*f));
    f->b.width = 12;
    f->b.height = 8;
    f->b.buffer_width = 6;
    f->b.buffer_height = 4;
    f->b.scale = 1;
    f->rect_enabled = 1;
    f->reported = 4;
    f->fail_lock = -1;
    for (i = 0; i < 64; ++i)
    {
        f->background[i] = (unsigned char)(i + 1);
        f->working[i] = 0xcc;
    }
    for (i = 0; i < 4; ++i)
    {
        f->m[i].object = &f->m[i];
        f->m[i].texture = i < 2 ? f->background : NULL;
        f->m[i].flags = i % 2 ? 16 : 20;
        f->m[i].base = 11;
        f->m[i].range = 100;
        f->original[i] = f->m[i];
    }
    for (i = 0; i < 7; ++i)
    {
        f->a[i].id = i;
        f->a[i].style = 99;
        f->a[i].type = i >= 3;
    }
    f->a[0].child = &f->a[1];
    f->a[1].next = &f->a[2];
    f->a[2].next = &f->a[3];
    f->a[1].child = &f->a[4];
    f->a[4].next = &f->a[5];
    f->a[5].child = &f->a[6];
    f->a[5].style = 10;
    s = &f->s;
    s->context = f;
    s->root = &f->a[0];
    s->actor_none = 0;
    s->actor_model = 1;
    s->faces = 8;
    s->edges = 7;
    s->bounding_points = 10;
    s->bounding_edges = 11;
    s->bounding_faces = 12;
    s->child = child;
    s->next = next;
    s->type = type;
    s->style = style;
    s->set_style = set_style;
    s->has = has;
    s->begin = begin;
    s->rect = rect;
    s->options = options;
    s->region_bounds = bounds;
    s->region_clear = clear;
    s->region_union = region_union;
    s->region_add = add;
    s->depth_clean = depth;
    s->lock = lock;
    s->unlock = unlock;
    s->scan_init = scan_init;
    s->scan_span = scan_span;
    s->scan_next = scan_next;
    s->scan_end = scan_end;
    s->render = render;
    s->publish = publish;
    s->snapshot_depth = snapshot_depth;
    s->detach_depth = detach;
    s->snapshot_rgb = snapshot_rgb;
    s->flush = flush;
    s->restore_background = restore;
    s->screen_scale = scale;
    s->screen_offset = offset;
    s->screen_mark = mark;
    s->draw = draw;
    m = &s->materials;
    m->context = f;
    m->gouraud = 4;
    m->count = count;
    m->enumerate = enumerate;
    m->read = read_material;
    m->write = write_material;
    m->allocate = allocate;
    m->free = free_memory;
    three_mm_world_render_init(&f->r);
}
static void materials_tests(void)
{
    Fixture f;
    ThreeMMWorldMaterials scope;
    int option, i, n;
    for (option = 0; option < 4; ++option)
    {
        init(&f);
        assert(three_mm_world_materials_begin(&scope, option & 1, option & 2, &f.s.materials));
        for (i = 0; i < 4; ++i)
        {
            int remove = (option & 2) && i < 2;
            int flatten = (option & 1) && (i >= 2 || remove) && !(i % 2);
            assert(f.m[i].texture == (remove ? NULL : f.original[i].texture));
            assert(f.m[i].flags == (flatten ? 16U : f.original[i].flags));
            assert(f.m[i].base == (remove ? 15 : 11));
            assert(f.m[i].range == (remove ? 15 : 100));
        }
        n = f.n;
        three_mm_world_materials_end(&scope, &f.s.materials);
        for (i = 0; i < 4; ++i)
            assert(equal_material(&f.m[i], &f.original[i]));
        assert(!f.live && (option || !f.allocations));
        for (i = n + 1; i < f.n; ++i)
            assert(f.events[i] > f.events[i - 1]);
        n = f.n;
        three_mm_world_materials_end(&scope, &f.s.materials);
        assert(f.n == n);
    }
    init(&f);
    f.fail_alloc = 1;
    assert(!three_mm_world_materials_begin(&scope, 1, 1, &f.s.materials));
    assert(!f.n && !f.live);
    init(&f);
    f.reported = (size_t)-1;
    assert(!three_mm_world_materials_begin(&scope, 1, 1, &f.s.materials));
    assert(!f.allocations);
    init(&f);
    f.reported = 1;
    assert(!three_mm_world_materials_begin(&scope, 1, 1, &f.s.materials));
    for (i = 0; i < 4; ++i)
        assert(equal_material(&f.m[i], &f.original[i]));
    assert(!f.live && f.n == 3 && f.events[1] == 200 && f.events[2] == 200);
}
static void clean_tests(void)
{
    Fixture f;
    int x, y, scenario;
    init(&f);
    assert(three_mm_world_render_clean(&f.b, &f.s));
    for (y = 0; y < 7; ++y)
        for (x = 0; x < 9 && y * 9 + x < 64; ++x)
            assert(f.working[y * 9 + x] == (y >= 1 && y < 4 && x >= 1 && x < 3 ? f.background[y * 8 + x] : 0xcc));
    assert(f.scan_y == 4 && !f.scanner_live && f.events[0] == 10 && f.events[f.n - 3] == 22 &&
           f.events[f.n - 2] == 23 && f.events[f.n - 1] == 24);
    for (scenario = 0; scenario < 5; ++scenario)
    {
        init(&f);
        if (scenario == 0)
            f.empty = 1;
        if (scenario == 1)
            f.fail_lock = BG;
        if (scenario == 2)
            f.fail_lock = RGB;
        if (scenario == 3)
            f.invalid_pixels = 1;
        if (scenario == 4)
            f.bad_span = 1;
        assert(three_mm_world_render_clean(&f.b, &f.s) == (scenario == 0));
        if (scenario == 0)
            assert(!f.n);
        if (scenario == 1)
            assert(position(&f, 21) < 0 && position(&f, 23) < 0);
        if (scenario == 2)
            assert(position(&f, 22) < 0 && f.events[f.n - 2] == 23 && f.events[f.n - 1] == 24);
        if (scenario >= 3)
            assert(f.events[f.n - 3] == 22 && f.events[f.n - 2] == 23 && f.events[f.n - 1] == 24);
        assert(!f.scanner_live);
        for (x = 0; x < 64; ++x)
            assert(f.working[x] == 0xcc);
    }
}
static void render_tests(void)
{
    Fixture f;
    int bound_style, i;
    init(&f);
    assert(three_mm_world_render(&f.r, &f.b, &f.s));
    assert(!f.n);
    for (bound_style = 10; bound_style <= 12; ++bound_style)
    {
        init(&f);
        f.a[5].style = bound_style;
        f.begin_options = 1;
        f.change_in_render = 1;
        three_mm_world_render_dirty(&f.r);
        assert(three_mm_world_render(&f.r, &f.b, &f.s));
        assert(!f.r.changed && !f.live);
        assert(f.a[0].style == 99 && f.a[5].style == bound_style && f.a[6].style == 7 && f.a[3].style == 7);
        assert(f.events[0] == 101 && f.events[1] == 102);
        assert(position(&f, 31) > position(&f, 30) && position(&f, 44) > position(&f, 31));
        assert(position(&f, 200) < position(&f, 50) && position(&f, 151) > position(&f, 50));
        i = position(&f, 50);
        assert(f.events[i + 1] == 200 && f.events[i + 2] == 201 && f.events[i + 3] == 202);
        assert(f.events[f.n - 2] == 30 && f.events[f.n - 1] == 60);
        for (i = 0; i < 4; ++i)
            assert(equal_material(&f.m[i], &f.original[i]));
        f.n = 0;
        assert(three_mm_world_render(&f.r, &f.b, &f.s));
        assert(!f.n);
    }
    init(&f);
    f.b.scale = 4;
    f.rect_disable = 1;
    three_mm_world_render_dirty(&f.r);
    assert(three_mm_world_render(&f.r, &f.b, &f.s));
    assert(position(&f, 151) >= 0 && position(&f, 152) < 0);
    init(&f);
    f.fail_alloc = f.flat = 1;
    three_mm_world_render_dirty(&f.r);
    assert(!three_mm_world_prerender(&f.r, &f.b, &f.s));
    assert(f.r.changed && position(&f, 50) < 0 && position(&f, 80) < 0);
    f.fail_alloc = 0;
    f.n = 0;
    assert(three_mm_world_prerender(&f.r, &f.b, &f.s));
    assert(!f.r.changed);
    assert(f.events[f.n - 4] == 80 && f.events[f.n - 3] == 81 && f.events[f.n - 2] == 82 && f.events[f.n - 1] == 83);
    init(&f);
    f.b.scale = 4;
    f.rect_overflow = 1;
    three_mm_world_render_dirty(&f.r);
    assert(!three_mm_world_render(&f.r, &f.b, &f.s));
    assert(f.r.changed && !f.live && position(&f, 60) < 0);
}
static void output_tests(void)
{
    Fixture f;
    int mode;
    for (mode = 0; mode < 6; ++mode)
    {
        init(&f);
        f.b.half_x = mode & 1;
        f.b.half_y = mode & 2;
        f.b.scale = mode < 4 ? 1 : mode == 4 ? 2 : 4;
        three_mm_world_render_mark(&f.b, -3, 4, &f.s);
        assert(f.n == 4 && f.events[0] == 90 && f.events[1] == 91 && f.events[2] == 92 && f.events[3] == 93);
        assert(f.scale[0] == (mode < 4 && f.b.half_x ? 2 : 1));
        assert(f.scale[1] == f.b.scale && f.scale[3] == f.b.scale);
        assert(f.scale[2] == (mode < 4 && f.b.half_y ? 2 : 1));
        assert(f.offset[0] == -3 && f.offset[1] == 4);
        assert(three_mm_world_render_draw(&f.b, 2, -1, &f.s));
        assert(f.draw_source[2] == 6 && f.draw_source[3] == 4 && f.draw_destination[0] == -2 &&
               f.draw_destination[1] == 1 && f.draw_destination[2] == 10 && f.draw_destination[3] == 9);
        f.n = 0;
        assert(!three_mm_world_render_draw(&f.b, (-2147483647 - 1), 0, &f.s));
        assert(!f.n);
        assert(!three_mm_world_render_draw(&f.b, -2147483647, 0, &f.s));
        assert(!f.n);
        three_mm_world_unprerender(&f.s);
        assert(f.n == 1 && f.events[0] == 84);
    }
}
int main(void)
{
    materials_tests();
    clean_tests();
    render_tests();
    output_tests();
    puts("C89 World render: materials, sequencing, spans, failure cleanup and output passed.");
    return 0;
}
