#include "mm/background_mesh.h"
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
            fprintf(stderr, "Background mesh C: %d\n", __LINE__);                                                      \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Model
{
    MMU32 vertices, faces, puts, face_puts;
    int added;
} Model;
typedef struct Map
{
    unsigned char *bytes;
    MMU32 width, height;
    int added;
} Map;
typedef struct Host
{
    int allocations, fail_allocation, live_memory, fail_map, fail_material, fail_model, model_calls, fail_actor,
        actor_calls;
    int maps, materials, models, world, material, material_added, remove_trace[512], removes;
    Map map;
    int actors[128];
} Host;
static void *resize(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)p;
    void *out;
    (void)old_bytes;
    (void)priority;
    ++h->allocations;
    if (h->allocations == h->fail_allocation)
        return NULL;
    out = realloc(old, bytes);
    if (out && !old)
        ++h->live_memory;
    return out;
}
static void release(void *p, void *old, MMU32 bytes)
{
    Host *h = (Host *)p;
    (void)bytes;
    CHECK(old && h->live_memory > 0);
    --h->live_memory;
    free(old);
}
static void *map_new(void *p, MMU32 w, MMU32 hgt)
{
    Host *h = (Host *)p;
    if (h->fail_map)
        return NULL;
    CHECK(!h->maps);
    h->map.width = w;
    h->map.height = hgt;
    h->map.added = 0;
    h->map.bytes = (unsigned char *)malloc((w + 3) * hgt);
    CHECK(h->map.bytes);
    memset(h->map.bytes, 197, (w + 3) * hgt);
    ++h->maps;
    return &h->map;
}
static unsigned char *row(void *p, void *object, MMU32 y)
{
    Host *h = (Host *)p;
    CHECK(object == &h->map && y < h->map.height);
    return h->map.bytes + y * (h->map.width + 3);
}
static void map_add(void *p, void *object)
{
    Host *h = (Host *)p;
    MMU32 x, y;
    CHECK(object == &h->map && !h->map.added);
    for (y = 0; y < h->map.height; ++y)
        for (x = h->map.width; x < h->map.width + 3; ++x)
            CHECK(h->map.bytes[y * (h->map.width + 3) + x] == 197);
    h->map.added = 1;
}
static void map_remove(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->map && h->map.added);
    h->map.added = 0;
    h->remove_trace[h->removes++] = 3;
}
static void map_free(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->map && !h->map.added && h->maps == 1);
    free(h->map.bytes);
    --h->maps;
}
static void *material_new(void *p)
{
    Host *h = (Host *)p;
    if (h->fail_material)
        return NULL;
    CHECK(!h->materials);
    ++h->materials;
    return &h->material;
}
static void material_configure(void *p, void *object, void *map)
{
    Host *h = (Host *)p;
    CHECK(object == &h->material && map == &h->map && h->map.added);
}
static void material_add(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->material && !h->material_added);
    h->material_added = 1;
}
static void material_remove(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->material && h->material_added && !h->models);
    h->material_added = 0;
    h->remove_trace[h->removes++] = 2;
}
static void material_free(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->material && !h->material_added);
    --h->materials;
}
static void *model_new(void *p, MMU32 vertices, MMU32 faces)
{
    Host *h = (Host *)p;
    Model *m;
    ++h->model_calls;
    if (h->model_calls == h->fail_model)
        return NULL;
    CHECK(vertices <= 12288 && faces <= 4096 && h->material_added);
    m = (Model *)calloc(1, sizeof(*m));
    CHECK(m);
    m->vertices = vertices;
    m->faces = faces;
    ++h->models;
    return m;
}
static void model_configure(void *p, void *object)
{
    (void)p;
    CHECK(!((Model *)object)->added);
}
static void put_vertex(void *p, void *object, MMU32 index, const MMBackgroundVertex *v)
{
    Model *m = (Model *)object;
    (void)p;
    CHECK(index == m->puts++ && index < m->vertices && v->u > 0 && v->u < 1 && v->v > 0 && v->v < 1);
    CHECK(v->z < 0);
}
static void face(void *p, void *object, MMU32 index, const MMU32 *indices, void *material)
{
    Host *h = (Host *)p;
    Model *m = (Model *)object;
    int i;
    CHECK(material == &h->material && index == m->face_puts++ && index < m->faces);
    for (i = 0; i < 3; ++i)
        CHECK(indices[i] < m->vertices);
}
static void model_add(void *p, void *object)
{
    Model *m = (Model *)object;
    (void)p;
    CHECK(!m->added && m->puts == m->vertices && m->face_puts == m->faces);
    m->added = 1;
}
static void model_remove(void *p, void *object)
{
    Host *h = (Host *)p;
    Model *m = (Model *)object;
    CHECK(m->added);
    m->added = 0;
    h->remove_trace[h->removes++] = 1;
}
static void model_free(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(!((Model *)object)->added);
    free(object);
    --h->models;
}
static void *actor_new(void *p)
{
    Host *h = (Host *)p;
    ++h->actor_calls;
    if (h->actor_calls == h->fail_actor)
        return NULL;
    CHECK(h->actor_calls < 128);
    return &h->actors[h->actor_calls];
}
static void actor_configure(void *p, void *actor, void *model, void *material)
{
    Host *h = (Host *)p;
    CHECK(actor && ((Model *)model)->added && material == &h->material);
}
static void actor_add(void *p, void *world, void *actor)
{
    Host *h = (Host *)p;
    CHECK(world == &h->world && actor);
    ++h->world;
}
static void init(Host *h, MMBackgroundMesh *m, MMBackgroundMeshServices *s)
{
    MMStorageAllocator allocator;
    memset(h, 0, sizeof(*h));
    memset(s, 0, sizeof(*s));
    allocator.context = h;
    allocator.resize = resize;
    allocator.release = release;
    CHECK(mm_background_mesh_init(m, &allocator));
    s->context = h;
    s->map_new = map_new;
    s->map_row = row;
    s->map_add = map_add;
    s->map_remove = map_remove;
    s->map_free = map_free;
    s->material_new = material_new;
    s->material_configure = material_configure;
    s->material_add = material_add;
    s->material_remove = material_remove;
    s->material_free = material_free;
    s->model_new = model_new;
    s->model_configure = model_configure;
    s->vertex = put_vertex;
    s->face = face;
    s->model_add = model_add;
    s->model_remove = model_remove;
    s->model_free = model_free;
    s->actor_new = actor_new;
    s->actor_configure = actor_configure;
    s->actor_add = actor_add;
}
static void dispose(Host *h, MMBackgroundMesh *m, MMBackgroundMeshServices *s)
{
    h->world = 0;
    mm_background_mesh_dispose(m, s);
    CHECK(!h->maps && !h->materials && !h->models && !h->live_memory);
}
static void matrices(double *camera, double *projection)
{
    memset(camera, 0, 16 * sizeof(double));
    memset(projection, 0, 16 * sizeof(double));
    camera[0] = camera[5] = camera[10] = camera[15] = 1;
    projection[0] = projection[5] = projection[10] = 1;
    projection[11] = -1;
    projection[14] = 2;
}
int main(void)
{
    Host h;
    MMBackgroundMesh m;
    MMBackgroundMeshServices s;
    unsigned char colors[25], *large_colors;
    unsigned short depths[25], *large_depths;
    double camera[16], projection[16], distance;
    MMBackgroundVertex *v;
    MMBackgroundTriangle *t;
    int i, j, result, calls, base;
    MMU32 n = 258U * 129U;
    for (i = 0; i < 25; ++i)
    {
        colors[i] = (unsigned char)i;
        depths[i] = 26214;
    }
    matrices(camera, projection);
    init(&h, &m, &s);
    CHECK(mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s) == 1);
    CHECK(m.vertex_count == 4 && m.triangle_count == 2 && !m.infinite_count);
    distance = 2 / (1 - 26214 / 32768.0);
    v = (MMBackgroundVertex *)m.geometry.buffers[0].data;
    t = (MMBackgroundTriangle *)m.geometry.buffers[1].data;
    CHECK(v[0].x == -distance && v[0].y == distance && v[0].z == -distance && v[0].u == .5 && v[0].v == .5);
    CHECK(t[0].vertices[0] == 0 && t[0].vertices[1] == 1 && t[0].vertices[2] == 2 && t[1].vertices[2] == 3);
    CHECK(mm_background_mesh_add(&m, &h.world, &s) && h.models == 1 && h.world == 1);
    calls = h.model_calls;
    h.world = 0;
    CHECK(mm_background_mesh_add(&m, &h.world, &s) && h.model_calls == calls);
    h.world = 0;
    for (i = 0; i < 25; ++i)
        depths[i] = 65535U;
    CHECK(mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s) == 1);
    CHECK(!m.triangle_count && m.infinite_count == 25 && !memcmp(m.pixels.buffers[1].data, colors, 25));
    CHECK(!h.models && !h.maps && !h.materials && h.removes == 3 && h.remove_trace[0] == 1 && h.remove_trace[1] == 2 &&
          h.remove_trace[2] == 3);
    CHECK(mm_background_mesh_add(&m, &h.world, &s) && !h.world);
    for (i = 0; i < 25; ++i)
        depths[i] = 26214;
    depths[2] = 65535U;
    CHECK(mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s) == 1);
    for (i = 0; i < 25; ++i)
        CHECK(m.pixels.buffers[1].data[i] == colors[2]);
    CHECK(mm_background_mesh_build(&m, 1025, 1, colors, 25, depths, 25, camera, projection, &s) == 0 &&
          !m.triangle_count && !m.infinite_count);
    projection[0] = HUGE_VAL;
    CHECK(mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s) == 0);
    matrices(camera, projection);
    camera[0] = DBL_MAX;
    CHECK(mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s) == 0);
    dispose(&h, &m, &s);
    matrices(camera, projection);
    for (i = 0; i < 25; ++i)
        depths[i] = 26214;
    for (i = 1; i < 30; ++i)
    {
        init(&h, &m, &s);
        h.fail_allocation = i;
        result = mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s);
        CHECK(result == 1 || result == -1);
        if (result < 0)
        {
            h.fail_allocation = 0;
            CHECK(mm_background_mesh_build(&m, 5, 5, colors, 25, depths, 25, camera, projection, &s) == 1);
        }
        dispose(&h, &m, &s);
        if (result == 1)
            break;
    }
    CHECK(i < 30);
    large_colors = (unsigned char *)calloc(n, 1);
    large_depths = (unsigned short *)malloc(n * sizeof(*large_depths));
    CHECK(large_colors && large_depths);
    for (j = 0; (MMU32)j < n; ++j)
        large_depths[j] = 26214;
    for (i = 0; i < 7; ++i)
    {
        init(&h, &m, &s);
        CHECK(mm_background_mesh_build(&m, 258, 129, large_colors, n, large_depths, n, camera, projection, &s) ==
              1);
        base = h.live_memory;
        if (i == 0)
            h.fail_map = 1;
        else if (i == 1)
            h.fail_material = 1;
        else if (i < 4)
            h.fail_model = i - 1;
        else
            h.fail_allocation = h.allocations + i - 3;
        CHECK(!mm_background_mesh_add(&m, &h.world, &s));
        CHECK(!m.prepared.map && !h.maps && !h.models && !h.materials && h.live_memory == base);
        h.fail_map = h.fail_material = h.fail_model = h.fail_allocation = 0;
        h.model_calls = 0;
        CHECK(mm_background_mesh_add(&m, &h.world, &s) && m.prepared.count == 2 && h.models == 2 && h.world == 2);
        h.world = 0;
        h.actor_calls = 0;
        h.fail_actor = 2;
        CHECK(!mm_background_mesh_add(&m, &h.world, &s) && h.world == 1 && h.models == 2);
        dispose(&h, &m, &s);
    }
    free(large_depths);
    free(large_colors);
    puts("Background mesh C passed");
    return 0;
}
