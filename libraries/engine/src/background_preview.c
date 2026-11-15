/* Preview background image ownership and two-entry mesh cache. */
#include "three_mm/background_preview.h"
#include <string.h>
#define MINIMUM (-2147483647 - 1)
typedef char depth_sample_must_be_two_bytes[sizeof(unsigned short) == 2 ? 1 : -1];
static int ensure(ThreeMMCollectionStorage *s, unsigned section, ThreeMMU32 count, ThreeMMU32 stride)
{
    ThreeMMU32 bytes, capacity;
    if (count >= THREE_MM_STORAGE_LIMIT / stride)
        return 0;
    bytes = count * stride;
    capacity = s->buffers[section].capacity;
    if (capacity >= bytes)
        return 1;
    if (capacity < 1024)
        capacity = 1024;
    while (capacity < bytes)
    {
        if (capacity >= THREE_MM_STORAGE_LIMIT / 2)
        {
            capacity = bytes;
            break;
        }
        capacity *= 2;
    }
    return three_mm_storage_resize_exact(s, section, capacity, THREE_MM_STORAGE_NORMAL) == THREE_MM_STORAGE_OK;
}
int three_mm_background_preview_init(ThreeMMBackgroundPreview *p, const ThreeMMStorageAllocator *allocator)
{
    memset(p, 0, sizeof(*p));
    return three_mm_storage_init(&p->metadata, allocator) == THREE_MM_STORAGE_OK &&
           three_mm_storage_init(&p->images, allocator) == THREE_MM_STORAGE_OK;
}
void three_mm_background_preview_dispose(ThreeMMBackgroundPreview *p, const ThreeMMBackgroundPreviewServices *s)
{
    ThreeMMU32 i;
    for (i = 0; i < p->mesh_count; ++i)
        s->mesh_release(s->context, p->meshes[i]);
    p->mesh_count = 0;
    three_mm_storage_dispose(&p->images);
    three_mm_storage_dispose(&p->metadata);
    p->camera_count = p->light_count = p->rgb_count = p->depth_count = p->width = p->height = 0;
}
int three_mm_background_preview_camera(ThreeMMBackgroundPreview *p, ThreeMMU32 number)
{
    if (!ensure(&p->metadata, 0, p->camera_count + 1, sizeof(number)))
        return 0;
    ((ThreeMMU32 *)p->metadata.buffers[0].data)[p->camera_count++] = number;
    return 1;
}
int three_mm_background_preview_light(ThreeMMBackgroundPreview *p, const ThreeMMBackgroundLight *light)
{
    if (!ensure(&p->metadata, 1, p->light_count + 1, sizeof(*light)))
        return 0;
    ((ThreeMMBackgroundLight *)p->metadata.buffers[1].data)[p->light_count++] = *light;
    return 1;
}
int three_mm_background_preview_select(ThreeMMBackgroundPreview *p, ThreeMMU32 slot,
                                       const ThreeMMBackgroundPreviewServices *s)
{
    ThreeMMReadStream stream;
    ThreeMMBackgroundCameraData camera;
    ThreeMMU32 number, child, width, height, n;
    ThreeMMI32 r[4];
    void *bitmap;
    if (slot >= p->camera_count)
        return 0;
    number = ((ThreeMMU32 *)p->metadata.buffers[0].data)[slot];
    if (!s->camera_read(s->context, number, &stream) || !three_mm_background_camera_decode(&stream, &camera))
        return 0;
    p->camera = camera;
    s->publish(s->context, p);
    if (camera.near_z <= 0 || camera.far_z <= camera.near_z)
        return 0;
    if (!s->child(s->context, number, 0, &child) || !(bitmap = s->fetch(s->context, child, 0)))
        return 0;
    s->bounds(s->context, bitmap, r);
    if (r[2] <= r[0] || r[3] <= r[1])
    {
        s->release(s->context, bitmap);
        return 0;
    }
    width = (ThreeMMU32)r[2] - (ThreeMMU32)r[0];
    height = (ThreeMMU32)r[3] - (ThreeMMU32)r[1];
    if (width > 1024U || height > 1024U || r[0] == MINIMUM || r[1] == MINIMUM)
    {
        s->release(s->context, bitmap);
        return 0;
    }
    p->width = width;
    p->height = height;
    n = width * height;
    s->publish(s->context, p);
    if (!ensure(&p->images, 0, n, 1))
    {
        s->release(s->context, bitmap);
        return 0;
    }
    memset(p->images.buffers[0].data, 0, n);
    p->rgb_count = n;
    s->publish(s->context, p);
    s->draw_rgb(s->context, bitmap, p->images.buffers[0].data, width, height, -r[0], -r[1]);
    s->release(s->context, bitmap);
    if (!s->child(s->context, number, 1, &child) || !(bitmap = s->fetch(s->context, child, 1)))
        return 0;
    if (!ensure(&p->images, 1, n, 2))
    {
        s->release(s->context, bitmap);
        return 0;
    }
    memset(p->images.buffers[1].data, 255, n * 2);
    p->depth_count = n;
    s->publish(s->context, p);
    s->draw_depth(s->context, bitmap, (unsigned short *)p->images.buffers[1].data, width, height);
    s->release(s->context, bitmap);
    p->active = slot;
    s->publish(s->context, p);
    if (s->selected)
        s->selected(s->context, slot);
    return 1;
}
void *three_mm_background_preview_mesh(ThreeMMBackgroundPreview *p, const ThreeMMBackgroundPreviewServices *s)
{
    ThreeMMU32 i, row, axis, key;
    ThreeMMI32 native_projection[16], aspect;
    double transform[16], projection[16];
    void *mesh;
    for (i = 0; i < p->mesh_count; ++i)
        if (p->keys[i] == p->active)
        {
            mesh = p->meshes[i];
            key = p->keys[i];
            if (i + 1 < p->mesh_count)
            {
                p->meshes[i] = p->meshes[i + 1];
                p->keys[i] = p->keys[i + 1];
            }
            p->meshes[p->mesh_count - 1] = mesh;
            p->keys[p->mesh_count - 1] = key;
            return mesh;
        }
    mesh = s->mesh_new(s->context);
    if (!mesh)
        return NULL;
    memset(transform, 0, sizeof(transform));
    transform[15] = 1;
    for (row = 0; row < 4; ++row)
        for (axis = 0; axis < 3; ++axis)
            transform[row * 4 + axis] = s->scalar_float(s->context, p->camera.matrix.m[row][axis]);
    if (!p->height || p->camera.near_z <= 0 || p->camera.far_z <= p->camera.near_z)
    {
        s->mesh_release(s->context, mesh);
        return NULL;
    }
    aspect = s->float_scalar(s->context, (float)p->width / p->height);
    s->perspective(s->context, p->camera.fov, aspect, -p->camera.near_z, -p->camera.far_z, native_projection);
    for (i = 0; i < 16; ++i)
        projection[i] = s->scalar_float(s->context, native_projection[i]);
    if (!s->mesh_build(s->context, mesh, p, transform, projection))
    {
        s->mesh_release(s->context, mesh);
        return NULL;
    }
    if (p->mesh_count == 2)
    {
        s->mesh_release(s->context, p->meshes[0]);
        p->meshes[0] = p->meshes[1];
        p->keys[0] = p->keys[1];
        --p->mesh_count;
    }
    p->meshes[p->mesh_count] = mesh;
    p->keys[p->mesh_count++] = p->active;
    return mesh;
}
