#include "player.h"
#include <string.h>

static const char *names[MARCUS_RENDERER_COUNT] = {"BRender (CPU)", "BRender (compute)", "SDL GPU"};
static const char *ids[MARCUS_RENDERER_COUNT] = {"brender", "brender-compute", "sdl-gpu"};
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
           options->axis_view >= 0 && options->axis_view <= 3;
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
