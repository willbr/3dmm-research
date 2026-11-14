/* Body geometry/render policy converted from engine body.cpp and body_world.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BODY_GEOMETRY_H
#define THREE_MM_ENGINE_BODY_GEOMETRY_H
#include "actor_math.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMBodyRect
    {
        ThreeMMI32 left, top, right, bottom;
    } ThreeMMBodyRect;
    typedef struct ThreeMMBodyBounds
    {
        ThreeMMI32 min[3], max[3];
    } ThreeMMBodyBounds;
    /* Plain persistent state; zero initialization matches a fresh native Body.
     * Copies contain no references. Render callbacks mutate bounds, not visibility
     * counts. Empty bounds preserve the last visible rectangle. */
    typedef struct ThreeMMBodyGeometry
    {
        ThreeMMBodyRect bounds, last_visible;
        ThreeMMI32 found, set;
    } ThreeMMBodyGeometry;
    typedef struct ThreeMMBodyGeometryServices
    {
        void *context;
        void *(*owner)(void *, void *actor);
        ThreeMMBodyGeometry *(*state)(void *, void *body);
        ThreeMMI32 (*part_count)(void *, void *body);
        void *(*part)(void *, void *body, ThreeMMI32);
        ThreeMMI32 (*part_set)(void *, void *body, ThreeMMI32);
        void *(*highlight)(void *, void *body);
        int (*actor_type)(void *, void *actor);
        void (*set_actor_type)(void *, void *actor, int);
        void (*local_bounds)(void *, void *body, ThreeMMBodyBounds *);
        void (*root_matrix)(void *, void *body, ThreeMMActorMatrix *);
        void (*highlight_matrix)(void *, void *body, const ThreeMMBodyBounds *);
        void (*missing_owner)(void *);
        int none_type, model_type;
    } ThreeMMBodyGeometryServices;
    /* Callbacks borrow all objects; local_bounds is synchronous and must not
     * reenter or mutate the same body. Highlight type is restored before matrix
     * queries/publication. Native actor-tree traversal remains a renderer service. */
    void *three_mm_body_from_actor(void *, ThreeMMI32 *set, const ThreeMMBodyGeometryServices *);
    void three_mm_body_geometry_init(ThreeMMBodyGeometry *);
    int three_mm_body_in_view(const ThreeMMBodyGeometry *);
    void three_mm_body_clear_bounds(ThreeMMBodyGeometry *);
    void three_mm_body_rendered(void *actor, const ThreeMMBodyRect *, const ThreeMMBodyGeometryServices *);
    void three_mm_body_prepare_render(void *actor, const ThreeMMBodyGeometryServices *);
    void three_mm_body_render_rect(void *actor, ThreeMMBodyRect *, const ThreeMMBodyGeometryServices *);
    void three_mm_body_visible_rect(const ThreeMMBodyGeometry *, ThreeMMBodyRect *);
    void three_mm_body_center(const ThreeMMBodyGeometry *, ThreeMMI32 *x, ThreeMMI32 *y);
    void three_mm_body_bounds(void *body, int world, ThreeMMBodyBounds *, const ThreeMMBodyGeometryServices *);
    void three_mm_body_transform_bounds(const ThreeMMBodyBounds *, const ThreeMMActorMatrix *, ThreeMMBodyBounds *);
    void three_mm_body_position(void *body, ThreeMMI32 out[3], const ThreeMMBodyGeometryServices *);
    /* Picking uses an explicit caller-owned context. Strictly closer visible hits
     * win (first hit wins equal depth); root/highlight hits may select a body but
     * publish set -1. Search marks found for highlight hits and keeps searching. */
    typedef struct ThreeMMBodyPick
    {
        void *body, *actor;
        ThreeMMI32 depth;
    } ThreeMMBodyPick;
    void three_mm_body_pick_begin(ThreeMMBodyPick *);
    int three_mm_body_pick_visit(ThreeMMBodyPick *, void *actor, ThreeMMI32 depth, const ThreeMMBodyGeometryServices *);
    void *three_mm_body_pick_result(const ThreeMMBodyPick *, ThreeMMI32 *set, const ThreeMMBodyGeometryServices *);
    void three_mm_body_search_begin(ThreeMMBodyGeometry *);
    int three_mm_body_search_visit(void *target, void *actor, const ThreeMMBodyGeometryServices *);
#ifdef __cplusplus
}
#endif
#endif
