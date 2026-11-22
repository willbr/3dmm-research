#include "mm/world_render.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Actor
{
    struct Actor *child, *next;
    int id, type, style;
} Actor;
typedef struct Fixture
{
    MMWorldRender r;
    MMWorldBuffers b;
    MMWorldRenderServices s;
    MMWorldMaterial m[4], original[4];
    Actor a[7];
    MMBodyPartBinding bindings[2][2];
    unsigned char background[64], working[64], *z_background, *z_working;
    MMPixelRect marked;
    int events[512], n, live, allocations, fail_alloc;
    int wire, flat, no_texture, begin_options, rect_enabled, rect_disable, rect_overflow;
    int change_in_render, draw_destination[4];
    int body_refs, prepared;
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
static int equal_material(const MMWorldMaterial *a, const MMWorldMaterial *b)
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
static void read_material(void *p, void *object, MMWorldMaterial *out)
{
    (void)p;
    *out = *(MMWorldMaterial *)object;
}
static void write_material(void *p, const MMWorldMaterial *in)
{
    Fixture *f = (Fixture *)p;
    MMWorldMaterial *out = (MMWorldMaterial *)in->object;
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
    return callback == MM_WORLD_BEGIN || ((Fixture *)p)->rect_enabled;
}
static void begin(void *p, void *actor)
{
    Fixture *f = (Fixture *)p;
    event(f, 100 + ((Actor *)actor)->id);
    if (f->begin_options)
        f->wire = f->flat = f->no_texture = 1;
}
static void rect(void *p, void *actor, MMI32 out[4])
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
static int body(void *p, void *actor, MMRenderBody *out)
{
    Fixture *f = (Fixture *)p;
    Actor *root = (Actor *)actor;
    assert(root->id == 1 || root->id == 2);
    out->target = root;
    out->resource = root;
    out->draw.bindings = f->bindings[root->id - 1];
    return 1;
}
static void retain_body(void *p, void *resource)
{
    Fixture *f = (Fixture *)p;
    assert(resource == &f->a[f->body_refs + 1]);
    ++f->body_refs;
}
static void release_body(void *p, void *resource)
{
    Fixture *f = (Fixture *)p;
    assert(f->body_refs > 0 && resource == &f->a[3 - f->body_refs]);
    --f->body_refs;
}
static void prepare_body(void *p, const MMRenderBody *entry)
{
    Fixture *f = (Fixture *)p;
    assert(f->body_refs == 2 && entry->target == &f->a[f->prepared + 1]);
    assert(entry->draw.bindings == f->bindings[f->prepared]);
    ++f->prepared;
}
static void render(void *p, const MMRenderFrame *frame)
{
    Fixture *f = (Fixture *)p;
    const MMRenderBody *bodies;
    assert(frame->world == &f->a[0] && frame->camera == &f->a[6] &&
           frame->colour == f->working && frame->depth == f->background);
    assert(f->z_working[14] == f->z_background[14] && f->working[11] == f->background[10]);
    assert(frame->scene && frame->scene->camera.matrix[9] == 1234 && frame->scene->camera.fov == 789);
    assert(frame->scene->bodies.count == (f->s.body ? 2U : 0U));
    if (f->s.body)
    {
        bodies = (const MMRenderBody *)frame->scene->storage.buffers[0].data;
        assert(f->body_refs == 2 && f->prepared == 2 &&
               bodies[0].target == &f->a[1] && bodies[1].target == &f->a[2]);
    }
    event(f, 50);
    if (f->change_in_render)
        mm_world_render_dirty(&f->r);
}
static void flush(void *p)
{
    event((Fixture *)p, 83);
}
static void mark(void *p, const MMRegion *region)
{
    Fixture *f = (Fixture *)p;
    f->marked = region->bounds;
    event(f, 92);
}
static void draw(void *p, const MMPixelSurface *src, const MMI32 dst[4])
{
    Fixture *f = (Fixture *)p;
    assert(src == &f->b.working.surface && src->pixels == f->working && src->width == 6 && src->height == 4 &&
           src->pitch == 9 && src->depth == 8);
    memcpy(f->draw_destination, dst, sizeof(f->draw_destination));
    event(f, 94);
}
static void init(Fixture *f)
{
    MMWorldRenderServices *s;
    MMWorldMaterialServices *m;
    int i;
    const MMPixelRect dirty = {-3, 1, 6, 9};
    memset(f, 0, sizeof(*f));
    f->b.width = 12;
    f->b.height = 8;
    f->b.buffer_width = 6;
    f->b.buffer_height = 4;
    f->b.scale = 1;
    mm_region_set_rect(&f->b.dirty_working, &dirty);
    f->b.background.surface.pixels = f->background;
    f->b.background.surface.width = 6;
    f->b.background.surface.height = 4;
    f->b.background.surface.pitch = 8;
    f->b.background.surface.depth = 8;
    f->b.background.bytes = sizeof(f->background);
    f->b.working = f->b.background;
    f->b.working.surface.pixels = f->working;
    f->b.working.surface.pitch = 9;
    f->rect_enabled = 1;
    f->reported = 4;
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
    mm_render_frame_borrow(&s->frame, &f->a[0], &f->a[6], f->working, f->background);
    s->camera.matrix[9] = 1234;
    s->camera.fov = 789;
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
    s->render = render;
    s->flush = flush;
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
    mm_world_render_init(&f->r);
}
static void materials_tests(void)
{
    Fixture f;
    MMWorldMaterials scope;
    int option, i, n;
    for (option = 0; option < 4; ++option)
    {
        init(&f);
        assert(mm_world_materials_begin(&scope, option & 1, option & 2, &f.s.materials));
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
        mm_world_materials_end(&scope, &f.s.materials);
        for (i = 0; i < 4; ++i)
            assert(equal_material(&f.m[i], &f.original[i]));
        assert(!f.live && (option || !f.allocations));
        for (i = n + 1; i < f.n; ++i)
            assert(f.events[i] > f.events[i - 1]);
        n = f.n;
        mm_world_materials_end(&scope, &f.s.materials);
        assert(f.n == n);
    }
    init(&f);
    f.fail_alloc = 1;
    assert(!mm_world_materials_begin(&scope, 1, 1, &f.s.materials));
    assert(!f.n && !f.live);
    init(&f);
    f.reported = (size_t)-1;
    assert(!mm_world_materials_begin(&scope, 1, 1, &f.s.materials));
    assert(!f.allocations);
    init(&f);
    f.reported = 1;
    assert(!mm_world_materials_begin(&scope, 1, 1, &f.s.materials));
    for (i = 0; i < 4; ++i)
        assert(equal_material(&f.m[i], &f.original[i]));
    assert(!f.live && f.n == 3 && f.events[1] == 200 && f.events[2] == 200);
}
static void init_depth(Fixture *f)
{
    const MMDepthBitmap *working, *background;
    f->b.depth_background = mm_depth_owner_new(6, 4, NULL, NULL);
    f->b.depth_working = mm_depth_owner_new(6, 4, NULL, NULL);
    assert(f->b.depth_background && f->b.depth_working);
    working = mm_depth_owner_bitmap(f->b.depth_working);
    background = mm_depth_owner_bitmap(f->b.depth_background);
    f->z_background = (unsigned char *)background->pixels;
    f->z_working = (unsigned char *)working->pixels;
    memset(f->z_background, 0x52, (size_t)background->bytes);
    memset(f->z_working, 0xcc, (size_t)working->bytes);
    f->b.z.height = 4;
    f->b.z.row = working->row_bytes;
    f->b.z.pixels = working->pixels;
}
static void dispose_buffers(Fixture *f)
{
    mm_depth_owner_release(f->b.depth_working);
    mm_depth_owner_release(f->b.depth_background);
    mm_region_set_rect(&f->b.dirty_working, NULL);
    mm_region_set_rect(&f->b.dirty_screen, NULL);
}
static void clean_tests(void)
{
    Fixture f;
    const MMPixelRect left_gap = {-3, 1, 1, 4}, right_gap = {3, 1, 6, 4};
    int x, y, scenario;
    init(&f);
    init_depth(&f);
    assert(mm_region_apply_rect(&f.b.dirty_working, MM_REGION_DIFFERENCE, &left_gap, NULL, 0, NULL));
    assert(mm_region_apply_rect(&f.b.dirty_working, MM_REGION_DIFFERENCE, &right_gap, NULL, 0, NULL));
    assert(mm_world_render_clean(&f.b));
    for (y = 0; y < 7; ++y)
        for (x = 0; x < 9 && y * 9 + x < 64; ++x)
            assert(f.working[y * 9 + x] == (y >= 1 && y < 4 && x >= 1 && x < 3 ? f.background[y * 8 + x] : 0xcc));
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 12; ++x)
            assert(f.z_working[y * 12 + x] == (y >= 1 && x >= 2 && x < 6 ? 0x52 : 0xcc));
    assert(!f.n);
    dispose_buffers(&f);
    for (scenario = 0; scenario < 4; ++scenario)
    {
        init(&f);
        init_depth(&f);
        if (scenario == 0)
            mm_region_set_rect(&f.b.dirty_working, NULL);
        if (scenario == 1)
            f.b.background.bytes = 1;
        if (scenario == 2)
            f.b.working.surface.pixels = NULL;
        if (scenario == 3)
            f.b.working.bytes = 1;
        assert(mm_world_render_clean(&f.b) == (scenario == 0));
        assert(!f.n);
        for (x = 0; x < 64; ++x)
            assert(f.working[x] == 0xcc);
        dispose_buffers(&f);
    }
}
static void render_tests(void)
{
    Fixture f;
    int bound_style, i;
    init(&f);
    init_depth(&f);
    assert(mm_world_render(&f.r, &f.b, &f.s));
    assert(!f.n);
    dispose_buffers(&f);
    for (bound_style = 10; bound_style <= 12; ++bound_style)
    {
        init(&f);
        init_depth(&f);
        f.a[5].style = bound_style;
        f.begin_options = 1;
        f.change_in_render = 1;
        mm_world_render_dirty(&f.r);
        assert(mm_world_render(&f.r, &f.b, &f.s));
        assert(!f.r.changed && !f.live);
        assert(f.a[0].style == 99 && f.a[5].style == bound_style && f.a[6].style == 7 && f.a[3].style == 7);
        assert(f.events[0] == 101 && f.events[1] == 102);
        assert(position(&f, 200) < position(&f, 50) && position(&f, 151) > position(&f, 50));
        i = position(&f, 50);
        assert(f.events[i + 1] == 200 && f.events[i + 2] == 201 && f.events[i + 3] == 202);
        assert(f.b.dirty_working.bounds.left == -1 && f.b.dirty_working.bounds.right == 3);
        assert(f.b.dirty_screen.bounds.left == -3 && f.b.dirty_screen.bounds.bottom == 9);
        for (i = 0; i < 4; ++i)
            assert(equal_material(&f.m[i], &f.original[i]));
        f.n = 0;
        assert(mm_world_render(&f.r, &f.b, &f.s));
        assert(!f.n);
        dispose_buffers(&f);
    }
    init(&f);
    init_depth(&f);
    f.b.scale = 4;
    f.rect_disable = 1;
    mm_world_render_dirty(&f.r);
    assert(mm_world_render(&f.r, &f.b, &f.s));
    assert(position(&f, 151) >= 0 && position(&f, 152) < 0);
    assert(f.b.dirty_working.bounds.left == -4 && f.b.dirty_working.bounds.right == 12);
    dispose_buffers(&f);
    init(&f);
    init_depth(&f);
    f.fail_alloc = f.flat = 1;
    mm_world_render_dirty(&f.r);
    assert(!mm_world_prerender(&f.r, &f.b, &f.s));
    assert(f.r.changed && position(&f, 50) < 0 && position(&f, 83) < 0);
    f.fail_alloc = 0;
    f.n = 0;
    assert(mm_world_prerender(&f.r, &f.b, &f.s));
    assert(!f.r.changed);
    assert(f.events[f.n - 1] == 83 && !memcmp(f.z_background, f.z_working, 48));
    for (i = 0; i < 4; ++i)
    {
        assert(!memcmp(f.background + i * 8, f.working + i * 9, 6));
        assert(f.background[i * 8 + 6] == i * 8 + 7 && f.background[i * 8 + 7] == i * 8 + 8);
    }
    f.n = 0;
    f.working[0] = 0x99;
    f.b.working.bytes = 1;
    assert(!mm_world_prerender(&f.r, &f.b, &f.s));
    assert(f.n == 1 && f.events[0] == 83 && !memcmp(f.z_background, f.z_working, 48));
    assert(f.background[0] == 0xcc);
    dispose_buffers(&f);
    init(&f);
    init_depth(&f);
    f.b.scale = 4;
    f.rect_overflow = 1;
    mm_world_render_dirty(&f.r);
    assert(!mm_world_render(&f.r, &f.b, &f.s));
    assert(f.r.changed && !f.live);
    dispose_buffers(&f);
    for (i = 0; i < 2; ++i)
    {
        init(&f);
        init_depth(&f);
        f.s.body = body;
        f.s.retain_body = retain_body;
        f.s.release_body = release_body;
        f.s.prepare_body = prepare_body;
        if (i)
            f.b.background.bytes = 1;
        mm_world_render_dirty(&f.r);
        assert(mm_world_render(&f.r, &f.b, &f.s) == !i);
        assert(!f.body_refs && f.prepared == 2 && f.r.changed == i);
        dispose_buffers(&f);
    }
}
static void output_tests(void)
{
    Fixture f;
    int mode, w, h;
    const MMPixelRect screen = {0, 0, 8, 8};
    for (mode = 0; mode < 6; ++mode)
    {
        init(&f);
        f.b.half_x = mode & 1;
        f.b.half_y = mode & 2;
        f.b.scale = mode < 4 ? 1 : mode == 4 ? 2 : 4;
        mm_region_set_rect(&f.b.dirty_screen, &screen);
        mm_world_render_mark(&f.b, -3, 4, &f.s);
        w = (mode < 4 && f.b.half_x ? 16 : 8) / f.b.scale;
        h = (mode < 4 && f.b.half_y ? 16 : 8) / f.b.scale;
        assert(f.n == 1 && f.events[0] == 92 && mm_region_empty(&f.b.dirty_screen));
        assert(f.marked.left == -3 && f.marked.top == 4 && f.marked.right == w - 3 && f.marked.bottom == h + 4);
        assert(mm_world_render_draw(&f.b, 2, -1, &f.s));
        assert(f.draw_destination[0] == -2 && f.draw_destination[1] == 1 &&
               f.draw_destination[2] == 10 && f.draw_destination[3] == 9);
        f.n = 0;
        assert(!mm_world_render_draw(&f.b, (-2147483647 - 1), 0, &f.s));
        assert(!f.n);
        assert(!mm_world_render_draw(&f.b, -2147483647, 0, &f.s));
        assert(!f.n);
        f.b.working.bytes = 1;
        assert(!mm_world_render_draw(&f.b, 0, 0, &f.s) && !f.n);
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
