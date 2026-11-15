/* Background depth reconstruction and renderer ownership. */
#include "three_mm/background_mesh.h"
#include <float.h>
#include <math.h>
#include <string.h>
static int mesh_finite(double v)
{
    return v == v && v <= DBL_MAX && v >= -DBL_MAX;
}
static double minimum(double a, double b)
{
    return b < a ? b : a;
}
static double maximum(double a, double b)
{
    return a < b ? b : a;
}
static int continuous(double near_value, double far_value)
{
    return mesh_finite(far_value) && near_value > 0 && far_value - near_value <= maximum(.02, near_value * .08);
}
static int reserve(ThreeMMCollectionStorage *s, unsigned section, ThreeMMU32 count, ThreeMMU32 stride)
{
    ThreeMMU32 bytes, capacity;
    if (!stride || count > THREE_MM_STORAGE_LIMIT / stride)
        return 0;
    bytes = count * stride;
    capacity = s->buffers[section].capacity;
    if (bytes <= capacity)
        return 1;
    if (capacity < 1024)
        capacity = 1024;
    while (capacity < bytes)
    {
        if (capacity > THREE_MM_STORAGE_LIMIT / 2)
        {
            capacity = bytes;
            break;
        }
        capacity *= 2;
    }
    return three_mm_storage_resize_exact(s, section, capacity, THREE_MM_STORAGE_NORMAL) == THREE_MM_STORAGE_OK;
}
static void prepared_dispose(ThreeMMBackgroundPrepared *p, const ThreeMMBackgroundMeshServices *s)
{
    ThreeMMU32 i;
    void **models = (void **)p->models.buffers[0].data;
    for (i = 0; i < p->count; ++i)
    {
        s->model_remove(s->context, models[i]);
        s->model_free(s->context, models[i]);
    }
    if (p->material)
    {
        if (p->material_added)
            s->material_remove(s->context, p->material);
        s->material_free(s->context, p->material);
    }
    if (p->map)
    {
        if (p->map_added)
            s->map_remove(s->context, p->map);
        s->map_free(s->context, p->map);
    }
    p->map = p->material = NULL;
    p->count = 0;
    p->map_added = p->material_added = 0;
    three_mm_storage_dispose(&p->models);
}
int three_mm_background_mesh_init(ThreeMMBackgroundMesh *m, const ThreeMMStorageAllocator *allocator)
{
    memset(m, 0, sizeof(*m));
    return three_mm_storage_init(&m->geometry, allocator) == THREE_MM_STORAGE_OK &&
           three_mm_storage_init(&m->pixels, allocator) == THREE_MM_STORAGE_OK &&
           three_mm_storage_init(&m->prepared.models, allocator) == THREE_MM_STORAGE_OK;
}
void three_mm_background_mesh_dispose(ThreeMMBackgroundMesh *m, const ThreeMMBackgroundMeshServices *s)
{
    prepared_dispose(&m->prepared, s);
    three_mm_storage_dispose(&m->geometry);
    three_mm_storage_dispose(&m->pixels);
    m->vertex_count = m->triangle_count = m->width = m->height = m->infinite_count = 0;
    memset(m->low, 0, sizeof(m->low));
    memset(m->high, 0, sizeof(m->high));
}
typedef struct Build
{
    ThreeMMBackgroundMesh *mesh;
    ThreeMMU32 width, height;
    double *distances;
    ThreeMMI32 *indices;
    const double *camera, *projection;
    int ok;
} Build;
static ThreeMMU32 vertex(Build *b, ThreeMMU32 x, ThreeMMU32 y)
{
    ThreeMMBackgroundMesh *m = b->mesh;
    ThreeMMI32 *index = &b->indices[y * b->width + x];
    ThreeMMBackgroundVertex point;
    ThreeMMU32 result, axis;
    double distance, px, py, world[3];
    if (*index >= 0)
        return (ThreeMMU32)*index;
    distance = b->distances[y * b->width + x];
    px = ((double)x - b->width / 2) / (b->width / 2) * distance / b->projection[0];
    py = (b->height / 2 - (double)y) / (b->height / 2) * distance / b->projection[5];
    for (axis = 0; axis < 3; ++axis)
        world[axis] =
            px * b->camera[axis] + py * b->camera[4 + axis] - distance * b->camera[8 + axis] + b->camera[12 + axis];
    for (axis = 0; axis < 3; ++axis)
        if (!mesh_finite(world[axis]) || world[axis] > FLT_MAX || world[axis] < -FLT_MAX)
        {
            b->ok = -1;
            return 0;
        }
    point.x = world[0];
    point.y = world[1];
    point.z = world[2];
    point.u = x + .5;
    point.v = y + .5;
    result = m->vertex_count;
    if (!reserve(&m->geometry, 0, result + 1, sizeof(point)))
    {
        b->ok = 0;
        return 0;
    }
    ((ThreeMMBackgroundVertex *)m->geometry.buffers[0].data)[result] = point;
    *index = (ThreeMMI32)result;
    ++m->vertex_count;
    for (axis = 0; axis < 3; ++axis)
    {
        if (!result || (float)world[axis] < m->low[axis])
            m->low[axis] = (float)world[axis];
        if (!result || m->high[axis] < (float)world[axis])
            m->high[axis] = (float)world[axis];
    }
    return result;
}
static void triangle(Build *b, ThreeMMU32 ax, ThreeMMU32 ay, ThreeMMU32 bx, ThreeMMU32 by, ThreeMMU32 cx, ThreeMMU32 cy)
{
    double a = b->distances[ay * b->width + ax], v = b->distances[by * b->width + bx],
           c = b->distances[cy * b->width + cx];
    ThreeMMBackgroundTriangle t;
    ThreeMMBackgroundMesh *m = b->mesh;
    if (!continuous(minimum(a, minimum(v, c)), maximum(a, maximum(v, c))))
        return;
    t.vertices[0] = vertex(b, ax, ay);
    if (b->ok <= 0)
        return;
    t.vertices[1] = vertex(b, bx, by);
    if (b->ok <= 0)
        return;
    t.vertices[2] = vertex(b, cx, cy);
    if (b->ok <= 0)
        return;
    if (!reserve(&m->geometry, 1, m->triangle_count + 1, sizeof(t)))
    {
        b->ok = 0;
        return;
    }
    ((ThreeMMBackgroundTriangle *)m->geometry.buffers[1].data)[m->triangle_count++] = t;
}
static void cell(Build *b, ThreeMMU32 left, ThreeMMU32 top, ThreeMMU32 right, ThreeMMU32 bottom)
{
    double near_value = HUGE_VAL, far_value = 0, d;
    ThreeMMU32 x, y, middle;
    if (b->ok <= 0)
        return;
    for (y = top; y <= bottom; ++y)
        for (x = left; x <= right; ++x)
        {
            d = b->distances[y * b->width + x];
            near_value = minimum(near_value, d);
            far_value = maximum(far_value, d);
        }
    if (!mesh_finite(near_value))
        return;
    if (!continuous(near_value, far_value) && (right - left > 1 || bottom - top > 1))
    {
        if (right - left >= bottom - top && right - left > 1)
        {
            middle = (left + right) / 2;
            cell(b, left, top, middle, bottom);
            cell(b, middle, top, right, bottom);
        }
        else
        {
            middle = (top + bottom) / 2;
            cell(b, left, top, right, middle);
            cell(b, left, middle, right, bottom);
        }
        return;
    }
    triangle(b, left, top, right, top, right, bottom);
    if (b->ok > 0)
        triangle(b, left, top, right, bottom, left, bottom);
}
static void spread(unsigned char *filled, ThreeMMU32 *queue, ThreeMMU32 *tail, unsigned char *colors, ThreeMMU32 index,
                   ThreeMMU32 next)
{
    if (!filled[next])
    {
        colors[next] = colors[index];
        filled[next] = 1;
        queue[(*tail)++] = next;
    }
}
int three_mm_background_mesh_build(ThreeMMBackgroundMesh *m, ThreeMMU32 width, ThreeMMU32 height,
                                   const unsigned char *colors, ThreeMMU32 color_count, const unsigned short *depths,
                                   ThreeMMU32 depth_count, const double camera[16], const double projection[16],
                                   const ThreeMMBackgroundMeshServices *s)
{
    ThreeMMCollectionStorage scratch[2];
    Build b;
    ThreeMMU32 n, i, x, y, head, tail = 0, *queue;
    unsigned char *filled, *infinite_colors;
    double denominator, distance;
    int result = 1, has_infinite = 0;
    prepared_dispose(&m->prepared, s);
    three_mm_storage_init(&m->prepared.models, &m->geometry.allocator);
    m->vertex_count = m->triangle_count = m->width = m->height = m->infinite_count = 0;
    memset(m->low, 0, sizeof(m->low));
    memset(m->high, 0, sizeof(m->high));
    if (!width || !height || width > 1024 || height > 1024 || color_count != width * height ||
        depth_count != color_count || !colors || !depths || !projection[0] || !projection[5])
        return 0;
    for (i = 0; i < 16; ++i)
        if (!mesh_finite(camera[i]) || !mesh_finite(projection[i]))
            return 0;
    n = width * height;
    three_mm_storage_init(&scratch[0], &m->geometry.allocator);
    three_mm_storage_init(&scratch[1], &m->geometry.allocator);
    for (i = 0; i < n; ++i)
        if (depths[i] == 65535U)
        {
            has_infinite = 1;
            break;
        }
    if (has_infinite)
    {
        if (!reserve(&m->pixels, 1, n, 1))
        {
            result = -1;
            goto done;
        }
        infinite_colors = m->pixels.buffers[1].data;
        memcpy(infinite_colors, colors, n);
        m->infinite_count = n;
        if (!reserve(&scratch[1], 0, n, 1) || !reserve(&scratch[1], 1, n, sizeof(*queue)))
        {
            result = -1;
            goto done;
        }
        filled = scratch[1].buffers[0].data;
        memset(filled, 0, n);
        queue = (ThreeMMU32 *)scratch[1].buffers[1].data;
        for (i = 0; i < n; ++i)
            if (depths[i] == 65535U)
            {
                filled[i] = 1;
                queue[tail++] = i;
            }
        for (head = 0; head < tail; ++head)
        {
            i = queue[head];
            if (i % width)
                spread(filled, queue, &tail, infinite_colors, i, i - 1);
            if (i % width + 1 < width)
                spread(filled, queue, &tail, infinite_colors, i, i + 1);
            if (i >= width)
                spread(filled, queue, &tail, infinite_colors, i, i - width);
            if (i + width < n)
                spread(filled, queue, &tail, infinite_colors, i, i + width);
        }
    }
    if (width < 2 || height < 2)
        goto done;
    if (!reserve(&m->pixels, 0, n, 1))
    {
        result = -1;
        goto done;
    }
    memcpy(m->pixels.buffers[0].data, colors, n);
    m->width = width;
    m->height = height;
    if (!reserve(&scratch[0], 0, n, sizeof(double)) || !reserve(&scratch[0], 1, n, sizeof(ThreeMMI32)))
    {
        result = -1;
        goto done;
    }
    memset(&b, 0, sizeof(b));
    b.mesh = m;
    b.width = width;
    b.height = height;
    b.camera = camera;
    b.projection = projection;
    b.ok = 1;
    b.distances = (double *)scratch[0].buffers[0].data;
    b.indices = (ThreeMMI32 *)scratch[0].buffers[1].data;
    for (i = 0; i < n; ++i)
    {
        b.distances[i] = HUGE_VAL;
        b.indices[i] = -1;
        if (depths[i] != 65535U)
        {
            denominator =
                projection[10] - (depths[i] <= 32767U ? (double)depths[i] : (double)depths[i] - 65536.0) / 32768.0;
            if (denominator != 0)
            {
                distance = projection[14] / denominator;
                if (mesh_finite(distance) && distance > 0)
                    b.distances[i] = distance;
            }
        }
    }
    for (y = 0; y + 1 < height && b.ok > 0; y += 4)
        for (x = 0; x + 1 < width && b.ok > 0; x += 4)
            cell(&b, x, y, width - 1 < x + 4 ? width - 1 : x + 4, height - 1 < y + 4 ? height - 1 : y + 4);
    if (!b.ok)
        result = -1;
    else if (b.ok < 0)
        result = 0;
done:
    three_mm_storage_dispose(&scratch[1]);
    three_mm_storage_dispose(&scratch[0]);
    return result;
}
int three_mm_background_mesh_add(ThreeMMBackgroundMesh *m, void *world, const ThreeMMBackgroundMeshServices *s)
{
    ThreeMMBackgroundPrepared p;
    ThreeMMCollectionStorage scratch;
    ThreeMMI32 *indices;
    ThreeMMU32 *selected, selected_count, model_count, first, count, i, face, corner, index, face_indices[3], y;
    ThreeMMBackgroundVertex value, *vertices = (ThreeMMBackgroundVertex *)m->geometry.buffers[0].data;
    ThreeMMBackgroundTriangle *triangles = (ThreeMMBackgroundTriangle *)m->geometry.buffers[1].data;
    void *model, *actor;
    unsigned char *row;
    if (!m->triangle_count)
        return 1;
    if (!m->prepared.map)
    {
        memset(&p, 0, sizeof(p));
        three_mm_storage_init(&p.models, &m->geometry.allocator);
        three_mm_storage_init(&scratch, &m->geometry.allocator);
        p.map = s->map_new(s->context, m->width, m->height);
        if (!p.map)
            goto fail;
        for (y = 0; y < m->height; ++y)
        {
            row = s->map_row(s->context, p.map, y);
            if (!row)
                goto fail;
            memcpy(row, m->pixels.buffers[0].data + y * m->width, m->width);
        }
        s->map_add(s->context, p.map);
        p.map_added = 1;
        p.material = s->material_new(s->context);
        if (!p.material)
            goto fail;
        s->material_configure(s->context, p.material, p.map);
        s->material_add(s->context, p.material);
        p.material_added = 1;
        model_count = (m->triangle_count + 4095U) / 4096U;
        if (!reserve(&p.models, 0, model_count, sizeof(void *)) ||
            !reserve(&scratch, 0, m->vertex_count, sizeof(ThreeMMI32)) ||
            !reserve(&scratch, 1, 12288U, sizeof(ThreeMMU32)))
            goto fail;
        indices = (ThreeMMI32 *)scratch.buffers[0].data;
        selected = (ThreeMMU32 *)scratch.buffers[1].data;
        for (first = 0; first < m->triangle_count; first += 4096U)
        {
            for (i = 0; i < m->vertex_count; ++i)
                indices[i] = -1;
            selected_count = 0;
            count = m->triangle_count - first;
            if (count > 4096U)
                count = 4096U;
            for (face = first; face < first + count; ++face)
                for (corner = 0; corner < 3; ++corner)
                {
                    index = triangles[face].vertices[corner];
                    if (indices[index] < 0)
                    {
                        indices[index] = (ThreeMMI32)selected_count;
                        selected[selected_count++] = index;
                    }
                }
            model = s->model_new(s->context, selected_count, count);
            if (!model)
                goto fail;
            s->model_configure(s->context, model);
            for (i = 0; i < selected_count; ++i)
            {
                value = vertices[selected[i]];
                value.u /= m->width;
                value.v /= m->height;
                s->vertex(s->context, model, i, &value);
            }
            for (i = 0; i < count; ++i)
            {
                for (corner = 0; corner < 3; ++corner)
                    face_indices[corner] = (ThreeMMU32)indices[triangles[first + i].vertices[corner]];
                s->face(s->context, model, i, face_indices, p.material);
            }
            s->model_add(s->context, model);
            ((void **)p.models.buffers[0].data)[p.count++] = model;
        }
        three_mm_storage_dispose(&scratch);
        three_mm_storage_dispose(&m->prepared.models);
        m->prepared = p;
    }
    for (i = 0; i < m->prepared.count; ++i)
    {
        actor = s->actor_new(s->context);
        if (!actor)
            return 0;
        s->actor_configure(s->context, actor, ((void **)m->prepared.models.buffers[0].data)[i], m->prepared.material);
        s->actor_add(s->context, world, actor);
    }
    return 1;
fail:
    three_mm_storage_dispose(&scratch);
    prepared_dispose(&p, s);
    return 0;
}
