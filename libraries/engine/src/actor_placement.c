/* Actor placement converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_placement.h"
#include "mm/actor_math.h"
#include "mm/actor_owner.h"
#include "mm/actor_transform_edit.h"
#include "mm/scene_content.h"
#include "mm/wide.h"

void mm_actor_placement(MMActorPoint *point, int text,
                             const MMActorPlacementServices *services)
{
    MMActorMatrix camera;
    MMI32 hither, yon, z;
    if (point->dxr == 0 && point->dyr == 0 && point->dzr == 0)
    {
        services->camera(services->context, &camera, &hither, &yon);
        z = -1638400L;
        if (1638400L > yon)
            z = mm_fixed_add(yon, hither) / -2;
        point->dxr = mm_fixed_add(mm_fixed_mac3(0, camera.m[0][0],
            0, camera.m[1][0], z, camera.m[2][0]), camera.m[3][0]);
        point->dyr = 0;
        point->dzr = mm_fixed_add(mm_fixed_mac3(0, camera.m[0][2],
            0, camera.m[1][2], z, camera.m[2][2]), camera.m[3][2]);
    }
    if (text)
        point->dyr = mm_fixed_add(point->dyr, 655360L);
}

MMActorPoint mm_actor_mouse_delta(MMI32 x, MMI32 y, MMI32 previous_x, MMI32 previous_y, MMI32 depth)
{
    MMActorPoint delta;
    delta.dxr = mm_signed_word(((MMU32)x - (MMU32)previous_x) << 16);
    delta.dyr = mm_signed_word(((MMU32)previous_y - (MMU32)y) << 16);
    delta.dzr = depth;
    return delta;
}

MMI32 mm_actor_mouse_key_depth(MMI32 depth, MMU32 elapsed_ms, int up, int down)
{
    MMI32 ticks, seconds, delta;
    if (!!up == !!down)
        return depth;
    ticks = mm_signed_word(elapsed_ms);
    if (ticks < 1)
        ticks = 1;
    seconds = mm_fixed_div(mm_signed_word((MMU32)ticks << 16), 65536000L);
    delta = mm_fixed_mul(seconds, 6553600L);
    return up ? mm_fixed_add(depth, delta) : mm_fixed_sub(depth, delta);
}

void mm_actor_mouse_to_world(MMActorPoint *delta, const MMActorMatrix *axes, const MMActorMatrix *camera,
                             int recording)
{
    /* 0.20 or 0.02 world units per pixel in 16.16. */
    MMI32 scale = recording ? 13107 : 1310;
    MMI32 x = mm_fixed_mul(mm_fixed_mac3(delta->dxr, axes->m[0][0], delta->dyr, axes->m[0][1],
                                         delta->dzr, axes->m[0][2]), scale);
    MMI32 y = mm_fixed_mul(mm_fixed_mac3(delta->dxr, axes->m[1][0], delta->dyr, axes->m[1][1],
                                         delta->dzr, axes->m[1][2]), scale);
    MMI32 z = mm_fixed_mul(mm_fixed_mac3(delta->dxr, axes->m[2][0], delta->dyr, axes->m[2][1],
                                         delta->dzr, axes->m[2][2]), scale);
    delta->dxr = mm_fixed_mac3(x, camera->m[0][0], y, camera->m[1][0], z, camera->m[2][0]);
    delta->dyr = mm_fixed_mac3(x, camera->m[0][1], y, camera->m[1][1], z, camera->m[2][1]);
    delta->dzr = mm_fixed_mac3(x, camera->m[0][2], y, camera->m[1][2], z, camera->m[2][2]);
}

void mm_actor_scale_drag_depth(MMActorPoint *delta, MMI32 actor_z, MMI32 camera_z)
{
    MMI32 depth = mm_fixed_sub(actor_z, camera_z);
    MMI32 scale;
    if (depth < 0)
        depth = mm_fixed_sub(0, depth);
    scale = mm_fixed_div(depth, 6553600L);
    if (scale < 65536L)
        scale = 65536L;
    scale = mm_fixed_mul(scale, 72089L); /* 1.1 */
    delta->dxr = mm_fixed_mul(delta->dxr, scale);
    delta->dyr = mm_fixed_mul(delta->dyr, scale);
    delta->dzr = mm_fixed_mul(delta->dzr, scale);
}

