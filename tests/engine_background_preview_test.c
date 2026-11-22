#include "mm/background_preview.h"
#include "mm/masked_bitmap.h"
#include "mm/movie_session.h"
#include "mm/preview_camera.h"
#include "chunk_seed_fixture.h"
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
    SELECTED,
    POINT_READ
};
typedef struct Host
{
    MMBackgroundPreview preview;
    MMBackgroundCameraState placement;
    MMBackgroundPreviewServices services;
    MMMemoryStream memory;
    unsigned char wire[100];
    MMI32 rect[4];
    int allocations, fail_allocation, live_memory, fail, trace[64], traces, publishes, rgb, depth;
    int scalar_calls, invalid_projection;
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
static int read_at(void *p, MMU32 offset, void *out, size_t count)
{
    Host *h = (Host *)p;
    if (offset >= MM_BACKGROUND_CAMERA_BYTES)
    {
        event(h, POINT_READ);
        if (h->fail == POINT_READ)
            return 0;
    }
    return mm_read_exact(&h->memory.stream, offset, out, count);
}
static int camera_payload(void *p, MMU32 type, MMU32 number, MMReadReceiver receive, void *out, int *result)
{
    Host *h = (Host *)p;
    MMReadStream stream = h->memory.stream;
    CHECK(type == MM_BACKGROUND_CAMERA && number >= 100 && number < 103);
    event(h, READ);
    if (h->fail == READ)
        return MM_CHUNK_NOT_FOUND;
    stream.context = h;
    stream.read_at = read_at;
    *result = receive(out, &stream);
    return MM_CHUNK_OK;
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
static void release(void *p, void *object, int depth)
{
    Host *h = (Host *)p;
    if (!depth)
    {
        CHECK(object == &h->rgb && h->rgb == 1);
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
    CHECK(state == &h->preview && state->camera.data.far_z == 65536000 && state->camera.data.matrix.m[3][0] == 196608);
    ++h->publishes;
}
static void selected(void *p, MMU32 slot)
{
    Host *h = (Host *)p;
    CHECK(slot == h->preview.active && !h->depth && !h->rgb && h->preview.depth_count == 12);
    event(h, SELECTED);
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
    const Host *h = (const Host *)p;
    CHECK(fov == 0x2345 && aspect == (MMI32)(((float)4 / 3) * 65536.0f) && near_z == -65536 && far_z == -65536000);
    memset(out, 0, 16 * sizeof(*out));
    out[0] = h->invalid_projection ? 0 : 65536;
    out[5] = out[10] = 65536;
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
    CHECK(mm_background_camera_init(&h->placement, &a));
    CHECK(mm_background_preview_init(&h->preview, &h->placement, &a));
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
    s->payload = camera_payload;
    s->child = child;
    s->fetch = fetch;
    s->release = release;
    s->bounds = bounds;
    s->draw_rgb = draw_rgb;
    s->draw_depth = draw_depth;
    s->publish = publish;
    s->selected = selected;
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
    mm_background_preview_dispose(&h->preview, &h->services);
    mm_background_camera_dispose(&h->placement);
    CHECK(!h->live_memory && !h->rgb && !h->depth);
}
static void placement(void)
{
    Host h;
    MMActorPoint point;
    init(&h);
    CHECK(!mm_background_preview_place(&h.preview, &point));
    h.memory.stream.length = sizeof(h.wire);
    word(h.wire + 16, 5);
    word(h.wire + 76, 6);
    word(h.wire + 88, 7);
    CHECK(mm_background_preview_select_placement(&h.preview, 1, &h.services) == 1);
    CHECK(h.traces == 13 && h.trace[0] == READ && h.trace[1] == POINT_READ && h.trace[2] == POINT_READ &&
          h.trace[3] == RGB_CHILD);
    CHECK(h.preview.camera.number == 101 && h.preview.camera.images.colour == 700 && h.preview.camera.images.depth == 800);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 5);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 6);
    mm_background_preview_reuse_place(&h.preview);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 6);

    word(h.wire + 16, 55);
    word(h.wire + 76, 66);
    word(h.wire + 88, 77);
    CHECK(select_camera(&h, 2));
    CHECK(h.traces == 11 && h.trace[1] == RGB_CHILD);
    CHECK(h.preview.active == 2 && h.placement.current == 1);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 7);
    h.traces = 0;
    h.fail_allocation = h.allocations + 1;
    CHECK(mm_background_preview_select_placement(&h.preview, 0, &h.services) == MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY);
    CHECK(h.traces == 1 && h.preview.camera.number == 102 && h.placement.current == 1);
    h.fail_allocation = 0;
    h.traces = 0;
    h.fail = POINT_READ;
    CHECK(!mm_background_preview_select_placement(&h.preview, 0, &h.services));
    CHECK(h.traces == 2 && h.preview.camera.number == 102 && h.placement.current == 1);
    h.fail = DEPTH_CHILD;
    h.traces = 0;
    CHECK(!mm_background_preview_select_placement(&h.preview, 0, &h.services));
    CHECK(h.preview.active == 2 && h.preview.camera.images.present == MM_BACKGROUND_HAS_COLOUR &&
          h.placement.current == 1);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 5);
    h.fail = h.traces = 0;
    CHECK(mm_background_preview_select_placement(&h.preview, 0, &h.services) == 1 &&
          mm_background_preview_place(&h.preview, &point) && point.dxr == 55);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 66);
    h.memory.stream.length = 76;
    h.traces = 0;
    CHECK(mm_background_preview_select_placement(&h.preview, 1, &h.services) == 1 && !h.placement.point_count);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 55);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 55);
    dispose(&h);
}
static void prepared_camera(void)
{
    Host h;
    MMBackgroundCameraBinding binding;
    MMActorPoint point;
    init(&h);
    word(h.wire + 16, 6);
    CHECK(mm_background_camera_binding_read(&h.memory.stream, 101, &binding));
    binding.images.present = MM_BACKGROUND_HAS_COLOUR;
    binding.images.colour = 700;
    h.services.payload = NULL;
    h.services.child = NULL;
    CHECK(!mm_background_preview_apply(&h.preview, 0, &binding, &h.memory.stream, 1, &h.services));
    CHECK(!h.traces);
    CHECK(!mm_background_preview_apply(&h.preview, 1, &binding, &h.memory.stream, 1, &h.services));
    CHECK(h.trace[0] == RGB_FETCH && h.trace[h.traces - 1] == RGB_RELEASE);
    CHECK(h.preview.rgb_count == 12 && !h.preview.depth_count && !mm_background_preview_place(&h.preview, &point));
    binding.images.present |= MM_BACKGROUND_HAS_DEPTH;
    binding.images.depth = 800;
    h.traces = 0;
    CHECK(mm_background_preview_apply(&h.preview, 1, &binding, &h.memory.stream, 1, &h.services) == 1);
    CHECK(h.traces == 8 && h.trace[0] == RGB_FETCH && h.trace[7] == SELECTED);
    CHECK(mm_background_preview_place(&h.preview, &point) && point.dxr == 6);
    dispose(&h);
}

