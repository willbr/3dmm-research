#include "mm/preview_pick.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node
{
    struct Node *first, *next;
    int id;
} Node;
typedef struct Host
{
    MMBodyPartBinding bindings[3][2];
    Node nodes[16];
    int used, world_live, live, attempts, fail_at, failure, ready, background, with_mesh;
    int scene_calls, copied[3], copies, face_calls, current, transparent;
    int pose_refs, pose_retains, pose_releases;
    MMU32 width, height;
    MMI32 x, y;
    MMPreviewCamera camera;
    float hit[4][2], u, v;
    unsigned char pixels[16];
    unsigned short depths[128 * 96];
} Host;
static void *resize(void *v, void *old, MMU32 old_size, MMU32 size, int priority)
{
    Host *h = (Host *)v;
    void *p;
    (void)old_size;
    (void)priority;
    ++h->attempts;
    if (h->fail_at && h->attempts >= h->fail_at)
        return 0;
    p = realloc(old, size);
    if (p && !old)
        ++h->live;
    return p;
}
static void release(void *v, void *p, MMU32 size)
{
    Host *h = (Host *)v;
    (void)size;
    assert(h->live > 0);
    --h->live;
    free(p);
}
static float scalar_float(MMI32 x)
{
    return (float)x / 65536;
}
static MMI32 scalar(float x)
{
    return (MMI32)(x * 65536.f);
}
static unsigned short angle(float x)
{
    return (unsigned short)(x * 182);
}
static Node *node(Host *h, Node *parent, int id)
{
    Node *n;
    assert(h->used < 16);
    n = h->nodes + h->used++;
    memset(n, 0, sizeof(*n));
    n->id = id;
    if (parent)
    {
        n->next = parent->first;
        parent->first = n;
    }
    return n;
}
static void *attach_body_part(void *v, void *parent, MMI32 index, const MMBodyPartBinding *binding)
{
    Host *h = (Host *)v;
    MMI32 id = binding->matrix.m[3][0];
    if (h->failure == 4)
        return 0;
    assert(index == 0 && parent && binding->model_handle && h->copies < 3);
    h->copied[h->copies++] = id;
    return node(h, (Node *)parent, id);
}
static int ready(void *v)
{
    return ((Host *)v)->ready;
}
static void describe(void *v, MMPreviewData *d)
{
    Host *h = (Host *)v;
    d->scene = 1;
    d->background = h->background;
    d->width = 128;
    d->height = 96;
    d->depths = h->depths;
}
static MMU32 body_count(void *v)
{
    (void)v;
    return 3;
}
static int body(void *v, MMU32 index, MMPreviewBody *b)
{
    Host *h = (Host *)v;
    memset(b, 0, sizeof(*b));
    b->draw.bindings = h->bindings[index];
    b->resource = h->bindings[index];
    b->owner = (MMI32)index;
    b->order_low = index ? 10 : 20;
    return 1;
}
static void retain_pose_resource(void *v, void *resource)
{
    Host *h = (Host *)v;
    assert(resource);
    ++h->pose_refs;
    ++h->pose_retains;
}
static void release_pose_resource(void *v, void *resource)
{
    Host *h = (Host *)v;
    assert(resource && h->pose_refs && !h->world_live);
    --h->pose_refs;
    ++h->pose_releases;
}
static void *world(void *v)
{
    Host *h = (Host *)v;
    if (h->failure == 1)
        return 0;
    ++h->world_live;
    return node(h, 0, -2);
}
static void free_world(void *v, void *w)
{
    Host *h = (Host *)v;
    assert(w == h->nodes && h->world_live == 1);
    /* C scratch remains alive until after the world has been disposed. */
    if (h->scene_calls)
        assert(h->live > 0);
    --h->world_live;
}
static void *camera(void *v, void *w, const MMPreviewCamera *c)
{
    Host *h = (Host *)v;
    h->camera = *c;
    if (h->failure == 2)
        return 0;
    return node(h, (Node *)w, -2);
}
static int invertible(void *v, void *c)
{
    (void)c;
    return ((Host *)v)->failure != 3;
}
static int mesh(void *v, MMPreviewMesh *m)
{
    (void)m;
    return ((Host *)v)->failure != 5;
}
static int add_mesh(void *v, void *w)
{
    Host *h = (Host *)v;
    if (h->failure == 6)
        return 0;
    if (h->with_mesh)
        node(h, (Node *)w, 3);
    return 1;
}
static void perspective(void *v, const MMPreviewCamera *c, MMI32 out[16])
{
    (void)v;
    (void)c;
    memset(out, 0, 16 * sizeof(*out));
    out[10] = scalar(1);
    out[14] = scalar(2);
}
static int scene_pick(void *v, void *w, void *c, MMU32 width, MMU32 height, MMI32 x, MMI32 y, MMPreviewPickState *state)
{
    Host *h = (Host *)v;
    Node *n = ((Node *)w)->first;
    (void)c;
    assert(h->pose_refs == 3);
    ++h->scene_calls;
    h->width = width;
    h->height = height;
    h->x = x;
    h->y = y;
    for (; n; n = n->next)
        if (n->id >= 0)
            mm_preview_pick_model(state, n, n, 0, 0, 0, 0, 65536);
    return h->failure != 7;
}
static MMU32 face_count(void *v, void *m)
{
    (void)v;
    (void)m;
    return 2;
}
static void prepare_face(void *v, void *m, MMU32 i, void *out)
{
    Host *h = (Host *)v;
    ++h->face_calls;
    *(int *)out = ((Node *)m)->id * 10 + (int)i;
}
static void model_pick(void *v, void *m, void *faces, void *material, void *position, void *direction, MMI32 near_clip,
                       MMI32 far_clip, MMPreviewPickState *state)
{
    Host *h = (Host *)v;
    int i, id = ((Node *)m)->id;
    (void)material;
    (void)position;
    (void)direction;
    (void)near_clip;
    (void)far_clip;
    assert(faces != m);
    h->current = id;
    for (i = 0; i < 2; ++i)
    {
        assert(((int *)faces)[i] == id * 10 + i);
        mm_preview_pick_face(state, h->hit[id][i], 0, 0);
    }
}
static int texture(void *v, void *m, MMPreviewPickTexture *out)
{
    Host *h = (Host *)v;
    (void)m;
    if (!h->transparent || h->current != 1)
        return 0;
    out->pixels = h->pixels;
    out->width = out->height = 2;
    out->stride = 4;
    out->base_x = out->base_y = 1;
    out->opaque = h->transparent == 2;
    out->indexed = h->transparent != 3;
    return 1;
}
static void map(void *v, void *m, void *input, float uv[2])
{
    Host *h = (Host *)v;
    (void)m;
    (void)input;
    uv[0] = h->u;
    uv[1] = h->v;
}
static void init(Host *h, MMPreviewRenderServices *r, MMPreviewPickServices *s, MMPreviewProjection *p)
{
    int i;
    memset(h, 0, sizeof(*h));
    memset(r, 0, sizeof(*r));
    memset(s, 0, sizeof(*s));
    memset(p, 0, sizeof(*p));
    h->ready = 1;
    memset(h->depths, 255, sizeof(h->depths));
    memset(h->pixels, 1, sizeof(h->pixels));
    for (i = 0; i < 4; ++i)
    {
        h->hit[i][0] = 4;
        h->hit[i][1] = 5;
    }
    for (i = 0; i < 3; ++i)
    {
        h->bindings[i][0].model_handle = h;
        h->bindings[i][0].matrix.m[3][0] = i;
    }
    r->context = h;
    r->allocator.context = h;
    r->allocator.resize = resize;
    r->allocator.release = release;
    r->math.scalar_float = scalar_float;
    r->math.scalar = scalar;
    r->math.angle = angle;
    r->ready = ready;
    r->describe = describe;
    r->body_count = body_count;
    r->body = body;
    r->attach_body_part = attach_body_part;
    r->retain_pose_resource = retain_pose_resource;
    r->release_pose_resource = release_pose_resource;
    r->frame_factory.world = world;
    r->frame_factory.free_world = free_world;
    r->camera = camera;
    r->mesh = mesh;
    r->add_mesh = add_mesh;
    s->context = h;
    s->render = r;
    s->invertible = invertible;
    s->perspective = perspective;
    s->scene_pick = scene_pick;
    s->face_bytes = sizeof(int);
    s->face_count = face_count;
    s->prepare_face = prepare_face;
    s->model_pick = model_pick;
    s->texture = texture;
    s->map = map;
    p->valid = 1;
    p->vertical_fov = 60;
    p->aspect = 1.25f;
    p->near_clip = 1;
    p->far_clip = 20;
    p->transform[0] = p->transform[4] = p->transform[8] = 1;
}
static int run(Host *h, MMPreviewProjection *p, MMPreviewPickServices *s, int authored, int output, MMU32 w,
               MMU32 height, MMI32 x, MMI32 y, MMI32 *object, float *distance)
{
    int result = mm_preview_pick(p, authored, output, w, height, x, y, object, distance, s);
    assert(!h->live && !h->world_live);
    assert(!h->pose_refs && h->pose_retains == h->pose_releases);
    return result;
}
int main(void)
{
    Host h;
    MMPreviewRenderServices r;
    MMPreviewPickServices s;
    MMPreviewProjection p;
    MMI32 object;
    float distance;
    int i, attempts, result;
    init(&h, &r, &s, &p);
    assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) == 0);
    assert(object == 1 && distance == 4 && h.face_calls == 6);
    assert(h.copied[0] == 1 && h.copied[1] == 2 && h.copied[2] == 0);
    attempts = h.attempts;
    /* Every C allocation failure unwinds the copied prefix and scratch. */
    for (i = 1; i <= attempts; ++i)
    {
        init(&h, &r, &s, &p);
        h.fail_at = i;
        result = run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance);
        assert(result != 0 && object == -1 && distance > FLT_MAX);
    }
    for (i = 1; i <= 7; ++i)
    {
        init(&h, &r, &s, &p);
        h.failure = i;
        h.background = 1;
        result = run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance);
        assert(result != 0 && object == -1 && distance > FLT_MAX);
    }
    /* Texture wrapping includes negative UVs, padded rows and base offsets. */
    for (i = 0; i < 2; ++i)
    {
        init(&h, &r, &s, &p);
        h.transparent = 1;
        h.u = -.25f;
        h.v = 1.75f;
        h.hit[1][0] = 2;
        h.pixels[10] = (unsigned char)i;
        assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) == 0);
        assert(object == (i ? 1 : 2) && distance == (i ? 2 : 4));
    }
    /* Opaque and non-indexed maps do not use palette-zero transparency. */
    for (i = 2; i < 4; ++i)
    {
        init(&h, &r, &s, &p);
        h.transparent = i;
        h.hit[1][0] = 2;
        memset(h.pixels, 0, sizeof(h.pixels));
        assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) == 0);
        assert(object == 1 && distance == 2);
    }
    /* Orthographic SDK parameters are normalized to camera-forward distances. */
    init(&h, &r, &s, &p);
    p.orthographic = 1;
    p.ortho_height = 10;
    for (i = 0; i < 3; ++i)
    {
        h.hit[i][0] = .25f;
        h.hit[i][1] = .5f;
    }
    assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) == 0);
    assert(object == 1 && distance == 5.75f && h.camera.width == scalar(6.25f));
    /* Clipping and an unowned mesh can occlude actors without selecting them. */
    init(&h, &r, &s, &p);
    h.background = h.with_mesh = 1;
    h.hit[3][0] = 2;
    assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) == 0);
    assert(object == -1 && distance > FLT_MAX);
    init(&h, &r, &s, &p);
    for (i = 0; i < 3; ++i)
    {
        h.hit[i][0] = .5f;
        h.hit[i][1] = 21;
    }
    assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) == 0);
    assert(object == -1 && distance > FLT_MAX);
    /* Authored Z images cap hits; output sizing preserves source sampling. */
    for (i = 0; i < 2; ++i)
    {
        init(&h, &r, &s, &p);
        h.background = 1;
        h.depths[48 * 128 + 64] = 0;
        assert(run(&h, &p, &s, 1, i, 256, 256, 128, 128, &object, &distance) == 0);
        assert(object == -1 && distance > FLT_MAX && h.x == 0 && h.y == 0);
        assert(h.width == (i ? 256U : 128U) && h.height == (i ? 192U : 96U));
    }
    /* Letterboxes and out-of-window positions do not allocate a world. */
    init(&h, &r, &s, &p);
    h.background = 1;
    assert(run(&h, &p, &s, 1, 0, 256, 256, 128, 0, &object, &distance) == 0);
    assert(!h.used && object == -1 && distance > FLT_MAX);
    assert(run(&h, &p, &s, 1, 0, 256, 256, -1, 100, &object, &distance) == 0);
    assert(!h.used);
    for (i = 0; i < 6; ++i)
    {
        init(&h, &r, &s, &p);
        if (i == 0)
            h.ready = 0;
        if (i == 1)
            p.vertical_fov = (float)HUGE_VAL;
        if (i == 2)
            p.near_clip = 0;
        if (i == 3)
            p.far_clip = 40000;
        if (i == 4)
            p.transform[0] = 40000;
        if (i == 5)
        {
            p.orthographic = 1;
            p.ortho_height = 60001;
        }
        assert(run(&h, &p, &s, 0, 0, 128, 128, 64, 64, &object, &distance) != 0);
        assert(!h.scene_calls && object == -1 && distance > FLT_MAX);
    }
    puts("C89 Preview picking: stable owners, ties, texture holes, clipping, authored depth, coordinates and failure "
         "cleanup passed.");
    return 0;
}
