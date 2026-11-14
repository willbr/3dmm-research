/* Three synchronized image panes. Application code is C89. */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include "backend.h"
#include "image_view.h"

#define TILE_SIZE 1024
typedef struct ImageTile
{
    SDL_Texture *texture;
    int x, y, w, h;
} ImageTile;
typedef struct ImageSet
{
    ImageTile *tiles[4];
    int count, width, height;
    unsigned long changed;
} ImageSet;
typedef struct Pane
{
    ImDrawList *draw;
    ImVec2 origin, size;
} Pane;

static SDL_Renderer *renderer;
static ImageSet images;
static ReviewImageView view = {0, 0, 1};
static char paths[2][4096], error[1024];
static int fit = 1;

static ImVec2 vec(float x, float y)
{
    ImVec2 v;
    v.x = x;
    v.y = y;
    return v;
}
static void clear_images(ImageSet *set)
{
    int i, j;
    for (i = 0; i < 4; ++i)
    {
        if (set->tiles[i])
            for (j = 0; j < set->count; ++j)
                SDL_DestroyTexture(set->tiles[i][j].texture);
        SDL_free(set->tiles[i]);
    }
    memset(set, 0, sizeof(*set));
}
static SDL_Surface *read_image(const char *path)
{
    SDL_Surface *source, *rgba;
    source = SDL_LoadPNG(path);
    if (!source)
        return NULL;
    if ((double)source->w * source->h > 128000000)
    {
        SDL_DestroySurface(source);
        SDL_SetError("Image exceeds the 128 megapixel limit");
        return NULL;
    }
    rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    return rgba;
}
static int tile_texture(ImageTile *tile, SDL_Surface *surface)
{
    tile->texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!tile->texture)
        return 0;
    SDL_SetTextureScaleMode(tile->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(tile->texture, SDL_BLENDMODE_NONE);
    return 1;
}
static int load_images(void)
{
    ImageSet next;
    SDL_Surface *source[2], *part, *delta;
    ImageTile *tile;
    unsigned char *a, *b, *d;
    int i, index, x, y, row, columns, rows, size;
    SDL_PropertiesID properties;
    memset(&next, 0, sizeof(next));
    memset(source, 0, sizeof(source));
    delta = NULL;
    error[0] = 0;
    for (i = 0; i < 2; ++i)
    {
        source[i] = read_image(paths[i]);
        if (!source[i])
        {
            SDL_snprintf(error, sizeof(error), "%s: %s", i ? "Ours" : "Retail", SDL_GetError());
            goto failed;
        }
    }
    if (source[0]->w != source[1]->w || source[0]->h != source[1]->h)
    {
        SDL_snprintf(error, sizeof(error), "Image sizes differ: Retail %dx%d, Ours %dx%d. Use matching captures.",
                     source[0]->w, source[0]->h, source[1]->w, source[1]->h);
        goto failed;
    }
    next.width = source[0]->w;
    next.height = source[0]->h;
    properties = SDL_GetRendererProperties(renderer);
    size = (int)SDL_GetNumberProperty(properties, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, TILE_SIZE);
    if (size <= 0 || size > TILE_SIZE)
        size = TILE_SIZE;
    columns = (next.width + size - 1) / size;
    rows = (next.height + size - 1) / size;
    next.count = columns * rows;
    for (i = 0; i < 4; ++i)
    {
        next.tiles[i] = (ImageTile *)SDL_calloc((size_t)next.count, sizeof(ImageTile));
        if (!next.tiles[i])
        {
            SDL_SetError("Cannot allocate image tiles");
            goto failed;
        }
    }
    for (index = 0; index < next.count; ++index)
    {
        x = (index % columns) * size;
        y = (index / columns) * size;
        for (i = 0; i < 4; ++i)
        {
            tile = &next.tiles[i][index];
            tile->x = x;
            tile->y = y;
            tile->w = next.width - x < size ? next.width - x : size;
            tile->h = next.height - y < size ? next.height - y : size;
        }
        tile = &next.tiles[2][index];
        for (i = 0; i < 2; ++i)
        {
            part = SDL_CreateSurfaceFrom(tile->w, tile->h, SDL_PIXELFORMAT_RGBA32,
                       (unsigned char *)source[i]->pixels + y * source[i]->pitch + x * 4, source[i]->pitch);
            if (!part)
                goto failed;
            row = tile_texture(&next.tiles[i][index], part);
            SDL_DestroySurface(part);
            if (!row)
                goto failed;
        }
        delta = SDL_CreateSurface(tile->w, tile->h, SDL_PIXELFORMAT_RGBA32);
        if (!delta)
            goto failed;
        for (row = 0; row < tile->h; ++row)
        {
            a = (unsigned char *)source[0]->pixels + (y + row) * source[0]->pitch + x * 4;
            b = (unsigned char *)source[1]->pixels + (y + row) * source[1]->pitch + x * 4;
            d = (unsigned char *)delta->pixels + row * delta->pitch;
            next.changed += (unsigned long)review_image_diff_row(a, b, d, (size_t)tile->w);
        }
        if (!tile_texture(tile, delta))
            goto failed;
        for (row = 0; row < tile->h; ++row)
            review_image_highlight_row((unsigned char *)delta->pixels + row * delta->pitch, (size_t)tile->w);
        if (!tile_texture(&next.tiles[3][index], delta) ||
            !SDL_SetTextureBlendMode(next.tiles[3][index].texture, SDL_BLENDMODE_BLEND))
            goto failed;
        SDL_DestroySurface(delta);
        delta = NULL;
    }
    SDL_DestroySurface(source[0]);
    SDL_DestroySurface(source[1]);
    clear_images(&images);
    images = next;
    fit = 1;
    return 1;
failed:
    if (!error[0])
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
    SDL_DestroySurface(delta);
    SDL_DestroySurface(source[0]);
    SDL_DestroySurface(source[1]);
    clear_images(&next);
    return 0;
}

