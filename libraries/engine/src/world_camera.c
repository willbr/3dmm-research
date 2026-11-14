/* Camera and selection policy converted from bren/bwld.cpp. */
#include "three_mm/world_camera.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static int valid_scale(ThreeMMI32 scale)
{
    return scale == 1 || scale == 2 || scale == 4;
}
static ThreeMMI32 divide(ThreeMMI32 value, ThreeMMI32 divisor)
{
    ThreeMMU32 magnitude;
    if (value >= 0)
        return value / divisor;
    magnitude = (0U - (ThreeMMU32)value) / (ThreeMMU32)divisor;
    return magnitude == (ThreeMMU32)MAXIMUM + 1U ? MINIMUM : -(ThreeMMI32)magnitude;
}
static int multiply(ThreeMMI32 value, ThreeMMI32 scale, ThreeMMI32 *out)
{
    if (value > MAXIMUM / scale || value < MINIMUM / scale)
        return 0;
    *out = value * scale;
    return 1;
}
static int subtract(ThreeMMI32 value, ThreeMMI32 origin, ThreeMMI32 *out)
{
    if ((origin > 0 && value < MINIMUM + origin) || (origin < 0 && value > MAXIMUM + origin))
        return 0;
    *out = value - origin;
    return 1;
}
void three_mm_world_camera_init(ThreeMMWorldCamera *w)
{
    memset(w, 0, sizeof(*w));
}
void three_mm_world_camera_set(ThreeMMWorldCamera *w, const ThreeMMActorMatrix *matrix, ThreeMMI32 near_z,
                               ThreeMMI32 far_z, unsigned short fov, const ThreeMMWorldCameraServices *s)
{
    w->matrix = *matrix;
    w->near_z = near_z;
    w->far_z = far_z;
    w->fov = fov;
    s->publish(s->context, w);
    s->dirty(s->context);
}
void three_mm_world_camera_get(const ThreeMMWorldCamera *w, ThreeMMActorMatrix *matrix, ThreeMMI32 *near_z,
                               ThreeMMI32 *far_z, unsigned short *fov)
{
    *matrix = w->matrix;
    if (near_z)
        *near_z = w->near_z;
    if (far_z)
        *far_z = w->far_z;
    if (fov)
        *fov = w->fov;
}
void three_mm_world_pick_begin(ThreeMMWorldCamera *w)
{
    w->picked = NULL;
    w->pick_depth = MAXIMUM;
}
int three_mm_world_pick_visit(ThreeMMWorldCamera *w, void *actor, ThreeMMI32 depth)
{
    if (depth < w->pick_depth)
    {
        w->picked = actor;
        w->pick_depth = depth;
    }
    return 0;
}
int three_mm_world_pick_point(const ThreeMMWorldBuffers *b, ThreeMMI32 origin_x, ThreeMMI32 origin_y, ThreeMMI32 x,
                              ThreeMMI32 y, ThreeMMI32 *out_x, ThreeMMI32 *out_y)
{
    if (!valid_scale(b->scale))
        return 0;
    if (b->scale > 1)
    {
        if (!multiply(x, b->scale, &x) || !multiply(y, b->scale, &y))
            return 0;
    }
    else
    {
        if (b->half_x)
            x = divide(x, 2);
        if (b->half_y)
            y = divide(y, 2);
    }
    if (!subtract(x, origin_x, &x) || !subtract(y, origin_y, &y))
        return 0;
    *out_x = x;
    *out_y = y;
    return 1;
}
void *three_mm_world_pick(ThreeMMWorldCamera *w, const ThreeMMWorldBuffers *b, ThreeMMI32 origin_x, ThreeMMI32 origin_y,
                          ThreeMMI32 x, ThreeMMI32 y, const ThreeMMWorldCameraServices *s)
{
    three_mm_world_pick_begin(w);
    if (three_mm_world_pick_point(b, origin_x, origin_y, x, y, &x, &y))
        s->pick(s->context, x, y);
    return w->picked;
}
int three_mm_world_rendered_bounds(ThreeMMI32 scale, const ThreeMMI32 bounds[4], ThreeMMI32 rect[4])
{
    ThreeMMI32 r[4];
    if (!valid_scale(scale) || bounds[2] > MAXIMUM - scale || bounds[3] > MAXIMUM - scale)
        return 0;
    r[0] = divide(bounds[0], scale);
    r[1] = divide(bounds[1], scale);
    r[2] = divide(bounds[2] + scale, scale);
    r[3] = divide(bounds[3] + scale, scale);
    memcpy(rect, r, sizeof(r));
    return 1;
}
void *three_mm_world_actor_owner(void *actor, void *(*parent)(void *), void *(*owner)(void *))
{
    void *next;
    if (!actor)
        return NULL;
    while ((next = parent(actor)) != NULL)
        actor = next;
    return owner(actor);
}
