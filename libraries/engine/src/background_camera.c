/* Background camera/placement policy converted from engine/bkgd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/background_camera.h"
#include "three_mm/wide.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p, int big)
{
    return big ? ((ThreeMMU32)p[0] << 24) | ((ThreeMMU32)p[1] << 16) | ((ThreeMMU32)p[2] << 8) | p[3]
               : p[0] | ((ThreeMMU32)p[1] << 8) | ((ThreeMMU32)p[2] << 16) | ((ThreeMMU32)p[3] << 24);
}
static void point(const unsigned char *p, int big, ThreeMMActorPoint *out)
{
    out->dxr = three_mm_signed_word(word(p, big));
    out->dyr = three_mm_signed_word(word(p + 4, big));
    out->dzr = three_mm_signed_word(word(p + 8, big));
}
int three_mm_background_camera_decode(const ThreeMMReadStream *r, ThreeMMBackgroundCameraData *out)
{
    unsigned char bytes[THREE_MM_BACKGROUND_CAMERA_BYTES];
    ThreeMMBackgroundCameraData d;
    int i, big;
    if (r->length < 76 || (r->length - 76) % 12 || !three_mm_read_exact(r, 0, bytes, 76))
        return 0;
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && !(bytes[0] == 1 && bytes[1] == 0))
        return 0;
    memset(&d, 0, sizeof(d));
    d.big = big;
    d.point_count = (ThreeMMI32)((r->length - 76) / 12);
    d.near_z = three_mm_signed_word(word(bytes + 4, big));
    d.far_z = three_mm_signed_word(word(bytes + 8, big));
    d.fov = (unsigned short)(big ? ((unsigned)bytes[12] << 8) | bytes[13] : bytes[12] | ((unsigned)bytes[13] << 8));
    point(bytes + 16, big, &d.place);
    for (i = 0; i < 12; ++i)
        d.matrix.m[i / 3][i % 3] = three_mm_signed_word(word(bytes + 28 + i * 4, big));
    *out = d;
    return 1;
}
int three_mm_background_point_decode(const ThreeMMReadStream *r, const ThreeMMBackgroundCameraData *d, ThreeMMI32 index,
                                     ThreeMMActorPoint *out)
{
    unsigned char bytes[THREE_MM_BACKGROUND_POINT_BYTES];
    ThreeMMActorPoint p;
    if (r->length < 76 || index < 0 || index >= d->point_count || (ThreeMMU32)index >= (r->length - 76) / 12 ||
        !three_mm_read_exact(r, 76U + (ThreeMMU32)index * 12U, bytes, 12))
        return 0;
    point(bytes, d->big, &p);
    *out = p;
    return 1;
}
void three_mm_background_camera_init(ThreeMMBackgroundCameraState *s)
{
    memset(s, 0, sizeof(*s));
    s->current = -1;
}
void three_mm_background_camera_dispose(ThreeMMBackgroundCameraState *p, const ThreeMMBackgroundCameraServices *s)
{
    if (p->points)
    {
        s->release(s->context, p->points);
        p->points = NULL;
    }
    p->next_point = p->last_point = 0;
}
int three_mm_background_camera_set(ThreeMMBackgroundCameraState *p, ThreeMMI32 index,
                                   const ThreeMMBackgroundCameraServices *s)
{
    ThreeMMReadStream r;
    ThreeMMBackgroundCameraData d;
    ThreeMMActorPoint place;
    ThreeMMU32 number, rgb, depth;
    ThreeMMI32 i;
    unsigned short a, c;
    s->lights_on(s->context);
    if (!s->read(s->context, index, &r, &number))
        return 0;
    if (r.length < 76)
    {
        s->diagnostic(s->context, THREE_MM_BACKGROUND_CAMERA_SHORT);
        return 0;
    }
    if ((r.length - 76) % 12)
    {
        s->diagnostic(s->context, THREE_MM_BACKGROUND_CAMERA_TAIL);
        return 0;
    }
    if (!three_mm_background_camera_decode(&r, &d) || !s->child(s->context, number, 0, &rgb) ||
        !s->child(s->context, number, 1, &depth) || !s->background(s->context, rgb, depth))
        return 0;
    three_mm_background_camera_dispose(p, s);
    if (d.point_count > 0 && (p->points = s->new_points(s->context, d.point_count)) != NULL)
    {
        s->lock(s->context, p->points);
        for (i = 0; i < d.point_count; ++i)
        {
            if (!three_mm_background_point_decode(&r, &d, i, &place))
            {
                s->unlock(s->context, p->points);
                three_mm_background_camera_dispose(p, s);
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
void three_mm_background_place(ThreeMMBackgroundCameraState *p, ThreeMMActorPoint *out,
                               const ThreeMMBackgroundCameraServices *s)
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
void three_mm_background_reuse_place(ThreeMMBackgroundCameraState *p)
{
    p->next_point = p->last_point;
}