static void controls(void)
{
    int i;
    float percent;
    const char *labels[2] = {"Retail", "Ours"};
    for (i = 0; i < 2; ++i)
    {
        igPushID_Int(i);
        igText("%-6s", labels[i]);
        igSameLine(0, -1);
        igSetNextItemWidth(-90);
        igInputTextWithHint("##path", "PNG image or contact sheet", paths[i], sizeof(paths[i]), 0, NULL, NULL);
        igSameLine(0, -1);
        if (igButton("Browse...", vec(0, 0)))
            review_pick_image(paths[i], (int)sizeof(paths[i]));
        igPopID();
    }
    if (igButton("Load images", vec(0, 0)))
        load_images();
    igSameLine(0, -1);
    igBeginDisabled(!images.count);
    if (igButton("Fit width", vec(0, 0)))
        fit = 1;
    igSameLine(0, -1);
    if (igButton("Fit all", vec(0, 0)))
        fit = 2;
    igSameLine(0, -1);
    if (igButton("1:1", vec(0, 0)))
    {
        review_image_zoom(&view, 1, 0, 0);
        fit = 0;
    }
    igSameLine(0, -1);
    percent = (float)(view.zoom * 100);
    igSetNextItemWidth(220);
    if (igSliderFloat("Zoom", &percent, 0.5f, 3200, "%.1f%%", ImGuiSliderFlags_Logarithmic))
    {
        review_image_zoom(&view, percent / 100, 0, 0);
        fit = 0;
    }
    igEndDisabled();
    igTextUnformatted("Wheel: zoom | Drag: pan all three | Double-click: fit width | Hold D: highlight differences", NULL);
    if (images.count)
        igText("%d x %d | %lu changed RGB pixels (%.2f%%) | Diff: absolute RGB difference; black = equal",
               images.width, images.height, images.changed,
               100.0 * images.changed / ((double)images.width * images.height));
    if (error[0])
        igTextWrapped("%s%s", error, images.count ? " (Previous images remain displayed.)" : "");
}

