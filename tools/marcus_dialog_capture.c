/* Render the actual dialog controls offscreen for artwork/RGB review. */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    SDL_Surface *surface;
    SDL_Renderer *renderer;
    MarcusUI ui;
    char path[2048];
    int scale, confirm, ok;
    if (argc != 3 || (scale = atoi(argv[2])) < 1 || scale > 8)
    {
        fprintf(stderr, "Usage: marcus-dialog-capture OUTPUT_DIRECTORY SCALE_1_TO_8\n");
        return 2;
    }
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true);
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    ok = 1;
    for (confirm = 0; confirm < 2 && ok; ++confirm)
    {
        surface =
            SDL_CreateSurface(UI_DIALOG_BUTTON_WIDTH * scale, UI_DIALOG_BUTTON_HEIGHT * scale, SDL_PIXELFORMAT_RGBA32);
        renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
        if (!renderer)
        {
            SDL_DestroySurface(surface);
            ok = 0;
            break;
        }
        SDL_memset(&ui, 0, sizeof(ui));
        ui.renderer = renderer;
        ui.opacity = 1;
        ui.mouse_x = ui.mouse_y = -1;
        SDL_SetRenderScale(renderer, (float)scale, (float)scale);
        ui_dialog_button(&ui, 0, 0, confirm, 1, "");
        SDL_RenderPresent(renderer);
        SDL_snprintf(path, sizeof(path), "%s/%s-%dx.bmp", argv[1], confirm ? "ok" : "cancel", scale);
        ok = ui.dialog_icons[confirm ? 4 : 0] != NULL && ui.dialog_width == UI_DIALOG_BUTTON_WIDTH * scale &&
             ui.dialog_height == UI_DIALOG_BUTTON_HEIGHT * scale;
        if (ok)
            ok = SDL_SaveBMP(surface, path);
        else
            SDL_SetError("Dialog supersampling texture was not created at the requested resolution");
        ui_destroy(&ui);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }
    if (!ok)
        fprintf(stderr, "%s\n", SDL_GetError());
    SDL_Quit();
    return ok ? 0 : 1;
}
