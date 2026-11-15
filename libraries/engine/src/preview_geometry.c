/* Preview depth clipping and actor-tree copying converted from archive helpers. */
#include "three_mm/preview_render.h"
#include <float.h>
#include <math.h>
#include <string.h>
typedef struct CopyNode
{
    void *parent;
    const void *source;
} CopyNode;
int three_mm_preview_copy_tree(void *parent, const void *source, ThreeMMI32 owner,
                               const ThreeMMStorageAllocator *allocator, const ThreeMMPreviewTreeServices *s)
{
    ThreeMMCollectionStorage storage;
    ThreeMMArray stack;
    CopyNode node, next;
    void *copy, *child;
    int ok = 0;
    if (three_mm_storage_init(&storage, allocator && allocator->resize ? allocator : 0) != THREE_MM_STORAGE_OK)
        return 0;
    memset(&stack, 0, sizeof(stack));
    stack.storage = &storage;
    stack.width = sizeof(node);
    stack.free_count = -1;
    node.parent = parent;
    node.source = source;
    if (three_mm_array_add(&stack, &node, 0) != THREE_MM_STORAGE_OK)
        goto done;
    while (stack.count)
    {
        three_mm_array_pop(&stack, &node);
        copy = s->clone_attach(s->context, node.parent, node.source);
        if (!copy)
            goto done;
        if (s->owner && s->model(s->context, node.source) && !s->owner(s->context, copy, owner))
            goto done;
        for (child = s->first(s->context, node.source); child; child = s->next(s->context, child))
        {
            next.parent = copy;
            next.source = child;
            if (three_mm_array_add(&stack, &next, 0) != THREE_MM_STORAGE_OK)
                goto done;
        }
    }
    ok = 1;
done:
    three_mm_storage_dispose(&storage);
    return ok;
}
void three_mm_preview_depth_init(ThreeMMPreviewDepthRange *r, double x, double y, double near_clip, double far_clip,
                                 int parallel)
{
    r->x_slope = x;
    r->y_slope = y;
    r->near_clip = near_clip;
    r->far_clip = far_clip;
    r->parallel = parallel;
    r->nearest = HUGE_VAL;
    r->farthest = 0;
}
static double distance(const ThreeMMPreviewDepthRange *r, const double *p, unsigned plane)
{
    switch (plane)
    {
    case 0:
        return (r->parallel ? 1 : p[2]) * r->x_slope + p[0];
    case 1:
        return (r->parallel ? 1 : p[2]) * r->x_slope - p[0];
    case 2:
        return (r->parallel ? 1 : p[2]) * r->y_slope + p[1];
    case 3:
        return (r->parallel ? 1 : p[2]) * r->y_slope - p[1];
    case 4:
        return p[2] - r->near_clip;
    default:
        return r->far_clip - p[2];
    }
}
void three_mm_preview_depth_include(ThreeMMPreviewDepthRange *r, const double triangle[3][3])
{
    double polygon[12][3], clipped[12][3], distances[12], previous[3], previous_distance, d, t;
    unsigned count = 3, plane, i, inside, output, axis;
    memcpy(polygon, triangle, 9 * sizeof(double));
    for (plane = 0; plane < 6 && count; ++plane)
    {
        inside = 0;
        for (i = 0; i < count; ++i)
        {
            distances[i] = distance(r, polygon[i], plane);
            if (distances[i] >= 0)
                ++inside;
        }
        if (!inside)
            return;
        if (inside == count)
            continue;
        output = 0;
        memcpy(previous, polygon[count - 1], sizeof(previous));
        previous_distance = distances[count - 1];
        for (i = 0; i < count; ++i)
        {
            d = distances[i];
            if ((d > 0 && previous_distance < 0) || (d < 0 && previous_distance > 0))
            {
                t = previous_distance / (previous_distance - d);
                for (axis = 0; axis < 3; ++axis)
                    clipped[output][axis] = previous[axis] + t * (polygon[i][axis] - previous[axis]);
                ++output;
            }
            if (d >= 0)
            {
                memcpy(clipped[output], polygon[i], 3 * sizeof(double));
                ++output;
            }
            memcpy(previous, polygon[i], sizeof(previous));
            previous_distance = d;
        }
        memcpy(polygon, clipped, output * 3 * sizeof(double));
        count = output;
    }
    for (i = 0; i < count; ++i)
    {
        if (polygon[i][2] < r->nearest)
            r->nearest = polygon[i][2];
        if (polygon[i][2] > r->farthest)
            r->farthest = polygon[i][2];
    }
}
void three_mm_preview_depth_fit(const ThreeMMPreviewDepthRange *r, double *near_clip, double *far_clip)
{
    double n, f;
    if (!(r->nearest <= DBL_MAX && r->nearest >= -DBL_MAX))
    {
        *near_clip = r->near_clip;
        *far_clip = r->far_clip;
        return;
    }
    n = r->nearest * .5;
    if (n < r->near_clip)
        n = r->near_clip;
    f = r->farthest * 1.05;
    if (f < n * 2)
        f = n * 2;
    if (f > r->far_clip)
        f = r->far_clip;
    *near_clip = n;
    *far_clip = f;
}
