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
void mm_background_camera_init(MMBackgroundCameraState *s)
{
    memset(s, 0, sizeof(*s));
    s->current = -1;
}
void mm_background_camera_dispose(MMBackgroundCameraState *p, const MMBackgroundCameraServices *s)
{
    if (p->points)
    {
        s->release(s->context, p->points);
        p->points = NULL;
    }
    p->next_point = p->last_point = 0;
}
int mm_background_camera_set(MMBackgroundCameraState *p, MMI32 index,
                                   const MMBackgroundCameraServices *s)
{
    MMReadStream r;
    MMBackgroundCameraData d;
    MMActorPoint place;
    MMU32 number, rgb, depth;
    MMI32 i;
    unsigned short a, c;
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
    if (!mm_background_camera_decode(&r, &d) || !s->child(s->context, number, 0, &rgb) ||
        !s->child(s->context, number, 1, &depth) || !s->background(s->context, rgb, depth))
        return 0;
    mm_background_camera_dispose(p, s);
    if (d.point_count > 0 && (p->points = s->new_points(s->context, d.point_count)) != NULL)
    {
        s->lock(s->context, p->points);
        for (i = 0; i < d.point_count; ++i)
        {
            if (!mm_background_point_decode(&r, &d, i, &place))
            {
                s->unlock(s->context, p->points);
                mm_background_camera_dispose(p, s);
                return 0;
            }
            s->put_point(s->context, p->points, i, &place);
        }
        s->unlock(s->context, p->points);
    }
    p->current = index;
    p->place = d.place;
    p->position.dxr = d.matrix.m[3][0];
    p->position.dyr = d.matrix.m[3][1];
    p->position.dzr = d.matrix.m[3][2];
    s->euler_yxy(s->context, &d.matrix, &a, &c);
    p->yaw = (unsigned short)(a + c);
    s->rotate_y(s->context, &p->mouse, p->yaw);
    s->translate(s->context, &p->mouse, &p->position);
    s->camera(s->context, &d);
    s->dirty(s->context);
    return 1;
}
void mm_background_place(MMBackgroundCameraState *p, MMActorPoint *out,
                               const MMBackgroundCameraServices *s)
{
    if (!p->next_point)
        *out = p->place;
    else
        s->point(s->context, p->points, p->next_point - 1, out);
    p->last_point = p->next_point;
    if (p->points)
    {
        ++p->next_point;
        if (p->next_point > s->point_count(s->context, p->points))
            p->next_point = 0;
    }
}
void mm_background_reuse_place(MMBackgroundCameraState *p)
{
    p->next_point = p->last_point;
}
