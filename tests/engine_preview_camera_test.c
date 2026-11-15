#include "mm/preview_camera.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Host
{
    int ready, scene, background, bodies, attempts, fail_at, live, reads, refs, fetches, releases, fail, select_result,
        selects;
    unsigned char wire[76];
    MMMemoryStream memory;
    MMI32 rect[4];
    MMU32 selected;
    float camera_z;
} Host;
static float scalar_float(MMI32 n)
{
    return (float)n / 65536;
}
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
static float fasin(float x)
{
    return (float)asin(x);
}
static float fatan(float x)
{
    return (float)atan(x);
}
static float fatan2(float x, float y)
{
    return (float)atan2(x, y);
}
static float ffmod(float x, float y)
{
    return (float)fmod(x, y);
}
static void word(unsigned char *p, MMU32 n)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (8 * i));
}
static void init(Host *h)
{
    memset(h, 0, sizeof(*h));
    h->ready = h->scene = 1;
    h->camera_z = 10;
    h->wire[0] = 1;
    word(h->wire + 4, 65536);
    word(h->wire + 8, 100 * 65536UL);
    h->wire[12] = 0;
    h->wire[13] = 32;
    word(h->wire + 28, 65536);
    word(h->wire + 44, 65536);
    word(h->wire + 60, 65536);
    word(h->wire + 64, 3 * 65536UL);
    assert(mm_memory_stream_init(&h->memory, h->wire, 76));
    h->rect[0] = -2;
    h->rect[1] = 3;
    h->rect[2] = 638;
    h->rect[3] = 483;
}
static void *resize(void *v, void *old, MMU32 old_size, MMU32 size, int priority)
{
    Host *h = (Host *)v;
    void *p;
    (void)old_size;
    (void)priority;
    if (++h->attempts >= h->fail_at && h->fail_at)
        return 0;
    p = realloc(old, size);
    if (p && !old)
        ++h->live;
    return p;
}
static void release_memory(void *v, void *p, MMU32 size)
{
    Host *h = (Host *)v;
    (void)size;
    assert(h->live > 0);
    --h->live;
    free(p);
}
static int ready(void *v)
{
    return ((Host *)v)->ready;
}
static void describe(void *v, MMPreviewData *d)
{
    Host *h = (Host *)v;
    d->scene = h->scene;
    d->background = h->background;
    d->width = 640;
    d->height = 480;
    d->camera.fov = 8192;
    d->camera.matrix[0] = d->camera.matrix[4] = d->camera.matrix[8] = 65536;
    d->camera.matrix[11] = (MMI32)(h->camera_z * 65536);
}
static MMU32 bodies(void *v)
{
    return (MMU32)((Host *)v)->bodies;
}
static int body(void *v, MMU32 n, MMPreviewBody *b)
{
    (void)v;
    if (n == 1)
        return 0;
    memset(b, 0, sizeof(*b));
    b->low[2] = -1;
    b->high[2] = 1;
    return 1;
}
static int camera_read(void *v, MMU32 n, MMReadStream *out)
{
    Host *h = (Host *)v;
    assert(n == 10 || n == 20);
    ++h->reads;
    *out = h->memory.stream;
    return h->fail != 1;
}
static int child(void *v, MMU32 n, int depth, MMU32 *out)
{
    Host *h = (Host *)v;
    assert((n == 10 || n == 20) && !depth);
    *out = 30;
    return h->fail != 2;
}
static void *fetch(void *v, MMU32 n, int depth)
{
    Host *h = (Host *)v;
    assert(n == 30 && !depth);
    ++h->fetches;
    if (h->fail == 3)
        return 0;
    assert(!h->refs);
    ++h->refs;
    return &h->refs;
}
static void release(void *v, void *p)
{
    Host *h = (Host *)v;
    assert(p == &h->refs && h->refs == 1);
    --h->refs;
    ++h->releases;
}
static void bounds(void *v, void *p, MMI32 r[4])
{
    Host *h = (Host *)v;
    assert(p == &h->refs && h->refs == 1);
    memcpy(r, h->rect, sizeof(h->rect));
}
static float convert(void *v, MMI32 n)
{
    (void)v;
    return scalar_float(n);
}
static int select_camera(void *v, MMU32 n)
{
    Host *h = (Host *)v;
    ++h->selects;
    h->selected = n;
    return h->select_result;
}
static void cache_tests(void)
{
    Host h;
    MMPreviewProjectionCache c;
    MMPreviewProjection p, saved;
    MMBackgroundPreviewServices s;
    MMStorageAllocator a;
    MMU32 numbers[2] = {10, 20};
    int i, attempts;
    memset(&s, 0, sizeof(s));
    s.context = &h;
    s.camera_read = camera_read;
    s.child = child;
    s.fetch = fetch;
    s.bounds = bounds;
    s.release = release;
    s.scalar_float = convert;
    a.context = &h;
    a.resize = resize;
    a.release = release_memory;
    init(&h);
    assert(mm_preview_projection_cache_init(&c, &a));
    assert(mm_preview_projection_cache_get(&c, numbers, 2, 0, &p, &s) == 0);
    assert(p.valid && p.vertical_fov == 45 && p.aspect == 640.f / 480 && p.transform[9] == 3 && p.display_depth == 10);
    assert(h.reads == 1 && h.releases == 1 && !h.refs);
    saved = p;
    h.fail = 1;
    assert(mm_preview_projection_cache_get(&c, numbers, 2, 0, &p, &s) == 0 && !memcmp(&p, &saved, sizeof(p)));
    assert(h.reads == 1);
    assert(mm_preview_projection_cache_get(&c, numbers, 2, 1, &p, &s) == MM_PREVIEW_CAMERA_READ && !p.valid);
    h.fail = 0;
    assert(mm_preview_projection_cache_get(&c, numbers, 2, 1, &p, &s) == MM_PREVIEW_CAMERA_READ && h.reads == 2);
    mm_preview_projection_cache_reset(&c);
    assert(mm_preview_projection_cache_get(&c, numbers, 2, 1, &p, &s) == 0 && h.reads == 3);
    assert(mm_preview_projection_cache_get(&c, numbers, 2, 2, &p, &s) == MM_PREVIEW_CAMERA_READ && !p.valid &&
           h.reads == 3);
    attempts = h.attempts;
    mm_preview_projection_cache_dispose(&c);
    assert(!h.live);
    for (i = 1; i <= attempts; ++i)
    {
        init(&h);
        h.fail_at = i;
        assert(mm_preview_projection_cache_init(&c, &a));
        assert(mm_preview_projection_cache_get(&c, numbers, 2, 0, &p, &s) == MM_PREVIEW_CAMERA_MEMORY);
        assert(!c.count && !h.reads && !p.valid);
        h.fail_at = 0;
        assert(mm_preview_projection_cache_get(&c, numbers, 2, 0, &p, &s) == 0 && p.valid);
        mm_preview_projection_cache_dispose(&c);
        assert(!h.live && !h.refs);
    }
    for (i = 1; i <= 8; ++i)
    {
        init(&h);
        h.fail = i;
        if (i == 4)
            h.wire[0] = 0;
        if (i == 5)
            h.rect[2] = h.rect[0];
        if (i == 6)
        {
            h.rect[0] = (-2147483647 - 1);
            h.rect[2] = 2147483647;
        }
        if (i == 7)
            word(h.wire + 8, 65536);
        if (i == 8)
        {
            h.wire[12] = 0;
            h.wire[13] = 128;
        }
        assert(mm_preview_projection_cache_init(&c, &a));
        assert(mm_preview_projection_cache_get(&c, numbers, 2, 0, &p, &s) == MM_PREVIEW_CAMERA_READ);
        assert(!h.refs && !p.valid && h.reads == 1);
        assert(mm_preview_projection_cache_get(&c, numbers, 2, 0, &saved, &s) == MM_PREVIEW_CAMERA_READ);
        assert(h.reads == 1 && !memcmp(&p, &saved, sizeof(p)));
        if (i >= 7)
            assert(p.display_depth == 10 && p.aspect > 0);
        mm_preview_projection_cache_dispose(&c);
        assert(!h.live);
    }
}
int main(void)
{
    Host h;
    MMPreviewRenderServices s;
    MMPreviewCameraPose pose;
    MMPreviewOrbit o, before;
    MMPreviewView v;
    MMI32 low[3] = {-65536, -131072, -196608}, high[3] = {65536, 131072, 196608};
    MMI32 matrix[12] = {0, 65536, 0, -65536, 0, 0, 0, 0, 65536, 65536, 131072, 196608};
    MMI32 place[3] = {65536, -131072, 196608}, old_bounds[6] = {0, 0, 0, 65536, 65536, 65536};
    MMI32 new_bounds[6] = {65536, 0, 0, 196608, 65536, 65536};
    float corners[24], start[3] = {1, 2, 3}, end[3] = {2, 4, 6}, point[3] = {-9, -9, -9};
    MMU32 current;
    int ready_value;
    init(&h);
    memset(&s, 0, sizeof(s));
    memset(&o, 0, sizeof(o));
    memset(&v, 0, sizeof(v));
    s.context = &h;
    s.ready = ready;
    s.describe = describe;
    s.body_count = bodies;
    s.body = body;
    s.math.scalar_float = scalar_float;
    s.math.sin = fsin;
    s.math.cos = fcos;
    s.math.tan = ftan;
    s.math.asin = fasin;
    s.math.atan2 = fatan2;
    s.math.fmod = ffmod;
    o.anchor[0] = 7;
    assert(mm_preview_camera_match(1, &o, &pose, &s, fatan) == 0);
    assert(pose.position[2] == 10 && pose.move_speed == 2.5f && pose.vertical_fov == 45 && o.anchor[0] == 7);
    assert(o.framed && o.includes_camera && o.object == -1 && o.center[2] == 0);
    h.background = 1;
    h.bodies = 2;
    assert(mm_preview_camera_match(.5f, &o, &pose, &s, fatan) == 0 && pose.vertical_fov > 45);
    assert(fabs(o.radius - 5.f / 2.8f) < .00001);
    before = o;
    h.ready = 0;
    assert(mm_preview_camera_match(1, &o, &pose, &s, fatan) == MM_PREVIEW_CAMERA_INVALID);
    assert(!memcmp(&o, &before, sizeof(o)) && pose.move_speed == 1 && pose.vertical_fov == 45 && pose.position[2] == 0);
    h.ready = 1;
    assert(mm_preview_camera_match(0, &o, &pose, &s, fatan) == MM_PREVIEW_CAMERA_INVALID);
    h.scene = 0;
    assert(mm_preview_camera_match(1, &o, &pose, &s, fatan) == MM_PREVIEW_CAMERA_INVALID);
    h.scene = 1;
    assert(mm_preview_camera_match(1, &o, &pose, &s, fatan) == 0);
    v.width = 640;
    v.height = 480;
    v.zoom = 1;
    v.vertical_fov = 45;
    assert(mm_preview_orbit_pan(&v, .1f, 0, &o, 1, &s.math) == 0 && o.center[0] > 0 && o.center[1] == 0);
    v.roll = 90;
    assert(mm_preview_orbit_pan(&v, .1f, 0, &o, 1, &s.math) == 0 && o.center[1] > 0);
    v.roll = 0;
    v.orthographic = 1;
    assert(mm_preview_orbit_pan(&v, 1000000, 0, &o, 1, &s.math) == 0 && fabs(o.center[0] - 32700) < .01);
    before = o;
    assert(mm_preview_orbit_pan(&v, 1, 0, &o, 1, &s.math) == 0 && !memcmp(&o, &before, sizeof(o)));
    assert(mm_preview_orbit_pan(&v, FLT_MAX, 0, &o, 1, &s.math) == MM_PREVIEW_CAMERA_PAN_RANGE &&
           !memcmp(&o, &before, sizeof(o)));
    assert(mm_preview_orbit_pan(&v, (float)HUGE_VAL, 0, &o, 1, &s.math) == MM_PREVIEW_CAMERA_INVALID);
    v.pitch = 91;
    assert(mm_preview_orbit_pan(&v, 0, 0, &o, 1, &s.math) == MM_PREVIEW_CAMERA_INVALID);
    o.object = 2;
    assert(mm_preview_orbit_follows(&o, 2, 1) && !mm_preview_orbit_follows(&o, 2, 0));
    mm_preview_orbit_move(&o, 3, start, end);
    assert(o.anchor[0] == 7);
    mm_preview_orbit_move(&o, 2, start, end);
    assert(o.anchor[0] == 8 && o.anchor[1] == 2 && o.anchor[2] == 3);
    mm_preview_orbit_resize(&o, old_bounds, new_bounds, &s.math);
    assert(o.anchor[0] == 9.5f);
    before = o;
    mm_preview_orbit_reset(&o);
    before.framed = 0;
    assert(!memcmp(&o, &before, sizeof(o)));
    assert(mm_preview_camera_bounds(low, high, matrix, corners, &s.math));
    assert(corners[0] == 3 && corners[1] == 1 && corners[2] == 0 && corners[21] == -1 && corners[22] == 3 &&
           corners[23] == 6);
    low[0] = high[0] + 1;
    assert(!mm_preview_camera_bounds(low, high, matrix, corners, &s.math) && corners[0] == 0 && corners[23] == 0);
    assert(!mm_preview_camera_placement(0, place, point, &s.math) && point[0] == -9);
    assert(!mm_preview_camera_placement(1, 0, point, &s.math) && !mm_preview_camera_placement(1, place, 0, &s.math));
    assert(mm_preview_camera_placement(1, place, point, &s.math) && point[0] == 1 && point[1] == -2 && point[2] == 3);
    ready_value = 1;
    current = 0;
    h.select_result = 1;
    assert(mm_preview_camera_select(&ready_value, &current, 1, 2, 0, &h, select_camera) == 0 && !h.selects);
    assert(mm_preview_camera_select(&ready_value, &current, 1, 2, 2, &h, select_camera) == MM_PREVIEW_CAMERA_INVALID &&
           !h.selects);
    h.select_result = -1;
    assert(mm_preview_camera_select(&ready_value, &current, 1, 2, 1, &h, select_camera) == MM_PREVIEW_CAMERA_MEMORY &&
           ready_value && !current);
    h.select_result = 0;
    assert(mm_preview_camera_select(&ready_value, &current, 1, 2, 1, &h, select_camera) == MM_PREVIEW_CAMERA_READ &&
           !ready_value && !current);
    ready_value = 1;
    h.select_result = 1;
    assert(mm_preview_camera_select(&ready_value, &current, 1, 2, 1, &h, select_camera) == 0 && current == 1 &&
           h.selected == 1);
    cache_tests();
    puts("C89 Preview cameras: match/pan, bounds, anchors, selection, guide caching and partial allocation recovery "
         "passed.");
    return 0;
}
