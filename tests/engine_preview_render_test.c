#include "three_mm/preview_render.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Preview C check: %d\n", __LINE__);                                                        \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
enum
{
    F_COLOUR = 1,
    F_DEPTH,
    F_WORLD,
    F_CAMERA,
    F_LIGHT,
    F_COPY,
    F_MODEL,
    F_MESH,
    F_BEGIN,
    F_RENDER,
    F_END,
    F_OUTPUT
};
typedef struct Node
{
    struct Node *first, *next;
    int model, id;
} Node;
typedef struct Host
{
    int fail, allocations, fail_allocation, live_memory, pixels, world, lights, sink, restores, edges, copies, owners;
    int background, with_body, with_model;
    ThreeMMU32 width, height;
    Node nodes[32], source[4];
    unsigned nodes_used;
    ThreeMMPreviewMaterial material;
    ThreeMMPreviewCamera camera;
    unsigned char colours[128 * 128], background_colours[128 * 128], rgba[128 * 128 * 4];
    unsigned short depths[128 * 128], background_depths[128 * 128];
    float distances[128 * 128];
    ThreeMMBackgroundLight light;
    int colour_token, depth_token;
    void *light_payload;
} Host;
static float fsin(float x)
{
    return (float)sin(x);
}
static float fcos(float x)
{
    return (float)cos(x);
}
static float ftan(float x)
{
    return (float)tan(x);
}
static float fsqrt(float x)
{
    return (float)sqrt(x);
}
static float fasin(float x)
{
    return (float)asin(x);
}
static float fatan2(float y, float x)
{
    return (float)atan2(y, x);
}
static float ffmod(float x, float y)
{
    return (float)fmod(x, y);
}
static float scalar_float(ThreeMMI32 x)
{
    return (float)x / 65536;
}
static ThreeMMI32 scalar(float x)
{
    return (ThreeMMI32)(x * 65536.f);
}
static ThreeMMI32 scalar_double(double x)
{
    return (ThreeMMI32)(x * 65536.f);
}
static unsigned short angle(float x)
{
    return (unsigned short)(x * 182);
}
static void *resize(void *v, void *old, ThreeMMU32 old_bytes, ThreeMMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    void *p;
    (void)old_bytes;
    (void)priority;
    ++h->allocations;
    if (h->fail_allocation && h->allocations >= h->fail_allocation)
        return NULL;
    p = realloc(old, bytes);
    if (p && !old)
        ++h->live_memory;
    return p;
}
static void release(void *v, void *p, ThreeMMU32 bytes)
{
    Host *h = (Host *)v;
    (void)bytes;
    CHECK(h->live_memory > 0);
    --h->live_memory;
    free(p);
}
static void *first(void *v, const void *n)
{
    (void)v;
    return ((const Node *)n)->first;
}
static void *next(void *v, const void *n)
{
    (void)v;
    return ((const Node *)n)->next;
}
static void *model(void *v, const void *n)
{
    (void)v;
    return ((const Node *)n)->model ? (void *)n : NULL;
}
static Node *node(Host *h, Node *parent)
{
    Node *n;
    CHECK(h->nodes_used < 32);
    n = h->nodes + h->nodes_used++;
    memset(n, 0, sizeof(*n));
    if (parent)
    {
        n->next = parent->first;
        parent->first = n;
    }
    return n;
}
static void *clone(void *v, void *parent, const void *source)
{
    Host *h = (Host *)v;
    const Node *old = (const Node *)source;
    Node *out;
    if (h->fail == F_COPY)
        return NULL;
    out = node(h, (Node *)parent);
    out->model = old->model;
    out->id = old->id;
    ++h->copies;
    return out;
}
static int owner(void *v, void *copy, ThreeMMI32 id)
{
    Host *h = (Host *)v;
    CHECK(((Node *)copy)->model);
    CHECK(id >= 3 && id <= 5);
    ++h->owners;
    return 1;
}
static int ready(void *v)
{
    (void)v;
    return 1;
}
static void describe(void *v, ThreeMMPreviewData *d)
{
    Host *h = (Host *)v;
    unsigned i;
    d->scene = 1;
    d->background = h->background;
    d->width = 128;
    d->height = 64;
    d->light_count = 1;
    d->lights = &h->light;
    d->colours = h->background_colours;
    d->depths = h->background_depths;
    for (i = 0; i < 256; ++i)
    {
        d->palette[i * 4] = (unsigned char)i;
        d->palette[i * 4 + 1] = (unsigned char)i;
        d->palette[i * 4 + 2] = (unsigned char)i;
        d->palette[i * 4 + 3] = 255;
    }
    d->camera.fov = angle(45);
    d->camera.near_z = scalar(.1f);
    d->camera.far_z = scalar(1000);
    d->camera.aspect = scalar(2);
    d->camera.matrix[0] = d->camera.matrix[4] = d->camera.matrix[8] = scalar(1);
    d->camera.matrix[11] = scalar(10);
    if (h->with_model)
    {
        d->model = h->source + 1;
        for (i = 0; i < 3; ++i)
        {
            d->model_low[i] = -1;
            d->model_high[i] = 1;
        }
    }
}
static int mesh(void *v, ThreeMMPreviewMesh *out)
{
    Host *h = (Host *)v;
    out->empty = 1;
    (void)h;
    return 1;
}
static ThreeMMU32 bodies(void *v)
{
    return (ThreeMMU32)((Host *)v)->with_body;
}
static int body(void *v, ThreeMMU32 index, ThreeMMPreviewBody *out)
{
    Host *h = (Host *)v;
    unsigned i;
    CHECK(index < (ThreeMMU32)h->with_body);
    out->tree = h->with_body == 1 ? h->source : h->source + 1 + index;
    out->owner = 3 + index;
    out->order_high = index == 0 ? 1 : 0;
    out->order_low = 1;
    for (i = 0; i < 3; ++i)
    {
        out->low[i] = -1;
        out->high[i] = 1;
    }
    return 1;
}
static int bounds(void *v, ThreeMMI32 id, float corners[24], int *valid)
{
    (void)v;
    (void)id;
    (void)corners;
    *valid = 0;
    return 1;
}
static void *colour(void *v, ThreeMMU32 w, ThreeMMU32 height)
{
    Host *h = (Host *)v;
    CHECK(w <= 128 && height <= 128);
    if (h->fail == F_COLOUR)
        return NULL;
    h->width = w;
    h->height = height;
    ++h->pixels;
    return &h->colour_token;
}
static void *depth(void *v, void *c)
{
    Host *h = (Host *)v;
    CHECK(c == &h->colour_token);
    if (h->fail == F_DEPTH)
        return NULL;
    ++h->pixels;
    return &h->depth_token;
}
static void *world(void *v)
{
    Host *h = (Host *)v;
    if (h->fail == F_WORLD)
        return NULL;
    ++h->world;
    return node(h, NULL);
}
static void free_pixel(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p && h->pixels);
    --h->pixels;
}
static void free_world(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p && h->world && !h->lights);
    CHECK(h->material.flags == 7 && h->material.map == h);
    if (h->light_payload)
        CHECK(*(unsigned char *)h->light_payload == 17);
    --h->world;
}
static void fill(void *v, void *p, ThreeMMU32 value)
{
    Host *h = (Host *)v;
    ThreeMMU32 i;
    if (p == &h->colour_token)
        memset(h->colours, (int)value, h->width * h->height);
    else
        for (i = 0; i < h->width * h->height; ++i)
            h->depths[i] = (unsigned short)value;
}
static void buffers(void *v, void *c, void *d, ThreeMMPreviewBuffer *out)
{
    Host *h = (Host *)v;
    CHECK(c && d);
    out->colour = h->colours;
    out->depth = (unsigned char *)h->depths;
    out->colour_stride = h->width;
    out->depth_stride = h->width * 2;
}
static void *camera(void *v, void *w, const ThreeMMPreviewCamera *c)
{
    Host *h = (Host *)v;
    if (h->fail == F_CAMERA)
        return NULL;
    h->camera = *c;
    return node(h, (Node *)w);
}
static void update_camera(void *v, void *a, const ThreeMMPreviewCamera *c)
{
    CHECK(a);
    ((Host *)v)->camera = *c;
}
static void *light(void *v, void *w, void *data, const ThreeMMBackgroundLight *source)
{
    Host *h = (Host *)v;
    CHECK(data && source);
    if (h->fail == F_LIGHT)
        return NULL;
    h->light_payload = data;
    memset(data, 17, 16);
    return node(h, (Node *)w);
}
static void enable(void *v, void *a)
{
    CHECK(a);
    ++((Host *)v)->lights;
}
static void disable(void *v, void *a)
{
    CHECK(a && ((Host *)v)->lights);
    --((Host *)v)->lights;
}
static int add_model(void *v, void *w, void *m)
{
    Host *h = (Host *)v;
    Node *n;
    CHECK(m);
    if (h->fail == F_MODEL)
        return 0;
    n = node(h, (Node *)w);
    n->model = 1;
    return 1;
}
static int add_mesh(void *v, void *w)
{
    CHECK(w);
    return ((Host *)v)->fail != F_MESH;
}
static void *material(void *v, void *a)
{
    CHECK(a);
    return &((Host *)v)->material;
}
static void *face_material(void *v, void *m, ThreeMMU32 i)
{
    CHECK(m && !i);
    return &((Host *)v)->material;
}
static ThreeMMU32 material_count(void *v, void *m)
{
    (void)v;
    CHECK(m);
    return 1;
}
static void read_material(void *v, void *m, ThreeMMPreviewMaterial *out)
{
    Host *h = (Host *)v;
    CHECK(m == &h->material);
    *out = h->material;
}
static void write_material(void *v, const ThreeMMPreviewMaterial *m)
{
    Host *h = (Host *)v;
    CHECK(m->material == &h->material);
    h->material = *m;
    if (m->flags == 7)
        ++h->restores;
}
static void edges(void *v, void *a)
{
    CHECK(a);
    ++((Host *)v)->edges;
}
static ThreeMMU32 vertices(void *v, void *m)
{
    (void)v;
    CHECK(m);
    return 3;
}
static ThreeMMU32 faces(void *v, void *m)
{
    (void)v;
    CHECK(m);
    return 1;
}
static void vertex(void *v, void *m, ThreeMMU32 i, ThreeMMI32 xyz[3])
{
    (void)v;
    CHECK(m && i < 3);
    xyz[0] = scalar(i == 0 ? -1.f : 1.f);
    xyz[1] = scalar(i == 2 ? 1.f : -1.f);
    xyz[2] = scalar(-3.f);
}
static void face(void *v, void *m, ThreeMMU32 i, unsigned short xyz[3])
{
    (void)v;
    CHECK(m && !i);
    xyz[0] = 0;
    xyz[1] = 1;
    xyz[2] = 2;
}
static void relative(void *v, void *a, void *c, ThreeMMI32 out[12])
{
    (void)v;
    CHECK(a && c);
    memset(out, 0, 12 * sizeof(*out));
    out[0] = out[4] = out[8] = scalar(1);
}
static int begin(void *v, const ThreeMMPreviewProjection *p, const unsigned char *palette, int opaque, ThreeMMU32 w,
                 ThreeMMU32 height)
{
    Host *h = (Host *)v;
    CHECK(p->valid && palette && w == h->width && height == h->height);
    (void)opaque;
    if (h->fail == F_BEGIN)
        return 0;
    h->sink = 1;
    return 1;
}
static void sink_off(void *v)
{
    ((Host *)v)->sink = 0;
}
static void render(void *v, void *w, void *c, void *p, void *d)
{
    Host *h = (Host *)v;
    CHECK(w && c && p && d);
    h->colours[0] = 42;
    h->depths[0] = 0;
}
static int renderer_ok(void *v)
{
    return ((Host *)v)->fail != F_RENDER;
}
static int end(void *v, const ThreeMMPreviewBuffer *b)
{
    CHECK(b->colour);
    return ((Host *)v)->fail != F_END;
}
static const unsigned char *rgba(void *v)
{
    (void)v;
    return NULL;
}
static void depth_projection(void *v, void *c, ThreeMMI32 matrix[16])
{
    (void)v;
    CHECK(c);
    memset(matrix, 0, 16 * sizeof(*matrix));
    matrix[10] = scalar(1);
    matrix[14] = scalar(2);
}
static int output(void *v, ThreeMMU32 w, ThreeMMU32 height, int depth_requested, unsigned char **rgba_out,
                  float **depth_out)
{
    Host *h = (Host *)v;
    ThreeMMU32 i;
    if (h->fail == F_OUTPUT)
        return 0;
    *rgba_out = h->rgba;
    *depth_out = depth_requested ? h->distances : NULL;
    for (i = 0; i < w * height; ++i)
        h->distances[i] = (float)HUGE_VAL;
    return 1;
}
static ThreeMMPreviewRenderServices services(Host *h)
{
    ThreeMMPreviewRenderServices s;
    memset(&s, 0, sizeof(s));
    s.context = h;
    s.allocator.context = h;
    s.allocator.resize = resize;
    s.allocator.release = release;
    s.math.sin = fsin;
    s.math.cos = fcos;
    s.math.tan = ftan;
    s.math.sqrt = fsqrt;
    s.math.asin = fasin;
    s.math.atan2 = fatan2;
    s.math.fmod = ffmod;
    s.math.scalar = scalar;
    s.math.scalar_double = scalar_double;
    s.math.scalar_float = scalar_float;
    s.math.angle = angle;
    s.tree.context = h;
    s.tree.first = first;
    s.tree.next = next;
    s.tree.clone_attach = clone;
    s.tree.model = model;
    s.tree.owner = owner;
    s.ready = ready;
    s.describe = describe;
    s.mesh = mesh;
    s.body_count = bodies;
    s.body = body;
    s.object_bounds = bounds;
    s.colour = colour;
    s.depth = depth;
    s.world = world;
    s.free_pixel = free_pixel;
    s.free_world = free_world;
    s.fill = fill;
    s.buffers = buffers;
    s.camera = camera;
    s.update_camera = update_camera;
    s.light_bytes = 16;
    s.direct_light_type = 0;
    s.light = light;
    s.enable_light = enable;
    s.disable_light = disable;
    s.add_model = add_model;
    s.add_mesh = add_mesh;
    s.material = material;
    s.face_material = face_material;
    s.material_count = material_count;
    s.read_material = read_material;
    s.write_material = write_material;
    s.light_flag = 1;
    s.prelit_flag = 2;
    s.smooth_flag = 4;
    s.edges = edges;
    s.vertices = vertices;
    s.faces = faces;
    s.vertex = vertex;
    s.face = face;
    s.relative = relative;
    s.raster_begin = begin;
    s.sink_off = sink_off;
    s.render = render;
    s.renderer_ok = renderer_ok;
    s.raster_end = end;
    s.raster_rgba = rgba;
    s.depth_projection = depth_projection;
    s.output = output;
    return s;
}
static void init(Host *h, ThreeMMPreviewView *v, ThreeMMPreviewOrbit *o)
{
    unsigned i;
    memset(h, 0, sizeof(*h));
    memset(v, 0, sizeof(*v));
    memset(o, 0, sizeof(*o));
    h->background = h->with_body = h->with_model = 1;
    h->material.material = &h->material;
    h->material.flags = 7;
    h->material.map = h;
    h->source[0].first = h->source + 1;
    h->source[1].next = h->source + 2;
    h->source[2].next = h->source + 3;
    for (i = 1; i < 4; ++i)
    {
        h->source[i].model = 1;
        h->source[i].id = (int)i;
    }
    memset(h->background_colours, 23, sizeof(h->background_colours));
    for (i = 0; i < 128 * 128; ++i)
        h->background_depths[i] = 0xffff;
    v->width = 96;
    v->height = 64;
    v->zoom = 1;
    v->vertical_fov = 45;
    v->object = -1;
    v->depth = 1;
    v->display = 1;
    v->fit_depth = 1;
    v->stable_orbit = 1;
}
static void clean(const Host *h)
{
    CHECK(!h->live_memory && !h->world && !h->pixels && !h->lights && !h->sink);
    CHECK(h->material.flags == 7 && h->material.map == h);
}
static void depth_test(void)
{
    ThreeMMPreviewDepthRange range;
    double n, f;
    const double triangle[3][3] = {{-20, -20, 4}, {20, -20, 4}, {0, 20, 4}},
                 behind[3][3] = {{-1, -1, -4}, {1, -1, -4}, {0, 1, -4}};
    three_mm_preview_depth_init(&range, 1, 1, .1, 100, 0);
    three_mm_preview_depth_include(&range, behind);
    three_mm_preview_depth_fit(&range, &n, &f);
    CHECK(n == .1 && f == 100);
    three_mm_preview_depth_include(&range, triangle);
    three_mm_preview_depth_fit(&range, &n, &f);
    CHECK(n == 2 && f == 4.2);
    three_mm_preview_depth_init(&range, 1, 1, .1, 100, 1);
    three_mm_preview_depth_include(&range, triangle);
    three_mm_preview_depth_fit(&range, &n, &f);
    CHECK(n == 2 && f == 4.2);
}
int main(void)
{
    static Host h;
    ThreeMMPreviewView v;
    ThreeMMPreviewOrbit orbit;
    ThreeMMPreviewImage image;
    ThreeMMPreviewRenderServices s;
    int result, fail, allocations;
    depth_test();
    init(&h, &v, &orbit);
    s = services(&h);
    CHECK(three_mm_preview_render(&v, &orbit, &image, &s) == THREE_MM_PREVIEW_OK);
    clean(&h);
    CHECK(image.width == 96 && image.height == 64 && image.covered == 1 && image.rgba[0] == 42 &&
          image.rgba[3] == 255 && image.depth[0] == 2);
    CHECK(h.restores == 1 && h.copies == 4 && h.owners == 3 && orbit.framed && h.camera.near_z == scalar(1.5f));
    CHECK(h.nodes[0].first->next->first->id == 1 && h.nodes[0].first->next->first->next->id == 2);
    allocations = h.allocations;
    for (fail = 1; fail <= allocations; ++fail)
    {
        init(&h, &v, &orbit);
        s = services(&h);
        h.fail_allocation = fail;
        result = three_mm_preview_render(&v, &orbit, &image, &s);
        CHECK(result != THREE_MM_PREVIEW_OK);
        CHECK(!image.width && !image.rgba);
        clean(&h);
    }
    for (fail = F_COLOUR; fail <= F_OUTPUT; ++fail)
    {
        init(&h, &v, &orbit);
        s = services(&h);
        h.fail = fail;
        if (fail >= F_BEGIN)
            v.rasterizer = 1;
        CHECK(three_mm_preview_render(&v, &orbit, &image, &s) != THREE_MM_PREVIEW_OK);
        CHECK(!image.width && !image.rgba);
        clean(&h);
    }
    init(&h, &v, &orbit);
    s = services(&h);
    v.authored = 1;
    v.output_resolution = 1;
    v.width = 96;
    v.height = 96;
    v.display = 0;
    CHECK(three_mm_preview_render(&v, &orbit, &image, &s) == THREE_MM_PREVIEW_OK);
    clean(&h);
    CHECK(h.width == 96 && h.height == 48 && image.rgba[3] == 0 && image.rgba[24 * 96 * 4] == 42 &&
          image.rgba[(24 * 96 + 1) * 4] == 23);
    init(&h, &v, &orbit);
    s = services(&h);
    h.background = 0;
    v.display = 4;
    CHECK(three_mm_preview_render(&v, &orbit, &image, &s) == THREE_MM_PREVIEW_OK);
    clean(&h);
    CHECK(h.edges == 4 && h.restores == 1);
    init(&h, &v, &orbit);
    s = services(&h);
    h.with_body = 3;
    h.with_model = 0;
    CHECK(three_mm_preview_render(&v, &orbit, &image, &s) == THREE_MM_PREVIEW_OK);
    clean(&h);
    CHECK(h.nodes[0].first->id == 1 && h.nodes[0].first->next->id == 3 && h.nodes[0].first->next->next->id == 2);
    v.width = 63;
    CHECK(three_mm_preview_render(&v, &orbit, &image, &s) == THREE_MM_PREVIEW_INVALID);
    clean(&h);
    puts("Preview C: clipping, tree order, camera, output fitting, material/sink restoration and allocation/host "
         "failures passed.");
    return 0;
}
