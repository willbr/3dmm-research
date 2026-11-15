/* Camera and selection policy converted from bren/bwld.cpp. */
#include "mm/world_camera.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static int valid_scale(MMI32 scale)
{
    return scale == 1 || scale == 2 || scale == 4;
}
static MMI32 divide(MMI32 value, MMI32 divisor)
{
    MMU32 magnitude;
    if (value >= 0)
        return value / divisor;
    magnitude = (0U - (MMU32)value) / (MMU32)divisor;
    return magnitude == (MMU32)MAXIMUM + 1U ? MINIMUM : -(MMI32)magnitude;
}
static int multiply(MMI32 value, MMI32 scale, MMI32 *out)
{
    if (value > MAXIMUM / scale || value < MINIMUM / scale)
        return 0;
    *out = value * scale;
    return 1;
}
static int subtract(MMI32 value, MMI32 origin, MMI32 *out)
{
    if ((origin > 0 && value < MINIMUM + origin) || (origin < 0 && value > MAXIMUM + origin))
        return 0;
    *out = value - origin;
    return 1;
}
void mm_world_camera_init(MMWorldCamera *w)
{
    memset(w, 0, sizeof(*w));
}
void mm_world_camera_set(MMWorldCamera *w, const MMActorMatrix *matrix, MMI32 near_z,
                               MMI32 far_z, unsigned short fov, const MMWorldCameraServices *s)
{
    w->matrix = *matrix;
    w->near_z = near_z;
    w->far_z = far_z;
    w->fov = fov;
    s->publish(s->context, w);
    s->dirty(s->context);
}
void mm_world_camera_get(const MMWorldCamera *w, MMActorMatrix *matrix, MMI32 *near_z,
                               MMI32 *far_z, unsigned short *fov)
{
    *matrix = w->matrix;
    if (near_z)
        *near_z = w->near_z;
    if (far_z)
        *far_z = w->far_z;
    if (fov)
        *fov = w->fov;
}
void mm_world_pick_begin(MMWorldCamera *w)
{
    w->picked = NULL;
    w->pick_depth = MAXIMUM;
}
int mm_world_pick_visit(MMWorldCamera *w, void *actor, MMI32 depth)
{
    if (depth < w->pick_depth)
    {
        w->picked = actor;
        w->pick_depth = depth;
    }
    return 0;
}
int mm_world_pick_point(const MMWorldBuffers *b, MMI32 origin_x, MMI32 origin_y, MMI32 x,
                              MMI32 y, MMI32 *out_x, MMI32 *out_y)
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
void *mm_world_pick(MMWorldCamera *w, const MMWorldBuffers *b, MMI32 origin_x, MMI32 origin_y,
                          MMI32 x, MMI32 y, const MMWorldCameraServices *s)
{
    mm_world_pick_begin(w);
    if (mm_world_pick_point(b, origin_x, origin_y, x, y, &x, &y))
        s->pick(s->context, x, y);
    return w->picked;
}
int mm_world_rendered_bounds(MMI32 scale, const MMI32 bounds[4], MMI32 rect[4])
{
    MMI32 r[4];
    if (!valid_scale(scale) || bounds[2] > MAXIMUM - scale || bounds[3] > MAXIMUM - scale)
        return 0;
    r[0] = divide(bounds[0], scale);
    r[1] = divide(bounds[1], scale);
    r[2] = divide(bounds[2] + scale, scale);
    r[3] = divide(bounds[3] + scale, scale);
    memcpy(rect, r, sizeof(r));
    return 1;
}
void *mm_world_actor_owner(void *actor, void *(*parent)(void *), void *(*owner)(void *))
{
    void *next;
    if (!actor)
        return NULL;
    while ((next = parent(actor)) != NULL)
        actor = next;
    return owner(actor);
}
