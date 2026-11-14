/* World camera, picking and rendered-coordinate policy. */
#ifndef THREE_MM_ENGINE_WORLD_CAMERA_H
#define THREE_MM_ENGINE_WORLD_CAMERA_H
#include "actor_motion.h"
#include "world_buffers.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMWorldCamera
    {
        ThreeMMActorMatrix matrix;
        ThreeMMI32 near_z, far_z;
        unsigned short fov;
        void *picked;
        ThreeMMI32 pick_depth;
    } ThreeMMWorldCamera;
    typedef struct ThreeMMWorldCameraServices
    {
        void *context;
        void (*publish)(void *, const ThreeMMWorldCamera *);
        void (*dirty)(void *);
        /* Synchronous; each candidate calls three_mm_world_pick_visit.
         * Actor handles are borrowed. Callbacks must not dispose the owner. */
        void (*pick)(void *, ThreeMMI32 x, ThreeMMI32 y);
    } ThreeMMWorldCameraServices;
    void three_mm_world_camera_init(ThreeMMWorldCamera *);
    /* As in the native API, callers supply a valid matrix and near < far.
     * Setting the camera dirties the working region, not the changed latch. */
    void three_mm_world_camera_set(ThreeMMWorldCamera *, const ThreeMMActorMatrix *, ThreeMMI32 near_z,
                                   ThreeMMI32 far_z, unsigned short fov, const ThreeMMWorldCameraServices *);
    void three_mm_world_camera_get(const ThreeMMWorldCamera *, ThreeMMActorMatrix *, ThreeMMI32 *near_z,
                                   ThreeMMI32 *far_z, unsigned short *fov);
    void three_mm_world_pick_begin(ThreeMMWorldCamera *);
    /* Equal depths retain the first actor; MAX is not a hit. Always continue. */
    int three_mm_world_pick_visit(ThreeMMWorldCamera *, void *actor, ThreeMMI32 depth);
    /* No viewport clamp. Half-resolution division truncates toward zero.
     * Invalid scales or unrepresentable results fail without writing outputs. */
    int three_mm_world_pick_point(const ThreeMMWorldBuffers *, ThreeMMI32 origin_x, ThreeMMI32 origin_y, ThreeMMI32 x,
                                  ThreeMMI32 y, ThreeMMI32 *out_x, ThreeMMI32 *out_y);
    void *three_mm_world_pick(ThreeMMWorldCamera *, const ThreeMMWorldBuffers *, ThreeMMI32 origin_x,
                              ThreeMMI32 origin_y, ThreeMMI32 x, ThreeMMI32 y, const ThreeMMWorldCameraServices *);
    /* Inclusive SDK bounds become exclusive client bounds. Preserve the legacy
     * (exclusive + scale - 1) / scale expression, even for negative bounds.
     * Overflow fails without publishing a partial rectangle. */
    int three_mm_world_rendered_bounds(ThreeMMI32 scale, const ThreeMMI32 bounds[4], ThreeMMI32 rect[4]);
    /* Host callbacks expose typed actor fields; traversal policy lives here.
     * As with the SDK actor tree, parent links must be acyclic. */
    void *three_mm_world_actor_owner(void *actor, void *(*parent)(void *), void *(*owner)(void *));
#ifdef __cplusplus
}
#endif
#endif
