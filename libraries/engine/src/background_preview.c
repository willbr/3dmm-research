/* Preview background image ownership and two-entry mesh cache. */
#include "mm/background_preview.h"
#include <string.h>
#define MINIMUM (-2147483647 - 1)
typedef char depth_sample_must_be_two_bytes[sizeof(unsigned short) == 2 ? 1 : -1];
static int ensure(MMCollectionStorage *s, unsigned section, MMU32 count, MMU32 stride)
{
    MMU32 bytes, capacity;
    if (count >= MM_STORAGE_LIMIT / stride)
        return 0;
    bytes = count * stride;
    capacity = s->buffers[section].capacity;
    if (capacity >= bytes)
        return 1;
    if (capacity < 1024)
        capacity = 1024;
    while (capacity < bytes)
    {
        if (capacity >= MM_STORAGE_LIMIT / 2)
        {
            capacity = bytes;
            break;
        }
        capacity *= 2;
    }
    return mm_storage_resize_exact(s, section, capacity, MM_STORAGE_NORMAL) == MM_STORAGE_OK;
}
int mm_background_preview_init(MMBackgroundPreview *p, const MMStorageAllocator *allocator)
{
    memset(p, 0, sizeof(*p));
    return mm_background_camera_init(&p->placement, allocator) &&
           mm_storage_init(&p->metadata, allocator) == MM_STORAGE_OK &&
           mm_storage_init(&p->images, allocator) == MM_STORAGE_OK;
}
void mm_background_preview_dispose(MMBackgroundPreview *p, const MMBackgroundPreviewServices *s)
{
    MMU32 i;
    for (i = 0; i < p->mesh_count; ++i)
        s->mesh_release(s->context, p->meshes[i]);
    p->mesh_count = 0;
    mm_background_camera_dispose(&p->placement);
    p->placement.current = -1;
    mm_storage_dispose(&p->images);
    mm_storage_dispose(&p->metadata);
    p->camera_count = p->light_count = p->rgb_count = p->depth_count = p->width = p->height = 0;
}
int mm_background_preview_camera(MMBackgroundPreview *p, MMU32 number)
{
    if (!ensure(&p->metadata, 0, p->camera_count + 1, sizeof(number)))
        return 0;
    ((MMU32 *)p->metadata.buffers[0].data)[p->camera_count++] = number;
    return 1;
}
int mm_background_preview_light(MMBackgroundPreview *p, const MMBackgroundLight *light)
{
    if (!ensure(&p->metadata, 1, p->light_count + 1, sizeof(*light)))
        return 0;
    ((MMBackgroundLight *)p->metadata.buffers[1].data)[p->light_count++] = *light;
    return 1;
}
static int select_images(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundCameraBinding *binding,
                         const MMBackgroundPreviewServices *s)
{
    MMU32 width, height, n;
    MMI32 r[4];
    void *bitmap;
    p->camera = *binding;
    s->publish(s->context, p);
    if (binding->data.near_z <= 0 || binding->data.far_z <= binding->data.near_z)
        return 0;
    if (!mm_background_camera_binding_image(&p->camera, 0, s->child, s->context) ||
        !(bitmap = s->fetch(s->context, p->camera.images.colour, 0)))
        return 0;
    s->bounds(s->context, bitmap, r);
    if (r[2] <= r[0] || r[3] <= r[1])
    {
        s->release(s->context, bitmap);
        return 0;
    }
    width = (MMU32)r[2] - (MMU32)r[0];
    height = (MMU32)r[3] - (MMU32)r[1];
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
    if (!mm_background_camera_binding_image(&p->camera, 1, s->child, s->context) ||
        !(bitmap = s->fetch(s->context, p->camera.images.depth, 1)))
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
int mm_background_preview_apply(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundCameraBinding *binding,
                                 const MMReadStream *stream, int update_placement, const MMBackgroundPreviewServices *s)
{
    int status;
    if (slot >= p->camera_count || binding->number != ((const MMU32 *)p->metadata.buffers[0].data)[slot])
        return 0;
    if (update_placement)
    {
        status = mm_background_placement_stage(&p->placement, stream, &binding->data);
        if (status == MM_STORAGE_MEMORY)
            return MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY;
        if (status != MM_STORAGE_OK)
            return 0;
    }
    if (!select_images(p, slot, binding, s))
        return 0;
    if (update_placement)
        mm_background_placement_commit(&p->placement, (MMI32)slot, &p->camera, 1);
    return 1;
}
typedef struct CameraSelection
{
    MMBackgroundPreview *preview;
    const MMBackgroundPreviewServices *services;
    MMU32 slot, number;
    int update_placement;
} CameraSelection;

static int apply_camera_payload(void *context, const MMReadStream *stream)
{
    CameraSelection *selection = (CameraSelection *)context;
    MMBackgroundCameraBinding binding;
    if (!mm_background_camera_binding_read(stream, selection->number, &binding))
        return 0;
    return mm_background_preview_apply(selection->preview, selection->slot, &binding, stream,
                                       selection->update_placement, selection->services);
}
static int select_camera(MMBackgroundPreview *p, MMU32 slot, int update_placement,
                         const MMBackgroundPreviewServices *s)
{
    CameraSelection selection;
    int result;
    if (slot >= p->camera_count)
        return 0;
    selection.preview = p;
    selection.services = s;
    selection.slot = slot;
    selection.number = ((const MMU32 *)p->metadata.buffers[0].data)[slot];
    selection.update_placement = update_placement;
    if (s->payload(s->context, MM_BACKGROUND_CAMERA, selection.number, apply_camera_payload, &selection, &result) !=
        MM_CHUNK_OK)
        return 0;
    return result;
}
int mm_background_preview_select(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundPreviewServices *s)
{
    return select_camera(p, slot, 0, s);
}
int mm_background_preview_select_placement(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundPreviewServices *s)
{
    return select_camera(p, slot, 1, s);
}
int mm_background_preview_place(MMBackgroundPreview *p, MMActorPoint *out)
{
    if (!out || p->placement.current < 0)
        return 0;
    mm_background_place(&p->placement, out);
    return 1;
}
void mm_background_preview_reuse_place(MMBackgroundPreview *p)
{
    mm_background_reuse_place(&p->placement);
}
void *mm_background_preview_mesh(MMBackgroundPreview *p, const MMBackgroundPreviewServices *s)
{
    const MMBackgroundCameraData *camera = &p->camera.data;
    MMU32 i, row, axis, key;
    MMI32 native_projection[16], aspect;
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
            transform[row * 4 + axis] = s->scalar_float(s->context, camera->matrix.m[row][axis]);
    if (!p->height || camera->near_z <= 0 || camera->far_z <= camera->near_z)
    {
        s->mesh_release(s->context, mesh);
        return NULL;
    }
    aspect = s->float_scalar(s->context, (float)p->width / p->height);
    s->perspective(s->context, camera->fov, aspect, -camera->near_z, -camera->far_z, native_projection);
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
