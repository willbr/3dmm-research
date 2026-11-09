#include "player.h"
#include <string.h>

static const char *names[MARCUS_RENDERER_COUNT] = {"BRender (CPU)", "BRender (compute)", "SDL GPU"};
static const char *ids[MARCUS_RENDERER_COUNT] = {"brender", "brender-compute", "sdl-gpu"};
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
