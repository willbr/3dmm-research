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
    struct MMBodyState;
    struct MMBodyShape;
    struct MMBodyDrawView;
    typedef struct MMBodyGeometryServices
    {
        void *context;
        void *(*owner)(void *, void *actor);
        struct MMBodyState *(*state)(void *, void *body);
        void *(*actor_at)(void *, void *actors, MMI32);
        int (*actor_type)(void *, void *actor);
        void (*set_actor_type)(void *, void *actor, int);
        void (*local_bounds)(void *, void *root, MMBodyBounds *);
        void (*highlight_matrix)(void *, void *highlight, const MMBodyBounds *);
        void (*missing_owner)(void *);
        int none_type, model_type;
    } MMBodyGeometryServices;
    /* state returns the borrowed live state. actor_at indexes its renderer
     * array, including the root at 0 and the highlight at 1.
     * local_bounds is synchronous and must not reenter or mutate the same
     * body. Highlight type is restored before consuming the root matrix. */
    void *mm_body_from_actor(void *, MMI32 *set, const MMBodyGeometryServices *);
    /* Admit only the body's root actor. On success, return the borrowed owner
     * and publish its draw view. On failure, leave the view untouched. */
    void *mm_body_read_render(void *actor, struct MMBodyDrawView *, const MMBodyGeometryServices *);
    int mm_body_in_view(const MMBodyGeometry *);
    void mm_body_clear_bounds(MMBodyGeometry *);
    void mm_body_rendered(void *actor, const MMBodyRect *, const MMBodyGeometryServices *);
    void mm_body_prepare_render(void *actor, const MMBodyGeometryServices *);
    int mm_body_render_rect(void *actor, MMBodyRect *, const MMBodyGeometryServices *);
    void mm_body_visible_rect(const MMBodyGeometry *, MMBodyRect *);
    void mm_body_center(const MMBodyGeometry *, MMI32 *x, MMI32 *y);
    void mm_body_bounds(const struct MMBodyShape *, int world, MMBodyBounds *, const MMBodyGeometryServices *);
    void mm_body_transform_bounds(const MMBodyBounds *, const MMActorMatrix *, MMBodyBounds *);
    void mm_body_position(const struct MMBodyShape *, MMI32 out[3]);
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
    /* The scan visits hits synchronously in renderer order and stops on a
     * nonzero visitor result. A missed pick leaves the set output untouched.
     * A search always publishes its set, including -1 for no part hit. */
    typedef int MMBodyHitVisitor(void *, void *actor, MMI32 depth);
    typedef void MMBodyHitScan(void *, MMBodyHitVisitor *, void *visitor_context);
    void *mm_body_pick_at(MMBodyHitScan *, void *scan_context, MMI32 *set, const MMBodyGeometryServices *);
    int mm_body_search_at(void *target, MMBodyHitScan *, void *scan_context, MMI32 *set,
                          const MMBodyGeometryServices *);
#ifdef __cplusplus
}
#endif
#endif
