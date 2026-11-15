/* Body geometry/render policy converted from engine body.cpp and body_world.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BODY_GEOMETRY_H
#define MM_ENGINE_BODY_GEOMETRY_H
#include "actor_math.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMBodyRect
    {
        MMI32 left, top, right, bottom;
    } MMBodyRect;
    typedef struct MMBodyBounds
    {
        MMI32 min[3], max[3];
    } MMBodyBounds;
    /* Plain persistent state; zero initialization matches a fresh native Body.
     * Copies contain no references. Render callbacks mutate bounds, not visibility
     * counts. Empty bounds preserve the last visible rectangle. */
    typedef struct MMBodyGeometry
    {
        MMBodyRect bounds, last_visible;
        MMI32 found, set;
    } MMBodyGeometry;
    typedef struct MMBodyGeometryServices
    {
        void *context;
        void *(*owner)(void *, void *actor);
        MMBodyGeometry *(*state)(void *, void *body);
        MMI32 (*part_count)(void *, void *body);
        void *(*part)(void *, void *body, MMI32);
        MMI32 (*part_set)(void *, void *body, MMI32);
        void *(*highlight)(void *, void *body);
        int (*actor_type)(void *, void *actor);
        void (*set_actor_type)(void *, void *actor, int);
        void (*local_bounds)(void *, void *body, MMBodyBounds *);
        void (*root_matrix)(void *, void *body, MMActorMatrix *);
        void (*highlight_matrix)(void *, void *body, const MMBodyBounds *);
        void (*missing_owner)(void *);
        int none_type, model_type;
    } MMBodyGeometryServices;
    /* Callbacks borrow all objects; local_bounds is synchronous and must not
     * reenter or mutate the same body. Highlight type is restored before matrix
     * queries/publication. Native actor-tree traversal remains a renderer service. */
    void *mm_body_from_actor(void *, MMI32 *set, const MMBodyGeometryServices *);
    void mm_body_geometry_init(MMBodyGeometry *);
    int mm_body_in_view(const MMBodyGeometry *);
    void mm_body_clear_bounds(MMBodyGeometry *);
    void mm_body_rendered(void *actor, const MMBodyRect *, const MMBodyGeometryServices *);
    void mm_body_prepare_render(void *actor, const MMBodyGeometryServices *);
    void mm_body_render_rect(void *actor, MMBodyRect *, const MMBodyGeometryServices *);
    void mm_body_visible_rect(const MMBodyGeometry *, MMBodyRect *);
    void mm_body_center(const MMBodyGeometry *, MMI32 *x, MMI32 *y);
    void mm_body_bounds(void *body, int world, MMBodyBounds *, const MMBodyGeometryServices *);
    void mm_body_transform_bounds(const MMBodyBounds *, const MMActorMatrix *, MMBodyBounds *);
    void mm_body_position(void *body, MMI32 out[3], const MMBodyGeometryServices *);
    /* Picking uses an explicit caller-owned context. Strictly closer visible hits
     * win (first hit wins equal depth); root/highlight hits may select a body but
     * publish set -1. Search marks found for highlight hits and keeps searching. */
    typedef struct MMBodyPick
    {
        void *body, *actor;
        MMI32 depth;
    } MMBodyPick;
    void mm_body_pick_begin(MMBodyPick *);
    int mm_body_pick_visit(MMBodyPick *, void *actor, MMI32 depth, const MMBodyGeometryServices *);
    void *mm_body_pick_result(const MMBodyPick *, MMI32 *set, const MMBodyGeometryServices *);
    void mm_body_search_begin(MMBodyGeometry *);
    int mm_body_search_visit(void *target, void *actor, const MMBodyGeometryServices *);
#ifdef __cplusplus
}
#endif
#endif
