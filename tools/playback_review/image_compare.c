/* Movie comparison viewer. Application code uses C89. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include "backend.h"
#include "image_view.h"
#include "image_decode.h"
#ifndef REVIEW_SOURCE_DIR
#define REVIEW_SOURCE_DIR "."
#endif
#define FRAME_WIDTH 544
#define FRAME_HEIGHT 306
#define CELL_WIDTH 552
#define CELL_HEIGHT 336
#define CACHE_BUDGET (256u * 1024u * 1024u)

typedef struct Frame
{
    int scene;
    long number, raw;
    SDL_Texture *texture[4];
    unsigned long used;
    size_t bytes;
    int loaded, backend, tier, revision, retail_count, ours_count;
    int score_state[4];
    size_t changed_pixels[4];
} Frame;
typedef struct Pane
{
    ImDrawList *draw;
    ImVec2 origin, size;
} Pane;
static SDL_Renderer *renderer;
static SDL_Process *process;
static ReviewDecoder *decoder;
static unsigned long decode_token;
static Frame *frames, *atlases;
static int atlas_count;
static double zoom_target = 1, zoom_anchor_x, zoom_anchor_y;
static Uint64 last_navigation, previous_draw;
static int total, ready[5], candidate, columns = 12, fit = 1, closing;
static int backend_count = 5;
static int *visible, visible_count, visible_dirty = 1, uncompared;
static int hide_matches = 1;
static float match_threshold = 100;
static int benchmark_highlight;
static int sheet_width, sheet_height;
static unsigned long generation;
static size_t cache_bytes;
static ReviewImageView view = {0, 0, 1};
static char movie[4096], session[4096], message[2048], state[32], image_error[512];
static const char *names[4] = {"BRender CPU C", "BRender compute", "SDL GPU", "BRender CPU ASM x86"};
static const char *backends[5] = {"retail", "brender", "brender-compute", "sdl-gpu", "brender-asm"};

static ImVec2 vec(float x, float y)
{
    ImVec2 v;
    v.x = x;
    v.y = y;
    return v;
}
static void clear_frame(Frame *frame)
{
    int i;
    for (i = 0; i < 4; ++i)
    {
        SDL_DestroyTexture(frame->texture[i]);
        frame->texture[i] = NULL;
    }
    cache_bytes -= frame->bytes;
    frame->bytes = 0;
    frame->loaded = 0;
}
static void clear_frames(void)
{
    int i;
    for (i = 0; i < total; ++i)
        clear_frame(&frames[i]);
    ++decode_token;
    SDL_free(frames);
    SDL_free(visible);
    visible = NULL;
    visible_count = 0;
    visible_dirty = 1;
    for (i = 0; i < atlas_count; ++i)
        clear_frame(&atlases[i]);
    SDL_free(atlases);
    atlases = NULL;
    atlas_count = 0;
    frames = NULL;
    total = 0;
    memset(ready, 0, sizeof(ready));
}
static void cancel_capture(void)
{
    char path[8192];
    SDL_IOStream *file;
    if (!process)
        return;
    SDL_snprintf(path, sizeof(path), "%s/cancel", session);
    file = SDL_IOFromFile(path, "wb");
    if (file)
        SDL_CloseIO(file);
    SDL_strlcpy(message, "Stopping capture...", sizeof(message));
}
static void start_capture(void)
{
    char script[8192], parent[8192];
    const char *args[9];
    SDL_PropertiesID properties;
    SDL_PathInfo info;
    int attempt;
    if (process || !movie[0])
        return;
    SDL_snprintf(parent, sizeof(parent), "%s/tmp/movie-compare", REVIEW_SOURCE_DIR);
    if (!SDL_CreateDirectory(parent))
    {
        SDL_strlcpy(message, SDL_GetError(), sizeof(message));
        return;
    }
    for (attempt = 0; attempt < 100; ++attempt)
    {
        SDL_snprintf(session, sizeof(session), "%s/run-%lu-%d", parent,
                     (unsigned long)SDL_GetPerformanceCounter(), attempt);
        if (!SDL_GetPathInfo(session, &info) && SDL_CreateDirectory(session))
            break;
    }
    if (attempt == 100)
    {
        SDL_strlcpy(message, "Cannot reserve a capture directory", sizeof(message));
        return;
    }
    SDL_snprintf(script, sizeof(script), "%s/tools/movie_compare_capture.py", REVIEW_SOURCE_DIR);
    args[0] = "python";
    args[1] = "-B";
    args[2] = script;
    args[3] = "--movie";
    args[4] = movie;
    args[5] = "--output";
    args[6] = session;
    args[7] = NULL;
    properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, 1);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, 1);
    process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process)
    {
        SDL_strlcpy(message, SDL_GetError(), sizeof(message));
        return;
    }
    clear_frames();
    candidate = 0;
    fit = 1;
    state[0] = image_error[0] = 0;
    SDL_strlcpy(message, "Starting background capture...", sizeof(message));
}
static void read_status(void)
{
    char path[8192], next_state[32], next_message[2048], count_line[256];
    FILE *file;
    Frame *next;
    int count, counts[5], i, status, parsed;
    int *map;
    size_t bytes;
    if (!session[0])
        return;
    SDL_snprintf(path, sizeof(path), "%s/status.txt", session);
    file = fopen(path, "rb");
    if (file)
    {
        next_state[0] = next_message[0] = 0;
        memset(counts, 0, sizeof(counts));
        parsed = 0;
        if (fscanf(file, "%31s%d\n", next_state, &count) == 2 && fgets(count_line, sizeof(count_line), file))
            parsed = sscanf(count_line, "%d%d%d%d%d", &counts[0], &counts[1], &counts[2], &counts[3], &counts[4]);
        if ((parsed == 4 || parsed == 5) && count >= 0 && count <= 250000 &&
            fgets(next_message, sizeof(next_message), file))
        {
            backend_count = parsed;
            if (!frames && count > 0)
            {
                FILE *manifest;
                SDL_snprintf(path, sizeof(path), "%s/frames.tsv", session);
                manifest = fopen(path, "rb");
                next = (Frame *)SDL_calloc((size_t)count, sizeof(Frame));
                if (manifest && next)
                {
                    for (i = 0; i < count; ++i)
                        if (fscanf(manifest, "%d%ld%ld", &next[i].scene, &next[i].number, &next[i].raw) != 3)
                            break;
                    map = i == count ? (int *)SDL_malloc((size_t)count * sizeof(int)) : NULL;
                    if (map)
                    {
                        visible = map;
                        visible_dirty = 1;
                        frames = next;
                        total = count;
                        atlas_count = (total + 63) / 64;
                        atlases = (Frame *)SDL_calloc((size_t)atlas_count, sizeof(Frame));
                        if (!atlases)
                            atlas_count = 0;
                        next = NULL;
                    }
                }
                if (manifest)
                    fclose(manifest);
                SDL_free(next);
            }
            for (i = 0; i < 5; ++i)
                ready[i] = counts[i] >= 0 && counts[i] <= total ? counts[i] : 0;
            SDL_strlcpy(state, next_state, sizeof(state));
            next_message[strcspn(next_message, "\r\n")] = 0;
            SDL_strlcpy(message, next_message, sizeof(message));
        }
        fclose(file);
    }
    if (process && SDL_WaitProcess(process, 0, &status))
    {
        if (strcmp(state, "complete") && strcmp(state, "partial") && strcmp(state, "cancelled") && strcmp(state, "error"))
        {
            bytes = SDL_ReadIO(SDL_GetProcessOutput(process), next_message, sizeof(next_message) - 1);
            next_message[bytes] = 0;
            SDL_snprintf(message, sizeof(message), "Capture process exited (%d). %s", status, next_message);
        }
        SDL_DestroyProcess(process);
        process = NULL;
    }
}
static void upload_decoded(void)
{
    ReviewDecodeResult result;
    Frame *frame;
    SDL_Surface *surface;
    int i, index, n;
    if (!review_decoder_poll(decoder, &result))
        return;
    if (result.token == decode_token && result.index < (result.atlas ? atlas_count : total) && result.backend == candidate)
    {
        n = result.atlas ? 64 : 1;
        for (i = 0; i < n; ++i)
        {
            index = result.atlas ? result.index * 64 + i : result.index;
            if (index < total && result.score_state[i])
            {
                frames[index].score_state[candidate] = result.score_state[i];
                frames[index].changed_pixels[candidate] = result.changed_pixels[i];
                visible_dirty = 1;
            }
        }
        if (result.scores_only)
        {
            if (result.error[0]) SDL_strlcpy(image_error, result.error, sizeof(image_error));
            review_decode_free(&result);
            return;
        }
        frame = result.atlas ? &atlases[result.index] : &frames[result.index];
        clear_frame(frame);
        frame->backend = result.backend;
        frame->tier = result.tier;
        frame->revision = result.revision;
        frame->retail_count = result.retail_count;
        frame->ours_count = result.ours_count;
        if (!result.error[0])
            for (i = 0; i < 4; ++i)
            {
                surface = result.images[i];
                if (!surface)
                    continue;
                frame->texture[i] = SDL_CreateTextureFromSurface(renderer, surface);
                if (!frame->texture[i])
                {
                    SDL_strlcpy(result.error, SDL_GetError(), sizeof(result.error));
                    break;
                }
                SDL_SetTextureScaleMode(frame->texture[i], SDL_SCALEMODE_NEAREST);
                SDL_SetTextureBlendMode(frame->texture[i], i == 3 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                frame->bytes += (size_t)surface->w * surface->h * 4;
                cache_bytes += (size_t)surface->w * surface->h * 4;
            }
        if (result.error[0])
        {
            SDL_strlcpy(image_error, result.error, sizeof(image_error));
            clear_frame(frame);
            frame->loaded = -1;
        }
        else
            frame->loaded = result.wanted;
    }
    review_decode_free(&result);
}
static Frame *load_frame(int index, int tier)
{
    Frame *frame;
    int wanted;
    frame = &frames[index];
    wanted = ready[0] > index ? 1 : 0;
    if (wanted && ready[candidate + 1] > index)
        wanted = 2;
    frame->used = generation;
    if (frame->backend != candidate)
    {
        clear_frame(frame);
        frame->backend = candidate;
        frame->tier = tier;
    }
    if (wanted && frame->loaded >= 0 && frame->loaded != wanted)
        review_decoder_request(decoder, session, index, candidate, tier, wanted, decode_token);
    return frame;
}
static Frame *load_atlas(int page)
{
    Frame *atlas;
    int r, o, end, revision;
    if (!atlas_count)
        return NULL;
    atlas = &atlases[page];
    atlas->used = generation;
    r = ready[0];
    o = ready[candidate + 1];
    if (!strcmp(state, "rendering"))
    {
        if (r != total) r -= r % 16;
        if (o != total) o -= o % 16;
    }
    end = (page + 1) * 64;
    if (r > end) r = end;
    if (o > end) o = end;
    if (r < page * 64) r = page * 64;
    if (o < page * 64) o = page * 64;
    revision = (r - page * 64) * 65 + o - page * 64;
    if (revision && (!atlas->loaded || atlas->revision != revision))
        review_decoder_request_atlas(decoder, session, page, candidate, r, o, total, revision, decode_token);
    return atlas;
}
static void trim_cache(void)
{
    int i;
    /* Never release a texture referenced by the current frame's draw data. */
    if (cache_bytes > CACHE_BUDGET)
        for (i = 0; i < total && cache_bytes > CACHE_BUDGET; ++i)
            if (frames[i].loaded && frames[i].used != generation)
                clear_frame(&frames[i]);
    if (cache_bytes > CACHE_BUDGET)
        for (i = 0; i < atlas_count && cache_bytes > CACHE_BUDGET; ++i)
            if (atlases[i].loaded && atlases[i].used != generation)
                clear_frame(&atlases[i]);
}
static void choose_renderer(int next)
{
    int i;
    if (candidate == next)
        return;
    candidate = next;
    visible_dirty = 1;
    ++decode_token;
    for (i = 0; i < total; ++i)
        clear_frame(&frames[i]);
    for (i = 0; i < atlas_count; ++i)
        clear_frame(&atlases[i]);
    image_error[0] = 0;
}
static void rebuild_visible(void)
{
    int i, old_slot, old_index, anchor;
    if (!visible_dirty || !visible) return;
    old_slot = (int)((view.y > 8 ? view.y - 8 : 0) / CELL_HEIGHT) * columns +
        (int)((view.x > 8 ? view.x - 8 : 0) / CELL_WIDTH);
    if (old_slot >= visible_count) old_slot = visible_count - 1;
    old_index = old_slot >= 0 ? visible[old_slot] : -1;
    visible_count = uncompared = 0;
    for (i = 0; i < total; ++i)
    {
        if (frames[i].score_state[candidate] != 1) ++uncompared;
        if (!hide_matches || !review_image_hide_match(frames[i].score_state[candidate] == 1,
            frames[i].changed_pixels[candidate], FRAME_WIDTH * FRAME_HEIGHT, match_threshold))
            visible[visible_count++] = i;
    }
    if (old_index >= 0 && visible_count)
    {
        for (anchor = 0; anchor < visible_count - 1 && visible[anchor] < old_index; ++anchor) {}
        view.y += (anchor / columns - old_slot / columns) * CELL_HEIGHT;
        view.x += (anchor % columns - old_slot % columns) * CELL_WIDTH;
    }
    visible_dirty = 0;
}
static void request_scores(void)
{
    int i, available;
    if (!hide_matches) return;
    available = ready[0] < ready[candidate + 1] ? ready[0] : ready[candidate + 1];
    for (i = 0; i < available; ++i)
        if (!frames[i].score_state[candidate])
        {
            review_decoder_request_scores(decoder, session, i / 64, candidate, ready[0],
                ready[candidate + 1], total, decode_token);
            return;
        }
}
static void controls(void)
{
    int i, next, match_step;
    char match_format[32];
    float percent;
    igBeginDisabled(process != NULL);
    igSetNextItemWidth(-145);
    if (igInputTextWithHint("##movie", "Choose a .3mm movie or ZIP", movie, sizeof(movie),
                            ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL))
        start_capture();
    igSameLine(0, -1);
    if (igButton("Open movie...", vec(0, 0)) && review_pick_movie(movie, (int)sizeof(movie)))
        start_capture();
    igEndDisabled();
    if (igButton(process ? "Cancel capture" : "Render movie", vec(0, 0)))
    {
        if (process)
            cancel_capture();
        else
            start_capture();
    }
    for (i = 0; i < 4; ++i)
    {
        igSameLine(0, -1);
        if (igRadioButton_Bool(names[i], candidate == i))
            choose_renderer(i);
    }
    if (!igGetIO_Nil()->WantTextInput && igIsKeyPressed_Bool(ImGuiKey_Tab, 0))
        choose_renderer((candidate + 1) % 4);
    igSameLine(0, -1);
    next = columns;
    igSetNextItemWidth(100);
    if (igInputInt("Columns", &next, 1, 1, 0))
    {
        columns = next < 1 ? 1 : next > 24 ? 24 : next;
        fit = 1;
    }
    if (total)
    {
        percent = (float)(ready[0] + ready[1] + ready[2] + ready[3] + ready[4]) / (backend_count * total);
        igProgressBar(percent, vec(-1, 0), message);
        igText("%d frames | Retail %d | CPU C %d | Compute %d | SDL GPU %d | CPU ASM %d", total, ready[0], ready[1], ready[2], ready[3], ready[4]);
    }
    else
        igTextWrapped("%s", message[0] ? message : "Open a movie to capture all frames in the background.");
    if (igButton("Fit width", vec(0, 0)))
        fit = 1;
    igSameLine(0, -1);
    if (igButton("Fit all", vec(0, 0)))
        fit = 2;
    igSameLine(0, -1);
    if (igButton("1:1", vec(0, 0)))
    {
        review_image_zoom(&view, 1, 0, 0);
        zoom_target = view.zoom;
        fit = 0;
    }
    igSameLine(0, -1);
    percent = (float)(view.zoom * 100);
    igSetNextItemWidth(220);
    if (igSliderFloat("Zoom", &percent, 0.5f, 3200, "%.1f%%", ImGuiSliderFlags_Logarithmic))
    {
        review_image_zoom(&view, percent / 100, 0, 0);
        zoom_target = view.zoom;
        fit = 0;
    }
    if (igCheckboxFlags_IntPtr("Hide matching frames", &hide_matches, 1)) visible_dirty = 1;
    igSameLine(0, -1);
    igBeginDisabled(!hide_matches);
    igSetNextItemWidth(220);
    match_step = (int)(match_threshold * 2 + 0.5f);
    SDL_snprintf(match_format, sizeof(match_format), "%.1f%%%%", match_step * 0.5f);
    if (igSliderInt("Match at least", &match_step, 180, 200, match_format,
                    ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
    {
        match_threshold = match_step * 0.5f;
        visible_dirty = 1;
    }
    igEndDisabled();
    rebuild_visible();
    igSameLine(0, -1);
    igText("Showing %d / %d | Uncompared %d", visible_count, total, uncompared);
    if (igIsItemHovered(0))
        igSetTooltip("Match = percentage of native pixels with identical RGB. Uncompared frames stay visible. 100%% hides only perfect matches.");
    igTextUnformatted("Wheel: zoom | Drag: pan all | Hold D: highlight | Tab: renderer | Labels excluded from diff", NULL);
    if (image_error[0])
        igTextWrapped("%s", image_error);
}
static void draw_frame(Pane *pane, Frame *frame, Frame *picture, int index, int is_atlas, int slot, float x, float y, int pass)
{
    ImTextureRef texture;
    char label[80];
    ImVec2 min, max, uv_min, uv_max;
    int available;
    min = vec(x, y);
    max = vec(x + (float)(FRAME_WIDTH * view.zoom), y + (float)(FRAME_HEIGHT * view.zoom));
    if (pass == 2)
    {
        SDL_snprintf(label, sizeof(label), "Scene %02d / Frame %03ld", frame->scene, frame->number);
        ImDrawList_AddText_FontPtr(pane->draw, NULL, (float)(16 * view.zoom),
            vec(x, max.y + (float)(3 * view.zoom)), 0xffe9ded7UL, label, NULL, 0, NULL);
        return;
    }
    if (pass == 1 && slot == 2) return;
    uv_min = is_atlas ? vec((index % 8) / 8.0f, ((index % 64) / 8) / 8.0f) : vec(0, 0);
    uv_max = is_atlas ? vec(uv_min.x + 0.125f, uv_min.y + 0.125f) : vec(1, 1);
    available = picture && (!is_atlas || (index < picture->retail_count && (slot == 0 || index < picture->ours_count)));
    memset(&texture, 0, sizeof(texture));
    if (available && picture->texture[pass == 1 ? 3 : slot])
    {
        texture._TexID = (ImTextureID)(size_t)picture->texture[pass == 1 ? 3 : slot];
        ImDrawList_AddImage(pane->draw, texture, min, max, uv_min, uv_max, 0xffffffffUL);
    }
    else if (!pass)
        ImDrawList_AddRectFilled(pane->draw, min, max, 0xff282420UL, 0, 0);
}
static void panes(void)
{
    Pane pane[3];
    ImGuiIO *io;
    Frame *frame, *overview, *picture;
    const char *titles[3] = {"Retail", "Ours", "Diff"};
    int i, index, cell, row, first, last, count_rows, tier, highlight, hovered, pass;
    double scale, top, bottom, elapsed, next_zoom, cursor_x, cursor_y;
    int is_atlas;
    Uint64 now;
    float x, y;
    if (!igBeginTable("Images", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame, vec(0, 0), 0))
        return;
    io = igGetIO_Nil();
    hovered = -1;
    sheet_width = columns * CELL_WIDTH + 8;
    count_rows = visible_count ? (visible_count + columns - 1) / columns : 1;
    sheet_height = count_rows * CELL_HEIGHT + 8;
    for (i = 0; i < 3; ++i)
    {
        igTableNextColumn();
        igTextUnformatted(titles[i], NULL);
        if (i == 1)
        {
            igSameLine(0, -1);
            igText("- %s", names[candidate]);
        }
        igPushID_Int(i);
        igBeginChild_Str("Canvas", vec(0, 0), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        pane[i].origin = igGetCursorScreenPos();
        pane[i].size = igGetContentRegionAvail();
        pane[i].draw = igGetWindowDrawList();
        if (pane[i].size.x < 1)
            pane[i].size.x = 1;
        if (pane[i].size.y < 1)
            pane[i].size.y = 1;
        if (!i && total && fit)
        {
            scale = pane[i].size.x / sheet_width;
            if (fit == 2 && pane[i].size.y / sheet_height < scale)
                scale = pane[i].size.y / sheet_height;
            review_image_zoom(&view, scale, 0, 0);
            zoom_target = view.zoom;
            view.x = sheet_width / 2.0;
            view.y = fit == 2 ? sheet_height / 2.0 : pane[i].size.y / (2 * view.zoom);
            fit = 0;
        }
        igInvisibleButton("Pan and zoom", pane[i].size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        if (igIsItemHovered(0)) hovered = i;
        if (total && igIsItemHovered(0) && io->MouseWheel)
        {
            zoom_target *= pow(1.2, io->MouseWheel);
            if (zoom_target < 0.005) zoom_target = 0.005;
            if (zoom_target > 32) zoom_target = 32;
            zoom_anchor_x = io->MousePos.x - pane[i].origin.x - pane[i].size.x / 2;
            zoom_anchor_y = io->MousePos.y - pane[i].origin.y - pane[i].size.y / 2;
            last_navigation = SDL_GetTicks();
        }
        if (igIsItemActive() && (igIsMouseDragging(ImGuiMouseButton_Left, 0) || igIsMouseDragging(ImGuiMouseButton_Middle, 0)))
        {
            review_image_pan(&view, io->MouseDelta.x, io->MouseDelta.y);
            zoom_target = view.zoom;
            last_navigation = SDL_GetTicks();
            igSetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (igIsItemHovered(0) && igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left))
            fit = 1;
        igEndChild();
        igPopID();
    }
    now = SDL_GetTicks();
    elapsed = previous_draw ? (now - previous_draw) / 1000.0 : 0.016;
    if (elapsed > 0.05) elapsed = 0.05;
    previous_draw = now;
    next_zoom = view.zoom + (zoom_target - view.zoom) * (1 - exp(-24 * elapsed));
    if (fabs(next_zoom - zoom_target) < zoom_target * 0.0001) next_zoom = zoom_target;
    review_image_zoom(&view, next_zoom, zoom_anchor_x, zoom_anchor_y);
    tier = 0;
    highlight = benchmark_highlight || (!io->WantTextInput && igIsKeyDown_Nil(ImGuiKey_D));
    for (i = 0; i < 3; ++i)
    {
        ImDrawList_PushClipRect(pane[i].draw, pane[i].origin,
            vec(pane[i].origin.x + pane[i].size.x, pane[i].origin.y + pane[i].size.y), 1);
        ImDrawList_AddCallback(pane[i].draw, igGetPlatformIO_Nil()->DrawCallback_SetSamplerNearest, NULL, 0);
    }
    top = view.y - pane[0].size.y / (2 * view.zoom);
    bottom = view.y + pane[0].size.y / (2 * view.zoom);
    first = top <= 8 ? 0 : (int)((top - 8) / CELL_HEIGHT);
    last = bottom <= 0 ? -1 : (int)(bottom / CELL_HEIGHT);
    if (last >= count_rows)
        last = count_rows - 1;
    /* Keep image pages, highlight pages and font glyphs contiguous in each
     * pane. Interleaving labels forced two texture switches per thumbnail. */
    for (pass = 0; pass < 3; ++pass)
    {
        if (pass == 1 && !highlight) continue;
        for (row = first; row <= last; ++row)
            for (cell = row * columns; cell < (row + 1) * columns && cell < visible_count; ++cell)
            {
                index = visible[cell];
                x = (float)((8 + cell % columns * CELL_WIDTH - view.x) * view.zoom);
                if (x + FRAME_WIDTH * view.zoom < -pane[0].size.x / 2 || x > pane[0].size.x / 2)
                    continue;
                frame = &frames[index];
                overview = atlas_count ? &atlases[index / 64] : NULL;
                if (!pass)
                {
                    overview = load_atlas(index / 64);
                    if (view.zoom >= 0.5 && now - last_navigation >= 150)
                        frame = load_frame(index, tier);
                }
                is_atlas = !(view.zoom >= 0.5 && frame->texture[0] && frame->backend == candidate);
                picture = is_atlas ? overview : frame;
                for (i = 0; i < 3; ++i)
                {
                    y = pane[i].origin.y + pane[i].size.y / 2 + (float)((8 + row * CELL_HEIGHT - view.y) * view.zoom);
                    draw_frame(&pane[i], frame, picture, index, is_atlas, i, pane[i].origin.x + pane[i].size.x / 2 + x, y, pass);
                }
            }
    }
    /* Warm nearby sheet tiles while idle so zooming out reveals cached images,
     * rather than initiating a decode for each newly visible frame. */
    for (i = 0; i < atlas_count && i < 24 && cache_bytes < CACHE_BUDGET * 3 / 4; ++i)
    {
        unsigned long used;
        used = atlases[i].used;
        load_atlas(i);
        atlases[i].used = used;
    }
    request_scores();
    if (hovered >= 0)
    {
        cursor_x = floor(view.x + (io->MousePos.x - pane[hovered].origin.x - pane[hovered].size.x / 2) / view.zoom) + 0.5;
        cursor_y = floor(view.y + (io->MousePos.y - pane[hovered].origin.y - pane[hovered].size.y / 2) / view.zoom) + 0.5;
        for (i = 0; i < 3; ++i)
        {
            x = pane[i].origin.x + pane[i].size.x / 2 + (float)((cursor_x - view.x) * view.zoom);
            y = pane[i].origin.y + pane[i].size.y / 2 + (float)((cursor_y - view.y) * view.zoom);
            ImDrawList_AddLine(pane[i].draw, vec(x - 10, y), vec(x + 10, y), 0xff000000UL, 3, 0);
            ImDrawList_AddLine(pane[i].draw, vec(x, y - 10), vec(x, y + 10), 0xff000000UL, 3, 0);
            ImDrawList_AddLine(pane[i].draw, vec(x - 10, y), vec(x + 10, y), 0xff00ffffUL, 1, 0);
            ImDrawList_AddLine(pane[i].draw, vec(x, y - 10), vec(x, y + 10), 0xff00ffffUL, 1, 0);
        }
    }
    for (i = 0; i < 3; ++i)
    {
        ImDrawList_AddCallback(pane[i].draw, igGetPlatformIO_Nil()->DrawCallback_SetSamplerLinear, NULL, 0);
        ImDrawList_PopClipRect(pane[i].draw);
    }
    igEndTable();
}
int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Event event;
    SDL_Surface *screenshot;
    const char *smoke_path, *benchmark_path;
    int i, j, smoke, running, width, height, status, benchmark_count, warmed, draw_commands, vsync;
    double benchmark_ms, benchmark_draws, frame_ms;
    Uint64 next_poll, frame_start, benchmark_deadline;
    ImDrawData *draw_data;
    ImDrawList *draw_list;
    FILE *benchmark_file;
    smoke_path = NULL;
    benchmark_path = NULL;
    benchmark_count = 0;
    benchmark_ms = benchmark_draws = 0;
    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--session") && i + 1 < argc)
            SDL_strlcpy(session, argv[++i], sizeof(session));
        else if (!strcmp(argv[i], "--smoke") && i + 1 < argc)
            smoke_path = argv[++i];
        else if (!strcmp(argv[i], "--benchmark") && i + 1 < argc)
            benchmark_path = argv[++i];
        else if (!strcmp(argv[i], "--benchmark-highlight"))
            benchmark_highlight = 1;
        else if (argv[i][0] != '-' && !movie[0])
            SDL_strlcpy(movie, argv[i], sizeof(movie));
        else
        {
            fprintf(stderr, "Usage: image-compare [movie.3mm|movie.zip] [--session capture-directory] [--smoke screenshot.png] [--benchmark timing.json] [--benchmark-highlight]\n");
            return 2;
        }
    }
    if (benchmark_path && (!session[0] || smoke_path))
    {
        fprintf(stderr, "The overview benchmark requires --session and cannot be combined with --smoke.\n");
        return 2;
    }
    smoke = smoke_path ? 3 : 0;
    if (benchmark_path) { fit = 1; hide_matches = 0; }
    if (!SDL_Init(SDL_INIT_VIDEO) || !SDL_CreateWindowAndRenderer("3DMM Movie Compare", 1740, 1000,
            review_window_flags(smoke || benchmark_path != NULL), &window, &renderer))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    vsync = SDL_SetRenderVSync(renderer, benchmark_path ? 0 : 1);
    igCreateContext(NULL);
    igGetIO_Nil()->IniFilename = NULL;
    igStyleColorsDark(NULL);
    if (!review_backend_init(window, renderer))
        return 1;
    decoder = review_decoder_create();
    if (!decoder)
    {
        fprintf(stderr, "Cannot start image decoder: %s\n", SDL_GetError());
        return 1;
    }
    if (session[0])
        read_status();
    else if (movie[0])
        start_capture();
    status = 0;
    running = 1;
    next_poll = 0;
    benchmark_deadline = SDL_GetTicks() + 120000;
    while (running)
    {
        frame_start = SDL_GetPerformanceCounter();
        ++generation;
        while (SDL_PollEvent(&event))
        {
            review_backend_event(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                closing = 1;
                cancel_capture();
            }
        }
        if (SDL_GetTicks() >= next_poll)
        {
            read_status();
            next_poll = SDL_GetTicks() + 150;
        }
        if (closing && !process)
            running = 0;
        upload_decoded();
        review_backend_frame();
        igNewFrame();
        SDL_GetWindowSize(window, &width, &height);
        igSetNextWindowPos(vec(0, 0), 0, vec(0, 0));
        igSetNextWindowSize(vec((float)width, (float)height), 0);
        igBegin("Movie comparison", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
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
            if (!screenshot || !SDL_SavePNG(screenshot, smoke_path))
            {
                fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
                status = 1;
            }
            SDL_DestroySurface(screenshot);
        }
        SDL_RenderPresent(renderer);
        trim_cache();
        if (benchmark_path)
        {
            warmed = total > 0 && generation > 60;
            for (i = 0; i < atlas_count; ++i)
                if (atlases[i].loaded <= 0) warmed = 0;
            if (warmed)
            {
                draw_commands = 0;
                draw_data = igGetDrawData();
                for (i = 0; i < draw_data->CmdLists.Size; ++i)
                {
                    draw_list = draw_data->CmdLists.Data[i];
                    for (j = 0; j < draw_list->CmdBuffer.Size; ++j)
                        if (!draw_list->CmdBuffer.Data[j].UserCallback && draw_list->CmdBuffer.Data[j].ElemCount)
                            ++draw_commands;
                }
                benchmark_ms += (SDL_GetPerformanceCounter() - frame_start) * 1000.0 / SDL_GetPerformanceFrequency();
                benchmark_draws += draw_commands;
                if (++benchmark_count == 60)
                {
                    benchmark_file = fopen(benchmark_path, "wb");
                    if (!benchmark_file) status = 1;
                    else
                    {
                        fprintf(benchmark_file, "{\"frames\":%d,\"columns\":%d,\"highlight\":%d,\"samples\":%d,\"mean_frame_ms\":%.3f,\"mean_imgui_draw_commands\":%.1f}\n",
                            total, columns, benchmark_highlight, benchmark_count, benchmark_ms / benchmark_count, benchmark_draws / benchmark_count);
                        fclose(benchmark_file);
                    }
                    running = 0;
                }
            }
            if (SDL_GetTicks() >= benchmark_deadline)
            {
                fprintf(stderr, "Overview benchmark timed out waiting for cached frame pages.\n");
                running = 0;
                status = 1;
            }
        }
        if (smoke && generation > (unsigned long)((atlas_count < 120 ? atlas_count : 120) + 20) && --smoke == 0)
        {
            closing = 1;
            cancel_capture();
        }
        if (!benchmark_path && !vsync)
        {
            frame_ms = (SDL_GetPerformanceCounter() - frame_start) * 1000.0 / SDL_GetPerformanceFrequency();
            if (frame_ms < 1000.0 / 60)
                SDL_DelayPrecise((Uint64)((1000.0 / 60 - frame_ms) * 1000000));
        }
    }
    review_decoder_destroy(decoder);
    clear_frames();
    review_backend_quit();
    igDestroyContext(NULL);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
