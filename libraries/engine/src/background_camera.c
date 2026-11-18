/* Background camera/placement policy converted from engine/bkgd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/background_camera.h"
#include "mm/wide.h"
#include <string.h>
int mm_background_camera_image_link(const MMChunkAccess *access, MMU32 camera, int depth, MMU32 *number)
{
    const MMU32 bitmap_type = 0x4d424d50UL, depth_type = 0x5a424d50UL;
    return mm_chunk_link_number(access, MM_BACKGROUND_CAMERA, camera, 0,
                                depth ? depth_type : bitmap_type, number);
}

static MMU32 word(const unsigned char *p, int big)
{
    return big ? ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3]
               : p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}
static void point(const unsigned char *p, int big, MMActorPoint *out)
{
    out->dxr = mm_signed_word(word(p, big));
    out->dyr = mm_signed_word(word(p + 4, big));
    out->dzr = mm_signed_word(word(p + 8, big));
}
int mm_background_camera_decode(const MMReadStream *r, MMBackgroundCameraData *out)
{
    unsigned char bytes[MM_BACKGROUND_CAMERA_BYTES];
    MMBackgroundCameraData d;
    int i, big;
    if (r->length < 76 || (r->length - 76) % 12 || !mm_read_exact(r, 0, bytes, 76))
        return 0;
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && !(bytes[0] == 1 && bytes[1] == 0))
        return 0;
    memset(&d, 0, sizeof(d));
    d.big = big;
    d.point_count = (MMI32)((r->length - 76) / 12);
    d.near_z = mm_signed_word(word(bytes + 4, big));
    d.far_z = mm_signed_word(word(bytes + 8, big));
    d.fov = (unsigned short)(big ? ((unsigned)bytes[12] << 8) | bytes[13] : bytes[12] | ((unsigned)bytes[13] << 8));
    point(bytes + 16, big, &d.place);
    for (i = 0; i < 12; ++i)
        d.matrix.m[i / 3][i % 3] = mm_signed_word(word(bytes + 28 + i * 4, big));
    *out = d;
    return 1;
}
int mm_background_camera_binding_read(const MMReadStream *stream, MMU32 number, MMBackgroundCameraBinding *out)
{
    MMBackgroundCameraBinding binding;
    memset(&binding, 0, sizeof(binding));
    if (!mm_background_camera_decode(stream, &binding.data))
        return 0;
    binding.number = number;
    *out = binding;
    return 1;
}
int mm_background_camera_binding_image(MMBackgroundCameraBinding *binding, int depth,
                                       MMBackgroundCameraImageRead read, void *context)
{
    MMU32 number;
    unsigned flag = depth ? MM_BACKGROUND_HAS_DEPTH : MM_BACKGROUND_HAS_COLOUR;
    if (binding->images.present & flag)
        return 1;
    if (!read || !read(context, binding->number, depth, &number))
        return 0;
    if (depth)
        binding->images.depth = number;
    else
        binding->images.colour = number;
    binding->images.present |= flag;
    return 1;
}
int mm_background_point_decode(const MMReadStream *r, const MMBackgroundCameraData *d, MMI32 index,
                                     MMActorPoint *out)
{
    unsigned char bytes[MM_BACKGROUND_POINT_BYTES];
    MMActorPoint p;
    if (r->length < 76 || index < 0 || index >= d->point_count || (MMU32)index >= (r->length - 76) / 12 ||
        !mm_read_exact(r, 76U + (MMU32)index * 12U, bytes, 12))
        return 0;
    point(bytes, d->big, &p);
    *out = p;
    return 1;
}
int mm_background_camera_init(MMBackgroundCameraState *s, const MMStorageAllocator *allocator)
{
    memset(s, 0, sizeof(*s));
    s->current = -1;
    return mm_storage_init(&s->points, allocator) == MM_STORAGE_OK;
}
void mm_background_camera_dispose(MMBackgroundCameraState *p)
{
    mm_storage_dispose(&p->points);
    p->point_count = p->next_point = p->last_point = 0;
    p->point_buffer = 0;
}

int mm_background_placement_stage(MMBackgroundCameraState *p, const MMReadStream *stream,
                                  const MMBackgroundCameraData *data)
{
    const unsigned buffer = 1 - p->point_buffer;
    MMActorPoint *points;
    MMU32 bytes;
    MMI32 i;
    int status;
    if (data->point_count <= 0)
        return data->point_count == 0 ? MM_STORAGE_OK : MM_STORAGE_IO;
    if ((MMU32)data->point_count > (MM_STORAGE_LIMIT - 1) / sizeof(*points))
        return MM_STORAGE_MEMORY;
    bytes = (MMU32)data->point_count * sizeof(*points);
    if (bytes > p->points.buffers[buffer].capacity)
    {
        status = mm_storage_resize_exact(&p->points, buffer, bytes, MM_STORAGE_NORMAL);
        if (status != MM_STORAGE_OK)
            return status;
    }
    points = (MMActorPoint *)p->points.buffers[buffer].data;
    for (i = 0; i < data->point_count; ++i)
        if (!mm_background_point_decode(stream, data, i, points + i))
            return MM_STORAGE_IO;
    return MM_STORAGE_OK;
}
void mm_background_placement_commit(MMBackgroundCameraState *p, MMI32 index, const MMBackgroundCameraBinding *binding,
                                    int use_points)
{
    p->point_buffer = 1 - p->point_buffer;
    p->point_count = use_points ? binding->data.point_count : 0;
    p->current = index;
    p->binding = *binding;
    p->place = binding->data.place;
    p->next_point = p->last_point = 0;
}
int mm_background_camera_set(MMBackgroundCameraState *p, MMI32 index,
                                   const MMBackgroundCameraServices *s)
{
    MMReadStream r;
    MMBackgroundCameraBinding binding;
    MMU32 number;
    s->lights_on(s->context);
    if (!s->read(s->context, index, &r, &number))
        return 0;
    if (r.length < 76)
    {
        s->diagnostic(s->context, MM_BACKGROUND_CAMERA_SHORT);
        return 0;
    }
    if ((r.length - 76) % 12)
    {
        s->diagnostic(s->context, MM_BACKGROUND_CAMERA_TAIL);
        return 0;
    }
    return mm_background_camera_binding_read(&r, number, &binding) &&
           mm_background_camera_apply(p, index, &binding, &r, s);
}

int mm_background_camera_apply(MMBackgroundCameraState *p, MMI32 index, const MMBackgroundCameraBinding *evaluated,
                               const MMReadStream *stream, const MMBackgroundCameraServices *s)
{
    MMBackgroundCameraBinding binding = *evaluated;
    const MMBackgroundCameraData *d = &binding.data;
    int status;
    unsigned short a, c;
    if (!mm_background_camera_binding_image(&binding, 0, s->child, s->context) ||
        !mm_background_camera_binding_image(&binding, 1, s->child, s->context) ||
        !s->background(s->context, binding.images.colour, binding.images.depth))
        return 0;
    mm_background_camera_dispose(p);
    status = mm_background_placement_stage(p, stream, d);
    if (status != MM_STORAGE_OK && status != MM_STORAGE_MEMORY)
    {
        mm_background_camera_dispose(p);
        return 0;
    }
    mm_background_placement_commit(p, index, &binding, status == MM_STORAGE_OK);
    p->position.dxr = d->matrix.m[3][0];
    p->position.dyr = d->matrix.m[3][1];
    p->position.dzr = d->matrix.m[3][2];
    s->euler_yxy(s->context, &d->matrix, &a, &c);
    p->yaw = (unsigned short)(a + c);
    s->rotate_y(s->context, &p->mouse, p->yaw);
    s->translate(s->context, &p->mouse, &p->position);
    s->camera(s->context, &p->binding.data);
    s->dirty(s->context);
    return 1;
}
void mm_background_place(MMBackgroundCameraState *p, MMActorPoint *out)
{
    const MMActorPoint *points = (const MMActorPoint *)p->points.buffers[p->point_buffer].data;
    if (!p->next_point)
        *out = p->place;
    else
        *out = points[p->next_point - 1];
    p->last_point = p->next_point;
    p->next_point = p->next_point < p->point_count ? p->next_point + 1 : 0;
}
void mm_background_reuse_place(MMBackgroundCameraState *p)
{
    p->next_point = p->last_point;
}