typedef struct GraphFixture
{
    ChunkIndexFixture index;
    const MMMaskedBitmap *bitmap;
    unsigned char root[8], palette[16], lights[12], camera[88], depth[20];
    int camera_reads;
} GraphFixture;

static void graph_access(void *p, MMChunkAccess *out)
{
    GraphFixture *graph = p;
    *out = fixture_chunk_access(&graph->index);
}

static int graph_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    GraphFixture *graph = p;
    MMMemoryStream stream;
    const void *bytes;
    size_t size;
    switch (entry->type)
    {
    case MM_BACKGROUND: bytes = graph->root; size = sizeof(graph->root); break;
    case MM_BACKGROUND_COLORS: bytes = graph->palette; size = sizeof(graph->palette); break;
    case MM_BACKGROUND_LIGHTS: bytes = graph->lights; size = sizeof(graph->lights); break;
    case MM_BACKGROUND_CAMERA:
        ++graph->camera_reads;
        bytes = graph->camera;
        size = sizeof(graph->camera);
        break;
    case 0x4d424d50UL: bytes = graph->bitmap->data; size = graph->bitmap->bytes; break;
    case 0x5a424d50UL: bytes = graph->depth; size = sizeof(graph->depth); break;
    default: return MM_MOVIE_BAD_FILE;
    }
    CHECK(mm_memory_stream_init(&stream, bytes, size));
    return receive(out, &stream.stream);
}

