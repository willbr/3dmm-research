#include "mm/background_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Background preview C: %d\n", __LINE__);                                                   \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
enum
{
    READ = 1,
    RGB_CHILD,
    RGB_FETCH,
    BOUNDS,
    RGB_DRAW,
    RGB_RELEASE,
    DEPTH_CHILD,
    DEPTH_FETCH,
    DEPTH_DRAW,
    DEPTH_RELEASE,
    SELECTED
};
typedef struct Mesh
{
    int live, id;
} Mesh;
typedef struct Host
{
    MMBackgroundPreview preview;
    MMBackgroundPreviewServices services;
    MMMemoryStream memory;
    unsigned char wire[76];
    MMI32 rect[4];
    int allocations, fail_allocation, live_memory, fail, trace[64], traces, publishes, rgb, depth;
    Mesh meshes[32];
    int created, released, builds, fail_new, fail_build, scalar_calls;
} Host;
static void event(Host *h, int code)
{
    CHECK(h->traces < 64);
    h->trace[h->traces++] = code;
}
static void *resize(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)p;
    void *out;
    (void)old_bytes;
    (void)priority;
    if (++h->allocations == h->fail_allocation)
        return NULL;
    out = realloc(old, bytes);
    if (out && !old)
        ++h->live_memory;
    return out;
}
static void free_memory(void *p, void *data, MMU32 bytes)
{
    Host *h = (Host *)p;
    (void)bytes;
    CHECK(data && h->live_memory > 0);
    --h->live_memory;
    free(data);
}
static void word(unsigned char *p, MMU32 value)
{
    int i;
    for (i = 0; i < 4; ++i)
        p[i] = (unsigned char)(value >> (i * 8));
}
static int camera_read(void *p, MMU32 number, MMReadStream *out)
{
    Host *h = (Host *)p;
    CHECK(number >= 100 && number < 103);
    event(h, READ);
    *out = h->memory.stream;
    return h->fail != READ;
}
static int child(void *p, MMU32 camera, int depth, MMU32 *out)
{
    Host *h = (Host *)p;
    int code = depth ? DEPTH_CHILD : RGB_CHILD;
    CHECK(camera >= 100 && camera < 103);
    event(h, code);
    *out = depth ? 800 : 700;
    return h->fail != code;
}
static void *fetch(void *p, MMU32 number, int depth)
{
    Host *h = (Host *)p;
    int code = depth ? DEPTH_FETCH : RGB_FETCH;
    CHECK(number == (MMU32)(depth ? 800 : 700));
    event(h, code);
    if (h->fail == code)
        return NULL;
    if (depth)
    {
        CHECK(!h->depth);
        h->depth = 1;
        return &h->depth;
    }
    CHECK(!h->rgb);
    h->rgb = 1;
    return &h->rgb;
}
static void release(void *p, void *object)
{
    Host *h = (Host *)p;
    if (object == &h->rgb)
    {
        CHECK(h->rgb == 1);
        h->rgb = 0;
        event(h, RGB_RELEASE);
    }
    else
    {
        CHECK(object == &h->depth && h->depth == 1);
        h->depth = 0;
        event(h, DEPTH_RELEASE);
    }
}
static void bounds(void *p, void *object, MMI32 *r)
{
    Host *h = (Host *)p;
    CHECK(object == &h->rgb && h->rgb);
    event(h, BOUNDS);
    memcpy(r, h->rect, sizeof(h->rect));
}
static void draw_rgb(void *p, void *object, unsigned char *pixels, MMU32 w, MMU32 height, MMI32 x,
                     MMI32 y)
{
    Host *h = (Host *)p;
    MMU32 i;
    CHECK(object == &h->rgb && h->rgb && w == 4 && height == 3 && x == 2 && y == -3);
    event(h, RGB_DRAW);
    CHECK(h->preview.width == 4 && h->preview.height == 3 && h->preview.rgb_count == 12);
    for (i = 0; i < 12; ++i)
    {
        CHECK(!pixels[i]);
        pixels[i] = (unsigned char)(i + 1);
    }
}
static void draw_depth(void *p, void *object, unsigned short *pixels, MMU32 w, MMU32 height)
{
    Host *h = (Host *)p;
    MMU32 i;
    CHECK(object == &h->depth && h->depth && w == 4 && height == 3);
    event(h, DEPTH_DRAW);
    CHECK(h->preview.depth_count == 12);
    for (i = 0; i < 12; ++i)
    {
        CHECK(pixels[i] == 65535U);
        pixels[i] = (unsigned short)(0x1234 + i);
    }
}
static void publish(void *p, const MMBackgroundPreview *state)
{
    Host *h = (Host *)p;
    CHECK(state == &h->preview && state->camera.far_z == 65536000 && state->camera.matrix.m[3][0] == 196608);
    ++h->publishes;
}
static void selected(void *p, MMU32 slot)
{
    Host *h = (Host *)p;
    CHECK(slot == h->preview.active && !h->depth && !h->rgb && h->preview.depth_count == 12);
    event(h, SELECTED);
}
static void *mesh_new(void *p)
{
    Host *h = (Host *)p;
    Mesh *m;
    if (h->fail_new)
        return NULL;
    CHECK(h->created < 32);
    m = &h->meshes[h->created];
    m->id = h->created++;
    m->live = 1;
    return m;
}
static void mesh_release(void *p, void *object)
{
    Host *h = (Host *)p;
    Mesh *m = (Mesh *)object;
    CHECK(m->live);
    m->live = 0;
    ++h->released;
}
static int mesh_build(void *p, void *object, const MMBackgroundPreview *state, const double *t, const double *q)
{
    Host *h = (Host *)p;
    CHECK(((Mesh *)object)->live && state == &h->preview && state->rgb_count == 12 && state->depth_count == 12);
    CHECK(t[0] == 1 && t[5] == 1 && t[10] == 1 && t[12] == 3 && t[15] == 1 && !t[3] && !t[7] && !t[11]);
    CHECK(q[0] == 1 && q[5] == 1 && q[11] == -1 && q[14] == 2);
    ++h->builds;
    return !h->fail_build;
}
static float scalar_float(void *p, MMI32 value)
{
    Host *h = (Host *)p;
    ++h->scalar_calls;
    return (float)value / 65536.0f;
}
static MMI32 float_scalar(void *p, float value)
{
    (void)p;
    CHECK(value == (float)4 / 3);
    return (MMI32)(value * 65536.0f);
}
static void perspective(void *p, unsigned short fov, MMI32 aspect, MMI32 near_z, MMI32 far_z,
                        MMI32 *out)
{
    (void)p;
    CHECK(fov == 0x2345 && aspect == (MMI32)(((float)4 / 3) * 65536.0f) && near_z == -65536 && far_z == -65536000);
    memset(out, 0, 16 * sizeof(*out));
    out[0] = out[5] = out[10] = 65536;
    out[11] = -65536;
    out[14] = 131072;
}
static void init(Host *h)
{
    MMStorageAllocator a;
    MMBackgroundPreviewServices *s;
    MMBackgroundLight light;
    int i;
    memset(h, 0, sizeof(*h));
    a.context = h;
    a.resize = resize;
    a.release = free_memory;
    CHECK(mm_background_preview_init(&h->preview, &a));
    for (i = 0; i < 3; ++i)
        CHECK(mm_background_preview_camera(&h->preview, 100U + (MMU32)i));
    memset(&light, 0, sizeof(light));
    light.type = 3;
    light.intensity = 123;
    CHECK(mm_background_preview_light(&h->preview, &light));
    CHECK(((MMBackgroundLight *)h->preview.metadata.buffers[1].data)[0].intensity == 123);
    h->wire[0] = 1;
    word(h->wire + 4, 65536);
    word(h->wire + 8, 65536000);
    h->wire[12] = 0x45;
    h->wire[13] = 0x23;
    word(h->wire + 28, 65536);
    word(h->wire + 28 + 4 * 4, 65536);
    word(h->wire + 28 + 8 * 4, 65536);
    word(h->wire + 28 + 9 * 4, 196608);
    CHECK(mm_memory_stream_init(&h->memory, h->wire, 76));
    h->rect[0] = -2;
    h->rect[1] = 3;
    h->rect[2] = 2;
    h->rect[3] = 6;
    s = &h->services;
    s->context = h;
    s->camera_read = camera_read;
    s->child = child;
    s->fetch = fetch;
    s->release = release;
    s->bounds = bounds;
    s->draw_rgb = draw_rgb;
    s->draw_depth = draw_depth;
    s->publish = publish;
    s->selected = selected;
    s->mesh_new = mesh_new;
    s->mesh_release = mesh_release;
    s->mesh_build = mesh_build;
    s->scalar_float = scalar_float;
    s->float_scalar = float_scalar;
    s->perspective = perspective;
}
static int select_camera(Host *h, MMU32 slot)
{
    h->traces = 0;
    return mm_background_preview_select(&h->preview, slot, &h->services);
}
static void dispose(Host *h)
{
    int i;
    mm_background_preview_dispose(&h->preview, &h->services);
    CHECK(!h->live_memory && !h->rgb && !h->depth);
    for (i = 0; i < h->created; ++i)
        CHECK(!h->meshes[i].live);
}
int main(void)
{
    Host h;
    int i, base, created, released, builds, scalar_calls;
    void *a, *b, *c;
    MMU32 keys[2];
    const int expected[] = {READ,        RGB_CHILD,   RGB_FETCH,  BOUNDS,        RGB_DRAW, RGB_RELEASE,
                            DEPTH_CHILD, DEPTH_FETCH, DEPTH_DRAW, DEPTH_RELEASE, SELECTED};
    const int failures[] = {READ, RGB_CHILD, RGB_FETCH, DEPTH_CHILD, DEPTH_FETCH};
    init(&h);
    CHECK(select_camera(&h, 1));
    CHECK(h.traces == 11 && !memcmp(h.trace, expected, sizeof(expected)) && h.publishes == 5 && h.preview.active == 1);
    CHECK(((unsigned short *)h.preview.images.buffers[1].data)[11] == 0x123F &&
          h.preview.images.buffers[0].data[11] == 12);
    h.traces = 0;
    CHECK(!mm_background_preview_select(&h.preview, 3, &h.services) && !h.traces);
    dispose(&h);
    for (i = 0; i < 5; ++i)
    {
        init(&h);
        h.fail = failures[i];
        CHECK(!select_camera(&h, 1) && !h.rgb && !h.depth && !h.preview.active);
        if (i >= 3)
            CHECK(h.preview.rgb_count == 12 && !h.preview.depth_count);
        dispose(&h);
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&h);
        base = h.allocations;
        h.fail_allocation = base + i;
        CHECK(!select_camera(&h, 1) && !h.rgb && !h.depth && !h.preview.active);
        CHECK(h.preview.width == 4 && h.preview.height == 3 && !h.preview.depth_count &&
              h.preview.rgb_count == (i == 1 ? 0U : 12U));
        CHECK(h.trace[h.traces - 1] == (i == 1 ? RGB_RELEASE : DEPTH_RELEASE));
        h.fail_allocation = 0;
        CHECK(select_camera(&h, 1));
        dispose(&h);
    }
    for (i = 0; i < 4; ++i)
    {
        init(&h);
        if (i == 0)
            h.rect[2] = h.rect[0];
        else if (i == 1)
            h.rect[2] = 1023;
        else if (i == 2)
        {
            h.rect[0] = (-2147483647 - 1);
            h.rect[2] = h.rect[0] + 3;
        }
        else
        {
            h.rect[0] = (-2147483647 - 1);
            h.rect[2] = 2147483647;
        }
        CHECK(!select_camera(&h, 1) && !h.rgb && !h.preview.width);
        CHECK(h.trace[h.traces - 1] == RGB_RELEASE);
        dispose(&h);
    }
    init(&h);
    word(h.wire + 4, 0);
    CHECK(!select_camera(&h, 1) && h.publishes == 1 && !h.preview.camera.near_z && h.traces == 1);
    dispose(&h);
    init(&h);
    h.memory.stream.length = 75;
    CHECK(!select_camera(&h, 1) && !h.publishes && h.traces == 1);
    dispose(&h);
    init(&h);
    CHECK(select_camera(&h, 0));
    a = mm_background_preview_mesh(&h.preview, &h.services);
    CHECK(a && h.preview.mesh_count == 1 && h.scalar_calls == 28);
    CHECK(select_camera(&h, 1));
    b = mm_background_preview_mesh(&h.preview, &h.services);
    CHECK(b && b != a && h.preview.mesh_count == 2);
    builds = h.builds;
    scalar_calls = h.scalar_calls;
    CHECK(select_camera(&h, 0));
    CHECK(mm_background_preview_mesh(&h.preview, &h.services) == a && h.builds == builds &&
          h.scalar_calls == scalar_calls);
    CHECK(h.preview.keys[0] == 1 && h.preview.keys[1] == 0);
    CHECK(select_camera(&h, 2));
    memcpy(keys, h.preview.keys, sizeof(keys));
    created = h.created;
    released = h.released;
    h.fail_new = 1;
    CHECK(!mm_background_preview_mesh(&h.preview, &h.services) && h.created == created &&
          h.released == released && !memcmp(keys, h.preview.keys, sizeof(keys)));
    h.fail_new = 0;
    h.fail_build = 1;
    CHECK(!mm_background_preview_mesh(&h.preview, &h.services) && h.released == released + 1 &&
          !memcmp(keys, h.preview.keys, sizeof(keys)));
    h.fail_build = 0;
    c = mm_background_preview_mesh(&h.preview, &h.services);
    CHECK(c && c != a && c != b && h.preview.keys[0] == 0 && h.preview.keys[1] == 2 && !((Mesh *)b)->live &&
          ((Mesh *)a)->live);
    dispose(&h);
    puts("Background preview C passed");
    return 0;
}
