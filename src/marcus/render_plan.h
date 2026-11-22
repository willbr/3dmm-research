#ifndef MARCUS_RENDER_PLAN_H
#define MARCUS_RENDER_PLAN_H
#include "player.h"
#include "mm/collections.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusRenderRequest
{
    MMU32 width, height;
    int renderer, authored, free_camera, palette_animating, transitioning, native_edits, temporary_pose;
} MarcusRenderRequest;

typedef struct MarcusRenderPlan
{
    MMU32 width, height, scale;
    int movie, orthographic, axis, depth, palette, grid, use_worker, materialized;
} MarcusRenderPlan;

int marcus_render_options_supported(int renderer, const MarcusRenderOptions *);
/* Requires validated settings. A frame outside the render limit leaves the plan untouched. */
int marcus_render_plan(const MarcusRenderRequest *, const MarcusRenderOptions *, MarcusRenderPlan *);

#ifdef __cplusplus
}
#endif
#endif