static float graph_scalar(void *p, MMI32 value)
{
    (void)p;
    return value / 65536.0f;
}

static void graph_owner(void)
{
    Host allocation = {0};
    GraphFixture graph = {0};
    MMMovieSessionGraph source = {NULL, graph_access, NULL, graph_payload, NULL};
    MMStorageAllocator allocator = {NULL, resize, free_memory};
    MMBackgroundPreviewServices renderer = {0};
    MMBackgroundPreviewOwner *owner = NULL, *previous;
    const MMBackgroundPreview *view;
    const unsigned char *palette;
    MMPreviewProjection projection;
    MMCollectionHeader header = {0};
    MMMaskedOwner *bitmap;
    MMMaskedRect bounds = {0, 0, 2, 2};
    unsigned char colours[1024] = {0};
    const unsigned char pixels[] = {7, 8, 9, 10}, authored[] = {11, 22, 128, 33};
    float point[3];
    int i, allocations;

    source.context = &graph;
    allocator.context = &allocation;
    renderer.scalar_float = graph_scalar;
    graph.root[0] = graph.camera[0] = graph.depth[0] = 1;
    graph.root[4] = 42;
    graph.depth[8] = graph.depth[10] = 2;
    memset(graph.depth + 12, 255, 8);
    word(graph.camera + 4, 65536);
    word(graph.camera + 8, 65536000);
    graph.camera[13] = 0x40;
    word(graph.camera + 16, 2 * 65536);
    word(graph.camera + 76, 3 * 65536);
    for (i = 0; i < 3; ++i)
        word(graph.camera + 28 + 16 * i, 65536);
    header.kind = MM_COLLECTION_ARRAY;
    header.big_endian = mm_masked_native_big_endian();
    header.width = 4;
    header.count = 1;
    CHECK(mm_collection_header_write(&header, graph.palette, sizeof(graph.palette)) == MM_STORAGE_OK);
    memcpy(graph.palette + 12, authored, sizeof(authored));
    header.width = 56;
    header.count = 0;
    CHECK(mm_collection_header_write(&header, graph.lights, sizeof(graph.lights)) == MM_STORAGE_OK);
    bitmap = mm_masked_owner_new(pixels, 2, 2, &bounds, 0, 0, 0, 0, 0, NULL);
    CHECK(bitmap);
    graph.bitmap = mm_masked_owner_bitmap(bitmap);
    fixture_chunk_seed_link(&graph.index, MM_BACKGROUND, 1, MM_BACKGROUND_COLORS, 2, 0);
    fixture_chunk_seed_link(&graph.index, MM_BACKGROUND, 1, MM_BACKGROUND_LIGHTS, 3, 0);
    fixture_chunk_seed_link(&graph.index, MM_BACKGROUND, 1, MM_BACKGROUND_CAMERA, 4, 0);
    fixture_chunk_seed_link(&graph.index, MM_BACKGROUND_CAMERA, 4, 0x4d424d50UL, 5, 0);
    fixture_chunk_seed_link(&graph.index, MM_BACKGROUND_CAMERA, 4, 0x5a424d50UL, 6, 0);

    for (i = 1; i <= 2; ++i)
    {
        allocation.fail_allocation = allocation.allocations + i;
        CHECK(mm_background_preview_open(&owner, &source, 1, colours, &renderer, &allocator) == -1);
        CHECK(!owner && !allocation.live_memory);
    }
    allocation.fail_allocation = 0;
    CHECK(mm_background_preview_open(&owner, &source, 1, colours, &renderer, &allocator) == 1);
    view = mm_background_preview_state(owner);
    CHECK(view->camera_count == 1 && !view->active && view->width == 2 && view->height == 2);
    CHECK(view->rgb_count == sizeof(pixels) && !memcmp(view->images.buffers[0].data, pixels, sizeof(pixels)));
    palette = mm_background_preview_palette(owner);
    CHECK(palette[42 * 4] == 11 && palette[42 * 4 + 2] == 127 && palette[42 * 4 + 3] == 0);
    mm_background_preview_overlay_palette(owner, colours);
    CHECK(!memcmp(colours + 42 * 4, authored, sizeof(authored)) && !colours[1020]);
    CHECK(mm_background_preview_next_place(owner, point) && point[0] == 2.f);
    CHECK(mm_background_preview_next_place(owner, point) && point[0] == 3.f);
    mm_background_preview_reuse_placement(owner);
    CHECK(mm_background_preview_next_place(owner, point) && point[0] == 3.f);
    i = graph.camera_reads;
    CHECK(mm_background_preview_projection(owner, 0, &projection) == MM_PREVIEW_CAMERA_OK && projection.aspect == 1.f);
    CHECK(mm_background_preview_projection(owner, 0, &projection) == MM_PREVIEW_CAMERA_OK && graph.camera_reads == i + 1);
    previous = owner;
    allocations = allocation.live_memory;
    CHECK(!mm_background_preview_open(&owner, &source, 99, colours, &renderer, &allocator));
    CHECK(owner == previous && allocation.live_memory == allocations);
    mm_background_preview_close(owner);
    CHECK(!allocation.live_memory);
    mm_masked_owner_release(bitmap);
}

