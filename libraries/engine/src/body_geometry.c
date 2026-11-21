/* Body geometry/render policy converted from engine body.cpp and body_world.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_geometry.h"
#include <string.h>
void *mm_body_from_actor(void *actor, MMI32 *set, const MMBodyGeometryServices *s)
{
    void *body = s->owner(s->context, actor);
    MMI32 i, n;
    if (!body)
    {
        if (s->missing_owner)
            s->missing_owner(s->context);
        return NULL;
    }
    if (set)
    {
        *set = -1;
        n = s->part_count(s->context, body);
        for (i = 0; i < n; ++i)
            if (s->part(s->context, body, i) == actor)
            {
                *set = s->part_set(s->context, body, i);
                break;
            }
    }
    return body;
}
void mm_body_geometry_init(MMBodyGeometry *g)
{
    memset(g, 0, sizeof(*g));
}
static int empty(const MMBodyRect *r)
{
    return r->bottom <= r->top || r->right <= r->left;
}
int mm_body_in_view(const MMBodyGeometry *g)
{
    return !empty(&g->bounds);
}
void mm_body_clear_bounds(MMBodyGeometry *g)
{
    memset(&g->bounds, 0, sizeof(g->bounds));
}
void mm_body_rendered(void *actor, const MMBodyRect *r, const MMBodyGeometryServices *s)
{
    void *body = mm_body_from_actor(actor, NULL, s);
    MMBodyRect *bounds;
    if (!body || empty(r))
        return;
    bounds = &s->state(s->context, body)->bounds;
    if (empty(bounds))
        *bounds = *r;
    else
    {
        if (r->left < bounds->left)
            bounds->left = r->left;
        if (r->top < bounds->top)
            bounds->top = r->top;
        if (r->right > bounds->right)
            bounds->right = r->right;
        if (r->bottom > bounds->bottom)
            bounds->bottom = r->bottom;
    }
}
void mm_body_prepare_render(void *actor, const MMBodyGeometryServices *s)
{
    void *body = mm_body_from_actor(actor, NULL, s), *highlight;
    MMBodyGeometry *g;
    MMBodyBounds bounds;
    if (!body)
        return;
    g = s->state(s->context, body);
    if (mm_body_in_view(g))
    {
        g->last_visible = g->bounds;
        mm_body_clear_bounds(g);
    }
    highlight = s->highlight(s->context, body);
    if (s->actor_type(s->context, highlight) == s->model_type)
    {
        mm_body_bounds(body, 0, &bounds, s);
        s->highlight_matrix(s->context, highlight, &bounds);
    }
}
void mm_body_render_rect(void *actor, MMBodyRect *out, const MMBodyGeometryServices *s)
{
    void *body = mm_body_from_actor(actor, NULL, s);
    if (body)
        *out = s->state(s->context, body)->bounds;
}
void mm_body_visible_rect(const MMBodyGeometry *g, MMBodyRect *out)
{
    *out = mm_body_in_view(g) ? g->bounds : g->last_visible;
}
/* C89 permits implementation-defined rounding for negative division. The
 * unsigned magnitude defines truncation toward zero, including INT32_MIN. */
static MMI32 half(MMI32 value)
{
    return value < 0 ? -(MMI32)((0U - (MMU32)value) / 2U) : value / 2;
}
void mm_body_center(const MMBodyGeometry *g, MMI32 *x, MMI32 *y)
{
    MMBodyRect r;
    mm_body_visible_rect(g, &r);
    /* Native signed sums wrap before truncation toward zero. */
    *x = half(mm_fixed_add(r.left, r.right));
    *y = half(mm_fixed_add(r.top, r.bottom));
}
void mm_body_transform_bounds(const MMBodyBounds *in, const MMActorMatrix *m, MMBodyBounds *out)
{
    MMBodyBounds b = *in, result;
    MMI32 point[3], v;
    unsigned i, j;
    for (j = 0; j < 3; ++j)
    {
        result.min[j] = 655360000L;
        result.max[j] = -655360000L;
    }
    for (i = 0; i < 8; ++i)
    {
        /* Retain native corner order and the +/-10000 initial extrema. */
        for (j = 0; j < 3; ++j)
            point[j] = i < 4 ? b.min[j] : b.max[j];
        if (i > 0 && i < 4)
            point[i - 1] = b.max[i - 1];
        if (i > 4)
            point[i - 5] = b.min[i - 5];
        for (j = 0; j < 3; ++j)
        {
            v = mm_fixed_add(
                mm_fixed_mac3(point[0], m->m[0][j], point[1], m->m[1][j], point[2], m->m[2][j]), m->m[3][j]);
            if (v < result.min[j])
                result.min[j] = v;
            if (v > result.max[j])
                result.max[j] = v;
        }
    }
    *out = result;
}
void mm_body_bounds(void *body, int world, MMBodyBounds *out, const MMBodyGeometryServices *s)
{
    void *highlight = s->highlight(s->context, body);
    int type = s->actor_type(s->context, highlight);
    MMBodyBounds bounds;
    MMActorMatrix matrix;
    s->set_actor_type(s->context, highlight, s->none_type);
    s->local_bounds(s->context, s->root(s->context, body), &bounds);
    s->set_actor_type(s->context, highlight, type);
    *out = bounds;
    if (world)
    {
        s->root_matrix(s->context, body, &matrix);
        mm_body_transform_bounds(&bounds, &matrix, out);
    }
}
void mm_body_position(void *body, MMI32 out[3], const MMBodyGeometryServices *s)
{
    MMActorMatrix matrix;
    s->root_matrix(s->context, body, &matrix);
    memcpy(out, matrix.m[3], 3 * sizeof(*out));
}
void mm_body_pick_begin(MMBodyPick *p)
{
    p->body = NULL;
    p->actor = NULL;
    p->depth = 2147483647L;
}
int mm_body_pick_visit(MMBodyPick *p, void *actor, MMI32 depth, const MMBodyGeometryServices *s)
{
    void *body = mm_body_from_actor(actor, NULL, s);
    if (depth < p->depth && body && mm_body_in_view(s->state(s->context, body)))
    {
        p->body = body;
        p->actor = actor;
        p->depth = depth;
    }
    return 0;
}
void *mm_body_pick_result(const MMBodyPick *p, MMI32 *set, const MMBodyGeometryServices *s)
{
    MMI32 i, n;
    if (p->body && set)
    {
        *set = -1;
        n = s->part_count(s->context, p->body);
        for (i = 0; i < n; ++i)
            if (s->part(s->context, p->body, i) == p->actor)
            {
                *set = s->part_set(s->context, p->body, i);
                break;
            }
    }
    return p->body;
}
void mm_body_search_begin(MMBodyGeometry *g)
{
    g->found = 0;
    g->set = -1;
}
int mm_body_search_visit(void *target, void *actor, const MMBodyGeometryServices *s)
{
    MMI32 set;
    void *body = mm_body_from_actor(actor, &set, s);
    MMBodyGeometry *g;
    if (!body || target != body)
        return 0;
    g = s->state(s->context, target);
    g->found = 1;
    if (actor == s->highlight(s->context, target))
        return 0;
    g->set = set;
    return 1;
}
