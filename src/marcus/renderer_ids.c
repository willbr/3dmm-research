#include "render_plan.h"
#include "mm/preview_render.h"
#include <string.h>

static const char *names[MARCUS_RENDERER_COUNT] = {"BRender (CPU C)", "BRender (compute)", "SDL GPU", "BRender (CPU ASM x86)"};
static const char *ids[MARCUS_RENDERER_COUNT] = {"brender", "brender-compute", "sdl-gpu", "brender-asm"};
int marcus_renderer_is_gpu(int renderer)
{
    return renderer == MARCUS_RENDERER_BRENDER_COMPUTE || renderer == MARCUS_RENDERER_SDL_GPU;
}
const char *marcus_display_mode_name(int mode)
{
    static const char *display_names[MARCUS_DISPLAY_COUNT] = {
        "Textured (authored)", "Solid", "Flat shading", "Textured (authored)", "Wireframe", "Depth", "Face normals"};
    return mode >= 0 && mode < MARCUS_DISPLAY_COUNT ? display_names[mode] : "Unknown mode";
}
int marcus_render_options_valid(const MarcusRenderOptions *options)
{
    return options && options->display_mode >= 0 && options->display_mode < MARCUS_DISPLAY_COUNT &&
           options->wire_overlay >= 0 && options->wire_overlay <= 1 &&
           options->antialias >= 0 && options->antialias <= 2 &&
           options->texture_filter >= 0 && options->texture_filter <= 1 &&
           options->orthographic >= 0 && options->orthographic <= 1 &&
           options->axis_view >= 0 && options->axis_view <= 3 &&
           options->ground_grid >= 0 && options->ground_grid <= 1;
}
int marcus_render_options_supported(int renderer, const MarcusRenderOptions *options)
{
    return marcus_renderer_is_gpu(renderer) ||
           (options->display_mode < MARCUS_DISPLAY_DEPTH && !options->wire_overlay && !options->antialias &&
            !options->texture_filter);
}
int marcus_render_plan(const MarcusRenderRequest *request, const MarcusRenderOptions *settings, MarcusRenderPlan *plan)
{
    MarcusRenderPlan frame;
    frame.scale = marcus_renderer_is_gpu(request->renderer) ? 1u << settings->antialias : 1u;
    if (request->width > MM_PREVIEW_MAX_DIMENSION / frame.scale ||
        request->height > MM_PREVIEW_MAX_DIMENSION / frame.scale)
        return 0;
    frame.width = request->width * frame.scale;
    frame.height = request->height * frame.scale;
    frame.movie = request->authored && !request->free_camera;
    frame.orthographic = !request->authored && settings->orthographic;
    frame.axis = request->authored ? 0 : settings->axis_view;
    frame.depth = !frame.movie || settings->ground_grid;
    frame.palette = frame.movie && request->palette_animating;
    frame.grid = settings->ground_grid && !(frame.movie && request->transitioning);
    /* Keep transient placements on the worker when it can apply them to the source movie. */
    frame.use_worker = request->renderer == MARCUS_RENDERER_BRENDER_ASM &&
                       !(request->native_edits && request->temporary_pose);
    frame.materialized = frame.use_worker && request->native_edits;
    *plan = frame;
    return 1;
}
const char *marcus_renderer_name(int renderer)
{
    return renderer >= 0 && renderer < MARCUS_RENDERER_COUNT ? names[renderer] : "Unknown renderer";
}
const char *marcus_renderer_id(int renderer)
{
    return renderer >= 0 && renderer < MARCUS_RENDERER_COUNT ? ids[renderer] : "unknown";
}
int marcus_renderer_from_id(const char *id)
{
    int i;
    for (i = 0; id && i < MARCUS_RENDERER_COUNT; ++i)
        if (!strcmp(id, ids[i]))
            return i;
    return -1;
}
