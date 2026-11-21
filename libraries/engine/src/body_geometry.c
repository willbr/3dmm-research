/* Body geometry/render policy converted from engine body.cpp and body_world.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_create.h"
#include <string.h>
static MMI32 actor_set(const MMBodyShape *shape, void *actor, const MMBodyGeometryServices *s)
{
    MMI32 i;
    for (i = 2; i < shape->part_count + 2; ++i)
        if (s->actor_at(s->context, shape->actors, i) == actor)
            return shape->bindings[i].set;
    return -1;
}
void *mm_body_from_actor(void *actor, MMI32 *set, const MMBodyGeometryServices *s)
{
    void *body = s->owner(s->context, actor);
    if (!body)
    {
        if (s->missing_owner)
            s->missing_owner(s->context);
        return NULL;
    }
    if (set)
        *set = actor_set(&s->state(s->context, body)->shape, actor, s);
    return body;
}
void *mm_body_read_render(void *actor, MMBodyDrawView *draw, const MMBodyGeometryServices *s)
{
    void *body = s->owner(s->context, actor);
    const MMBodyShape *shape;
    if (!body)
        return NULL;
    shape = &s->state(s->context, body)->shape;
    if (actor != shape->actors)
        return NULL;
    draw->bindings = shape->bindings;
    draw->part_count = shape->part_count;
    return body;
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
    bounds = &s->state(s->context, body)->geometry.bounds;
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
    MMBodyState *state;
    MMBodyGeometry *g;
    MMBodyBounds bounds;
    if (!body)
        return;
    state = s->state(s->context, body);
    g = &state->geometry;
    if (mm_body_in_view(g))
    {
        g->last_visible = g->bounds;
        mm_body_clear_bounds(g);
    }
    highlight = s->actor_at(s->context, state->shape.actors, 1);
    if (s->actor_type(s->context, highlight) == s->model_type)
    {
        mm_body_bounds(&state->shape, 0, &bounds, s);
        s->highlight_matrix(s->context, highlight, &bounds);
    }
}
int mm_body_render_rect(void *actor, MMBodyRect *out, const MMBodyGeometryServices *s)
{
    void *body = mm_body_from_actor(actor, NULL, s);
    if (!body)
        return 0;
    *out = s->state(s->context, body)->geometry.bounds;
    return 1;
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
static void include_transformed(const MMBodyBounds *in, const MMActorMatrix *m, MMBodyBounds *bounds)
{
    MMI32 point[3], v;
    unsigned i, j;
    for (i = 0; i < 8; ++i)
    {
        for (j = 0; j < 3; ++j)
            point[j] = i & (1U << j) ? in->max[j] : in->min[j];
        for (j = 0; j < 3; ++j)
        {
            v = mm_fixed_add(
                mm_fixed_mac3(point[0], m->m[0][j], point[1], m->m[1][j], point[2], m->m[2][j]), m->m[3][j]);
            if (v < bounds->min[j])
                bounds->min[j] = v;
            if (v > bounds->max[j])
                bounds->max[j] = v;
        }
    }
}
void mm_body_transform_bounds(const MMBodyBounds *in, const MMActorMatrix *m, MMBodyBounds *out)
{
    /* Whole-body bounds preserve the retail +/-10000 sentinels. */
    MMBodyBounds result = {{655360000L, 655360000L, 655360000L}, {-655360000L, -655360000L, -655360000L}};
    include_transformed(in, m, &result);
    *out = result;
}
int mm_body_set_bounds(const MMBodyShape *shape, MMI32 set, MMBodyModelBounds *read, MMBodyBounds *out)
{
    MMBodyBounds result = {{2147483647L, 2147483647L, 2147483647L},
                           {(-2147483647L - 1), (-2147483647L - 1), (-2147483647L - 1)}};
    MMI32 i;
    int have_bounds = 0;
    if (set < 0 || set >= shape->set_count)
        return 0;
    for (i = 0; i < shape->part_count; ++i)
    {
        const MMBodyPartBinding *part = &shape->bindings[i + 2];
        MMBodyBounds local;
        MMActorMatrix world;
        if (part->set != set || !part->model_handle)
            continue;
        read(part->model_handle, &local);
        mm_body_shape_part_world(shape, i, &world);
        include_transformed(&local, &world, &result);
        have_bounds = 1;
    }
    if (have_bounds)
        *out = result;
    return have_bounds;
}
void mm_body_bounds(const MMBodyShape *shape, int world, MMBodyBounds *out, const MMBodyGeometryServices *s)
{
    void *highlight = s->actor_at(s->context, shape->actors, 1);
    int type = s->actor_type(s->context, highlight);
    MMBodyBounds bounds;
    s->set_actor_type(s->context, highlight, s->none_type);
    s->local_bounds(s->context, shape->actors, &bounds);
    s->set_actor_type(s->context, highlight, type);
    if (world)
        mm_body_transform_bounds(&bounds, &shape->bindings[0].matrix, out);
    else
        *out = bounds;
}
void mm_body_position(const MMBodyShape *shape, MMI32 out[3])
{
    memcpy(out, shape->bindings[0].matrix.m[3], 3 * sizeof(*out));
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
    if (depth < p->depth && body && mm_body_in_view(&s->state(s->context, body)->geometry))
    {
        p->body = body;
        p->actor = actor;
        p->depth = depth;
    }
    return 0;
}
void *mm_body_pick_result(const MMBodyPick *p, MMI32 *set, const MMBodyGeometryServices *s)
{
    if (p->body && set)
        *set = actor_set(&s->state(s->context, p->body)->shape, p->actor, s);
    return p->body;
}
void mm_body_search_begin(MMBodyGeometry *g)
{
    g->found = 0;
    g->set = -1;
}
int mm_body_search_visit(void *target, void *actor, const MMBodyGeometryServices *s)
{
    void *body = mm_body_from_actor(actor, NULL, s);
    MMBodyState *state;
    if (!body || target != body)
        return 0;
    state = s->state(s->context, target);
    state->geometry.found = 1;
    if (actor == s->actor_at(s->context, state->shape.actors, 1))
        return 0;
    state->geometry.set = actor_set(&state->shape, actor, s);
    return 1;
}

typedef struct BodyPickContext
{
    MMBodyPick pick;
    const MMBodyGeometryServices *services;
} BodyPickContext;

static int pick_at_hit(void *context, void *actor, MMI32 depth)
{
    BodyPickContext *pick = (BodyPickContext *)context;
    return mm_body_pick_visit(&pick->pick, actor, depth, pick->services);
}

void *mm_body_pick_at(MMBodyHitScan *scan, void *context, MMI32 *set, const MMBodyGeometryServices *s)
{
    BodyPickContext pick;
    mm_body_pick_begin(&pick.pick);
    pick.services = s;
    scan(context, pick_at_hit, &pick);
    return mm_body_pick_result(&pick.pick, set, s);
}

typedef struct BodySearchContext
{
    void *target;
    const MMBodyGeometryServices *services;
} BodySearchContext;

static int search_at_hit(void *context, void *actor, MMI32 depth)
{
    const BodySearchContext *search = (const BodySearchContext *)context;
    (void)depth;
    return mm_body_search_visit(search->target, actor, search->services);
}

int mm_body_search_at(void *target, MMBodyHitScan *scan, void *context, MMI32 *set,
                      const MMBodyGeometryServices *s)
{
    BodySearchContext search;
    MMBodyGeometry *geometry = &s->state(s->context, target)->geometry;
    mm_body_search_begin(geometry);
    search.target = target;
    search.services = s;
    scan(context, search_at_hit, &search);
    *set = geometry->set;
    return geometry->found != 0;
}