void mm_actor_placement_snapshot(MMActorPlacementState *state, void *snapshot, const MMActorOwnerServices *actors)
{
    if (state->snapshot)
        actors->release(actors->context, state->snapshot);
    state->snapshot = snapshot;
}

void mm_actor_placement_discard(MMActorPlacementState *state, const MMActorOwnerServices *actors)
{
    mm_actor_placement_snapshot(state, NULL, actors);
    if (state->actor)
    {
        actors->release(actors->context, state->actor);
        state->actor = NULL;
    }
}

void mm_actor_placement_end(MMActorPlacementState *state, const MMActorOwnerServices *actors,
                            const MMActorPlacementEditServices *edit)
{
    if (state->actor)
        edit->add_roll(edit->context, state->actor);
    mm_actor_placement_discard(state, actors);
}

void mm_actor_placement_begin(MMActorPlacementState *state, void *actor, int entire_scene,
                              const MMActorOwnerServices *actors, const MMActorPlacementEditServices *edit)
{
    mm_actor_placement_end(state, actors, edit);
    actors->retain(actors->context, actor);
    state->actor = actor;
    state->entire_scene = entire_scene;
    edit->remove_roll(edit->context, actor);
}

int mm_actor_placement_move(const MMActorPlacementState *state, const MMActorPoint *delta, int ground,
                            const MMActorOwnerServices *actors, const MMActorTransformEditServices *effects)
{
    MMActorMoveResult moved = {0, 0, NULL, NULL};
    MMU32 flags = MM_MOVE_ORIENT | (state->entire_scene ? MM_MOVE_SCENE : MM_MOVE_SUBROUTE);
    if (!state->actor)
        return 0;
    if (ground)
        flags |= MM_MOVE_GROUND;
    return mm_actor_owner_move(actors->owner(actors->context, state->actor), delta, flags, &moved, effects) &&
           moved.moved;
}

static void reject_placement(const MMActorPlacementState *state, const MMSceneContentServices *scene,
                             const MMActorPlacementEditServices *edit)
{
    void *actor = state->actor;
    if (state->snapshot)
    {
        actor = state->snapshot;
        mm_scene_actor_add(actor, scene);
    }
    else
    {
        edit->add_roll(edit->context, actor);
        mm_scene_actor_remove(scene->actor_id(scene->context, actor), scene);
    }
    edit->out_of_view(edit->context, scene->actor_id(scene->context, actor));
}

int mm_actor_placement_finish(MMActorPlacementState *state, const MMActorOwnerServices *actors,
                              const MMSceneContentServices *scene, const MMActorPlacementEditServices *edit)
{
    void *undo, *actor = state->actor;
    MMActorOwnerResult copy = {NULL, NULL, NULL};
    MMSceneActorUndo *record;
    if (!actor)
        return 0;
    if (!edit->visible(edit->context, actor))
    {
        reject_placement(state, scene, edit);
        mm_actor_placement_discard(state, actors);
        return 0;
    }
    if (!state->snapshot)
        edit->add_roll(edit->context, actor);
    undo = scene->new_undo(scene->context, 0);
    if (undo && (state->snapshot || mm_actor_owner_duplicate(actor, 1, &copy, actors)))
    {
        record = scene->actor_state(scene->context, undo);
        record->operation = state->snapshot ? MM_SCENE_ACTOR_REPLACE : MM_SCENE_ACTOR_ADD;
        record->actor = state->snapshot ? state->snapshot : copy.actor;
        scene->set_actor_id(scene->context, record->actor, scene->actor_id(scene->context, actor));
        if (state->snapshot)
            actors->retain(actors->context, record->actor);
        if (!scene->commit_undo(scene->context, undo))
        {
            scene->not_undoable(scene->context);
            scene->clear_undo(scene->context);
        }
        edit->enable_tools(edit->context);
    }
    else
    {
        scene->not_undoable(scene->context);
        scene->clear_undo(scene->context);
    }
    mm_actor_placement_discard(state, actors);
    if (undo)
        scene->release_undo(scene->context, undo);
    return 1;
}
