/* World camera, picking and rendered-coordinate policy. */
#ifndef MM_ENGINE_WORLD_CAMERA_H
#define MM_ENGINE_WORLD_CAMERA_H
#include "actor_motion.h"
#include "world_buffers.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMWorldCamera
    {
        MMActorMatrix matrix;
        MMI32 near_z, far_z;
        unsigned short fov;
        void *picked;
        MMI32 pick_depth;
    } MMWorldCamera;
    typedef struct MMWorldCameraServices
    {
        void *context;
        void (*publish)(void *, const MMWorldCamera *);
        void (*dirty)(void *);
        /* Synchronous; each candidate calls mm_world_pick_visit.
         * Actor handles are borrowed. Callbacks must not dispose the owner. */
        void (*pick)(void *, MMI32 x, MMI32 y);
    } MMWorldCameraServices;
    void mm_world_camera_init(MMWorldCamera *);
    /* As in the native API, callers supply a valid matrix and near < far.
     * Setting the camera dirties the working region, not the changed latch. */
    void mm_world_camera_set(MMWorldCamera *, const MMActorMatrix *, MMI32 near_z,
                                   MMI32 far_z, unsigned short fov, const MMWorldCameraServices *);
    void mm_world_camera_get(const MMWorldCamera *, MMActorMatrix *, MMI32 *near_z,
                                   MMI32 *far_z, unsigned short *fov);
    void mm_world_pick_begin(MMWorldCamera *);
    /* Equal depths retain the first actor; MAX is not a hit. Always continue. */
    int mm_world_pick_visit(MMWorldCamera *, void *actor, MMI32 depth);
    /* Coordinates use the current RGB view's origin. No viewport clamp.
     * Half-resolution division truncates toward zero.
     * Invalid scales or unrepresentable results fail without writing outputs. */
    int mm_world_pick_point(const MMWorldBuffers *, MMI32 x, MMI32 y, MMI32 *out_x, MMI32 *out_y);
    void *mm_world_pick(MMWorldCamera *, const MMWorldBuffers *, MMI32 x, MMI32 y, const MMWorldCameraServices *);
    /* Inclusive SDK bounds become exclusive client bounds. Preserve the legacy
     * (exclusive + scale - 1) / scale expression, even for negative bounds.
     * Overflow fails without publishing a partial rectangle. */
    int mm_world_rendered_bounds(MMI32 scale, const MMI32 bounds[4], MMI32 rect[4]);
    /* Host callbacks expose typed actor fields; traversal policy lives here.
     * As with the SDK actor tree, parent links must be acyclic. */
    void *mm_world_actor_owner(void *actor, void *(*parent)(void *), void *(*owner)(void *));
#ifdef __cplusplus
}
#endif
#endif
