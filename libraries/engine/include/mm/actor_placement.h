/* Actor placement converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_PLACEMENT_H
#define MM_ENGINE_ACTOR_PLACEMENT_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorPlacementServices
{
    void *context;
    void (*camera)(void *, MMActorMatrix *, MMI32 *hither, MMI32 *yon);
} MMActorPlacementServices;
/* In/out point is the background's authored placement. All-zero means absent:
 * only then query the camera, project 25 units ahead (or the clipping midpoint)
 * and put the actor on the floor. Authored heights are retained. Text adds ten
 * units in either case. Scalars and matrix use BRender's signed 16.16 format. */
void mm_actor_placement(MMActorPoint *, int text,
                             const MMActorPlacementServices *);
/* Mouse deltas use screen X and inverted screen Y. Keyboard depth is signed
 * 16.16 pixels at 100 pixels/second, with a minimum sample interval of 1 ms. */
MMActorPoint mm_actor_mouse_delta(MMI32 x, MMI32 y, MMI32 previous_x, MMI32 previous_y, MMI32 depth);
MMI32 mm_actor_mouse_key_depth(MMI32 depth, MMU32 elapsed_ms, int up, int down);
/* Map mouse deltas through the three input axes, then rotate through the
 * camera's three axes. Translation is ignored. Both functions preserve the
 * original signed 16.16 rounding. */
void mm_actor_mouse_to_world(MMActorPoint *, const MMActorMatrix *axes, const MMActorMatrix *camera,
                             int recording);
void mm_actor_scale_drag_depth(MMActorPoint *, MMI32 actor_z, MMI32 camera_z);

struct MMActorOwnerServices;
struct MMActorTransformEditServices;
struct MMSceneContentServices;
typedef struct MMActorPlacementState
{
    void *actor, *snapshot;
    int entire_scene;
} MMActorPlacementState;
typedef struct MMActorPlacementEditServices
{
    void *context;
    void (*remove_roll)(void *, void *actor);
    void (*add_roll)(void *, void *actor);
    int (*visible)(void *, void *actor);
    void (*out_of_view)(void *, MMI32 actor_id);
    void (*enable_tools)(void *);
} MMActorPlacementEditServices;
/* Start with a zero-initialized state. Begin retains the live actor and
 * temporarily removes its roll-call reference. Set snapshot takes ownership.
 * Ending restores the roll-call reference and releases both owners. Discard
 * only releases ownership, for teardown when the movie is being closed. */
void mm_actor_placement_begin(MMActorPlacementState *, void *actor, int entire_scene,
                              const struct MMActorOwnerServices *, const MMActorPlacementEditServices *);
void mm_actor_placement_end(MMActorPlacementState *, const struct MMActorOwnerServices *,
                            const MMActorPlacementEditServices *);
void mm_actor_placement_snapshot(MMActorPlacementState *, void *snapshot, const struct MMActorOwnerServices *);
void mm_actor_placement_discard(MMActorPlacementState *, const struct MMActorOwnerServices *);
int mm_actor_placement_move(const MMActorPlacementState *, const MMActorPoint *, int ground,
                            const struct MMActorOwnerServices *, const struct MMActorTransformEditServices *);
/* Accept an in-view actor and register add/replace undo. Rejecting the actor
 * restores the replacement snapshot or removes a new actor. A failed undo
 * commit keeps an accepted edit and clears the undo list. */
int mm_actor_placement_finish(MMActorPlacementState *, const struct MMActorOwnerServices *,
                             const struct MMSceneContentServices *, const MMActorPlacementEditServices *);
#ifdef __cplusplus
}
#endif
#endif