static void draw_pane(Pane *pane, int index, int highlight)
{
    int i;
    float left, top, right, bottom;
    ImageTile *tile;
    ImTextureRef texture;
    ImVec2 end;
    end = vec(pane->origin.x + pane->size.x, pane->origin.y + pane->size.y);
    ImDrawList_PushClipRect(pane->draw, pane->origin, end, 1);
    ImDrawList_AddRectFilled(pane->draw, pane->origin, end, 0xff181513UL, 0, 0);
    ImDrawList_AddCallback(pane->draw, igGetPlatformIO_Nil()->DrawCallback_SetSamplerNearest, NULL, 0);
    memset(&texture, 0, sizeof(texture));
    for (i = 0; i < images.count; ++i)
    {
        tile = &images.tiles[index][i];
        left = pane->origin.x + pane->size.x / 2 + (float)((tile->x - view.x) * view.zoom);
        top = pane->origin.y + pane->size.y / 2 + (float)((tile->y - view.y) * view.zoom);
        right = left + (float)(tile->w * view.zoom);
        bottom = top + (float)(tile->h * view.zoom);
        if (right <= pane->origin.x || bottom <= pane->origin.y || left >= end.x || top >= end.y)
            continue;
        texture._TexID = (ImTextureID)(size_t)tile->texture;
        ImDrawList_AddImage(pane->draw, texture, vec(left, top), vec(right, bottom), vec(0, 0), vec(1, 1), 0xffffffffUL);
        if (highlight && index < 2)
        {
            texture._TexID = (ImTextureID)(size_t)images.tiles[3][i].texture;
            ImDrawList_AddImage(pane->draw, texture, vec(left, top), vec(right, bottom), vec(0, 0), vec(1, 1), 0xffffffffUL);
        }
    }
    ImDrawList_AddCallback(pane->draw, igGetPlatformIO_Nil()->DrawCallback_SetSamplerLinear, NULL, 0);
    ImDrawList_PopClipRect(pane->draw);
}
static void panes(void)
{
    Pane pane[3];
    ImGuiIO *io;
    int i, show;
    double scale;
    const char *titles[3] = {"Retail", "Ours", "Diff"};
    memset(pane, 0, sizeof(pane));
    if (!igBeginTable("Images", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame, vec(0, 0), 0))
        return;
    io = igGetIO_Nil();
    for (i = 0; i < 3; ++i)
    {
        igTableNextColumn();
        igTextUnformatted(titles[i], NULL);
        igPushID_Int(i);
        show = igBeginChild_Str("Canvas", vec(0, 0), 0,
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        pane[i].origin = igGetCursorScreenPos();
        pane[i].size = igGetContentRegionAvail();
        pane[i].draw = igGetWindowDrawList();
        if (pane[i].size.x < 1)
            pane[i].size.x = 1;
        if (pane[i].size.y < 1)
            pane[i].size.y = 1;
        if (!i && images.count && fit)
        {
            scale = pane[i].size.x / images.width;
            if (fit == 2 && pane[i].size.y / images.height < scale)
                scale = pane[i].size.y / images.height;
            review_image_zoom(&view, scale, 0, 0);
            view.x = images.width / 2.0;
            view.y = fit == 2 ? images.height / 2.0 : pane[i].size.y / (2 * view.zoom);
            fit = 0;
        }
        igInvisibleButton("Pan and zoom", pane[i].size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        if (show && images.count)
        {
            if (igIsItemHovered(0) && io->MouseWheel)
                review_image_zoom(&view, view.zoom * pow(1.2, io->MouseWheel),
                                  io->MousePos.x - pane[i].origin.x - pane[i].size.x / 2,
                                  io->MousePos.y - pane[i].origin.y - pane[i].size.y / 2);
            if (igIsItemActive() && (igIsMouseDragging(ImGuiMouseButton_Left, 0) ||
                                    igIsMouseDragging(ImGuiMouseButton_Middle, 0)))
            {
                review_image_pan(&view, io->MouseDelta.x, io->MouseDelta.y);
                igSetMouseCursor(ImGuiMouseCursor_ResizeAll);
            }
            if (igIsItemHovered(0) && igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left))
                fit = 1;
        }
        igEndChild();
        igPopID();
    }
    /* Apply input before drawing any pane so all three use this frame's view. */
    for (i = 0; i < 3; ++i)
        draw_pane(&pane[i], i, !io->WantTextInput && igIsKeyDown_Nil(ImGuiKey_D));
    igEndTable();
}

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Event event;
    SDL_Surface *screenshot;
    int running, width, height, smoke, status;
    if (argc != 1 && argc != 3 && !(argc == 5 && !strcmp(argv[3], "--smoke")))
    {
        fprintf(stderr, "Usage: image-compare [retail.png ours.png [--smoke screenshot.png]]\n");
        return 2;
    }
    smoke = argc == 5 ? 3 : 0;
    status = 0;
    if (!SDL_Init(SDL_INIT_VIDEO) || !SDL_CreateWindowAndRenderer("3DMM Image Compare", 1740, 1000,
            review_window_flags(smoke), &window, &renderer))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    igCreateContext(NULL);
    igGetIO_Nil()->IniFilename = NULL;
    igStyleColorsDark(NULL);
    if (!review_backend_init(window, renderer))
    {
        fprintf(stderr, "ImGui backend initialization failed\n");
        igDestroyContext(NULL);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (argc >= 3)
    {
        if (strlen(argv[1]) >= sizeof(paths[0]) || strlen(argv[2]) >= sizeof(paths[1]))
        {
            SDL_strlcpy(error, "Input path is too long", sizeof(error));
            status = 1;
        }
        else
        {
            strcpy(paths[0], argv[1]);
            strcpy(paths[1], argv[2]);
            if (!load_images())
                status = 1;
        }
        if (status)
            fprintf(stderr, "%s\n", error);
    }
    running = !smoke || !status;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            review_backend_event(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = 0;
        }
        review_backend_frame();
        igNewFrame();
        SDL_GetWindowSize(window, &width, &height);
        igSetNextWindowPos(vec(0, 0), 0, vec(0, 0));
        igSetNextWindowSize(vec((float)width, (float)height), 0);
        igBegin("Image comparison", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        controls();
        panes();
        igEnd();
        igRender();
        SDL_SetRenderDrawColor(renderer, 20, 23, 29, 255);
        SDL_RenderClear(renderer);
        review_backend_render(renderer);
        if (smoke == 1)
        {
            screenshot = SDL_RenderReadPixels(renderer, NULL);
            if (!screenshot || !SDL_SavePNG(screenshot, argv[4]))
            {
                fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
                status = 1;
            }
            SDL_DestroySurface(screenshot);
        }
        SDL_RenderPresent(renderer);
        if (smoke && --smoke == 0)
            running = 0;
        SDL_Delay(10);
    }
    clear_images(&images);
    review_backend_quit();
    igDestroyContext(NULL);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