int main(void)
{
    Host h;
    int i, base, mesh_allocations, scalar_calls, live_memory;
    MMBackgroundMesh *a, *b, *c;
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
    CHECK(!select_camera(&h, 1) && h.publishes == 1 && !h.preview.camera.data.near_z && h.traces == 1);
    dispose(&h);
    init(&h);
    h.memory.stream.length = 75;
    CHECK(!select_camera(&h, 1) && !h.publishes && h.traces == 1);
    dispose(&h);
    init(&h);
    CHECK(select_camera(&h, 0));
    base = h.allocations;
    a = mm_background_preview_mesh(&h.preview, &h.services);
    mesh_allocations = h.allocations - base;
    CHECK(a && h.preview.mesh_count == 1 && h.scalar_calls == 28 && mesh_allocations > 1);
    CHECK(a->triangle_count == 2 && a->vertex_count == 4 && a->infinite_count == 0);
    CHECK(a->low[0] > .6f && a->low[0] < .8f && a->high[0] > 4.1f && a->high[0] < 4.3f);
    CHECK(select_camera(&h, 1));
    b = mm_background_preview_mesh(&h.preview, &h.services);
    CHECK(b && b != a && h.preview.mesh_count == 2);
    scalar_calls = h.scalar_calls;
    CHECK(select_camera(&h, 0));
    CHECK(mm_background_preview_mesh(&h.preview, &h.services) == a && h.scalar_calls == scalar_calls);
    CHECK(h.preview.keys[0] == 1 && h.preview.keys[1] == 0);
    CHECK(select_camera(&h, 2));
    memcpy(keys, h.preview.keys, sizeof(keys));
    live_memory = h.live_memory;
    for (i = 1; i <= mesh_allocations; ++i)
    {
        h.fail_allocation = h.allocations + i;
        CHECK(!mm_background_preview_mesh(&h.preview, &h.services) && h.live_memory == live_memory &&
              !memcmp(keys, h.preview.keys, sizeof(keys)) && h.preview.mesh_count == 2 &&
              h.preview.meshes[0] == b && h.preview.meshes[1] == a);
    }
    h.fail_allocation = 0;
    c = mm_background_preview_mesh(&h.preview, &h.services);
    CHECK(c && c != a && h.preview.keys[0] == 0 && h.preview.keys[1] == 2 &&
          h.preview.meshes[0] == a && h.preview.meshes[1] == c && h.live_memory == live_memory);
    CHECK(select_camera(&h, 1));
    h.invalid_projection = 1;
    b = mm_background_preview_mesh(&h.preview, &h.services);
    CHECK(b && !b->triangle_count && !b->infinite_count && h.preview.keys[0] == 2 && h.preview.keys[1] == 1);
    dispose(&h);
    placement();
    prepared_camera();
    graph_owner();
    puts("Background preview C passed");
    return 0;
}
