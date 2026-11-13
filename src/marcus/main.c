#include "player.h"
#include "ui.h"
#include "settings.h"
#include "camera_guide.h"
#include "movie_browser.h"
#include "scene_organiser.h"
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#define BG 0x1c1e22UL
#define PANEL 0x25282dUL
#define LINE 0x363a40UL
#define TEXT 0xdde0e5UL
#define MUTED 0x969da8UL
#define ACCENT 0x78cbc4UL
#define ROW_HEIGHT 28
#define OBJECT_LIST_TOP 126
#define OBJECT_LIST_FOOTER 32
#define FRAME_NAV_X 192
#define SCENE_NAV_X FRAME_NAV_X
#define FRAME_NAV_Y 4
#define SCENE_NAV_Y 26
#define PLAY_X 332
#define PLAY_Y 2
#define LOOP_X 384
#define NAV_STEP 32
#define MOVIE_CONTROLS_HEIGHT 140
#define MOVIE_IDLE_MS 2500
#define RESOLUTION_COLUMNS 3
#define RESOLUTION_GRID_HEIGHT (((MARCUS_RESOLUTION_COUNT + RESOLUTION_COLUMNS - 1) / RESOLUTION_COLUMNS) * 40)
#define RENDER_SETTINGS_WIDTH 1064
#define RENDER_SETTINGS_HEIGHT 620
#define SETTINGS_HEIGHT 764
#define DEBUG_HISTORY 180
#define VIEW_STRIP_WIDTH 20
#define TIMELINE_HANDLE_HEIGHT 8
#define MOVIE_SCRUBBER_HEIGHT 32
#define TIMELINE_MIN_HEIGHT 168
#define VIEWPORT_MIN_BOTTOM 320
#define SHEET_OBJECTS 0
#define SHEET_SOUNDS 1
#define SHEET_BOTH 2
#define SHEET_OBJECT_ROW 1
#define SHEET_SOUND_ROW 2
#define SHEET_OBJECT_HEADER 3
#define SHEET_SOUND_HEADER 4
#define FLY_FORWARD 1
#define FLY_BACK 2
#define FLY_LEFT 4
#define FLY_RIGHT 8
#define FLY_UP 16
#define FLY_DOWN 32
#define FLY_FAST 64
#define FLY_FAST_RIGHT 128

#ifndef MARCUS_SOURCE_DIR
#define MARCUS_SOURCE_DIR "."
#endif

typedef struct DialogRequest
{
    Uint32 event_type;
    int kind;
    char *path;
} DialogRequest;

typedef struct CameraPerformance
{
    double render_ms[DEBUG_HISTORY], fps[DEBUG_HISTORY];
    unsigned char rendered[DEBUG_HISTORY];
    double last_ms, pending_ms;
    int pending_rendered;
} CameraPerformance;

typedef struct App
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *picture;
    SDL_Texture *movie_picture;
    SDL_Cursor *split_cursor;
    SDL_Cursor *timeline_cursor;
    MarcusUI ui;
    MarcusPlayer *player;
    MarcusBrowser *browser;
    MarcusSceneOrganiser *scene_organiser;
    MarcusMovieInput *movie_input, *zip_input;
    int movie_entry, zip_selected, zip_scroll;
    char zip_path[1024];
    MarcusInfo info;
    MarcusPlaybackState playback;
    MarcusMovieTime movie_time;
    int movie_time_available;
    int movie_time_status;
    MarcusCamera camera, flight;
    MarcusImage work_image, movie_image; /* Retained camera metadata; no borrowed pixels. */
    MarcusProjection projection, scene_camera;
    MarcusCameraGuide camera_guide;
    MarcusShortcuts shortcuts, edited_shortcuts;
    double frame_ms[DEBUG_HISTORY];
    int frame_next, frame_count;
    CameraPerformance camera_performance[2]; /* Movie, work; sampled on the app's frame clock. */
    CameraPerformance settings_performance;
    double settings_frame_ms[DEBUG_HISTORY];
    int settings_frame_next, settings_frame_count;
    char settings_path[1024], settings_message[256];
    int settings_open, rendering_settings, settings_work_camera, binding_capture;
    char movie[1024], resources[1024], content[1024], error[1024];
    int running, loaded, playing, loop, selected, authored, dirty, dragging, orbiting, panning;
    int continue_click, muted;
    int flying, fly_ready, camera_ready, fly_keys;
    int work_matched, work_needs_match;
    int row_scroll, object_scroll, width, height;
    int object_scroll_drag, resizing_objects, properties_scroll;
    float object_split, object_drag_offset, object_split_drag_offset;
    int animation_expanded;
    char animation_hint[640];
    int sheet_mode, sound_scroll, selected_sound, selected_sound_track;
    int both_scroll, sound_inspector, sheet_menu, sheet_menu_choice;
    int second_view, second_mode, resizing_view, movie_dirty;
    int movie_only, fullscreen;
    int movie_scrubbing, movie_resume, cursor_hidden;
    float movie_overlay_alpha;
    double movie_activity_tick, movie_scrub_seconds;
    int resizing_timeline;
    float timeline_height, timeline_drag_offset;
    float split_fraction, split_x;
    float sidebar, viewport_bottom, timeline_y, grid_x, grid_width;
    float yaw, pitch, zoom, pixels_per_frame;
    double timeline_start;
    Uint32 dialog_event;
} App;

static int object_row_count(const App *app)
{
    return app->info.object_count + (app->animation_expanded && app->selected >= 0 ? 4 : 0);
}
static int object_at_row(const App *app, int row, int *lane)
{
    *lane = 0;
    if (app->animation_expanded && app->selected >= 0 && row > app->selected)
    {
        if (row <= app->selected + 4)
        {
            *lane = row - app->selected;
            return app->selected;
        }
        row -= 4;
    }
    return row;
}
static int *sheet_scroll(App *app)
{
    return app->sheet_mode == SHEET_BOTH     ? &app->both_scroll
           : app->sheet_mode == SHEET_SOUNDS ? &app->sound_scroll
                                             : &app->row_scroll;
}
static int sheet_row_count(const App *app)
{
    return app->sheet_mode == SHEET_BOTH     ? object_row_count(app) + app->info.sound_track_count + 2
           : app->sheet_mode == SHEET_SOUNDS ? app->info.sound_track_count
                                             : object_row_count(app);
}
static int sheet_row(const App *app, int row, int *index, int *lane)
{
    int objects;
    *index = -1;
    *lane = 0;
    if (row < 0)
        return 0;
    objects = object_row_count(app);
    if (app->sheet_mode == SHEET_BOTH)
    {
        if (!row)
            return SHEET_OBJECT_HEADER;
        --row;
        if (row < objects)
        {
            *index = object_at_row(app, row, lane);
            return SHEET_OBJECT_ROW;
        }
        row -= objects;
        if (!row)
            return SHEET_SOUND_HEADER;
        --row;
    }
    else if (app->sheet_mode == SHEET_OBJECTS)
    {
        if (row >= objects)
            return 0;
        *index = object_at_row(app, row, lane);
        return SHEET_OBJECT_ROW;
    }
    if (row >= app->info.sound_track_count)
        return 0;
    *index = row;
    return SHEET_SOUND_ROW;
}
static int sound_sheet_row(App *app, int track)
{
    return track + (app->sheet_mode == SHEET_BOTH ? object_row_count(app) + 2 : 0) - *sheet_scroll(app);
}
static float sheet_menu_top(const App *app)
{
    float top;
    top = app->timeline_y + 80;
    return top + 88 > app->height - 24 ? app->timeline_y - 38 : top;
}
static const char *sheet_name(int mode)
{
    return mode == SHEET_BOTH ? "Both" : mode == SHEET_SOUNDS ? "Sounds" : "Objects";
}
static void set_sheet_mode(App *app, int mode)
{
    app->properties_scroll = 0;
    app->sheet_mode = mode;
    app->sheet_menu = 0;
    if (mode != SHEET_BOTH)
        app->sound_inspector = mode == SHEET_SOUNDS;
}
static void show_object_tracks(App *app)
{
    app->properties_scroll = 0;
    if (app->sheet_mode == SHEET_SOUNDS)
        app->sheet_mode = SHEET_OBJECTS;
    app->sound_inspector = 0;
}
static int animation_lane(int kind)
{
    if (kind == MARCUS_ANIMATION_ROTATION || kind == MARCUS_ANIMATION_SCALE || kind == MARCUS_ANIMATION_STRETCH ||
        kind == MARCUS_ANIMATION_MOVE || kind == MARCUS_ANIMATION_PATH)
        return 1;
    if (kind == MARCUS_ANIMATION_COSTUME)
        return 2;
    if (kind == MARCUS_ANIMATION_STEP || kind == MARCUS_ANIMATION_FREEZE)
        return 3;
    if (kind == MARCUS_ANIMATION_ENTRANCE || kind == MARCUS_ANIMATION_EXIT)
        return 4;
    return 0;
}
static void release_camera(App *app)
{
    app->object_scroll_drag = app->resizing_objects = 0;
    if (app->window && SDL_GetWindowRelativeMouseMode(app->window))
        SDL_SetWindowRelativeMouseMode(app->window, 0);
    app->flying = app->orbiting = app->panning = 0;
    app->fly_keys = 0;
}
static void view_mode(App *app, int mode)
{
    if (mode != 1) app->shortcuts.work_view.axis_view = 0;
    release_camera(app);
    if (mode == 2 && app->authored != 2)
    {
        app->flight = app->camera;
        app->fly_ready = app->camera_ready;
    }
    app->authored = mode;
    app->camera_ready = 0;
    app->orbiting = app->dragging = app->panning = 0;
    app->dirty = 1;
}

static double maximum(double a, double b)
{
    return a > b ? a : b;
}
static double minimum(double a, double b)
{
    return a < b ? a : b;
}
static double display_frame(const App *app, double frame)
{
    return frame - (double)app->info.first_frame + 1;
}
static float view_right(const App *app)
{
    return app->sidebar - VIEW_STRIP_WIDTH;
}
static float work_left(const App *app)
{
    return app->second_view ? app->split_x + 4 : 0;
}
static SDL_FRect viewport_rect(const App *app, int movie)
{
    const MarcusRenderResolution *resolution;
    SDL_FRect rect;
    float left, right, scale, height;
    resolution = marcus_render_resolution(movie || app->movie_only || (!app->second_view && app->authored == 1) ?
                                         app->shortcuts.render_resolution : app->shortcuts.work_render_resolution);
    if (app->movie_only)
    {
        scale = (float)minimum((double)app->width / resolution->width, (double)app->height / resolution->height);
        rect.w = resolution->width * scale;
        rect.h = resolution->height * scale;
        rect.x = (app->width - rect.w) / 2;
        rect.y = (app->height - rect.h) / 2;
        return rect;
    }
    left = movie ? 0 : work_left(app);
    right = movie ? app->split_x : view_right(app);
    height = app->viewport_bottom - 148;
    scale = (float)minimum((right - left - 32) / resolution->width, height / resolution->height);
    rect.w = resolution->width * scale;
    rect.h = resolution->height * scale;
    rect.x = left + (right - left - rect.w) / 2;
    rect.y = 136 + (height - rect.h) / 2;
    return rect;
}
static int inside_work(const App *app, float x, float y)
{
    float left;
    if (app->movie_only)
        return 0;
    left = work_left(app);
    return ui_inside(x, y, left + 12, 132, view_right(app) - left - 24, app->viewport_bottom - 144);
}
static int inside_movie(const App *app, float x, float y)
{
    if (app->movie_only)
        return ui_inside(x, y, 0, 0, (float)app->width, (float)app->height);
    return app->second_view ? ui_inside(x, y, 12, 132, app->split_x - 24, app->viewport_bottom - 144)
                            : app->authored == 1 && inside_work(app, x, y);
}
static void toggle_second_view(App *app)
{
    release_camera(app);
    app->dragging = app->orbiting = app->panning = app->resizing_view = app->resizing_timeline = 0;
    if (app->second_view)
    {
        app->second_mode = app->authored;
        app->authored = 1;
        app->second_view = 0;
    }
    else
    {
        app->second_view = 1;
        app->authored = app->second_mode;
    }
    SDL_DestroyTexture(app->picture);
    app->picture = NULL;
    app->camera_ready = 0;
    app->movie_dirty = app->dirty = 1;
}

static void fail(App *app, const char *message)
{
    /* Keep the initiating failure while its dialog is open. A subsequent
     * redraw of an invalid preview must not replace it with a generic error. */
    if (!app->error[0])
        SDL_strlcpy(app->error, message, sizeof(app->error));
    app->playing = 0;
    marcus_stop(app->player);
    app->dragging = app->orbiting = app->panning = 0;
    app->resizing_view = 0;
    app->resizing_timeline = 0;
    app->movie_scrubbing = app->movie_resume = 0;
    if (app->cursor_hidden)
        SDL_ShowCursor();
    app->cursor_hidden = 0;
    release_camera(app);
}
static void movie_activity(App *app)
{
    app->movie_activity_tick = (double)SDL_GetTicks();
    if (app->cursor_hidden)
        SDL_ShowCursor();
    app->cursor_hidden = 0;
}
static void update_movie_controls(App *app, double now, double elapsed)
{
    int visible, hide_cursor;
    float change;
    visible = app->movie_only && (!app->playing || app->playback.wait == 1 || app->movie_scrubbing ||
                                  app->error[0] || app->settings_open || browser_visible(app->browser) ||
                                  now - app->movie_activity_tick < MOVIE_IDLE_MS);
    change = (float)(maximum(0, elapsed) / (visible ? 180 : 350));
    app->movie_overlay_alpha = (float)maximum(0, minimum(1,
        app->movie_overlay_alpha + (visible ? change : -change)));
    hide_cursor = app->movie_only && !app->movie_scrubbing && !app->error[0] && !app->settings_open &&
                  !browser_visible(app->browser) && SDL_GetMouseFocus() == app->window && now - app->movie_activity_tick >= MOVIE_IDLE_MS;
    if (hide_cursor != app->cursor_hidden)
    {
        if (hide_cursor)
            SDL_HideCursor();
        else
            SDL_ShowCursor();
        app->cursor_hidden = hide_cursor;
    }
}
static int set_fullscreen(App *app, int fullscreen)
{
    release_camera(app);
    if (!SDL_SetWindowFullscreen(app->window, fullscreen))
    {
        fail(app, SDL_GetError());
        return 0;
    }
    app->fullscreen = fullscreen;
    app->movie_dirty = app->dirty = 1;
    return 1;
}
static void editor_view(App *app)
{
    if (app->fullscreen && !set_fullscreen(app, 0))
        return;
    app->movie_only = 0;
    if (app->movie_scrubbing)
        app->playing = app->movie_resume;
    app->movie_scrubbing = app->movie_resume = 0;
    app->movie_overlay_alpha = 0;
    movie_activity(app);
    release_camera(app);
    app->movie_dirty = app->dirty = 1;
}
static void refresh(App *app)
{
    int previous_scene;
    previous_scene = app->info.scene;
    app->loaded = marcus_info(app->player, &app->info);
    app->movie_time_status = app->loaded ? marcus_movie_time_async(app->player, &app->movie_time) : -1;
    app->movie_time_available = app->movie_time_status == 1;
    app->movie_dirty = 1;
    if (app->selected >= app->info.object_count)
        app->selected = -1;
    if (!app->loaded || previous_scene != app->info.scene)
    {
        app->selected = -1;
        app->selected_sound = app->selected_sound_track = -1;
        app->sound_scroll = app->both_scroll = 0;
    }
}
static int object_list_rows(const App *app)
{
    return (int)maximum(1, floor((app->object_split - OBJECT_LIST_TOP - OBJECT_LIST_FOOTER) / ROW_HEIGHT));
}
static void scroll_objects(App *app, int scroll)
{
    int limit;
    limit = app->loaded ? app->info.object_count - object_list_rows(app) : 0;
    app->object_scroll = (int)maximum(0, minimum(maximum(0, limit), scroll));
}
static int object_scrollbar(const App *app, SDL_FRect *track, SDL_FRect *thumb)
{
    int rows, limit;
    rows = object_list_rows(app);
    limit = app->info.object_count - rows;
    if (!app->loaded || limit <= 0)
        return 0;
    track->x = (float)app->width - 14;
    track->y = OBJECT_LIST_TOP;
    track->w = 8;
    track->h = (float)(rows * ROW_HEIGHT);
    *thumb = *track;
    thumb->h = (float)maximum(20, track->h * rows / app->info.object_count);
    thumb->y += (track->h - thumb->h) * app->object_scroll / limit;
    return 1;
}
static int properties_scroll_limit(const App *app)
{
    int content;
    if (app->sheet_mode == SHEET_SOUNDS || (app->sheet_mode == SHEET_BOTH && app->sound_inspector))
        content = app->selected_sound >= 0 ? 440 : 190;
    else
        content = app->selected >= 0 ? 400 : 100;
    return (int)maximum(0, content - (app->height - 24 - app->object_split));
}
static void layout(App *app)
{
    int rows;
    float height;
    SDL_GetRenderOutputSize(app->renderer, &app->width, &app->height);
    app->sidebar = (float)app->width - 300;
    app->object_split = (float)maximum(OBJECT_LIST_TOP + ROW_HEIGHT + OBJECT_LIST_FOOTER,
                                      minimum(app->height - 184, app->object_split));
    scroll_objects(app, app->object_scroll);
    app->properties_scroll = (int)maximum(0, minimum(properties_scroll_limit(app), app->properties_scroll));
    height = (float)maximum(TIMELINE_MIN_HEIGHT, minimum(app->height - VIEWPORT_MIN_BOTTOM, app->timeline_height));
    app->timeline_y = (float)app->height - height;
    app->viewport_bottom = app->timeline_y - TIMELINE_HANDLE_HEIGHT;
    app->grid_x = 218;
    app->grid_width = app->sidebar - app->grid_x - 8;
    app->split_x = (float)maximum(330, minimum(view_right(app) - 330, view_right(app) * app->split_fraction));
    rows = (int)((height - 112) / ROW_HEIGHT);
    app->row_scroll = (int)maximum(0, minimum(app->row_scroll, object_row_count(app) - rows));
    app->sound_scroll = (int)maximum(0, minimum(app->sound_scroll, app->info.sound_track_count - rows));
    app->both_scroll =
        (int)maximum(0, minimum(app->both_scroll, object_row_count(app) + app->info.sound_track_count + 2 - rows));
}
static void fit_timeline(App *app)
{
    double span;
    span = (double)app->info.last_frame - app->info.first_frame + 1;
    app->timeline_start = app->info.first_frame;
    app->pixels_per_frame = (float)(app->grid_width / (span > 0 ? span : 1));
    if (app->pixels_per_frame > 24)
        app->pixels_per_frame = 24;
    if (app->pixels_per_frame < 0.005f)
        app->pixels_per_frame = 0.005f;
}
static void reset_view(App *app)
{
    release_camera(app);
    marcus_reset_framing(app->player);
    app->yaw = app->pitch = 0;
    app->zoom = 1;
    app->work_matched = app->work_needs_match = 1;
    app->fly_ready = app->camera_ready = 0;
    app->dragging = app->orbiting = app->panning = 0;
    app->dirty = 1;
}
static void reset_camera_view(App *app)
{
    app->shortcuts.work_view.axis_view = 0;
    reset_view(app);
}
static int select_scene(App *app, int index)
{
    int keep_view, selected;
    if (!app->loaded || index < 0 || index >= app->info.scene_count)
        return 0;
    keep_view = app->playing;
    selected = keep_view ? marcus_select_scene_keep_view(app->player, index) : marcus_select_scene(app->player, index);
    if (!selected)
    {
        fail(app, marcus_error(app->player));
        refresh(app);
        app->dirty = 1;
        return 0;
    }
    refresh(app);
    if (!keep_view)
    {
        release_camera(app);
        app->dragging = app->orbiting = app->panning = 0;
        app->fly_ready = app->camera_ready = 0;
        app->work_matched = app->work_needs_match = 1;
    }
    app->selected = -1;
    app->row_scroll = app->object_scroll = 0;
    app->sound_scroll = app->both_scroll = 0;
    app->selected_sound = app->selected_sound_track = -1;
    fit_timeline(app);
    app->dirty = 1;
    return 1;
}
static int seek(App *app, long frame)
{
    if (!app->loaded)
        return 0;
    if (frame < app->info.first_frame)
        frame = app->info.first_frame;
    if (frame > app->info.last_frame)
        frame = app->info.last_frame;
    if (!marcus_seek(app->player, frame))
    {
        fail(app, marcus_error(app->player));
        return 0;
    }
    refresh(app);
    app->dirty = 1;
    return 1;
}
static SDL_FRect movie_settings_rect(const App *app)
{
    SDL_FRect rect;
    rect.x = (float)app->width - UI_SETTINGS_SIZE - 24;
    rect.y = 24;
    rect.w = rect.h = UI_SETTINGS_SIZE;
    return rect;
}
static SDL_FRect camera_settings_rect(const App *app, int movie)
{
    SDL_FRect rect;
    rect.x = (movie ? 0 : work_left(app)) + 16;
    rect.y = 96;
    rect.w = rect.h = 28;
    return rect;
}
static SDL_FRect movie_controls_rect(const App *app)
{
    SDL_FRect rect;
    rect.w = (float)minimum(1000, app->width - 48);
    rect.h = MOVIE_CONTROLS_HEIGHT;
    rect.x = (app->width - rect.w) / 2;
    rect.y = app->height - rect.h - 24;
    return rect;
}
static SDL_FRect movie_scrubber_rect(const App *app)
{
    SDL_FRect rect;
    if (app->movie_only)
    {
        rect = movie_controls_rect(app);
        rect.x += 24;
        rect.y += 42;
        rect.w -= 48;
        rect.h = MOVIE_SCRUBBER_HEIGHT;
    }
    else
    {
        rect.x = LOOP_X + 200;
        rect.y = app->timeline_y + PLAY_Y;
        rect.w = app->sidebar - rect.x - 148;
        rect.h = UI_PLAY_HEIGHT;
    }
    return rect;
}
static double movie_scrub_time(App *app, float x)
{
    SDL_FRect rect;
    rect = movie_scrubber_rect(app);
    return maximum(0, minimum(1, (x - rect.x) / rect.w)) * app->movie_time.duration_seconds;
}
static int seek_movie(App *app, double seconds)
{
    int previous_scene;
    if (!app->movie_time_available)
        return 0;
    previous_scene = app->info.scene;
    app->movie_scrub_seconds = maximum(0, minimum(app->movie_time.duration_seconds, seconds));
    app->continue_click = 0;
    if (!marcus_seek_time(app->player, app->movie_scrub_seconds))
    {
        fail(app, marcus_error(app->player));
        return 0;
    }
    refresh(app);
    app->playback.wait = app->playback.scrolling = app->playback.transition = 0;
    if (app->movie_scrub_seconds >= app->movie_time.duration_seconds)
        app->playing = 0;
    if (previous_scene != app->info.scene)
    {
        app->row_scroll = app->object_scroll = 0;
        fit_timeline(app);
    }
    app->dirty = 1;
    return 1;
}
static void start_movie_scrub(App *app, float x)
{
    release_camera(app);
    app->dragging = app->resizing_view = app->resizing_timeline = 0;
    app->movie_scrubbing = 1;
    app->movie_resume = app->playing;
    app->playing = 0;
    movie_activity(app);
    seek_movie(app, movie_scrub_time(app, x));
}
static void finish_movie_scrub(App *app)
{
    if (!app->movie_scrubbing)
        return;
    app->movie_scrubbing = 0;
    app->playing = app->movie_resume && app->movie_scrub_seconds < app->movie_time.duration_seconds;
    app->movie_resume = 0;
    movie_activity(app);
}
static int open_input(App *app, MarcusMovieInput *input, int entry, const char *path)
{
    char title[2200];
    MarcusMovieEntry member;
    scene_organiser_close(app->scene_organiser);
    finish_movie_scrub(app);
    app->playing = 0;
    release_camera(app);
    app->error[0] = 0;
    if (!marcus_open_input(app->player, input, entry, app->resources, app->content))
    {
        fail(app, marcus_error(app->player));
        refresh(app);
        app->dirty = 1;
        return 0;
    }
    if (input != app->movie_input)
        marcus_input_destroy(app->movie_input);
    app->movie_input = input;
    app->movie_entry = entry;
    if (path != app->movie)
        SDL_strlcpy(app->movie, path, sizeof(app->movie));
    if (marcus_input_entry(input, entry, &member) && member.zipped)
        SDL_snprintf(title, sizeof(title), "Marcus - %s :: %s", path, member.name);
    else
        SDL_snprintf(title, sizeof(title), "Marcus - %s", path);
    SDL_SetWindowTitle(app->window, title);
    refresh(app);
    app->selected = -1;
    app->camera_ready = 0;
    app->row_scroll = app->object_scroll = 0;
    app->sound_scroll = app->both_scroll = 0;
    app->selected_sound = app->selected_sound_track = -1;
    fit_timeline(app);
    reset_view(app);
    browser_remember(app->browser, path, input, entry);
    return 1;
}
static void cancel_zip(App *app)
{
    marcus_input_destroy(app->zip_input);
    app->zip_input = NULL;
}
static void confirm_zip(App *app)
{
    if (app->zip_input && open_input(app, app->zip_input, app->zip_selected, app->zip_path))
        app->zip_input = NULL;
}
static void open_movie(App *app, const char *path)
{
    MarcusMovieInput *input;
    app->sheet_menu = 0;
    app->playing = 0;
    release_camera(app);
    app->dragging = app->orbiting = app->panning = 0;
    input = marcus_input_create();
    if (!input || !marcus_input_open(input, path))
    {
        fail(app, marcus_input_error(input));
        marcus_input_destroy(input);
        return;
    }
    cancel_zip(app);
    if (marcus_input_count(input) == 1)
    {
        if (!open_input(app, input, 0, path))
            marcus_input_destroy(input);
    }
    else
    {
        app->error[0] = 0;
        app->zip_input = input;
        app->zip_selected = app->zip_scroll = 0;
        SDL_strlcpy(app->zip_path, path, sizeof(app->zip_path));
    }
}
static void zip_select(App *app, int index)
{
    int count;
    count = marcus_input_count(app->zip_input);
    if (index < 0)
        index = 0;
    if (index >= count)
        index = count - 1;
    app->zip_selected = index;
    if (index < app->zip_scroll)
        app->zip_scroll = index;
    if (index >= app->zip_scroll + 10)
        app->zip_scroll = index - 9;
}
static void zip_key(App *app, SDL_Keycode key)
{
    if (key == SDLK_ESCAPE)
        cancel_zip(app);
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
        confirm_zip(app);
    else if (key == SDLK_UP)
        zip_select(app, app->zip_selected - 1);
    else if (key == SDLK_DOWN)
        zip_select(app, app->zip_selected + 1);
    else if (key == SDLK_PAGEUP)
        zip_select(app, app->zip_selected - 10);
    else if (key == SDLK_PAGEDOWN)
        zip_select(app, app->zip_selected + 10);
    else if (key == SDLK_HOME)
        zip_select(app, 0);
    else if (key == SDLK_END)
        zip_select(app, marcus_input_count(app->zip_input) - 1);
}
static void zip_click(App *app, float x, float y)
{
    float left, top;
    int row;
    left = app->width / 2.0f - 400;
    top = app->height / 2.0f - 240;
    if (ui_inside(x, y, left + 24, top + 92, 752, 280))
    {
        row = (int)((y - top - 92) / 28) + app->zip_scroll;
        if (row < marcus_input_count(app->zip_input))
            zip_select(app, row);
    }
    else if (ui_inside(x, y, left + 568, top + 432, 96, 28))
        cancel_zip(app);
    else if (ui_inside(x, y, left + 680, top + 432, 96, 28))
        confirm_zip(app);
}
static void SDLCALL dialog_callback(void *userdata, const char *const *files, int filter)
{
    DialogRequest *request;
    SDL_Event event;
    (void)filter;
    request = (DialogRequest *)userdata;
    if (files && !files[0])
    {
        SDL_free(request);
        return;
    }
    if (!files)
    {
        request->kind = 3;
        request->path = SDL_strdup(SDL_GetError());
    }
    else
        request->path = SDL_strdup(files[0]);
    memset(&event, 0, sizeof(event));
    event.type = request->event_type;
    event.user.data1 = request;
    if (!request->path || !SDL_PushEvent(&event))
    {
        SDL_free(request->path);
        SDL_free(request);
    }
}
static void choose_file(App *app, int kind)
{
    DialogRequest *request;
    static const SDL_DialogFileFilter resources[] = {{"3DMM resources", "chk"}};
    release_camera(app);
    if (kind == 0)
    {
        app->playing = 0;
        app->sheet_menu = 0;
        app->dragging = app->orbiting = app->panning = 0;
        cancel_zip(app);
        browser_show(app->browser, NULL, app->resources, app->content);
        return;
    }
    request = (DialogRequest *)SDL_calloc(1, sizeof(*request));
    if (!request)
    {
        fail(app, "Not enough memory to open the file chooser.");
        return;
    }
    request->event_type = app->dialog_event;
    request->kind = kind;
    app->playing = 0;
    if (kind == 2)
        SDL_ShowOpenFolderDialog(dialog_callback, request, app->window, app->content, 0);
    else
        SDL_ShowOpenFileDialog(dialog_callback, request, app->window, resources, 1, app->resources, 0);
}
static void toggle_play(App *app)
{
    if (!app->loaded)
        return;
    if (app->playing && app->playback.wait == 1)
    {
        app->continue_click = 1;
        return;
    }
    if (!app->playing && app->info.frame == app->info.last_frame && app->info.scene == app->info.scene_count - 1)
    {
        if (!select_scene(app, 0))
            return;
    }
    app->playing = !app->playing;
}
static void movie_controls_click(App *app, float x, float y)
{
    SDL_FRect rect, scrubber;
    MarcusSceneTime scene_time;
    int visible;
    visible = app->movie_overlay_alpha > 0.1f || !app->playing || app->playback.wait == 1;
    movie_activity(app);
    rect = movie_controls_rect(app);
    if (!ui_inside(x, y, rect.x, rect.y, rect.w, rect.h))
    {
        toggle_play(app);
        return;
    }
    if (!visible)
        return;
    scrubber = movie_scrubber_rect(app);
    if (app->movie_time_available && ui_inside(x, y, scrubber.x, scrubber.y, scrubber.w, scrubber.h))
        start_movie_scrub(app, x);
    else if (ui_inside(x, y, rect.x + 64, rect.y + 78, UI_PLAY_WIDTH, UI_PLAY_HEIGHT))
        toggle_play(app);
    else if (ui_inside(x, y, rect.x + 24, rect.y + 90, 28, UI_NAVIGATION_HEIGHT))
    {
        if (marcus_scene_time(app->player, app->info.scene - 1, &scene_time))
            seek_movie(app, scene_time.start_seconds);
    }
    else if (ui_inside(x, y, rect.x + 116, rect.y + 90, 28, UI_NAVIGATION_HEIGHT))
    {
        if (marcus_scene_time(app->player, app->info.scene + 1, &scene_time))
            seek_movie(app, scene_time.start_seconds);
    }
    else if (ui_inside(x, y, rect.x + 168, rect.y + 86, 56, 28))
        seek_movie(app, app->movie_time.position_seconds - 10);
    else if (ui_inside(x, y, rect.x + 232, rect.y + 86, 56, 28))
        seek_movie(app, app->movie_time.position_seconds + 10);
    else if (ui_inside(x, y, rect.x + 312, rect.y + 86, 64, 28))
    {
        app->muted = !app->muted;
        marcus_set_muted(app->player, app->muted);
    }
    else if (ui_inside(x, y, rect.x + rect.w - 212, rect.y + 86, 96, 28))
        set_fullscreen(app, !app->fullscreen);
    else if (ui_inside(x, y, rect.x + rect.w - 108, rect.y + 86, 84, 28))
        editor_view(app);
}
static void perform_action(App *app, int action)
{
    long target;
    int scene, direction;
    if (action == MARCUS_OPEN)
    {
        choose_file(app, 0);
        return;
    }
    if (!app->loaded)
        return;
    if (action <= MARCUS_LAST_FRAME)
    {
        if (action == MARCUS_FIRST_FRAME)
            target = app->info.first_frame;
        else if (action == MARCUS_LAST_FRAME)
            target = app->info.last_frame;
        else
            target = (long)minimum(
                app->info.last_frame,
                maximum(app->info.first_frame, (double)app->info.frame + (action == MARCUS_PREV_FRAME ? -1 : 1)));
        if (target == app->info.frame)
            return;
        app->playing = 0;
        seek(app, target);
    }
    else if (action <= MARCUS_LAST_SCENE)
    {
        direction = action - MARCUS_FIRST_SCENE;
        scene = direction == 0   ? 0
                : direction == 1 ? app->info.scene - 1
                : direction == 2 ? app->info.scene + 1
                                 : app->info.scene_count - 1;
        if (scene < 0 || scene >= app->info.scene_count || scene == app->info.scene)
            return;
        app->playing = 0;
        select_scene(app, scene);
    }
    else if (action == MARCUS_PLAY)
        toggle_play(app);
    else if (action == MARCUS_VIEW)
        view_mode(app, app->second_view ? (app->authored == 2 ? 0 : 2) : (app->authored + 1) % 3);
    else if (action == MARCUS_RESET_VIEW)
    {
        reset_camera_view(app);
        fit_timeline(app);
    }
}
static float frame_x(App *app, double frame)
{
    return app->grid_x + (float)((frame - app->timeline_start) * app->pixels_per_frame);
}
static long frame_at(App *app, float x)
{
    double frame;
    frame = floor(app->timeline_start + (x - app->grid_x) / app->pixels_per_frame);
    if (frame < app->info.first_frame)
        frame = app->info.first_frame;
    if (frame > app->info.last_frame)
        frame = app->info.last_frame;
    return (long)frame;
}
static void zoom_timeline(App *app, float factor, float anchor)
{
    double frame;
    frame = app->timeline_start + (anchor - app->grid_x) / app->pixels_per_frame;
    app->pixels_per_frame *= factor;
    if (app->pixels_per_frame < 0.005f)
        app->pixels_per_frame = 0.005f;
    if (app->pixels_per_frame > 64)
        app->pixels_per_frame = 64;
    app->timeline_start = frame - (anchor - app->grid_x) / app->pixels_per_frame;
    if (app->timeline_start < app->info.first_frame)
        app->timeline_start = app->info.first_frame;
}
static int sound_has_marker(App *app, int index, const MarcusSound *sound)
{
    MarcusWaveformInfo waveform;
    MarcusWaveformColumn peak;
    if (sound->carried || sound->motion)
        return 1;
    return !marcus_sound_waveform(app->player, index, 0, 1, 1, &peak, &waveform) ||
           waveform.kind != MARCUS_WAVEFORM_SAMPLED;
}
static int animation_event_at(App *app, int object, int lane, int row, float x, float y)
{
    MarcusAnimationInfo info;
    MarcusAnimationEvent event;
    int index, found;
    float center, marker_y, distance, closest;
    if (!lane || !marcus_animation_info(app->player, object, &info))
        return -1;
    found = -1;
    closest = 1000;
    for (index = 0; index < info.event_count; ++index)
        if (marcus_animation_event(app->player, object, index, &event) && animation_lane(event.kind) == lane)
        {
            center = frame_x(app, event.frame + 0.5);
            marker_y = app->timeline_y + 88 + row * ROW_HEIGHT + (index % 3) * 6;
            distance = (float)(fabs(x - center) + fabs(y - marker_y));
            if (fabs(x - center) <= 6 && fabs(y - marker_y) <= 5 && distance < closest)
            {
                closest = distance;
                found = index;
            }
        }
    return found;
}
static int select_timeline_row(App *app, float x, float y, int grid)
{
    int row, index, previous, closest, fallback, lane, object, rows, kind, visible_row, *scroll;
    MarcusAnimationEvent event;
    MarcusSound sound;
    MarcusWaveformInfo waveform;
    MarcusWaveformColumn peak;
    float center, marker_y;
    double pointer_frame;
    row = (int)((y - app->timeline_y - 80) / ROW_HEIGHT);
    visible_row = row;
    scroll = sheet_scroll(app);
    kind = sheet_row(app, row + *scroll, &object, &lane);
    if (kind == SHEET_OBJECT_HEADER || kind == SHEET_SOUND_HEADER)
    {
        app->dragging = 0;
        return 1;
    }
    if (!kind)
        return 0;
    if (kind == SHEET_OBJECT_ROW)
    {
        if (object >= 0 && object < app->info.object_count)
        {
            if (!grid && !lane && x < 30)
                app->animation_expanded = app->selected == object ? !app->animation_expanded : 1;
            app->selected = object;
            app->sound_inspector = 0;
            if (!grid && !lane && x < 30 && app->animation_expanded)
            {
                rows = (int)((app->height - app->timeline_y - 112) / ROW_HEIGHT);
                index = object + (app->sheet_mode == SHEET_BOTH ? 1 : 0);
                if (rows >= 5 && index + 4 >= *scroll + rows)
                    *scroll = index + 5 - rows;
            }
            index = grid ? animation_event_at(app, object, lane, row, x, y) : -1;
            if (index >= 0 && marcus_animation_event(app->player, object, index, &event))
            {
                seek(app, event.frame);
                return 1;
            }
        }
        return 0;
    }
    row = object;
    app->sound_inspector = 1;
    app->properties_scroll = 0;
    previous = app->selected_sound;
    app->selected_sound = -1;
    if (row < 0 || row >= app->info.sound_track_count)
        return 0;
    app->selected_sound_track = row;
    if (!grid)
        return 0;
    /* Stagger coincident cues vertically so queued entries can be selected. */
    closest = fallback = -1;
    for (index = app->info.sound_count - 1; index >= 0; --index)
        if (marcus_sound(app->player, index, &sound) && sound.track == row && sound_has_marker(app, index, &sound))
        {
            center = frame_x(app, sound.frame + 0.5);
            marker_y = app->timeline_y + 87 + visible_row * ROW_HEIGHT + (index % 3) * 5;
            if (fabs(x - center) <= 6 && fabs(y - marker_y) <= 4)
            {
                if (fallback < 0)
                    fallback = index;
                if (closest < 0 && index < previous)
                    closest = index;
            }
        }
    app->selected_sound = closest >= 0 ? closest : fallback;
    if (marcus_sound(app->player, app->selected_sound, &sound))
    {
        seek(app, sound.frame);
        return 1;
    }
    /* Clicking a waveform selects its cue while scrubbing to the pointer. */
    closest = fallback = -1;
    for (index = app->info.sound_count - 1; index >= 0; --index)
        if (marcus_sound(app->player, index, &sound) && sound.track == row)
        {
            pointer_frame = app->timeline_start + (x - app->grid_x) / app->pixels_per_frame;
            if (pointer_frame >= sound.timeline_frame && pointer_frame < sound.end_frame &&
                marcus_sound_waveform(app->player, index, 0, 1, 1, &peak, &waveform) &&
                waveform.kind == MARCUS_WAVEFORM_SAMPLED)
            {
                if (fallback < 0)
                    fallback = index;
                if (closest < 0 && index < previous)
                    closest = index;
            }
        }
    app->selected_sound = closest >= 0 ? closest : fallback;
    if (app->selected_sound >= 0)
    {
        seek(app, frame_at(app, x));
        return 1;
    }
    return 0;
}
static void open_settings(App *app)
{
    release_camera(app);
    app->sheet_menu = 0;
    app->resizing_view = app->resizing_timeline = 0;
    app->playing = app->dragging = app->orbiting = app->panning = 0;
    app->edited_shortcuts = app->shortcuts;
    app->binding_capture = -1;
    app->rendering_settings = 0;
    app->settings_open = 1;
}
static void normalize_camera_options(int renderer, MarcusRenderOptions *view)
{
    /* 3DMM materials already use Gouraud shading in the authored mode. */
    if (view->display_mode == MARCUS_DISPLAY_SMOOTH) view->display_mode = MARCUS_DISPLAY_TEXTURED;
    if (renderer == MARCUS_RENDERER_BRENDER)
    {
        if (view->display_mode >= MARCUS_DISPLAY_DEPTH) view->display_mode = MARCUS_DISPLAY_TEXTURED;
        view->wire_overlay = view->antialias = view->texture_filter = 0;
    }
}
static void open_rendering_settings(App *app, int work)
{
    open_settings(app);
    app->rendering_settings = 1;
    app->settings_work_camera = work;
    app->settings_performance = app->camera_performance[work];
    memcpy(app->settings_frame_ms, app->frame_ms, sizeof(app->settings_frame_ms));
    app->settings_frame_next = app->frame_next;
    app->settings_frame_count = app->frame_count;
    movie_activity(app);
}
static void cancel_settings(App *app)
{
    app->settings_open = 0;
    app->rendering_settings = 0;
    app->binding_capture = -1;
    app->settings_message[0] = 0;
    if (app->movie_only)
        movie_activity(app);
}
static void apply_settings(App *app)
{
    int rendering_changed, work_changed, i, scale;
    const MarcusRenderResolution *resolution;
    MarcusRenderOptions *view;
    normalize_camera_options(app->edited_shortcuts.renderer, &app->edited_shortcuts.movie_view);
    normalize_camera_options(app->edited_shortcuts.work_renderer, &app->edited_shortcuts.work_view);
    for (i = 0; i < 2; ++i)
    {
        view = i ? &app->edited_shortcuts.work_view : &app->edited_shortcuts.movie_view;
        resolution = marcus_render_resolution(i ? app->edited_shortcuts.work_render_resolution : app->edited_shortcuts.render_resolution);
        scale = 1 << view->antialias;
        if (resolution->width * scale > 4096 || resolution->height * scale > 4096)
        {
            SDL_strlcpy(app->settings_message, "Choose a smaller resolution or turn AA off (4096-pixel render limit).", sizeof(app->settings_message));
            return;
        }
    }
    work_changed = app->shortcuts.work_view.orthographic != app->edited_shortcuts.work_view.orthographic ||
                   app->shortcuts.work_view.axis_view != app->edited_shortcuts.work_view.axis_view;
    rendering_changed = app->shortcuts.renderer != app->edited_shortcuts.renderer ||
                        app->shortcuts.render_resolution != app->edited_shortcuts.render_resolution ||
                        app->shortcuts.work_renderer != app->edited_shortcuts.work_renderer ||
                        app->shortcuts.work_render_resolution != app->edited_shortcuts.work_render_resolution ||
                        memcmp(&app->shortcuts.movie_view, &app->edited_shortcuts.movie_view, sizeof(MarcusRenderOptions)) ||
                        memcmp(&app->shortcuts.work_view, &app->edited_shortcuts.work_view, sizeof(MarcusRenderOptions));
    if (!marcus_set_renderer(app->player, app->edited_shortcuts.renderer) ||
        !marcus_set_renderer(app->player, app->edited_shortcuts.work_renderer))
    {
        SDL_strlcpy(app->settings_message, marcus_error(app->player), sizeof(app->settings_message));
        return;
    }
    if (!shortcuts_save(app->settings_path, &app->edited_shortcuts, app->settings_message,
                        sizeof(app->settings_message)))
        return;
    if ((!app->shortcuts.debug_overlay && app->edited_shortcuts.debug_overlay) ||
        (!app->shortcuts.movie_debug_overlay && app->edited_shortcuts.movie_debug_overlay) ||
        (!app->shortcuts.work_debug_overlay && app->edited_shortcuts.work_debug_overlay))
    {
        app->movie_dirty = app->dirty = 1;
    }
    app->shortcuts = app->edited_shortcuts;
    if (work_changed)
    {
        app->work_matched = app->work_needs_match = 0;
        if (app->shortcuts.work_view.axis_view)
        {
            app->fly_ready = 0;
            app->yaw = app->shortcuts.work_view.axis_view == 2 ? 90.0f : 0;
            app->pitch = app->shortcuts.work_view.axis_view == 3 ? 90.0f : 0;
            if (app->second_view || app->authored != 1) app->authored = 0;
        }
    }
    if (app->shortcuts.work_view.display_mode || app->shortcuts.work_view.wire_overlay)
        app->work_matched = app->work_needs_match = 0;
    if (rendering_changed)
    {
        app->dirty = 1;
        app->movie_dirty = 1;
        app->error[0] = 0;
    }
    cancel_settings(app);
}
static void settings_click(App *app, float x, float y)
{
    float left, top;
    int renderer, resolution, i, gpu, *edited_renderer, *edited_resolution;
    MarcusRenderOptions *view;
    edited_renderer = app->settings_work_camera ? &app->edited_shortcuts.work_renderer : &app->edited_shortcuts.renderer;
    edited_resolution = app->settings_work_camera ? &app->edited_shortcuts.work_render_resolution :
                                                   &app->edited_shortcuts.render_resolution;
    view = app->settings_work_camera ? &app->edited_shortcuts.work_view : &app->edited_shortcuts.movie_view;
    gpu = *edited_renderer != MARCUS_RENDERER_BRENDER;
    if (app->rendering_settings)
    {
        left = (app->width - RENDER_SETTINGS_WIDTH) / 2.0f;
        top = (app->height - RENDER_SETTINGS_HEIGHT) / 2.0f;
        if (ui_inside(x, y, left + RENDER_SETTINGS_WIDTH - 42, top + 16, 28, 28) ||
            ui_inside(x, y, left + RENDER_SETTINGS_WIDTH - 224, top + RENDER_SETTINGS_HEIGHT - 48, 88, 28))
            cancel_settings(app);
        else if (ui_inside(x, y, left + RENDER_SETTINGS_WIDTH - 124, top + RENDER_SETTINGS_HEIGHT - 48, 96, 28))
            apply_settings(app);
        else
        {
            if (ui_inside(x, y, left + 760, top + 364, 272, 28))
            {
                if (app->settings_work_camera)
                    app->edited_shortcuts.work_debug_overlay = !app->edited_shortcuts.work_debug_overlay;
                else
                    app->edited_shortcuts.movie_debug_overlay = !app->edited_shortcuts.movie_debug_overlay;
            }
            for (renderer = 0; renderer < MARCUS_RENDERER_COUNT; ++renderer)
                if (ui_inside(x, y, left + 24 + renderer * 144, top + 86, 136, 32))
                {
                    *edited_renderer = renderer;
                    normalize_camera_options(renderer, view);
                    app->settings_message[0] = 0;
                    break;
                }
            if (ui_inside(x, y, left + 480, top + 80, 256, 28))
            {
                view->display_mode = (view->display_mode + 1) % MARCUS_DISPLAY_COUNT;
                if (view->display_mode == MARCUS_DISPLAY_SMOOTH) ++view->display_mode;
                if (!gpu && view->display_mode >= MARCUS_DISPLAY_DEPTH) view->display_mode = MARCUS_DISPLAY_TEXTURED;
            }
            if (gpu && ui_inside(x, y, left + 620, top + 120, 116, 28)) view->wire_overlay = !view->wire_overlay;
            for (i = 0; i < 3; ++i)
                if (gpu && ui_inside(x, y, left + 480 + i * 88, top + 190, 80, 28))
                    view->antialias = i;
            for (i = 0; i < 2; ++i)
            {
                if (gpu && ui_inside(x, y, left + 480 + i * 132, top + 258, 124, 28)) view->texture_filter = i;
                if (app->settings_work_camera && ui_inside(x, y, left + 480 + i * 132, top + 328, 124, 28))
                    view->orthographic = i;
            }
            for (i = 0; i < 4; ++i)
                if (app->settings_work_camera && ui_inside(x, y, left + 480 + i * 66, top + 396, 58, 28))
                {
                    view->axis_view = i;
                    if (i) view->orthographic = 1;
                }
            for (resolution = 0; resolution < MARCUS_RESOLUTION_COUNT; ++resolution)
                if (ui_inside(x, y, left + 24 + (resolution % RESOLUTION_COLUMNS) * 146,
                              top + 160 + (resolution / RESOLUTION_COLUMNS) * 40, 138, 32))
                {
                    *edited_resolution = resolution;
                    app->settings_message[0] = 0;
                    break;
                }
        }
        return;
    }
    left = app->width / 2.0f - 330;
    top = (app->height - SETTINGS_HEIGHT) / 2.0f;
    if (ui_inside(x, y, left + 618, top + 16, 28, 28))
        cancel_settings(app);
    else if (ui_inside(x, y, left + 430, top + 100, 204, MARCUS_ACTION_COUNT * 30))
    {
        app->binding_capture = (int)((y - top - 100) / 30);
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 24, top + 472, 610, 28))
    {
        app->edited_shortcuts.pause_on_focus_loss = !app->edited_shortcuts.pause_on_focus_loss;
        app->binding_capture = -1;
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 430, top + 508, 204, 28))
    {
        app->edited_shortcuts.renderer = (app->edited_shortcuts.renderer + 1) % MARCUS_RENDERER_COUNT;
        normalize_camera_options(app->edited_shortcuts.renderer, &app->edited_shortcuts.movie_view);
        app->binding_capture = -1;
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 430, top + 544, 204, 28))
    {
        app->edited_shortcuts.render_resolution = (app->edited_shortcuts.render_resolution + 1) % MARCUS_RESOLUTION_COUNT;
        app->binding_capture = -1;
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 430, top + 580, 204, 28))
    {
        app->edited_shortcuts.work_renderer = (app->edited_shortcuts.work_renderer + 1) % MARCUS_RENDERER_COUNT;
        normalize_camera_options(app->edited_shortcuts.work_renderer, &app->edited_shortcuts.work_view);
        app->binding_capture = -1;
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 430, top + 616, 204, 28))
    {
        app->edited_shortcuts.work_render_resolution =
            (app->edited_shortcuts.work_render_resolution + 1) % MARCUS_RESOLUTION_COUNT;
        app->binding_capture = -1;
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 24, top + 652, 610, 28))
    {
        app->edited_shortcuts.debug_overlay = !app->edited_shortcuts.debug_overlay;
        app->binding_capture = -1;
        app->settings_message[0] = 0;
    }
    else if (ui_inside(x, y, left + 24, top + 704, 148, 28))
    {
        shortcuts_defaults(&app->edited_shortcuts);
        app->binding_capture = -1;
        SDL_strlcpy(app->settings_message, "Defaults restored. Apply to save them.", sizeof(app->settings_message));
    }
    else if (ui_inside(x, y, left + 438, top + 704, 88, 28))
        cancel_settings(app);
    else if (ui_inside(x, y, left + 538, top + 704, 96, 28))
        apply_settings(app);
}
static void settings_key(App *app, SDL_Keycode key, SDL_Keymod modifiers)
{
    if (app->binding_capture >= 0)
    {
        if (key == SDLK_ESCAPE)
        {
            app->binding_capture = -1;
            app->settings_message[0] = 0;
        }
        else if (!shortcuts_is_modifier(key) && key != SDLK_UNKNOWN)
        {
            if (shortcuts_assign(&app->edited_shortcuts, app->binding_capture,
                                 key == SDLK_BACKSPACE ? SDLK_UNKNOWN : key, shortcuts_modifiers(modifiers),
                                 app->settings_message, sizeof(app->settings_message)))
                app->binding_capture = -1;
        }
    }
    else if (key == SDLK_ESCAPE)
        cancel_settings(app);
    else if (key == SDLK_RETURN)
        apply_settings(app);
}
static void pick_object(App *app, float x, float y)
{
    SDL_FRect rect;
    const MarcusImage *image;
    int movie, index, rows, track_row, *scroll;
    movie = app->second_view && inside_movie(app, x, y);
    if (movie ? app->movie_dirty || !app->movie_picture : app->dirty || !app->picture)
        return;
    image = movie ? &app->movie_image : &app->work_image;
    rect = viewport_rect(app, movie);
    if (!ui_inside(x, y, rect.x, rect.y, rect.w, rect.h))
        return;
    if (!marcus_pick(app->player, image, (int)((x - rect.x) * image->width / rect.w),
                     (int)((y - rect.y) * image->height / rect.h), &index))
    {
        fail(app, marcus_error(app->player));
        return;
    }
    app->selected = index;
    if (index < 0)
        return;
    show_object_tracks(app);
    if (index < app->object_scroll)
        app->object_scroll = index;
    if (index >= app->object_scroll + object_list_rows(app))
        app->object_scroll = index - object_list_rows(app) + 1;
    scroll_objects(app, app->object_scroll);
    rows = (int)maximum(1, (app->height - app->timeline_y - 112) / ROW_HEIGHT);
    track_row = index + (app->sheet_mode == SHEET_BOTH ? 1 : 0);
    scroll = sheet_scroll(app);
    if (track_row < *scroll)
        *scroll = track_row;
    if (track_row >= *scroll + rows)
        *scroll = track_row - rows + 1;
}
static void mouse_down(App *app, float x, float y, int button)
{
    SDL_FRect rect, track, thumb;
    int row, action;
    float right;
    right = view_right(app);
    if (app->error[0])
    {
        if (ui_inside(x, y, app->width / 2.0f - 50, app->height / 2.0f + 86, 100, 28))
            app->error[0] = 0;
        return;
    }
    if (app->zip_input)
    {
        if (button == SDL_BUTTON_LEFT)
            zip_click(app, x, y);
        return;
    }
    if (app->settings_open)
    {
        if (button == SDL_BUTTON_LEFT)
            settings_click(app, x, y);
        return;
    }
    if (app->movie_only)
    {
        if (button == SDL_BUTTON_LEFT)
        {
            rect = movie_settings_rect(app);
            if (ui_inside(x, y, rect.x, rect.y, rect.w, rect.h))
                open_rendering_settings(app, 0);
            else
                movie_controls_click(app, x, y);
        }
        return;
    }
    if (app->sheet_menu)
    {
        if (button == SDL_BUTTON_LEFT && ui_inside(x, y, 10, sheet_menu_top(app) + 2, 194, 84))
            set_sheet_mode(app, (int)((y - sheet_menu_top(app) - 2) / ROW_HEIGHT));
        else
            app->sheet_menu = 0;
        return;
    }
    if (button == SDL_BUTTON_LEFT)
    {
        if (ui_inside(x, y, app->sidebar, app->object_split - 4, 300, 8))
        {
            release_camera(app);
            app->dragging = app->resizing_view = app->resizing_timeline = 0;
            app->resizing_objects = 1;
            app->object_split_drag_offset = y - app->object_split;
            return;
        }
        if (object_scrollbar(app, &track, &thumb) &&
            ui_inside(x, y, track.x - 3, track.y, track.w + 6, track.h))
        {
            release_camera(app);
            app->dragging = app->resizing_view = app->resizing_timeline = 0;
            if (y >= thumb.y && y < thumb.y + thumb.h)
            {
                app->object_scroll_drag = 1;
                app->object_drag_offset = y - thumb.y;
            }
            else
                scroll_objects(app, app->object_scroll + (y < thumb.y ? -object_list_rows(app) : object_list_rows(app)));
            return;
        }
        rect = camera_settings_rect(app, 0);
        if (ui_inside(x, y, rect.x, rect.y, rect.w, rect.h))
        {
            open_rendering_settings(app, app->second_view || app->authored != 1);
            return;
        }
        rect = camera_settings_rect(app, 1);
        if (app->second_view && ui_inside(x, y, rect.x, rect.y, rect.w, rect.h))
        {
            open_rendering_settings(app, 0);
            return;
        }
    }
    rect = movie_scrubber_rect(app);
    if (button == SDL_BUTTON_LEFT && app->movie_time_available && !app->flying && !app->orbiting && !app->panning &&
        ui_inside(x, y, rect.x, rect.y, rect.w, rect.h))
    {
        start_movie_scrub(app, x);
        return;
    }
    if (button == SDL_BUTTON_LEFT && app->loaded && !app->flying && !app->orbiting && !app->panning &&
        ui_inside(x, y, 10, app->timeline_y + 50, 194, 28))
    {
        release_camera(app);
        app->dragging = 0;
        app->sheet_menu = 1;
        app->sheet_menu_choice = app->sheet_mode;
        return;
    }
    if (button == SDL_BUTTON_LEFT &&
        ui_inside(x, y, 0, app->viewport_bottom, app->sidebar, TIMELINE_HANDLE_HEIGHT))
    {
        app->resizing_timeline = 1;
        app->timeline_drag_offset = y - app->viewport_bottom;
        app->resizing_view = app->orbiting = app->panning = app->dragging = 0;
        return;
    }
    if (button == SDL_BUTTON_LEFT &&
        ui_inside(x, y, right, 89, VIEW_STRIP_WIDTH, app->viewport_bottom - 89))
    {
        toggle_second_view(app);
        return;
    }
    if (button == SDL_BUTTON_LEFT && app->second_view &&
        ui_inside(x, y, app->split_x - 4, 89, 8, app->viewport_bottom - 89))
    {
        app->resizing_view = 1;
        app->orbiting = app->panning = app->dragging = 0;
        return;
    }
    if (button == SDL_BUTTON_LEFT && app->playing && app->playback.wait == 1 &&
        inside_movie(app, x, y))
    {
        app->continue_click = 1;
        return;
    }
    if (button == SDL_BUTTON_RIGHT && app->loaded && !app->authored && app->camera_ready &&
        inside_work(app, x, y))
    {
        app->panning = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (!app->panning && !SDL_SetWindowRelativeMouseMode(app->window, 1))
        {
            fail(app, SDL_GetError());
            return;
        }
        app->orbiting = !app->panning;
        app->dragging = 0;
        return;
    }
    if (button == SDL_BUTTON_RIGHT && app->loaded && app->authored == 2 && app->fly_ready &&
        inside_work(app, x, y))
    {
        if (!SDL_SetWindowRelativeMouseMode(app->window, 1))
        {
            fail(app, SDL_GetError());
            return;
        }
        app->flying = 1;
        app->fly_keys = (SDL_GetModState() & SDL_KMOD_LSHIFT) ? FLY_FAST : 0;
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_W])
            app->fly_keys |= FLY_FORWARD;
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_S])
            app->fly_keys |= FLY_BACK;
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_A])
            app->fly_keys |= FLY_LEFT;
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_D])
            app->fly_keys |= FLY_RIGHT;
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_E])
            app->fly_keys |= FLY_UP;
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_Q])
            app->fly_keys |= FLY_DOWN;
        if (SDL_GetModState() & SDL_KMOD_RSHIFT)
            app->fly_keys |= FLY_FAST_RIGHT;
        return;
    }
    if (button != SDL_BUTTON_LEFT || app->flying || app->orbiting || app->panning)
        return;
    if (ui_inside(x, y, 16, 52, 112, 28))
        choose_file(app, 0);
    else if (ui_inside(x, y, 136, 52, 104, 28))
        choose_file(app, 1);
    else if (ui_inside(x, y, 248, 52, 104, 28))
        choose_file(app, 2);
    else if (ui_inside(x, y, 360, 52, 96, 28))
        open_settings(app);
    else if (ui_inside(x, y, LOOP_X, app->timeline_y + SCENE_NAV_Y, 60, UI_NAVIGATION_HEIGHT))
    {
        app->muted = !app->muted;
        marcus_set_muted(app->player, app->muted);
    }
    else if (!app->loaded)
    {
        if (ui_inside(x, y, app->sidebar / 2 - 65, 280, 130, 28))
            choose_file(app, 0);
    }
    else if (ui_inside(x, y, 16, app->timeline_y + FRAME_NAV_Y, 164, UI_NAVIGATION_HEIGHT))
    {
        app->playing = 0;
        release_camera(app);
        app->dragging = app->orbiting = app->panning = 0;
        app->sheet_menu = 0;
        if (!scene_organiser_show_frames(app->scene_organiser, app->player))
            fail(app, "Cannot open the frame organiser.");
    }
    else if (ui_inside(x, y, 16, app->timeline_y + SCENE_NAV_Y, 164, UI_NAVIGATION_HEIGHT))
    {
        app->playing = 0;
        release_camera(app);
        app->dragging = app->orbiting = app->panning = 0;
        app->sheet_menu = 0;
        scene_organiser_show(app->scene_organiser, app->player);
    }
    else if (ui_inside(x, y, SCENE_NAV_X, app->timeline_y + SCENE_NAV_Y, NAV_STEP * 4 - 4, UI_NAVIGATION_HEIGHT))
    {
        action = (int)((x - SCENE_NAV_X) / NAV_STEP);
        if (x - SCENE_NAV_X - action * NAV_STEP >= 28)
            return;
        perform_action(app, MARCUS_FIRST_SCENE + action);
    }
    else if (app->second_view && ui_inside(x, y, right - 220, 96, 72, 28))
        view_mode(app, 0);
    else if (app->second_view && ui_inside(x, y, right - 140, 96, 60, 28))
        view_mode(app, 2);
    else if (app->second_view && ui_inside(x, y, right - 72, 96, 56, 28))
        reset_camera_view(app);
    else if (!app->second_view && ui_inside(x, y, right - 312, 96, 84, 28))
        view_mode(app, 1);
    else if (!app->second_view && ui_inside(x, y, right - 220, 96, 72, 28))
        view_mode(app, 0);
    else if (!app->second_view && ui_inside(x, y, right - 140, 96, 60, 28))
        view_mode(app, 2);
    else if (!app->second_view && ui_inside(x, y, right - 72, 96, 56, 28))
        reset_camera_view(app);
    else if (ui_inside(x, y, FRAME_NAV_X, app->timeline_y + FRAME_NAV_Y, NAV_STEP * 4 - 4, UI_NAVIGATION_HEIGHT))
    {
        action = (int)((x - FRAME_NAV_X) / NAV_STEP);
        if (x - FRAME_NAV_X - action * NAV_STEP >= 28)
            return;
        perform_action(app, MARCUS_FIRST_FRAME + action);
    }
    else if (ui_inside(x, y, PLAY_X, app->timeline_y + PLAY_Y, UI_PLAY_WIDTH, UI_PLAY_HEIGHT))
    {
        if (SDL_GetModState() & SDL_KMOD_SHIFT)
        {
            release_camera(app);
            app->dragging = app->resizing_view = app->resizing_timeline = 0;
            app->sheet_menu = 0;
            app->movie_only = app->movie_dirty = 1;
            movie_activity(app);
            SDL_SetCursor(SDL_GetDefaultCursor());
            if (!app->playing || app->playback.wait == 1)
                toggle_play(app);
        }
        else
            toggle_play(app);
    }
    else if (ui_inside(x, y, LOOP_X, app->timeline_y + FRAME_NAV_Y, 60, UI_NAVIGATION_HEIGHT))
        app->loop = !app->loop;
    else if (ui_inside(x, y, app->sidebar - 132, app->timeline_y + PLAY_Y, 32, UI_PLAY_HEIGHT))
        zoom_timeline(app, 0.8f, app->grid_x + app->grid_width / 2);
    else if (ui_inside(x, y, app->sidebar - 94, app->timeline_y + PLAY_Y, 32, UI_PLAY_HEIGHT))
        zoom_timeline(app, 1.25f, app->grid_x + app->grid_width / 2);
    else if (ui_inside(x, y, app->sidebar - 56, app->timeline_y + PLAY_Y, 40, UI_PLAY_HEIGHT))
        fit_timeline(app);
    else if (ui_inside(x, y, app->sidebar + 1, OBJECT_LIST_TOP, 282, (float)(object_list_rows(app) * ROW_HEIGHT)))
    {
        row = (int)((y - OBJECT_LIST_TOP) / ROW_HEIGHT) + app->object_scroll;
        if (row < app->info.object_count)
        {
            app->selected = row;
            show_object_tracks(app);
        }
    }
    else if (ui_inside(x, y, 0, app->timeline_y + 80, app->grid_x, app->height - app->timeline_y - 112))
    {
        select_timeline_row(app, x, y, 0);
    }
    else if (ui_inside(x, y, app->grid_x, app->timeline_y + 48, app->grid_width, app->height - app->timeline_y - 80))
    {
        app->playing = 0;
        app->dragging = 1;
        if (y >= app->timeline_y + 80)
        {
            if (select_timeline_row(app, x, y, 1))
                return;
        }
        seek(app, frame_at(app, x));
    }
    else if (inside_movie(app, x, y) || inside_work(app, x, y))
        pick_object(app, x, y);
}
static void key_down(App *app, SDL_Keycode key, SDL_Keymod modifiers)
{
    long step;
    int action;
    if (app->movie_only)
        movie_activity(app);
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
        shortcuts_modifiers(modifiers) == MARCUS_MOD_ALT)
    {
        set_fullscreen(app, !app->fullscreen);
        return;
    }
    if (app->settings_open)
    {
        settings_key(app, key, modifiers);
        return;
    }
    if (key == SDLK_ESCAPE && (app->movie_only || app->fullscreen))
    {
        editor_view(app);
        return;
    }
    if (app->movie_scrubbing)
    {
        if (key == SDLK_ESCAPE)
            finish_movie_scrub(app);
        return;
    }
    if (app->error[0])
    {
        if (key == SDLK_ESCAPE || key == SDLK_RETURN)
            app->error[0] = 0;
        return;
    }
    if (app->zip_input)
    {
        zip_key(app, key);
        return;
    }
    if (app->sheet_menu)
    {
        if (key == SDLK_ESCAPE || key == SDLK_TAB)
            app->sheet_menu = 0;
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
            set_sheet_mode(app, app->sheet_menu_choice);
        else if (key == SDLK_UP)
            app->sheet_menu_choice = (app->sheet_menu_choice + 2) % 3;
        else if (key == SDLK_DOWN)
            app->sheet_menu_choice = (app->sheet_menu_choice + 1) % 3;
        else if (key == SDLK_HOME)
            app->sheet_menu_choice = SHEET_OBJECTS;
        else if (key == SDLK_END)
            app->sheet_menu_choice = SHEET_BOTH;
        return;
    }
    if (key == SDLK_COMMA && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)))
    {
        if (app->movie_only)
            open_rendering_settings(app, 0);
        else
            open_settings(app);
        return;
    }
    action = shortcuts_match(&app->shortcuts, key, modifiers);
    if (action >= 0)
    {
        if (!app->movie_only || action <= MARCUS_PLAY)
            perform_action(app, action);
        return;
    }
    if (!app->loaded)
        return;
    step = (modifiers & SDL_KMOD_SHIFT) ? 10 : 1;
    switch (key)
    {
    case SDLK_LEFT:
        app->playing = 0;
        seek(app, (long)maximum((double)app->info.first_frame, (double)app->info.frame - step));
        break;
    case SDLK_RIGHT:
        app->playing = 0;
        seek(app, (long)minimum((double)app->info.last_frame, (double)app->info.frame + step));
        break;
    case SDLK_HOME:
        app->playing = 0;
        seek(app, app->info.first_frame);
        break;
    case SDLK_END:
        app->playing = 0;
        seek(app, app->info.last_frame);
        break;
    case SDLK_PAGEUP:
        app->playing = 0;
        select_scene(app, app->info.scene - 1);
        break;
    case SDLK_PAGEDOWN:
        app->playing = 0;
        select_scene(app, app->info.scene + 1);
        break;
    case SDLK_ESCAPE:
        if (app->flying || app->orbiting || app->panning || app->selected >= 0 ||
            app->selected_sound >= 0 || app->selected_sound_track >= 0)
        {
            app->selected = -1;
            app->selected_sound = app->selected_sound_track = -1;
            release_camera(app);
            break;
        }
        app->playing = 0;
        break;
    default:
        break;
    }
}
static int flight_key(SDL_Scancode code)
{
    switch (code)
    {
    case SDL_SCANCODE_W:
        return FLY_FORWARD;
    case SDL_SCANCODE_S:
        return FLY_BACK;
    case SDL_SCANCODE_A:
        return FLY_LEFT;
    case SDL_SCANCODE_D:
        return FLY_RIGHT;
    case SDL_SCANCODE_E:
        return FLY_UP;
    case SDL_SCANCODE_Q:
        return FLY_DOWN;
    case SDL_SCANCODE_LSHIFT:
        return FLY_FAST;
    case SDL_SCANCODE_RSHIFT:
        return FLY_FAST_RIGHT;
    default:
        return 0;
    }
}
static void update_flight(App *app, double seconds)
{
    double yaw, pitch, forward, side, up, dx, dy, dz, length, distance;
    if (!app->flying || !app->fly_ready || app->error[0])
        return;
    forward = ((app->fly_keys & FLY_FORWARD) != 0) - ((app->fly_keys & FLY_BACK) != 0);
    side = ((app->fly_keys & FLY_RIGHT) != 0) - ((app->fly_keys & FLY_LEFT) != 0);
    up = ((app->fly_keys & FLY_UP) != 0) - ((app->fly_keys & FLY_DOWN) != 0);
    yaw = app->flight.yaw * 0.01745329252;
    pitch = app->flight.pitch * 0.01745329252;
    dx = -sin(yaw) * cos(pitch) * forward + cos(yaw) * side;
    dy = -sin(pitch) * forward + up;
    dz = -cos(yaw) * cos(pitch) * forward - sin(yaw) * side;
    length = sqrt(dx * dx + dy * dy + dz * dz);
    if (length == 0)
        return;
    app->work_matched = 0;
    if (seconds > 0.1)
        seconds = 0.1;
    distance = app->flight.move_speed * seconds * ((app->fly_keys & (FLY_FAST | FLY_FAST_RIGHT)) ? 4 : 1) / length;
    app->flight.x = (float)minimum(32700, maximum(-32700, app->flight.x + dx * distance));
    app->flight.y = (float)minimum(32700, maximum(-32700, app->flight.y + dy * distance));
    app->flight.z = (float)minimum(32700, maximum(-32700, app->flight.z + dz * distance));
    app->dirty = 1;
}
static void handle_event(App *app, const SDL_Event *event)
{
    DialogRequest *request;
    SDL_FRect viewport, object_track, object_thumb;
    int rows, maximum_scroll, *scroll;
    float x, y, delta;
    SDL_Keymod modifiers;
    if (event->type == SDL_EVENT_QUIT)
        app->running = 0;
    else if (!app->error[0] && scene_organiser_event(app->scene_organiser, event, app->width, app->height))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
    else if (!app->error[0] && browser_event(app->browser, event))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }

    }
    else if (event->type == app->dialog_event)
    {
        request = (DialogRequest *)event->user.data1;
        if (request->kind == 3)
            fail(app, request->path);
        else if (request->kind == 0)
            open_movie(app, request->path);
        else
        {
            if (request->kind == 1)
                SDL_strlcpy(app->resources, request->path, sizeof(app->resources));
            else
                SDL_strlcpy(app->content, request->path, sizeof(app->content));
            if (app->movie[0])
                open_input(app, app->movie_input, app->movie_entry, app->movie);
        }
        SDL_free(request->path);
        SDL_free(request);
    }
    else if (event->type == SDL_EVENT_DROP_FILE && !app->settings_open)
    {
        if (app->movie_only)
            editor_view(app);
        {
            SDL_PathInfo dropped;
            if ((SDL_GetPathInfo(event->drop.data, &dropped) && dropped.type == SDL_PATHTYPE_DIRECTORY) ||
                (SDL_strlen(event->drop.data) >= 4 && !SDL_strcasecmp(event->drop.data + SDL_strlen(event->drop.data) - 4, ".zip")))
            {
                app->playing = 0;
                release_camera(app);
                browser_show(app->browser, event->drop.data, app->resources, app->content);
            }
            else
            {
                open_movie(app, event->drop.data);
                if (app->zip_input)
                {
                    browser_show(app->browser, app->zip_path, app->resources, app->content);
                    cancel_zip(app);
                }
            }
        }
    }
    else if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        if (app->flying && flight_key(event->key.scancode))
            app->fly_keys |= flight_key(event->key.scancode);
        else
            key_down(app, event->key.key, event->key.mod);
    }
    else if (event->type == SDL_EVENT_KEY_UP)
        app->fly_keys &= ~flight_key(event->key.scancode);
    else if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (app->movie_scrubbing)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
            movie_activity(app);
            seek_movie(app, movie_scrub_time(app, event->motion.x));
            SDL_SetCursor(SDL_GetDefaultCursor());
            return;
        }
        if (app->movie_only)
        {
            if (event->motion.x != app->ui.mouse_x || event->motion.y != app->ui.mouse_y)
                movie_activity(app);
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
            SDL_SetCursor(SDL_GetDefaultCursor());
            return;
        }
        if (!app->flying)
        {
            if (!app->orbiting)
            {
                app->ui.mouse_x = event->motion.x;
                app->ui.mouse_y = event->motion.y;
            }
            if (app->sheet_menu && ui_inside(event->motion.x, event->motion.y, 10, sheet_menu_top(app) + 2, 194, 84))
                app->sheet_menu_choice = (int)((event->motion.y - sheet_menu_top(app) - 2) / ROW_HEIGHT);
        }
        else
        {
            app->flight.yaw = (float)fmod(app->flight.yaw - event->motion.xrel * 0.15, 360);
            app->flight.pitch = (float)minimum(90, maximum(-90, app->flight.pitch + event->motion.yrel * 0.15));
            if (event->motion.xrel != 0 || event->motion.yrel != 0)
                app->work_matched = 0;
            app->dirty = 1;
        }
        if (app->resizing_objects)
        {
            app->object_split = event->motion.y - app->object_split_drag_offset;
            layout(app);
        }
        if (app->object_scroll_drag && object_scrollbar(app, &object_track, &object_thumb))
            scroll_objects(app, (int)floor(0.5 + (event->motion.y - app->object_drag_offset - object_track.y) *
                           (app->info.object_count - object_list_rows(app)) / (object_track.h - object_thumb.h)));
        if (app->resizing_timeline)
        {
            app->timeline_height = (float)maximum(TIMELINE_MIN_HEIGHT, minimum(app->height - VIEWPORT_MIN_BOTTOM,
                app->height - event->motion.y + app->timeline_drag_offset - TIMELINE_HANDLE_HEIGHT));
            layout(app);
        }
        if (app->resizing_view)
        {
            app->split_x = (float)maximum(330, minimum(view_right(app) - 330, event->motion.x));
            app->split_fraction = app->split_x / view_right(app);
        }
        if (!app->flying && !app->orbiting)
        {
            if (app->timeline_cursor && (app->resizing_objects ||
                ui_inside(event->motion.x, event->motion.y, app->sidebar, app->object_split - 4, 300, 8)))
                SDL_SetCursor(app->timeline_cursor);
            else if (app->timeline_cursor && (app->resizing_timeline ||
                ui_inside(event->motion.x, event->motion.y, 0, app->viewport_bottom,
                          app->sidebar, TIMELINE_HANDLE_HEIGHT)))
                SDL_SetCursor(app->timeline_cursor);
            else if (app->split_cursor && app->second_view &&
                     (app->resizing_view || ui_inside(event->motion.x, event->motion.y, app->split_x - 4,
                                                       89, 8, app->viewport_bottom - 89)))
                SDL_SetCursor(app->split_cursor);
            else
                SDL_SetCursor(SDL_GetDefaultCursor());
        }
        if (app->dragging)
            seek(app, frame_at(app, event->motion.x));
        if (app->orbiting)
        {
            if (event->motion.xrel != 0 || event->motion.yrel != 0) app->shortcuts.work_view.axis_view = 0;
            if (event->motion.xrel != 0 || event->motion.yrel != 0)
                app->work_matched = 0;
            app->yaw -= event->motion.xrel * 0.4f;
            app->pitch += event->motion.yrel * 0.4f;
            if (app->pitch > 90)
                app->pitch = 90;
            if (app->pitch < -90)
                app->pitch = -90;
            app->dirty = 1;
        }
        if (app->panning)
        {
            if (event->motion.xrel != 0 || event->motion.yrel != 0)
                app->work_matched = 0;
            viewport = viewport_rect(app, 0);
            if (!marcus_set_renderer(app->player, app->shortcuts.work_renderer) ||
                !marcus_set_render_options(app->player, &app->shortcuts.work_view) ||
                !marcus_pan_orbit(app->player, -event->motion.xrel / viewport.w,
                                  event->motion.yrel / viewport.h, app->yaw, app->pitch, app->zoom))
                fail(app, marcus_error(app->player));
            app->dirty = 1;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        mouse_down(app, event->button.x, event->button.y, event->button.button);
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event->button.button == SDL_BUTTON_RIGHT)
        {
            release_camera(app);
            app->orbiting = app->panning = 0;
        }
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            app->object_scroll_drag = app->resizing_objects = 0;
            if (app->movie_scrubbing)
            {
                seek_movie(app, movie_scrub_time(app, event->button.x));
                finish_movie_scrub(app);
            }
            app->dragging = 0;
            app->resizing_view = 0;
            app->resizing_timeline = 0;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && (app->movie_only || app->movie_scrubbing))
        movie_activity(app);
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && app->sheet_menu)
    {
        delta = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            delta = -delta;
        if (delta > 0)
            app->sheet_menu_choice = (app->sheet_menu_choice + 2) % 3;
        if (delta < 0)
            app->sheet_menu_choice = (app->sheet_menu_choice + 1) % 3;
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && app->zip_input && !app->error[0])
    {
        delta = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            delta = -delta;
        app->zip_scroll -= (int)delta;
        maximum_scroll = marcus_input_count(app->zip_input) - 10;
        if (maximum_scroll < 0)
            maximum_scroll = 0;
        if (app->zip_scroll < 0)
            app->zip_scroll = 0;
        if (app->zip_scroll > maximum_scroll)
            app->zip_scroll = maximum_scroll;
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && app->loaded && !app->error[0] && !app->settings_open &&
             !app->movie_only)
    {
        x = event->wheel.mouse_x;
        y = event->wheel.mouse_y;
        delta = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            delta = -delta;
        modifiers = SDL_GetModState();
        if (app->flying || (app->authored == 2 && inside_work(app, x, y)))
            app->flight.move_speed = (float)minimum(10000, maximum(0.1, app->flight.move_speed * pow(1.25, delta)));
        else if (x >= app->sidebar && y >= OBJECT_LIST_TOP && y < app->object_split - 4)
        {
            scroll_objects(app, app->object_scroll - (int)delta);
        }
        else if (x >= app->sidebar && y >= app->object_split && y < app->height - 24)
            app->properties_scroll = (int)maximum(0, minimum(properties_scroll_limit(app), app->properties_scroll - delta * 28));
        else if (y >= app->timeline_y && x < app->sidebar)
        {
            if (modifiers & SDL_KMOD_CTRL)
                zoom_timeline(app, delta > 0 ? 1.25f : 0.8f, x);
            else if ((modifiers & SDL_KMOD_SHIFT) || y < app->timeline_y + 80 || event->wheel.x != 0)
            {
                app->timeline_start -= (delta + event->wheel.x) * 48 / app->pixels_per_frame;
                if (app->timeline_start < app->info.first_frame)
                    app->timeline_start = app->info.first_frame;
                if (app->timeline_start > app->info.last_frame)
                    app->timeline_start = app->info.last_frame;
            }
            else
            {
                rows = (int)((app->height - app->timeline_y - 112) / ROW_HEIGHT);
                maximum_scroll = sheet_row_count(app) - rows;
                if (maximum_scroll < 0)
                    maximum_scroll = 0;
                scroll = sheet_scroll(app);
                *scroll = (int)maximum(0, minimum(maximum_scroll, *scroll - (int)delta));
            }
        }
        else if (!app->authored && inside_work(app, x, y))
        {
            if (delta != 0)
                app->work_matched = 0;
            app->zoom *= delta > 0 ? 1.1f : 0.9f;
            if (app->zoom > 10)
                app->zoom = 10;
            if (app->zoom < 0.1f)
                app->zoom = 0.1f;
            app->dirty = 1;
        }
    }
    else if (event->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || event->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
    {
        app->fullscreen = event->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        app->movie_dirty = app->dirty = 1;
    }
    else if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        finish_movie_scrub(app);
        app->sheet_menu = 0;
        release_camera(app);
        app->dragging = app->orbiting = app->panning = 0;
        app->resizing_view = 0;
        app->resizing_timeline = 0;
        if (app->shortcuts.pause_on_focus_loss)
            app->playing = 0;
    }
}
static void draw_header(App *app)
{
    MarcusUI *ui;
    const char *name, *slash;
    ui = &app->ui;
    ui_fill(ui, 0, 0, (float)app->width, 44, 0x292c31);
    ui_fill(ui, 16, 13, 4, 18, ACCENT);
    ui_heading(ui, 29, 13, "MARCUS", TEXT);
    ui_text(ui, 108, 13, "/  Movie player", MUTED);
    name = app->movie[0] ? app->movie : "Untitled session";
    slash = strrchr(name, '/');
    if (slash)
        name = slash + 1;
    slash = strrchr(name, '\\');
    if (slash)
        name = slash + 1;
    ui_label(ui, 300, 13, (float)app->width - 510, name, TEXT);
    ui_heading(ui, (float)app->width - 160, 13, "MOVIE PLAYBACK", ACCENT);
    ui_button(ui, 16, 52, 112, "Open movie", 0, 1);
    ui_button(ui, 136, 52, 104, "Resources...", 0, 1);
    ui_button(ui, 248, 52, 104, "Content...", 0, 1);
    ui_button(ui, 360, 52, 96, "Settings", app->settings_open, 1);
    ui_label(ui, 472, 58, app->sidebar - 488, "Drop a movie or ZIP here", MUTED);
    ui_line(ui, 0, 88, (float)app->width, 88, LINE);
}
static int update_picture(App *app, SDL_Texture **texture, const MarcusImage *image)
{
    float width, height;
    if (*texture)
    {
        if (!SDL_GetTextureSize(*texture, &width, &height))
            return 0;
        if (width != image->width || height != image->height)
        {
            SDL_DestroyTexture(*texture);
            *texture = NULL;
        }
    }
    if (!*texture)
    {
        *texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                     image->width, image->height);
        if (*texture)
        {
            SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(*texture, SDL_SCALEMODE_NEAREST);
        }
    }
    return *texture && SDL_UpdateTexture(*texture, NULL, image->rgba, image->pitch);
}
static void draw_selection(App *app, const MarcusImage *image, const SDL_FRect *destination)
{
    MarcusBounds bounds;
    MarcusCameraGuide box;
    const MarcusGuideLine *edge;
    int i;
    SDL_Rect clip;
    if (!app->loaded || app->selected < 0 || image->scene != app->info.scene || image->frame != app->info.frame ||
        (image->authored_camera && app->playback.transition))
        return;
    if (!marcus_object_bounds(app->player, app->selected, &bounds))
    {
        fail(app, marcus_error(app->player));
        return;
    }
    object_bounds_build(image, &bounds, &box);
    clip.x = (int)destination->x;
    clip.y = (int)destination->y;
    clip.w = (int)destination->w;
    clip.h = (int)destination->h;
    SDL_SetRenderClipRect(app->renderer, &clip);
    for (i = 0; i < box.line_count; ++i)
    {
        edge = &box.lines[i];
        ui_line(&app->ui, destination->x + edge->x1 * destination->w, destination->y + edge->y1 * destination->h,
                destination->x + edge->x2 * destination->w, destination->y + edge->y2 * destination->h, bounds.colour);
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
}
static void record_camera_render(App *app, int work, Uint64 started)
{
    CameraPerformance *performance;
    performance = &app->camera_performance[work];
    performance->pending_ms += (double)(SDL_GetTicksNS() - started) / 1000000.0;
    performance->pending_rendered = 1;
}
static void draw_movie_view(App *app)
{
    const MarcusRenderResolution *resolution;
    MarcusImage image;
    SDL_FRect destination;
    SDL_FRect settings;
    Uint64 started;
    if (app->movie_dirty)
    {
        app->movie_dirty = 0;
        resolution = marcus_render_resolution(app->shortcuts.render_resolution);
        started = SDL_GetTicksNS();
        if (!marcus_set_renderer(app->player, app->shortcuts.renderer) ||
            !marcus_set_render_options(app->player, &app->shortcuts.movie_view) ||
            !marcus_render(app->player, resolution->width, resolution->height, 1, 0, 0, 1, &image))
            fail(app, marcus_error(app->player));
        else
        {
            record_camera_render(app, 0, started);
            app->movie_image = image;
            app->movie_image.rgba = NULL;
            if (!update_picture(app, &app->movie_picture, &image))
                fail(app, SDL_GetError());
        }
    }
    destination = viewport_rect(app, 1);
    if (app->movie_only)
    {
        if (app->movie_picture)
            SDL_RenderTexture(app->renderer, app->movie_picture, NULL, &destination);
        return;
    }
    settings = camera_settings_rect(app, 1);
    ui_settings_button(&app->ui, settings.x, settings.y, settings.w, 0);
    if (ui_inside(app->ui.mouse_x, app->ui.mouse_y, settings.x, settings.y, settings.w, settings.h))
        app->ui.hint = "Movie camera rendering settings";
    ui_text(&app->ui, 52, 102, "Movie camera", MUTED);
    ui_fill(&app->ui, destination.x - 1, destination.y - 1, destination.w + 2, destination.h + 2, 0x111316);
    if (app->movie_picture)
    {
        SDL_RenderTexture(app->renderer, app->movie_picture, NULL, &destination);
        draw_selection(app, &app->movie_image, &destination);
    }
    ui_fill(&app->ui, app->split_x - 1, 89, 2, app->viewport_bottom - 89, LINE);
    ui_fill(&app->ui, app->split_x - 2, (app->viewport_bottom + 89) / 2 - 15, 4, 30, MUTED);
}
static void draw_view_strip(App *app)
{
    float x, y, tip, tail;
    int hovered;
    x = view_right(app);
    y = (89 + app->viewport_bottom) / 2;
    hovered = ui_inside(app->ui.mouse_x, app->ui.mouse_y, x, 89, VIEW_STRIP_WIDTH, app->viewport_bottom - 89);
    ui_fill(&app->ui, x, 89, VIEW_STRIP_WIDTH, app->viewport_bottom - 89, hovered ? 0x343c45 : PANEL);
    ui_line(&app->ui, x, 89, x, app->viewport_bottom, LINE);
    tip = x + (app->second_view ? 13 : 7);
    tail = x + (app->second_view ? 7 : 13);
    ui_line(&app->ui, tail, y - 6, tip, y, hovered ? ACCENT : TEXT);
    ui_line(&app->ui, tip, y, tail, y + 6, hovered ? ACCENT : TEXT);
    if (hovered)
        app->ui.hint = app->second_view ? "Collapse work camera" : "Expand work camera";
}
static int match_work_camera(App *app)
{
    const MarcusRenderResolution *resolution;
    MarcusCamera camera;
    float speed;
    speed = app->fly_ready && !app->work_needs_match ? app->flight.move_speed : 0;
    resolution = marcus_render_resolution(app->shortcuts.work_render_resolution);
    if (!marcus_match_camera(app->player, resolution->width, resolution->height, &camera))
    {
        fail(app, marcus_error(app->player));
        return 0;
    }
    app->flight = camera;
    if (speed > 0)
        app->flight.move_speed = speed;
    app->yaw = camera.yaw;
    app->pitch = camera.pitch;
    app->zoom = 1;
    app->fly_ready = 1;
    app->work_needs_match = 0;
    return 1;
}
static void draw_view(App *app)
{
    const MarcusRenderResolution *resolution;
    MarcusImage image;
    SDL_FRect destination, settings;
    char label[256];
    int rendered, render_width, render_height, work;
    float left, width, right;
    MarcusUI *ui;
    Uint64 started;
    ui = &app->ui;
    left = work_left(app);
    right = view_right(app);
    width = right - left;
    work = app->second_view || app->authored != 1;
    if (work && app->loaded && app->work_needs_match &&
        (app->shortcuts.work_view.orthographic || app->shortcuts.work_view.axis_view))
        match_work_camera(app);
    if (work && app->shortcuts.work_view.axis_view) app->authored = 0;
    if (work && (app->shortcuts.work_view.orthographic || app->shortcuts.work_view.axis_view ||
                 app->shortcuts.work_view.display_mode || app->shortcuts.work_view.wire_overlay))
        app->work_matched = app->work_needs_match = 0;
    resolution = marcus_render_resolution(work ? app->shortcuts.work_render_resolution : app->shortcuts.render_resolution);
    render_width = resolution->width;
    render_height = resolution->height;
    ui_fill(ui, 0, 89, app->sidebar, app->viewport_bottom - 89, BG);
    draw_view_strip(app);
    settings = camera_settings_rect(app, 0);
    ui_settings_button(ui, settings.x, settings.y, settings.w, 0);
    if (ui_inside(ui->mouse_x, ui->mouse_y, settings.x, settings.y, settings.w, settings.h))
        ui->hint = work ? "Work camera rendering settings" : "Movie camera rendering settings";
    ui_label(ui, left + 52, 102, width - (app->second_view ? 280 : 372),
             work ? "Work camera" : "Movie camera", MUTED);
    if (app->second_view)
    {
        ui_button(ui, right - 220, 96, 72, "Orbit", !app->authored, app->loaded);
        ui_button(ui, right - 140, 96, 60, "Fly", app->authored == 2, app->loaded);
        ui_button(ui, right - 72, 96, 56, "Reset", 0, app->loaded);
    }
    else
    {
        ui_button(ui, right - 312, 96, 84, "Camera", app->authored == 1, app->loaded);
        ui_button(ui, right - 220, 96, 72, "Orbit", !app->authored, app->loaded);
        ui_button(ui, right - 140, 96, 60, "Fly", app->authored == 2, app->loaded);
        ui_button(ui, right - 72, 96, 56, "Reset", 0, app->loaded);
    }
    if (!app->loaded)
    {
        ui_text(ui, app->sidebar / 2 - 100, 207, "A new view of your movies", TEXT);
        ui_text(ui, app->sidebar / 2 - 142, 239, "Open a movie to explore its scenes and actors.", MUTED);
        ui_button(ui, app->sidebar / 2 - 65, 280, 130, "Open a movie", 1, 1);
        ui_text(ui, 24, app->viewport_bottom - 30,
                "Select Resources and Content if your original 3DMM files are elsewhere.", MUTED);
        return;
    }
    if (app->dirty)
    {
        app->dirty = 0;
        started = SDL_GetTicksNS();
        if (!marcus_set_renderer(app->player, work ? app->shortcuts.work_renderer : app->shortcuts.renderer) ||
            !marcus_set_render_options(app->player, work ? &app->shortcuts.work_view : &app->shortcuts.movie_view))
            rendered = 0;
        else if (app->authored != 1 && (app->work_matched || app->work_needs_match))
        {
            /* Keep the original backdrop and composited captions identical to
             * the movie until navigation needs the reconstructed 3D scenery. */
            rendered = match_work_camera(app) &&
                marcus_render(app->player, render_width, render_height, 1, 0, 0, 1, &image);
        }
        else if (app->authored == 2)
        {
            rendered = 1;
            if (!app->fly_ready)
            {
                rendered = marcus_render(app->player, render_width, render_height, 0,
                                          app->yaw, app->pitch, app->zoom, &image);
                if (rendered)
                {
                    app->flight = image.camera;
                    app->fly_ready = 1;
                }
            }
            if (rendered)
                rendered = marcus_render_fly(app->player, render_width, render_height, &app->flight, &image);
        }
        else
            rendered =
                marcus_render(app->player, render_width, render_height, app->authored == 1,
                              app->yaw, app->pitch, app->zoom, &image);
        if (rendered)
        {
            record_camera_render(app, work, started);
            app->work_image = image;
            app->work_image.rgba = NULL;
            app->camera = image.camera;
            if (work && app->shortcuts.work_view.axis_view)
            {
                app->yaw = image.camera.yaw;
                app->pitch = image.camera.pitch;
            }
            app->projection = image.projection;
            app->scene_camera = image.scene_camera;
            app->camera_ready = 1;
            if (!update_picture(app, &app->picture, &image))
                fail(app, SDL_GetError());
        }
        else
        {
            SDL_DestroyTexture(app->picture);
            app->picture = NULL;
            fail(app, marcus_error(app->player));
        }
    }
    destination = viewport_rect(app, 0);
    ui_fill(ui, destination.x - 1, destination.y - 1, destination.w + 2, destination.h + 2, 0x111316);
    if (app->picture)
        SDL_RenderTexture(app->renderer, app->picture, NULL, &destination);
    memset(&app->camera_guide, 0, sizeof(app->camera_guide));
    if (app->authored != 1 && !app->work_matched && app->picture)
    {
        SDL_Rect clip;
        int line;
        float x, y;
        const MarcusGuideLine *segment;
        camera_guide_build(&app->projection, &app->scene_camera, &app->camera_guide);
        clip.x = (int)destination.x;
        clip.y = (int)destination.y;
        clip.w = (int)destination.w;
        clip.h = (int)destination.h;
        SDL_SetRenderClipRect(app->renderer, &clip);
        for (line = 0; line < app->camera_guide.line_count; ++line)
        {
            segment = &app->camera_guide.lines[line];
            ui_line(ui, destination.x + segment->x1 * destination.w, destination.y + segment->y1 * destination.h,
                    destination.x + segment->x2 * destination.w, destination.y + segment->y2 * destination.h, 0xefbd78);
        }
        if (app->camera_guide.has_label)
        {
            x = destination.x + app->camera_guide.label_x * destination.w;
            y = destination.y + app->camera_guide.label_y * destination.h;
            ui_fill(ui, x - 2, y - 2, 5, 5, 0xefbd78);
            SDL_snprintf(label, sizeof(label), "Camera %02lu", app->info.camera + 1);
            ui_text(ui, (float)maximum(destination.x + 4, minimum(x + 8, destination.x + destination.w - 100)),
                    (float)maximum(destination.y + 4, minimum(y - 22, destination.y + destination.h - 20)), label,
                    0xefbd78);
        }
        SDL_SetRenderClipRect(app->renderer, NULL);
    }
    if (app->picture)
        draw_selection(app, &app->work_image, &destination);
    if (app->flying)
    {
        ui_line(ui, left + width / 2 - 6, destination.y + destination.h / 2, left + width / 2 + 6,
                destination.y + destination.h / 2, ACCENT);
        ui_line(ui, left + width / 2, destination.y + destination.h / 2 - 6, left + width / 2,
                destination.y + destination.h / 2 + 6, ACCENT);
    }
    if (app->second_view)
    {
        draw_movie_view(app);
        if (inside_work(app, ui->mouse_x, ui->mouse_y))
            ui->hint = app->authored == 2 ? "Click to select / hold RMB + WASD to fly / wheel speed"
                                          : "Click to select / RMB orbit / Shift + RMB pan / wheel zoom";
        if (inside_movie(app, ui->mouse_x, ui->mouse_y))
            ui->hint = "Click an object to select it";
        if (app->resizing_view || ui_inside(ui->mouse_x, ui->mouse_y, app->split_x - 4, 89, 8,
                                           app->viewport_bottom - 89))
            ui->hint = "Drag to resize the movie and work camera views";
    }
}
static void property(App *app, float y, const char *label, const char *value)
{
    y += app->object_split - 326 - app->properties_scroll;
    ui_text(&app->ui, app->sidebar + 18, y, label, MUTED);
    ui_label(&app->ui, app->sidebar + 120, y, 162, value, TEXT);
}
static void draw_sound_properties(App *app, float offset)
{
    MarcusSound sound;
    MarcusSoundTrack track;
    MarcusWaveformInfo waveform;
    MarcusWaveformColumn peak;
    char label[192];
    ui_heading(&app->ui, app->sidebar + 18, offset + 340, "SOUND PROPERTIES", MUTED);
    if (!app->loaded || !marcus_sound(app->player, app->selected_sound, &sound))
    {
        if (app->loaded && marcus_sound_track(app->player, app->selected_sound_track, &track))
        {
            ui_label(&app->ui, app->sidebar + 18, offset + 377, 264, track.name, TEXT);
            SDL_snprintf(label, sizeof(label), "%d sound cues / clips", track.cue_count);
            ui_text(&app->ui, app->sidebar + 18, offset + 405, label, MUTED);
        }
        else
            ui_text(&app->ui, app->sidebar + 18, offset + 377, "Select a sound track", TEXT);
        ui_text(&app->ui, app->sidebar + 18, offset + 435, "Click a cue or waveform.", MUTED);
        ui_text(&app->ui, app->sidebar + 18, offset + 463, "Markers show authored events.", MUTED);
        ui_text(&app->ui, app->sidebar + 18, offset + 487, "Motion cues mark cel bindings.", MUTED);
        return;
    }
    ui_label(&app->ui, app->sidebar + 18, offset + 374, 264, sound.name, TEXT);
    ui_heading(&app->ui, app->sidebar + 18, offset + 400,
            sound.style == MARCUS_SOUND_MUSIC ? "MUSIC"
            : sound.style == MARCUS_SOUND_SPEECH ? "SPEECH" : "SOUND EFFECT", 0xe4b575);
    ui_line(&app->ui, app->sidebar + 18, offset + 435, (float)app->width - 18, offset + 435, LINE);
    property(app, 450, "Owner", sound.owner);
    if (sound.carried)
        SDL_snprintf(label, sizeof(label), "%d / frame %.0f", sound.origin_scene + 1, sound.origin_frame);
    else
        SDL_snprintf(label, sizeof(label), "%.0f", display_frame(app, sound.frame));
    property(app, 478, sound.carried ? "From scene" : "Cue frame", label);
    SDL_snprintf(label, sizeof(label), "%.1f%%", sound.volume * 100.0 / 65536.0);
    property(app, 506, "Volume", label);
    property(app, 534, "Loop", sound.loop ? "Yes" : "No");
    property(app, 562, "Queued", sound.queue ? "Yes" : "No");
    property(app, 590, "Silence", sound.silent ? "Yes" : "No");
    SDL_snprintf(label, sizeof(label), "Cel %ld (binding)", sound.motion_cel);
    property(app, 618, "Trigger", sound.motion ? label : "Frame cue");
    SDL_snprintf(label, sizeof(label), "%ld : %08lX", sound.source_id, sound.chunk);
    property(app, 646, "Sound", label);
    SDL_snprintf(label, sizeof(label), "%lu", sound.child);
    property(app, 674, "Child ID", label);
    if (marcus_sound_waveform(app->player, app->selected_sound, 0, 1, 1, &peak, &waveform))
    {
        if (waveform.kind == MARCUS_WAVEFORM_SAMPLED)
        {
            SDL_snprintf(label, sizeof(label), "%.3f s (one play)", waveform.seconds);
            property(app, 702, "Duration", label);
        }
        else
            ui_label(&app->ui, app->sidebar + 18, offset + 702, 264, waveform.message, MUTED);
    }
    ui_text(&app->ui, app->sidebar + 18, offset + 738,
            sound.carried ? "Continues from an earlier scene." : "Scrubbing is silent.", MUTED);
}
static void draw_object_properties(App *app, float offset)
{
    MarcusUI *ui;
    MarcusObject object;
    char label[192];
    ui = &app->ui;
    ui_heading(ui, app->sidebar + 18, offset + 340, "OBJECT PROPERTIES", MUTED);
    if (!app->loaded || app->selected < 0 || !marcus_object(app->player, app->selected, &object))
    {
        ui_text(ui, app->sidebar + 18, offset + 377, "Select an actor or prop", TEXT);
        ui_text(ui, app->sidebar + 18, offset + 401, "in the object list or timeline.", MUTED);
        return;
    }
    ui_label(ui, app->sidebar + 18, offset + 374, 264, object.name, TEXT);
    ui_heading(ui, app->sidebar + 18, offset + 400, object.text ? "3D TEXT" : object.prop ? "PROP" : "ACTOR", ACCENT);
    ui_line(ui, app->sidebar + 18, offset + 435, (float)app->width - 18, offset + 435, LINE);
    property(app, 450, "Visibility", object.visible ? "On stage" : "Off stage");
    ui_heading(ui, app->sidebar + 18, offset + 484, "WORLD POSITION", MUTED);
    SDL_snprintf(label, sizeof(label), "%.3f", (double)object.x);
    property(app, 511, "X", object.has_lifetime ? label : "--");
    SDL_snprintf(label, sizeof(label), "%.3f", (double)object.y);
    property(app, 537, "Y", object.has_lifetime ? label : "--");
    SDL_snprintf(label, sizeof(label), "%.3f", (double)object.z);
    property(app, 563, "Z", object.has_lifetime ? label : "--");
    ui_line(ui, app->sidebar + 18, offset + 597, (float)app->width - 18, offset + 597, LINE);
    SDL_snprintf(label, sizeof(label), "%ld", object.actor_id);
    property(app, 614, "Actor ID", label);
    SDL_snprintf(label, sizeof(label), "%ld / %ld", object.action, object.cel);
    property(app, 640, "Action / cel", object.has_lifetime ? label : "--");
    if (object.last_frame < app->info.first_frame || object.first_frame > app->info.last_frame)
        SDL_strlcpy(label, "Outside scene", sizeof(label));
    else
        SDL_snprintf(label, sizeof(label), "%.0f - %.0f",
                     display_frame(app, maximum(object.first_frame, app->info.first_frame)),
                     display_frame(app, minimum(object.last_frame, app->info.last_frame)));
    property(app, 666, "Lifetime", object.has_lifetime ? label : "No entrance");
    SDL_snprintf(label, sizeof(label), "%ld : %08lX", object.source_id, object.template_chunk);
    property(app, 692, "Template", label);
}
static void draw_sidebar(App *app)
{
    MarcusUI *ui;
    MarcusObject object;
    char label[192];
    int row, index, rows, clipped;
    float y, offset;
    SDL_FRect track, thumb;
    SDL_Rect clip, previous_clip;
    unsigned long tint;
    ui = &app->ui;
    ui_fill(ui, app->sidebar, 89, 300, (float)app->height - 113, PANEL);
    ui_line(ui, app->sidebar, 89, app->sidebar, (float)app->height - 24, LINE);
    ui_heading(ui, app->sidebar + 18, 103, "SCENE OBJECTS", MUTED);
    SDL_snprintf(label, sizeof(label), "%d", app->loaded ? app->info.object_count : 0);
    ui_text(ui, app->sidebar + 263, 103, label, MUTED);
    rows = object_list_rows(app);
    for (row = 0; row < rows && app->loaded; ++row)
    {
        index = row + app->object_scroll;
        if (!marcus_object(app->player, index, &object))
            break;
        y = OBJECT_LIST_TOP + (float)(row * ROW_HEIGHT);
        if (index == app->selected)
        {
            ui_fill(ui, app->sidebar + 1, y, 282, ROW_HEIGHT, 0x34464c);
            ui_fill(ui, app->sidebar + 1, y, 3, ROW_HEIGHT, ACCENT);
        }
        tint = object.text ? 0xd2b780 : object.prop ? 0xb3a4d6 : ACCENT;
        ui_fill(ui, app->sidebar + 19, y + 10, 7, 7, tint);
        ui_label(ui, app->sidebar + 36, y + 5, 220, object.name, object.visible ? TEXT : MUTED);
        if (object.visible)
            ui_fill(ui, app->sidebar + 268, y + 11, 4, 4, ACCENT);
    }
    if (app->loaded && app->info.object_count > rows)
    {
        SDL_snprintf(label, sizeof(label), "%d-%d of %d", app->object_scroll + 1,
                     app->object_scroll + rows < app->info.object_count ? app->object_scroll + rows : app->info.object_count,
                     app->info.object_count);
        ui_text(ui, app->sidebar + 18, app->object_split - 26, label, MUTED);
    }
    if (object_scrollbar(app, &track, &thumb))
    {
        ui_fill(ui, track.x, track.y, track.w, track.h, 0x16191e);
        ui_fill(ui, thumb.x, thumb.y, thumb.w, thumb.h,
                app->object_scroll_drag || ui_inside(ui->mouse_x, ui->mouse_y, thumb.x - 3, thumb.y,
                                                    thumb.w + 6, thumb.h) ? ACCENT : 0x606976);
        if (ui_inside(ui->mouse_x, ui->mouse_y, track.x - 3, track.y, track.w + 6, track.h))
            ui->hint = "Drag to scroll scene objects / click the track to move one page";
    }
    ui_fill(ui, app->sidebar + 1, app->object_split - 4, 299, 8,
            app->resizing_objects ? 0x3b4650 : LINE);
    ui_line(ui, app->sidebar + 142, app->object_split - 1, app->sidebar + 158, app->object_split - 1, MUTED);
    ui_line(ui, app->sidebar + 142, app->object_split + 1, app->sidebar + 158, app->object_split + 1, MUTED);
    if (app->resizing_objects || ui_inside(ui->mouse_x, ui->mouse_y, app->sidebar, app->object_split - 4, 300, 8))
        ui->hint = "Drag to resize Scene Objects and Properties";
    offset = app->object_split - 326 - app->properties_scroll;
    clip.x = (int)app->sidebar + 1;
    clip.y = (int)app->object_split + 5;
    clip.w = 299;
    clip.h = (int)maximum(0, app->height - 24 - clip.y);
    clipped = SDL_RenderClipEnabled(app->renderer);
    SDL_GetRenderClipRect(app->renderer, &previous_clip);
    SDL_SetRenderClipRect(app->renderer, &clip);
    if (app->sheet_mode == SHEET_SOUNDS || (app->sheet_mode == SHEET_BOTH && app->sound_inspector))
        draw_sound_properties(app, offset);
    else
        draw_object_properties(app, offset);
    SDL_SetRenderClipRect(app->renderer, clipped ? &previous_clip : NULL);
}
static void draw_sound_waveform(App *app, int index, const MarcusSound *sound, int row)
{
    MarcusWaveformColumn columns[4096];
    MarcusWaveformInfo waveform;
    int column, count;
    float start, right, center, amplitude, gain, top, bottom, column_width;
    double first, step;
    unsigned long tint;
    /* A carried clip can start before the visible scene. Keep its original
     * source offset, and clip at completion, replacement, or the scene edge. */
    start = frame_x(app, sound->timeline_frame);
    if (start > app->grid_x + app->grid_width)
        return;
    start = (float)maximum(ceil(start), app->grid_x);
    first = app->timeline_start + (start - app->grid_x) / app->pixels_per_frame;
    right = (float)minimum(app->grid_x + app->grid_width, frame_x(app, sound->end_frame));
    column_width = (float)maximum(1, (right - start) / 4096);
    step = column_width / app->pixels_per_frame;
    count = (int)minimum(4096, ceil((right - start) / column_width));
    if (count < 1 || !marcus_sound_waveform_frames(app->player, index, first, step, count, columns, &waveform))
        return;
    center = app->timeline_y + 94 + row * ROW_HEIGHT;
    if (waveform.kind == MARCUS_WAVEFORM_MIDI && sound->carried)
    {
        ui_fill(&app->ui, start, center - 9, right - start, 19, 0x343044);
        ui_line(&app->ui, start, center + 9, right, center + 9, 0xb3a4d6);
        ui_label(&app->ui, start + 8, center - 8, right - start - 16, "MIDI continues", 0xb3a4d6);
        return;
    }
    if (waveform.kind != MARCUS_WAVEFORM_SAMPLED)
        return;
    amplitude = 10;
    gain = (float)maximum(0, minimum(1, sound->volume / 65536.0));
    tint = index == app->selected_sound ? 0xf4ca8e : sound->motion ? 0xa28ac3 : 0xc79b5f;
    ui_fill(&app->ui, start, center - 11, right - start, 23,
            index == app->selected_sound ? 0x504332 : 0x38332c);
    ui_line(&app->ui, start, center, right, center, 0x66533c);
    for (column = 0; column < count && start + column * column_width < right; ++column)
    {
        top = center - (float)minimum(1, columns[column].maximum * gain) * amplitude;
        bottom = center - (float)maximum(-1, columns[column].minimum * gain) * amplitude;
        ui_fill(&app->ui, start + column * column_width, top,
                (float)minimum(column_width, right - start - column * column_width),
                (float)maximum(1, bottom - top), tint);
    }
    if (sound->loop)
        ui_line(&app->ui, start, center + 11, right, center + 11, tint);
    if (ui_inside(app->ui.mouse_x, app->ui.mouse_y, start, center - 11, right - start, 23))
        app->ui.hint = "Click waveform to select and scrub / repeat to cycle overlapping sounds";
}
static void draw_animation_row(App *app, int object, int row, int lane)
{
    MarcusAnimationInfo info;
    MarcusActionSpan span;
    MarcusAnimationEvent event;
    MarcusObject owner;
    char label[240];
    int index, hovered;
    float left, right, center, marker_y, y;
    unsigned long tint;
    if (!marcus_animation_info(app->player, object, &info))
    {
        if (!lane)
            ui_label(&app->ui, app->grid_x + 8, app->timeline_y + 85 + row * ROW_HEIGHT, app->grid_width - 16,
                     "Animation events unavailable", MUTED);
        if (ui_inside(app->ui.mouse_x, app->ui.mouse_y, app->grid_x, app->timeline_y + 80 + row * ROW_HEIGHT,
                      app->grid_width, ROW_HEIGHT))
        {
            SDL_snprintf(app->animation_hint, sizeof(app->animation_hint), "Animation events unavailable: %s",
                         marcus_error(app->player));
            app->ui.hint = app->animation_hint;
        }
        return;
    }
    if (!marcus_object(app->player, object, &owner))
        return;
    y = app->timeline_y + 80 + row * ROW_HEIGHT;
    tint = owner.text ? 0x7f704b : owner.prop ? 0x67597f : 0x3d7372;
    if (!lane || lane == 4)
        for (index = 0; index < info.span_count; ++index)
        {
            if (!marcus_action_span(app->player, object, index, &span))
                continue;
            left = frame_x(app, span.first_frame);
            right = frame_x(app, span.end_frame);
            if (right < app->grid_x || left > app->grid_x + app->grid_width || right <= left)
                continue;
            left = (float)maximum(left, app->grid_x);
            right = (float)minimum(right, app->grid_x + app->grid_width);
            if (right - left <= 1)
                continue;
            ui_fill(&app->ui, left + 1, y + (lane ? 12 : 7), right - left - 1, lane ? 5 : 16,
                    span.frozen ? 0x4d526b : tint);
            if (lane)
                continue;
            ui_line(&app->ui, left + 1, y + 7, right - 1, y + 7, object == app->selected ? ACCENT : 0x819098);
            if (right - left > 36)
            {
                SDL_snprintf(label, sizeof(label), "%s%s", span.name, span.frozen ? " (frozen)" : "");
                ui_label(&app->ui, left + 5, y + 7, right - left - 9, label, TEXT);
            }
            if (ui_inside(app->ui.mouse_x, app->ui.mouse_y, left, y + 5, right - left, 20))
            {
                SDL_snprintf(app->animation_hint, sizeof(app->animation_hint),
                             "%s / %s / Frames %.0f-%.0f%s / click to scrub", owner.name, span.name,
                             display_frame(app, span.first_frame), display_frame(app, span.end_frame - 1),
                             span.frozen ? " / animation frozen" : "");
                app->ui.hint = app->animation_hint;
            }
        }
    if (!lane)
        return;
    hovered = animation_event_at(app, object, lane, row, app->ui.mouse_x, app->ui.mouse_y);
    tint = lane == 1 ? 0x8ab3df : lane == 2 ? 0xb3a4d6 : lane == 3 ? 0xe4b575 : ACCENT;
    for (index = 0; index < info.event_count; ++index)
    {
        if (!marcus_animation_event(app->player, object, index, &event) || animation_lane(event.kind) != lane)
            continue;
        center = frame_x(app, event.frame + 0.5);
        if (center < app->grid_x || center > app->grid_x + app->grid_width)
            continue;
        marker_y = y + 8 + (index % 3) * 6;
        ui_line(&app->ui, center - 3, marker_y, center, marker_y - 3, index == hovered ? TEXT : tint);
        ui_line(&app->ui, center, marker_y - 3, center + 3, marker_y, index == hovered ? TEXT : tint);
        ui_line(&app->ui, center + 3, marker_y, center, marker_y + 3, index == hovered ? TEXT : tint);
        ui_line(&app->ui, center, marker_y + 3, center - 3, marker_y, index == hovered ? TEXT : tint);
        if (index == hovered)
        {
            SDL_snprintf(app->animation_hint, sizeof(app->animation_hint), "%s / Frame %.0f / %s%s%s / click to seek",
                         owner.name, display_frame(app, event.frame), event.name, event.detail[0] ? ": " : "",
                         event.detail);
            app->ui.hint = app->animation_hint;
        }
    }
}
static void format_time(char *text, size_t size, double seconds, int hours)
{
    seconds = floor(maximum(0, seconds));
    if (hours)
        SDL_snprintf(text, size, "%.0f:%02.0f:%02.0f", floor(seconds / 3600),
                     floor(fmod(seconds, 3600) / 60), fmod(seconds, 60));
    else
        SDL_snprintf(text, size, "%.0f:%02.0f", floor(seconds / 60), fmod(seconds, 60));
}
static void draw_movie_scrubber(App *app, int available)
{
    SDL_FRect rect;
    MarcusSceneTime scene_time;
    MarcusUI *ui;
    char label[48];
    double seconds, fraction;
    float center, x, previous_tick;
    int index, hovered;
    ui = &app->ui;
    rect = movie_scrubber_rect(app);
    center = rect.y + rect.h / 2;
    seconds = app->movie_scrubbing ? app->movie_scrub_seconds : app->movie_time.position_seconds;
    fraction = available && app->movie_time.duration_seconds > 0 ? seconds / app->movie_time.duration_seconds : 0;
    if (!app->movie_only)
        ui_fill(ui, rect.x, rect.y, rect.w, rect.h, BG);
    ui_fill(ui, rect.x, center - 5, rect.w, 10, 0x151619);
    ui_line(ui, rect.x, center - 5, rect.x + rect.w, center - 5, 0x08090b);
    ui_line(ui, rect.x, center + 5, rect.x + rect.w, center + 5, 0x686b70);
    previous_tick = rect.x - 4;
    if (available && app->movie_time.duration_seconds > 0)
        for (index = 0; index < app->info.scene_count; ++index)
            if (marcus_scene_time(app->player, index, &scene_time))
            {
                x = rect.x + (float)(rect.w * scene_time.start_seconds / app->movie_time.duration_seconds);
                /* Dense chapter ticks must not obscure the track or thumb. */
                if (x - previous_tick >= 4 || index == app->info.scene)
                {
                    ui_line(ui, x, center - 3, x, center + 3,
                            index == app->info.scene ? 0x95a88e : 0x455348);
                    previous_tick = x;
                }
            }
    x = rect.x + (float)(rect.w * fraction);
    hovered = available && ui_inside(ui->mouse_x, ui->mouse_y, rect.x, rect.y, rect.w, rect.h);
    ui_slider_thumb(ui, (float)maximum(rect.x, minimum(rect.x + rect.w - 10, x - 5)), center - 10,
                    1, available, hovered || app->movie_scrubbing);
    if (hovered || app->movie_scrubbing)
    {
        ui->hint = "Movie scrubber: drag to seek across scenes; ticks mark scene boundaries";
        format_time(label, sizeof(label), movie_scrub_time(app, ui->mouse_x),
                    app->movie_time.duration_seconds >= 3600);
        x = (float)maximum(rect.x, minimum(rect.x + rect.w - 88, ui->mouse_x - 36));
        ui_fill(ui, x - 8, rect.y - 29, 96, 24, 0x27343d);
        ui_label(ui, x, rect.y - 26, 80, label, TEXT);
    }
}
static void draw_timeline(App *app)
{
    MarcusUI *ui;
    MarcusObject object;
    MarcusScene scene;
    MarcusSound sound;
    MarcusSoundTrack track;
    SDL_Rect clip;
    char label[320];
    char position[48], duration[48];
    int row, index, rows, selected_row, lane, kind, scroll, active, movie_available;
    float x, y, left;
    double frame, tick, end;
    unsigned long tint;
    ui = &app->ui;
    y = app->timeline_y;
    ui_fill(ui, 0, y - TIMELINE_HANDLE_HEIGHT, app->sidebar, TIMELINE_HANDLE_HEIGHT,
            app->resizing_timeline || ui_inside(ui->mouse_x, ui->mouse_y, 0, app->viewport_bottom,
                                                app->sidebar, TIMELINE_HANDLE_HEIGHT) ? 0x3b4650 : PANEL);
    ui_line(ui, app->sidebar / 2 - 22, y - 5, app->sidebar / 2 + 22, y - 5, MUTED);
    ui_line(ui, app->sidebar / 2 - 22, y - 3, app->sidebar / 2 + 22, y - 3, MUTED);
    if (app->resizing_timeline || ui_inside(ui->mouse_x, ui->mouse_y, 0, app->viewport_bottom,
                                          app->sidebar, TIMELINE_HANDLE_HEIGHT))
        ui->hint = "Drag up or down to resize the dope sheets";
    ui_fill(ui, 0, y, app->sidebar, (float)app->height - y - 24, PANEL);
    ui_line(ui, 0, y, app->sidebar, y, LINE);
    ui_frame_button(ui, FRAME_NAV_X, y + FRAME_NAV_Y, 0, app->loaded && app->info.frame > app->info.first_frame,
                         "First frame");
    ui_frame_button(ui, FRAME_NAV_X + NAV_STEP, y + FRAME_NAV_Y, 1,
                         app->loaded && app->info.frame > app->info.first_frame, "Previous frame");
    ui_frame_button(ui, FRAME_NAV_X + NAV_STEP * 2, y + FRAME_NAV_Y, 2,
                         app->loaded && app->info.frame < app->info.last_frame, "Next frame");
    ui_frame_button(ui, FRAME_NAV_X + NAV_STEP * 3, y + FRAME_NAV_Y, 3,
                         app->loaded && app->info.frame < app->info.last_frame, "Last frame");
    ui_play_button(ui, PLAY_X, y + PLAY_Y, app->playing, app->playback.wait == 1, app->loaded);
    if (app->loaded && ui_inside(ui->mouse_x, ui->mouse_y, PLAY_X, y + PLAY_Y, UI_PLAY_WIDTH, UI_PLAY_HEIGHT))
        ui->hint = "Play / pause; Shift+click to play with the movie filling the window; Esc returns to the editor";
    ui_button_sized(ui, LOOP_X, y + FRAME_NAV_Y, 60, UI_NAVIGATION_HEIGHT, "Loop", app->loop, app->loaded);
    if (ui_inside(ui->mouse_x, ui->mouse_y, LOOP_X, y + FRAME_NAV_Y, 60, UI_NAVIGATION_HEIGHT))
        ui->hint = "Repeat the current scene from its first frame";
    ui_button_sized(ui, LOOP_X, y + SCENE_NAV_Y, 60, UI_NAVIGATION_HEIGHT,
                    app->muted ? "Unmute" : "Mute", app->muted, 1);
    movie_available = app->movie_time_available;
    if (movie_available)
    {
        format_time(position, sizeof(position), app->movie_scrubbing ? app->movie_scrub_seconds : app->movie_time.position_seconds,
                    app->movie_time.duration_seconds >= 3600);
        format_time(duration, sizeof(duration), app->movie_time.duration_seconds,
                    app->movie_time.duration_seconds >= 3600);
        SDL_snprintf(label, sizeof(label), "%s / %s", position, duration);
    }
    else
        SDL_strlcpy(label, app->loaded && !app->movie_time_status ? "Calculating..." : "--:-- / --:--", sizeof(label));
    ui_label(ui, LOOP_X + 72, y + FRAME_NAV_Y + 1, 112, label,
             app->movie_time_available ? TEXT : MUTED);
    if (ui_inside(ui->mouse_x, ui->mouse_y, LOOP_X + 72, y + FRAME_NAV_Y,
                  112, UI_NAVIGATION_HEIGHT))
        ui->hint = !app->movie_time_status ? "Calculating duration; you can use the current scene"
                   : app->movie_time_status < 0 && app->loaded ? "Duration unavailable; scene playback is still available"
                   : "Movie position / total duration; time waiting for a click is excluded";
    ui_frame_button(ui, SCENE_NAV_X, y + SCENE_NAV_Y, 0, app->loaded && app->info.scene > 0, "First scene");
    ui_frame_button(ui, SCENE_NAV_X + NAV_STEP, y + SCENE_NAV_Y, 1, app->loaded && app->info.scene > 0,
                         "Previous scene");
    ui_frame_button(ui, SCENE_NAV_X + NAV_STEP * 2, y + SCENE_NAV_Y, 2,
                         app->loaded && app->info.scene + 1 < app->info.scene_count, "Next scene");
    ui_frame_button(ui, SCENE_NAV_X + NAV_STEP * 3, y + SCENE_NAV_Y, 3,
                         app->loaded && app->info.scene + 1 < app->info.scene_count, "Last scene");
    if (app->loaded)
    {
        SDL_snprintf(label, sizeof(label), "%.0f / %.0f", display_frame(app, app->info.frame),
                     display_frame(app, app->info.last_frame));
        ui_transport_readout(ui, 16, y + FRAME_NAV_Y, 0, label, 1);
        if (ui_inside(ui->mouse_x, ui->mouse_y, 16, y + FRAME_NAV_Y, 164, UI_NAVIGATION_HEIGHT))
            ui->hint = "Open frame organiser";
        SDL_snprintf(label, sizeof(label), "%d / %d", app->info.scene + 1, app->info.scene_count);
        ui_transport_readout(ui, 16, y + SCENE_NAV_Y, 1, label, 1);
        if (ui_inside(ui->mouse_x, ui->mouse_y, 16, y + SCENE_NAV_Y, 164, UI_NAVIGATION_HEIGHT))
            ui->hint = "Open scene organiser";
        if (marcus_scene(app->player, app->info.scene, &scene))
            ui_label(ui, LOOP_X + 72, y + SCENE_NAV_Y + 1, 112, scene.name, TEXT);
    }
    else
    {
        ui_transport_readout(ui, 16, y + FRAME_NAV_Y, 0, "- / -", 0);
        ui_transport_readout(ui, 16, y + SCENE_NAV_Y, 1, "- / -", 0);
    }
    ui_button_sized(ui, app->sidebar - 132, y + PLAY_Y, 32, UI_PLAY_HEIGHT, "-", 0, app->loaded);
    ui_button_sized(ui, app->sidebar - 94, y + PLAY_Y, 32, UI_PLAY_HEIGHT, "+", 0, app->loaded);
    ui_button_sized(ui, app->sidebar - 56, y + PLAY_Y, 40, UI_PLAY_HEIGHT, "Fit", 0, app->loaded);
    draw_movie_scrubber(app, movie_available);
    ui_fill(ui, 0, y + 48, app->sidebar, 32, 0x2c2f35);
    ui_button(ui, 10, y + 50, 194, sheet_name(app->sheet_mode), app->sheet_menu, app->loaded);
    ui_line(ui, 185, y + 62, 190, y + 67, app->loaded ? TEXT : MUTED);
    ui_line(ui, 190, y + 67, 195, y + 62, app->loaded ? TEXT : MUTED);
    if (ui_inside(ui->mouse_x, ui->mouse_y, 10, y + 50, 194, 28))
        ui->hint = "Choose Objects, Sounds, or Both for the dope sheet";
    rows = (int)((app->height - y - 112) / ROW_HEIGHT);
    scroll = *sheet_scroll(app);
    for (row = 0; row < rows; ++row)
    {
        kind = sheet_row(app, row + scroll, &index, &lane);
        selected_row = kind == SHEET_SOUND_ROW ? app->selected_sound_track : app->selected;
        active = app->sheet_mode != SHEET_BOTH || app->sound_inspector == (kind == SHEET_SOUND_ROW);
        ui_fill(ui, 0, y + 80 + row * ROW_HEIGHT, app->sidebar, ROW_HEIGHT,
                app->loaded && index >= 0 && index == selected_row && active ? 0x303f45
                : row % 2                                                    ? 0x23262b
                                                                             : 0x272a30);
        ui_line(ui, 0, y + 80 + (row + 1) * ROW_HEIGHT, app->sidebar, y + 80 + (row + 1) * ROW_HEIGHT, 0x30333a);
        if (!app->loaded)
            continue;
        if (kind == SHEET_OBJECT_HEADER || kind == SHEET_SOUND_HEADER)
        {
            ui_fill(ui, 0, y + 80 + row * ROW_HEIGHT, app->sidebar, ROW_HEIGHT, 0x2c3239);
            SDL_snprintf(label, sizeof(label), "%s  %d", kind == SHEET_OBJECT_HEADER ? "OBJECTS" : "SOUNDS",
                         kind == SHEET_OBJECT_HEADER ? app->info.object_count : app->info.sound_track_count);
            ui_heading(ui, 18, y + 85 + row * ROW_HEIGHT, label, MUTED);
            if (kind == SHEET_SOUND_HEADER && !app->info.sound_track_count)
                ui_text(ui, app->grid_x + 18, y + 85 + row * ROW_HEIGHT, "No sound tracks in this scene.", MUTED);
            continue;
        }
        if (!kind)
            continue;
        if (kind == SHEET_SOUND_ROW)
        {
            if (marcus_sound_track(app->player, index, &track))
            {
                ui_fill(ui, 18, y + 90 + row * ROW_HEIGHT, 7, 7, 0xe4b575);
                ui_label(ui, 35, y + 85 + row * ROW_HEIGHT, app->grid_x - 54, track.name, TEXT);
            }
            continue;
        }
        if (!marcus_object(app->player, index, &object))
            continue;
        if (lane)
        {
            ui_text(ui, 46, y + 85 + row * ROW_HEIGHT,
                    lane == 1   ? "Transform"
                    : lane == 2 ? "Appearance"
                    : lane == 3 ? "Timing"
                                : "Visibility",
                    MUTED);
            continue;
        }
        tint = object.text ? 0xd2b780 : object.prop ? 0xb3a4d6 : ACCENT;
        ui_text(ui, 8, y + 85 + row * ROW_HEIGHT, app->animation_expanded && index == app->selected ? "v" : ">", MUTED);
        ui_fill(ui, 30, y + 90 + row * ROW_HEIGHT, 7, 7, tint);
        ui_label(ui, 46, y + 85 + row * ROW_HEIGHT, app->grid_x - 64, object.name, object.visible ? TEXT : MUTED);
        if (ui_inside(ui->mouse_x, ui->mouse_y, 0, y + 80 + row * ROW_HEIGHT, 28, ROW_HEIGHT))
            ui->hint = "Click arrow to expand or collapse this actor's animation events";
    }
    if (app->loaded && app->sheet_mode == SHEET_SOUNDS && !app->info.sound_count)
        ui_text(ui, app->grid_x + 18, y + 94, "No authored sound cues in this scene.", MUTED);
    clip.x = (int)app->grid_x;
    clip.y = (int)y + 48;
    clip.w = (int)app->grid_width;
    clip.h = app->height - (int)y - 80;
    SDL_SetRenderClipRect(app->renderer, &clip);
    if (app->loaded)
    {
        tick = 1;
        while (tick * app->pixels_per_frame < 55)
            tick *= 2;
        end = app->timeline_start + app->grid_width / app->pixels_per_frame;
        for (frame = app->info.first_frame + floor((app->timeline_start - app->info.first_frame) / tick) * tick;
             frame <= end; frame += tick)
        {
            x = frame_x(app, frame);
            ui_line(ui, x, y + 48, x, (float)app->height - 32, LINE);
            if (frame >= app->info.first_frame && frame <= app->info.last_frame)
            {
                SDL_snprintf(label, sizeof(label), "%.0f", display_frame(app, frame));
                ui_text(ui, x + 5, y + 56, label, MUTED);
            }
        }
        if (app->pixels_per_frame >= 5)
            for (frame = floor(app->timeline_start); frame <= end; frame += 1)
            {
                x = frame_x(app, frame);
                ui_line(ui, x, y + 75, x, y + 80, 0x59606a);
            }
        for (row = 0; row < rows; ++row)
        {
            if (sheet_row(app, row + scroll, &index, &lane) != SHEET_OBJECT_ROW)
                continue;
            if (index >= app->info.object_count || !marcus_object(app->player, index, &object))
                continue;
            draw_animation_row(app, index, row, lane);
        }
        if (app->sheet_mode != SHEET_OBJECTS)
        {
            /* Draw the selected waveform last so it remains visible in overlaps. */
            for (index = 0; index < app->info.sound_count; ++index)
            {
                if (!marcus_sound(app->player, index, &sound))
                    continue;
                row = sound_sheet_row(app, sound.track);
                if (row >= 0 && row < rows && index != app->selected_sound)
                    draw_sound_waveform(app, index, &sound, row);
            }
            if (marcus_sound(app->player, app->selected_sound, &sound))
            {
                row = sound_sheet_row(app, sound.track);
                if (row >= 0 && row < rows)
                    draw_sound_waveform(app, app->selected_sound, &sound, row);
            }
            for (index = 0; index < app->info.sound_count; ++index)
            {
                if (!marcus_sound(app->player, index, &sound))
                    continue;
                row = sound_sheet_row(app, sound.track);
                if (row < 0 || row >= rows || sound.frame < app->info.first_frame ||
                    sound.frame > app->info.last_frame || !sound_has_marker(app, index, &sound))
                    continue;
                x = frame_x(app, sound.frame + 0.5);
                left = y + 87 + row * ROW_HEIGHT + (index % 3) * 5;
                tint = index == app->selected_sound ? TEXT : sound.carried ? ACCENT
                       : sound.silent ? MUTED : sound.motion ? 0xb3a4d6 : 0xe4b575;
                ui_line(ui, x, y + 82 + row * ROW_HEIGHT, x, y + 106 + row * ROW_HEIGHT, 0x635747);
                if (sound.carried)
                {
                    ui_line(ui, x - 4, left - 4, x, left, tint);
                    ui_line(ui, x, left, x - 4, left + 4, tint);
                    ui_line(ui, x - 4, left, x + 4, left, tint);
                }
                else if (sound.motion)
                {
                    ui_line(ui, x - 4, left, x, left - 4, tint);
                    ui_line(ui, x, left - 4, x + 4, left, tint);
                    ui_line(ui, x + 4, left, x, left + 4, tint);
                    ui_line(ui, x, left + 4, x - 4, left, tint);
                }
                if (x >= app->grid_x && x < app->grid_x + app->grid_width &&
                    ui_inside(ui->mouse_x, ui->mouse_y, x - 6, left - 4, 12, 8))
                    ui->hint = sound.carried ? "Continues from an earlier scene / click to inspect its source"
                               : "Click cue to inspect (repeat to cycle overlaps) / diamond: motion binding";
            }
        }
        /* The boundary follows the whole final frame, matching clip extents.
         * Keep the line inside the ruler when Fit puts it at the right edge. */
        x = frame_x(app, (double)app->info.last_frame + 1);
        if (x >= app->grid_x && x <= app->grid_x + app->grid_width + 0.5f)
        {
            x = (float)minimum(x, app->grid_x + app->grid_width - 2);
            ui_fill(ui, x, y + 48, 2, (float)app->height - y - 80, 0x9c9cff);
            if (ui_inside(ui->mouse_x, ui->mouse_y, x - 5, y + 48, 12, (float)app->height - y - 80))
                ui->hint = "End of scene / after the final frame";
        }
        x = frame_x(app, app->info.frame + 0.5);
        ui_line(ui, x, y + 48, x, (float)app->height - 32, 0x6d77d0);
        ui_slider_thumb(ui, x - 5, y + 48, 0, 1,
                        app->dragging || ui_inside(ui->mouse_x, ui->mouse_y, x - 5, y + 48, 10, 20));
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
    ui_line(ui, app->grid_x, y + 48, app->grid_x, (float)app->height - 32, LINE);
    ui_line(ui, app->sidebar, y, app->sidebar, (float)app->height - 24, LINE);
}
static void draw_sheet_menu(App *app)
{
    float top;
    int choice;
    if (!app->sheet_menu || !app->loaded)
        return;
    top = sheet_menu_top(app);
    ui_fill(&app->ui, 8, top - 2, 200, 92, 0x121519);
    ui_fill(&app->ui, 10, top, 194, 88, LINE);
    ui_fill(&app->ui, 11, top + 1, 192, 86, PANEL);
    for (choice = 0; choice < 3; ++choice)
    {
        if (choice == app->sheet_menu_choice)
            ui_fill(&app->ui, 12, top + 2 + choice * ROW_HEIGHT, 190, ROW_HEIGHT, 0x34464c);
        if (choice == app->sheet_mode)
            ui_text(&app->ui, 18, top + 7 + choice * ROW_HEIGHT, "*", ACCENT);
        ui_text(&app->ui, 36, top + 7 + choice * ROW_HEIGHT, sheet_name(choice), TEXT);
    }
    app->ui.hint = "Choose a dope sheet mode / Up, Down, Enter / Escape to cancel";
}
static void draw_error(App *app)
{
    char part[82];
    const char *p;
    size_t length;
    float x, y;
    int row;
    x = app->width / 2.0f - 320;
    y = app->height / 2.0f - 125;
    ui_fill(&app->ui, x - 1, y - 1, 642, 252, 0x606670);
    ui_fill(&app->ui, x, y, 640, 250, PANEL);
    ui_text(&app->ui, x + 24, y + 18, "Unable to display this movie", 0xf2ae9b);
    p = app->error;
    for (row = 0; row < 6 && *p; ++row)
    {
        length = strlen(p);
        if (length > 76)
            length = 76;
        memcpy(part, p, length);
        part[length] = 0;
        ui_text(&app->ui, x + 24, y + 56 + row * 21, part, TEXT);
        p += length;
    }
    ui_button(&app->ui, x + 270, y + 211, 100, "Dismiss", 1, 1);
}
static void draw_debug_panel(App *app, float x, float y, int camera, int frozen);
static void draw_rendering_settings(App *app)
{
    static const char *names[MARCUS_RENDERER_COUNT] = {"BRender CPU", "BRender compute", "SDL GPU"};
    static const char *aa[3] = {"Off", "2x SSAA", "4x SSAA"};
    static const char *axes[4] = {"Free", "Front", "Side", "Top"};
    float x, y;
    int renderer, resolution, edited_renderer, edited_resolution, i, gpu, overlay;
    const MarcusRenderOptions *view;
    edited_renderer = app->settings_work_camera ? app->edited_shortcuts.work_renderer : app->edited_shortcuts.renderer;
    edited_resolution = app->settings_work_camera ? app->edited_shortcuts.work_render_resolution :
                                                  app->edited_shortcuts.render_resolution;
    view = app->settings_work_camera ? &app->edited_shortcuts.work_view : &app->edited_shortcuts.movie_view;
    gpu = edited_renderer != MARCUS_RENDERER_BRENDER;
    x = (app->width - RENDER_SETTINGS_WIDTH) / 2.0f;
    y = (app->height - RENDER_SETTINGS_HEIGHT) / 2.0f;
    ui_opacity(&app->ui, 0.45f);
    ui_fill(&app->ui, 0, 0, (float)app->width, (float)app->height, 0);
    ui_opacity(&app->ui, 1);
    ui_fill(&app->ui, x - 1, y - 1, RENDER_SETTINGS_WIDTH + 2, RENDER_SETTINGS_HEIGHT + 2, LINE);
    ui_fill(&app->ui, x, y, RENDER_SETTINGS_WIDTH, RENDER_SETTINGS_HEIGHT, PANEL);
    ui_text(&app->ui, x + 24, y + 20,
            app->settings_work_camera ? "Work camera settings" : "Movie camera settings", TEXT);
    ui_button(&app->ui, x + RENDER_SETTINGS_WIDTH - 42, y + 16, 28, "x", 0, 1);
    ui_heading(&app->ui, x + 24, y + 56, "RENDERER", ACCENT);
    for (renderer = 0; renderer < MARCUS_RENDERER_COUNT; ++renderer)
        ui_button_sized(&app->ui, x + 24 + renderer * 144, y + 86, 136, 32, names[renderer],
                        edited_renderer == renderer, 1);
    ui_heading(&app->ui, x + 24, y + 134, "RENDER RESOLUTION (1x = 3DMM)", ACCENT);
    for (resolution = 0; resolution < MARCUS_RESOLUTION_COUNT; ++resolution)
        ui_button_sized(&app->ui, x + 24 + (resolution % RESOLUTION_COLUMNS) * 146,
                        y + 160 + (resolution / RESOLUTION_COLUMNS) * 40, 138, 32,
                        marcus_render_resolution(resolution)->label,
                        edited_resolution == resolution, 1);
    ui_heading(&app->ui, x + 480, y + 56, "DISPLAY MODE", ACCENT);
    ui_button(&app->ui, x + 480, y + 80, 256, marcus_display_mode_name(view->display_mode), 0, 1);
    ui_text(&app->ui, x + 480, y + 125, "Wire overlay", TEXT);
    ui_button(&app->ui, x + 620, y + 120, 116, view->wire_overlay ? "On" : "Off", view->wire_overlay, gpu);
    ui_heading(&app->ui, x + 480, y + 166, "ANTI-ALIASING", ACCENT);
    for (i = 0; i < 3; ++i)
        ui_button(&app->ui, x + 480 + i * 88, y + 190, 80, aa[i], view->antialias == i, gpu);
    ui_heading(&app->ui, x + 480, y + 234, "TEXTURE FILTER", ACCENT);
    ui_button(&app->ui, x + 480, y + 258, 124, "Nearest", !view->texture_filter, gpu);
    ui_button(&app->ui, x + 612, y + 258, 124, "Bilinear", view->texture_filter, gpu);
    if (app->settings_work_camera)
    {
        ui_heading(&app->ui, x + 480, y + 304, "PROJECTION", ACCENT);
        ui_button(&app->ui, x + 480, y + 328, 124, "Perspective", !view->orthographic, 1);
        ui_button(&app->ui, x + 612, y + 328, 124, "Orthographic", view->orthographic, 1);
        ui_heading(&app->ui, x + 480, y + 372, "VIEW PRESET", ACCENT);
        for (i = 0; i < 4; ++i)
            ui_button(&app->ui, x + 480 + i * 66, y + 396, 58, axes[i], view->axis_view == i, 1);
    }
    ui_text(&app->ui, x + 24, y + 452, "BRender: built-in shading, wireframe and orthographic projection.", MUTED);
    ui_text(&app->ui, x + 24, y + 477, "GPU: depth / face normals, wire overlay, SSAA and bilinear textures.", MUTED);
    draw_debug_panel(app, x + 744, y + 56, app->settings_work_camera, 1);
    ui_text(&app->ui, x + 760, y + 292, "Captured before opening settings.", MUTED);
    ui_text(&app->ui, x + 760, y + 316, "Uses the applied camera settings.", MUTED);
    ui_text(&app->ui, x + 760, y + 340, "Playback pauses in settings.", MUTED);
    overlay = app->settings_work_camera ? app->edited_shortcuts.work_debug_overlay :
                                         app->edited_shortcuts.movie_debug_overlay;
    ui_button(&app->ui, x + 760, y + 364, 272,
              overlay ? "Show stats over camera view: On" : "Show stats over camera view: Off", overlay, 1);
    ui_text(&app->ui, x + 24, y + RENDER_SETTINGS_HEIGHT - 106,
            app->settings_work_camera ? "Applies to the work camera." : "Applies to the movie camera and expanded player.", MUTED);
    ui_label(&app->ui, x + 24, y + RENDER_SETTINGS_HEIGHT - 80, RENDER_SETTINGS_WIDTH - 48, app->settings_message, 0xf2ae9b);
    ui_button(&app->ui, x + RENDER_SETTINGS_WIDTH - 224, y + RENDER_SETTINGS_HEIGHT - 48, 88, "Cancel", 0, 1);
    ui_button(&app->ui, x + RENDER_SETTINGS_WIDTH - 124, y + RENDER_SETTINGS_HEIGHT - 48, 96, "Apply", 1, 1);
}
static void draw_settings(App *app)
{
    float x, y, row_y;
    int row;
    char label[128];
    if (app->rendering_settings)
    {
        draw_rendering_settings(app);
        return;
    }
    x = app->width / 2.0f - 330;
    y = (app->height - SETTINGS_HEIGHT) / 2.0f;
    ui_fill(&app->ui, x - 1, y - 1, 662, SETTINGS_HEIGHT + 2, 0x606670);
    ui_fill(&app->ui, x, y, 660, SETTINGS_HEIGHT, PANEL);
    ui_text(&app->ui, x + 24, y + 20, "Settings", TEXT);
    ui_button(&app->ui, x + 618, y + 16, 28, "x", 0, 1);
    ui_heading(&app->ui, x + 24, y + 53, "KEYBOARD SHORTCUTS", ACCENT);
    ui_text(&app->ui, x + 24, y + 76,
            app->binding_capture >= 0 ? "Press a key. Escape cancels capture. Backspace clears the binding."
                                      : "Click a binding, then press the key combination you want.",
            MUTED);
    for (row = 0; row < MARCUS_ACTION_COUNT; ++row)
    {
        row_y = y + 100 + row * 30;
        if (row == 4 || row == 8)
            ui_line(&app->ui, x + 24, row_y - 1, x + 634, row_y - 1, LINE);
        ui_text(&app->ui, x + 28, row_y + 5, shortcuts_action_name(row), TEXT);
        shortcuts_label(&app->edited_shortcuts.binding[row], label, sizeof(label));
        ui_button(&app->ui, x + 430, row_y, 204, row == app->binding_capture ? "Press a key..." : label,
                  row == app->binding_capture, 1);
    }
    ui_text(&app->ui, x + 28, y + 477, "Pause playback on focus loss", TEXT);
    ui_button(&app->ui, x + 430, y + 472, 204, app->edited_shortcuts.pause_on_focus_loss ? "On" : "Off",
              app->edited_shortcuts.pause_on_focus_loss, 1);
    ui_text(&app->ui, x + 28, y + 513, "Movie camera renderer", TEXT);
    ui_button(&app->ui, x + 430, y + 508, 204, marcus_renderer_name(app->edited_shortcuts.renderer), 0, 1);
    ui_text(&app->ui, x + 28, y + 549, "Movie camera resolution", TEXT);
    ui_button(&app->ui, x + 430, y + 544, 204,
              marcus_render_resolution(app->edited_shortcuts.render_resolution)->label, 0, 1);
    ui_text(&app->ui, x + 28, y + 585, "Work camera renderer", TEXT);
    ui_button(&app->ui, x + 430, y + 580, 204, marcus_renderer_name(app->edited_shortcuts.work_renderer), 0, 1);
    ui_text(&app->ui, x + 28, y + 621, "Work camera resolution", TEXT);
    ui_button(&app->ui, x + 430, y + 616, 204,
              marcus_render_resolution(app->edited_shortcuts.work_render_resolution)->label, 0, 1);
    ui_text(&app->ui, x + 28, y + 657, "App debug overlay (FPS / frame time)", TEXT);
    ui_button(&app->ui, x + 430, y + 652, 204, app->edited_shortcuts.debug_overlay ? "On" : "Off",
              app->edited_shortcuts.debug_overlay, 1);
    ui_label(&app->ui, x + 24, y + 682, 610, app->settings_message, 0xf2ae9b);
    ui_button(&app->ui, x + 24, y + 704, 148, "Restore defaults", 0, 1);
    ui_button(&app->ui, x + 438, y + 704, 88, "Cancel", 0, 1);
    ui_button(&app->ui, x + 538, y + 704, 96, "Apply", 1, 1);
}
static void draw_zip(App *app)
{
    float x, y, row_y;
    int row, index, count;
    char label[1100];
    MarcusMovieEntry member;
    x = app->width / 2.0f - 400;
    y = app->height / 2.0f - 240;
    count = marcus_input_count(app->zip_input);
    ui_fill(&app->ui, x - 1, y - 1, 802, 482, 0x606670);
    ui_fill(&app->ui, x, y, 800, 480, PANEL);
    ui_text(&app->ui, x + 24, y + 20, "Choose a movie from this ZIP", TEXT);
    ui_label(&app->ui, x + 24, y + 50, 752, app->zip_path, MUTED);
    for (row = 0; row < 10; ++row)
    {
        index = app->zip_scroll + row;
        if (!marcus_input_entry(app->zip_input, index, &member))
            break;
        row_y = y + 92 + row * 28;
        if (index == app->zip_selected)
            ui_fill(&app->ui, x + 24, row_y, 752, 28, 0x34464c);
        SDL_snprintf(label, sizeof(label), "%d. %s", index + 1, member.name);
        ui_label(&app->ui, x + 32, row_y + 5, 736, label, index == app->zip_selected ? ACCENT : TEXT);
    }
    if (marcus_input_entry(app->zip_input, app->zip_selected, &member))
    {
        SDL_snprintf(label, sizeof(label), "%d of %d movies   /   %.1f MiB   /   Up/Down or scroll to browse",
                     app->zip_selected + 1, count, member.bytes / (1024 * 1024));
        ui_text(&app->ui, x + 24, y + 390, label, MUTED);
    }
    ui_button(&app->ui, x + 568, y + 432, 96, "Cancel", 0, 1);
    ui_button(&app->ui, x + 680, y + 432, 96, "Open", 1, 1);
}
static void movie_control_label(MarcusUI *ui, float x, float y, float width, const char *text,
                                unsigned long colour)
{
    ui_label(ui, x + 1, y + 1, width, text, 0);
    ui_label(ui, x, y, width, text, colour);
}
static void draw_movie_controls(App *app)
{
    SDL_FRect rect, settings;
    MarcusScene scene;
    MarcusUI *ui;
    char label[320], position[48], duration[48];
    const char *status;
    double seconds;
    float left;
    int available;
    if (app->movie_overlay_alpha <= 0)
        return;
    ui = &app->ui;
    ui->hint = NULL;
    rect = movie_controls_rect(app);
    left = rect.x + 24;
    available = app->movie_time_available;
    seconds = app->movie_scrubbing ? app->movie_scrub_seconds : app->movie_time.position_seconds;
    ui_opacity(ui, app->movie_overlay_alpha);
    settings = movie_settings_rect(app);
    ui_settings_button(ui, settings.x, settings.y, settings.w, app->settings_open);
    if (!app->settings_open && ui_inside(ui->mouse_x, ui->mouse_y, settings.x, settings.y, settings.w, settings.h))
        movie_control_label(ui, settings.x - 112, settings.y + settings.h + 8, 152, "Rendering settings", TEXT);
    status = app->movie_scrubbing ? "SEEKING" : !app->playing ? "PAUSED"
             : app->playback.wait == 1 ? "CONTINUE" : "PLAYING";
    ui_heading_label(ui, left + 1, rect.y + 17, 100, status, 0);
    ui_heading_label(ui, left, rect.y + 16, 100, status, ACCENT);
    if (marcus_scene(app->player, app->info.scene, &scene))
    {
        SDL_snprintf(label, sizeof(label), "Scene %d / %d    %s", app->info.scene + 1, app->info.scene_count, scene.name);
        movie_control_label(ui, left + 100, rect.y + 16, rect.w - 400, label, TEXT);
    }
    if (available)
    {
        format_time(position, sizeof(position), seconds, app->movie_time.duration_seconds >= 3600);
        format_time(duration, sizeof(duration), app->movie_time.duration_seconds,
                    app->movie_time.duration_seconds >= 3600);
        SDL_snprintf(label, sizeof(label), "%s / %s", position, duration);
    }
    else
        SDL_strlcpy(label, app->loaded && !app->movie_time_status ? "Calculating duration..." : "--:-- / --:--", sizeof(label));
    movie_control_label(ui, rect.x + rect.w - 240, rect.y + 16, 216, label, TEXT);
    draw_movie_scrubber(app, available);
    ui_frame_button(ui, left, rect.y + 90, 1, app->info.scene > 0, "Previous scene");
    ui_play_button(ui, rect.x + 64, rect.y + 78, app->playing, app->playback.wait == 1, app->loaded);
    ui_frame_button(ui, rect.x + 116, rect.y + 90, 2, app->info.scene + 1 < app->info.scene_count, "Next scene");
    ui_button_sized(ui, rect.x + 168, rect.y + 86, 56, 28, "-10 s", 0, available);
    ui_button_sized(ui, rect.x + 232, rect.y + 86, 56, 28, "+10 s", 0, available);
    ui_button_sized(ui, rect.x + 312, rect.y + 86, 64, 28, app->muted ? "Unmute" : "Mute", app->muted, 1);
    movie_control_label(ui, rect.x + 400, rect.y + 92, rect.w - 636,
                        app->movie_scrubbing ? "Release to finish seeking" : "Drag to seek / ticks mark scenes", MUTED);
    ui_button_sized(ui, rect.x + rect.w - 212, rect.y + 86, 96, 28,
                    app->fullscreen ? "Windowed" : "Fullscreen", app->fullscreen, 1);
    ui_button_sized(ui, rect.x + rect.w - 108, rect.y + 86, 84, 28, "Editor", 0, 1);
    ui_opacity(ui, 1);
}
static void record_frame(App *app, double milliseconds)
{
    CameraPerformance *performance;
    double elapsed;
    int camera, i, index, renders;
    if (milliseconds <= 0)
        return;
    app->frame_ms[app->frame_next] = milliseconds;
    for (camera = 0; camera < 2; ++camera)
    {
        performance = &app->camera_performance[camera];
        performance->render_ms[app->frame_next] = performance->pending_ms;
        performance->rendered[app->frame_next] = (unsigned char)performance->pending_rendered;
        if (performance->pending_rendered)
            performance->last_ms = performance->pending_ms;
        performance->pending_ms = 0;
        performance->pending_rendered = 0;
    }
    app->frame_next = (app->frame_next + 1) % DEBUG_HISTORY;
    if (app->frame_count < DEBUG_HISTORY)
        ++app->frame_count;
    for (camera = 0; camera < 2; ++camera)
    {
        performance = &app->camera_performance[camera];
        elapsed = 0;
        renders = 0;
        for (i = 0; i < app->frame_count && elapsed < 1000; ++i)
        {
            index = (app->frame_next - 1 - i + DEBUG_HISTORY) % DEBUG_HISTORY;
            elapsed += app->frame_ms[index];
            renders += performance->rendered[index];
        }
        index = (app->frame_next - 1 + DEBUG_HISTORY) % DEBUG_HISTORY;
        performance->fps[index] = renders * 1000.0 / elapsed;
    }
}
static double debug_value(const App *app, int index, int fps, int camera, int frozen)
{
    const CameraPerformance *performance;
    if (camera < 0)
        return fps ? 1000.0 / app->frame_ms[index] : app->frame_ms[index];
    performance = frozen ? &app->settings_performance : &app->camera_performance[camera];
    return fps ? performance->fps[index] : performance->render_ms[index];
}
static void draw_debug_graph(App *app, float x, float y, int fps, int camera, int frozen)
{
    double value, limit;
    float px, py, previous_x, previous_y;
    int i, index, frame_count, frame_next;
    char label[48];
    unsigned long colour;
    frame_count = frozen ? app->settings_frame_count : app->frame_count;
    frame_next = frozen ? app->settings_frame_next : app->frame_next;
    limit = fps ? 60 : 20;
    colour = fps ? ACCENT : 0xe6ba78UL;
    for (i = 0; i < frame_count; ++i)
    {
        index = (frame_next - frame_count + i + DEBUG_HISTORY) % DEBUG_HISTORY;
        value = debug_value(app, index, fps, camera, frozen);
        limit = maximum(limit, value);
    }
    limit = ceil(limit / (fps ? 30 : 10)) * (fps ? 30 : 10);
    SDL_snprintf(label, sizeof(label), "0 - %.0f%s", limit, fps ? " FPS" : " ms");
    ui_label(&app->ui, x + 178, y - 23, 94, label, MUTED);
    ui_fill(&app->ui, x, y, 272, 44, BG);
    for (i = 0; i < 3; ++i)
        ui_line(&app->ui, x, y + i * 21.5f, x + 271, y + i * 21.5f, LINE);
    previous_x = previous_y = 0;
    for (i = 0; i < frame_count; ++i)
    {
        index = (frame_next - frame_count + i + DEBUG_HISTORY) % DEBUG_HISTORY;
        value = debug_value(app, index, fps, camera, frozen);
        px = x + (DEBUG_HISTORY - frame_count + i) * 271.0f / (DEBUG_HISTORY - 1);
        py = y + 43 - (float)(value / limit * 43);
        if (i)
            ui_line(&app->ui, previous_x, previous_y, px, py, colour);
        else
            ui_fill(&app->ui, px, py, 1, 1, colour);
        previous_x = px;
        previous_y = py;
    }
}
static void draw_debug_panel(App *app, float x, float y, int camera, int frozen)
{
    const CameraPerformance *performance;
    const double *frame_ms;
    double total, average, render_total, fps;
    int count, index, renders, frame_count, frame_next;
    char label[96];
    performance = camera < 0 ? NULL : frozen ? &app->settings_performance : &app->camera_performance[camera];
    frame_ms = frozen ? app->settings_frame_ms : app->frame_ms;
    frame_count = frozen ? app->settings_frame_count : app->frame_count;
    frame_next = frozen ? app->settings_frame_next : app->frame_next;
    total = 0;
    render_total = 0;
    renders = 0;
    count = 0;
    /* Average the latest second; graph points retain individual frame spikes. */
    while (count < frame_count && total < 1000)
    {
        index = (frame_next - 1 - count + DEBUG_HISTORY) % DEBUG_HISTORY;
        total += frame_ms[index];
        if (performance)
        {
            render_total += performance->render_ms[index];
            renders += performance->rendered[index];
        }
        ++count;
    }
    average = count ? total / count : 0;
    fps = average > 0 ? 1000.0 / average : 0;
    if (performance)
    {
        fps = total > 0 ? renders * 1000.0 / total : 0;
        average = renders ? render_total / renders : performance->last_ms;
    }
    ui_fill(&app->ui, x - 1, y - 1, 306, 222, LINE);
    ui_fill(&app->ui, x, y, 304, 220, PANEL);
    ui_heading(&app->ui, x + 16, y + 12,
            camera < 0 ? "APP / DISPLAY" : camera ? "WORK CAMERA" : "MOVIE CAMERA", MUTED);
    if (count)
        SDL_snprintf(label, sizeof(label), "%s  %.1f", performance ? "Render FPS" : "FPS", fps);
    else
        SDL_strlcpy(label, "FPS  --", sizeof(label));
    ui_text(&app->ui, x + 16, y + 38, label, ACCENT);
    draw_debug_graph(app, x + 16, y + 62, 1, camera, frozen);
    if (average > 0)
        SDL_snprintf(label, sizeof(label), "%s  %.2f ms", performance ? "Render time" : "Frame time", average);
    else
        SDL_strlcpy(label, "Frame time  -- ms", sizeof(label));
    ui_text(&app->ui, x + 16, y + 116, label, 0xe6ba78);
    draw_debug_graph(app, x + 16, y + 140, 0, camera, frozen);
    ui_text(&app->ui, x + 16, y + 194,
            !performance ? "180 frames / includes display wait"
            : renders ? "Actual renders / excludes display wait"
            : average > 0 ? "Cached / showing last render time" : "Waiting for a camera render", MUTED);
}
static void draw_debug_overlay(App *app)
{
    float y;
    y = app->movie_only ? 24.0f : 132.0f;
    if (app->shortcuts.debug_overlay)
        draw_debug_panel(app, (float)app->width - 328, app->movie_only ? 80.0f : y, -1, 0);
    if (!app->loaded || app->settings_open)
        return;
    if (app->shortcuts.movie_debug_overlay && (app->movie_only || app->second_view || app->authored == 1))
        draw_debug_panel(app, 24, y, 0, 0);
    if (app->shortcuts.work_debug_overlay && !app->movie_only && (app->second_view || app->authored != 1))
        draw_debug_panel(app, work_left(app) + 24, y, 1, 0);
}
static void draw(App *app)
{
    char previous[64], next[64], previous_scene[64], next_scene[64], play[64], view[64], hint[512];
    if (app->movie_only)
    {
        ui_fill(&app->ui, 0, 0, (float)app->width, (float)app->height, 0);
        draw_movie_view(app);
        draw_movie_controls(app);
        draw_debug_overlay(app);
        if (app->settings_open)
            draw_settings(app);
        browser_draw(app->browser, &app->ui, app->width, app->height);
        if (app->error[0])
            draw_error(app);
        return;
    }
    shortcuts_label(&app->shortcuts.binding[MARCUS_PREV_FRAME], previous, sizeof(previous));
    shortcuts_label(&app->shortcuts.binding[MARCUS_NEXT_FRAME], next, sizeof(next));
    shortcuts_label(&app->shortcuts.binding[MARCUS_PREV_SCENE], previous_scene, sizeof(previous_scene));
    shortcuts_label(&app->shortcuts.binding[MARCUS_NEXT_SCENE], next_scene, sizeof(next_scene));
    shortcuts_label(&app->shortcuts.binding[MARCUS_PLAY], play, sizeof(play));
    shortcuts_label(&app->shortcuts.binding[MARCUS_VIEW], view, sizeof(view));
    SDL_snprintf(hint, sizeof(hint),
                 "%s / %s  Frame    %s / %s  Scene    %s  Play / pause    %s  View    Ctrl+,  Settings", previous, next,
                 previous_scene, next_scene, play, view);
    app->ui.hint = NULL;
    ui_fill(&app->ui, 0, 0, (float)app->width, (float)app->height, BG);
    draw_header(app);
    draw_view(app);
    draw_sidebar(app);
    draw_timeline(app);
    draw_sheet_menu(app);
    ui_fill(&app->ui, 0, (float)app->height - 24, (float)app->width, 24, 0x303339);
    ui_label(&app->ui, 16, (float)app->height - 21, (float)app->width - 235,
             app->ui.hint ? app->ui.hint
             : app->authored == 2
                 ? "Hold right mouse: look + WASD move   Q/E down/up   Shift faster   Wheel speed   Esc release"
                 : hint,
             MUTED);
    ui_text(&app->ui, (float)app->width - 194, (float)app->height - 21,
            app->playback.scrolling ? "SCROLLING CAPTION"
            : app->playback.transition ? "SCENE TRANSITION"
            : app->playback.wait == 1 ? "CLICK / SPACE TO CONTINUE"
            : app->playback.wait == 2 ? "WAITING FOR SOUND"
            : app->playback.wait == 3 ? "TIMED PAUSE"
            : "READ ONLY", ACCENT);
    draw_debug_overlay(app);
    if (app->settings_open)
        draw_settings(app);
    if (app->zip_input)
        draw_zip(app);
    scene_organiser_draw(app->scene_organiser, &app->ui, app->width, app->height);
    browser_draw(app->browser, &app->ui, app->width, app->height);
    if (app->error[0])
        draw_error(app);
}
static int screenshot(App *app, const char *path)
{
    SDL_Surface *surface;
    int result;
    surface = SDL_RenderReadPixels(app->renderer, NULL);
    if (!surface)
        return 0;
    result = SDL_SaveBMP(surface, path);
    SDL_DestroySurface(surface);
    return result;
}
static void find_defaults(App *app)
{
    const char *resources[5];
    const char *base;
    SDL_PathInfo info;
    char local[1024];
    int i;
    base = SDL_GetBasePath();
    SDL_snprintf(local, sizeof(local), "%s3dmovie.chk", base ? base : "");
    resources[0] = local;
    resources[1] = "3dmovie.chk";
    resources[2] = MARCUS_SOURCE_DIR "/build/studio-portable-x64/3dmovie/utest.chk";
    resources[3] = MARCUS_SOURCE_DIR "/build/3dmovie/utest.chk";
    resources[4] = NULL;
    for (i = 0; resources[i]; ++i)
        if (SDL_GetPathInfo(resources[i], &info) && info.type == SDL_PATHTYPE_FILE)
        {
            SDL_strlcpy(app->resources, resources[i], sizeof(app->resources));
            break;
        }
    if (!app->resources[0])
        SDL_strlcpy(app->resources, "3dmovie.chk", sizeof(app->resources));
    SDL_snprintf(local, sizeof(local), "%scontent-files", base ? base : "");
    if (SDL_GetPathInfo(local, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        SDL_strlcpy(app->content, local, sizeof(app->content));
    else
        SDL_strlcpy(app->content, MARCUS_SOURCE_DIR "/content-files", sizeof(app->content));
}
static void initialize_shortcuts(App *app)
{
    char *directory;
    shortcuts_defaults(&app->shortcuts);
    app->binding_capture = -1;
    if (!app->settings_path[0])
    {
        directory = SDL_GetPrefPath("3DMMForever", "Marcus");
        if (directory)
        {
            if (SDL_snprintf(app->settings_path, sizeof(app->settings_path), "%sshortcuts.cfg", directory) >=
                (int)sizeof(app->settings_path))
                app->settings_path[0] = 0;
            SDL_free(directory);
        }
    }
    shortcuts_load(app->settings_path, &app->shortcuts, app->settings_message, sizeof(app->settings_message));
}
static void usage(void)
{
    puts("Marcus - read-only 3D Movie Maker player\n"
         "Usage: marcus [movie.3mm|movie.vmm|movies.zip] [--resources 3dmovie.chk] [--content-dir folder]\n"
         "              [--font font.ttf] [--scene N] [--frame N] [--object N] [--orbit|--fly]\n"
         "              [--screenshot output.bmp] [--show-settings] [--sounds|--both] [--second-view]\n"
         "              [--settings-file shortcuts.cfg] [--zip-movie N] [--browse folder-or-zip]\n"
         "              [--renderer brender|brender-compute|sdl-gpu]");
    puts("Scenes and scene-relative frames start at 1; object indices start at 0.");
}
static int number(const char *text, long *output)
{
    char *end;
    double value;
    value = strtod(text, &end);
    if (!text[0] || *end || value < -2147483648.0 || value > 2147483647.0 || value != floor(value))
        return 0;
    *output = (long)value;
    return 1;
}
int main(int argc, char **argv)
{
    App app;
    SDL_Event event;
    const char *input, *font, *capture, *browse;
    int i, result, frames, requested_scene, requested_object, requested_zip, have_frame, show_settings;
    int requested_renderer, browser_entry, chosen_scene;
    MarcusMovieInput *browser_input;
    char browser_path[4096];
    long requested_frame, parsed, chosen_frame;
    double now, previous_tick;
    Uint64 frame_tick, completed_tick;
    memset(&app, 0, sizeof(app));
    input = font = capture = browse = NULL;
    result = have_frame = show_settings = 0;
    requested_scene = 1;
    requested_object = -1;
    requested_zip = 0;
    requested_frame = 0;
    requested_renderer = -1;
    app.running = app.authored = app.dirty = 1;
    app.selected = -1;
    app.selected_sound = app.selected_sound_track = -1;
    app.pixels_per_frame = 12;
    app.zoom = 1;
    app.split_fraction = 0.5f;
    app.timeline_height = 300;
    app.object_split = 326;
    app.second_mode = 2;
    app.movie_dirty = 1;
    find_defaults(&app);
    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            usage();
            return 0;
        }
        else if (!strcmp(argv[i], "--browse") && i + 1 < argc)
            browse = argv[++i];
        else if (!strcmp(argv[i], "--show-settings"))
            show_settings = 1;
        else if (!strcmp(argv[i], "--sounds"))
            set_sheet_mode(&app, SHEET_SOUNDS);
        else if (!strcmp(argv[i], "--both"))
            set_sheet_mode(&app, SHEET_BOTH);
        else if (!strcmp(argv[i], "--second-view"))
            app.second_view = 1;
        else if (!strcmp(argv[i], "--settings-file") && i + 1 < argc)
            SDL_strlcpy(app.settings_path, argv[++i], sizeof(app.settings_path));
        else if (!strcmp(argv[i], "--renderer") && i + 1 < argc)
        {
            requested_renderer = marcus_renderer_from_id(argv[++i]);
            if (requested_renderer < 0)
            {
                usage();
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--orbit"))
            app.authored = 0;
        else if (!strcmp(argv[i], "--fly"))
            app.authored = 2;
        else if (!strcmp(argv[i], "--resources") && i + 1 < argc)
            SDL_strlcpy(app.resources, argv[++i], sizeof(app.resources));
        else if (!strcmp(argv[i], "--content-dir") && i + 1 < argc)
            SDL_strlcpy(app.content, argv[++i], sizeof(app.content));
        else if (!strcmp(argv[i], "--font") && i + 1 < argc)
            font = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            capture = argv[++i];
        else if ((!strcmp(argv[i], "--scene") || !strcmp(argv[i], "--frame") || !strcmp(argv[i], "--object") ||
                  !strcmp(argv[i], "--zip-movie")) &&
                 i + 1 < argc)
        {
            if (!number(argv[i + 1], &parsed))
            {
                usage();
                return 2;
            }
            if (!strcmp(argv[i], "--zip-movie"))
            {
                if (parsed < 1 || parsed > INT_MAX)
                    return 2;
                requested_zip = (int)parsed;
            }
            else if (!strcmp(argv[i], "--scene"))
            {
                if (parsed < 1 || parsed > INT_MAX)
                    return 2;
                requested_scene = (int)parsed;
            }
            else if (!strcmp(argv[i], "--object"))
            {
                if (parsed < 0 || parsed > INT_MAX)
                    return 2;
                requested_object = (int)parsed;
            }
            else
            {
                requested_frame = parsed;
                have_frame = 1;
            }
            ++i;
        }
        else if (argv[i][0] == '-' || input)
        {
            usage();
            return 2;
        }
        else
            input = argv[i];
    }
    if (app.second_view && app.authored == 1)
        app.authored = 2;
    if (app.second_view)
        app.second_mode = app.authored;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    initialize_shortcuts(&app);
    app.window = SDL_CreateWindow("Marcus", 1380, 900, 0);
    app.renderer = app.window ? SDL_CreateRenderer(app.window, NULL) : NULL;
    app.player = marcus_create();
    if (!app.renderer || !app.player)
    {
        result = 1;
        goto cleanup;
    }
    if (requested_renderer >= 0)
    {
        app.shortcuts.renderer = requested_renderer;
        app.shortcuts.work_renderer = requested_renderer;
    }
    if (!marcus_set_renderer(app.player, app.shortcuts.renderer))
    {
        if (requested_renderer >= 0)
        {
            SDL_strlcpy(app.error, marcus_error(app.player), sizeof(app.error));
            fprintf(stderr, "Marcus: %s\n", app.error);
            result = 1;
            goto cleanup;
        }
        SDL_snprintf(app.settings_message, sizeof(app.settings_message), "%s Using BRender CPU.",
                     marcus_error(app.player));
        app.shortcuts.renderer = MARCUS_RENDERER_BRENDER;
    }
    if (!marcus_set_renderer(app.player, app.shortcuts.work_renderer))
    {
        SDL_snprintf(app.settings_message, sizeof(app.settings_message), "Work camera: %s Using BRender CPU.",
                     marcus_error(app.player));
        app.shortcuts.work_renderer = MARCUS_RENDERER_BRENDER;
    }
    normalize_camera_options(app.shortcuts.renderer, &app.shortcuts.movie_view);
    normalize_camera_options(app.shortcuts.work_renderer, &app.shortcuts.work_view);
    SDL_SetWindowResizable(app.window, 1);
    SDL_SetWindowMinimumSize(app.window, 1080, 820);
    app.split_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    app.timeline_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    SDL_SetRenderVSync(app.renderer, 1);
    if (!ui_init(&app.ui, app.renderer, font))
    {
        result = 1;
        goto cleanup;
    }
    app.browser = browser_create(app.window, app.renderer);
    if (!app.browser)
    {
        result = 1;
        goto cleanup;
    }
    app.scene_organiser = scene_organiser_create();
    if (!app.scene_organiser)
    {
        result = 1;
        goto cleanup;
    }
    app.dialog_event = SDL_RegisterEvents(1);
    layout(&app);
    reset_view(&app);
    if (input)
    {
        open_movie(&app, input);
        if (app.zip_input && requested_zip)
        {
            if (requested_zip > marcus_input_count(app.zip_input))
                fail(&app, "Requested ZIP movie does not exist.");
            else
            {
                app.zip_selected = requested_zip - 1;
                confirm_zip(&app);
            }
        }
        else if (requested_zip > 1)
            fail(&app, "Requested ZIP movie does not exist.");
        if (capture && app.zip_input && !app.error[0])
            fail(&app, "Use --zip-movie N to select a movie before capturing a multi-movie ZIP.");
        if (app.loaded && requested_scene != 1 && !select_scene(&app, requested_scene - 1))
            fail(&app, "Requested scene does not exist.");
        if (app.loaded && have_frame)
        {
            if (requested_frame < 1 || requested_frame > display_frame(&app, app.info.last_frame))
                fail(&app, "Requested frame is outside this scene.");
            else
                seek(&app, (long)((double)app.info.first_frame + requested_frame - 1));
        }
        if (app.loaded && requested_object >= 0)
        {
            if (requested_object >= app.info.object_count && app.info.object_count)
                fail(&app, "Requested object does not exist.");
            else
                app.selected = app.info.object_count ? requested_object : -1;
        }
    }
    if (app.zip_input && !requested_zip && !capture && !app.error[0])
    {
        browser_show(app.browser, app.zip_path, app.resources, app.content);
        cancel_zip(&app);
    }
    if (show_settings && !app.zip_input)
        open_settings(&app);
    else if (browse && !app.zip_input)
        browser_show(app.browser, browse, app.resources, app.content);
    frames = 0;
    previous_tick = (double)SDL_GetTicks();
    frame_tick = SDL_GetTicksNS();
    while (app.running)
    {
        layout(&app);
        while (SDL_PollEvent(&event))
        {
            SDL_ConvertEventToRenderCoordinates(app.renderer, &event);
            handle_event(&app, &event);
        }
        i = scene_organiser_take(app.scene_organiser);
        if (i >= 0)
            select_scene(&app, i);
        if (scene_organiser_take_frame(app.scene_organiser, &chosen_scene, &chosen_frame))
        {
            if (chosen_scene == app.info.scene || select_scene(&app, chosen_scene))
                seek(&app, chosen_frame);
        }
        browser_input = browser_take(app.browser, &browser_entry, browser_path, sizeof(browser_path));
        if (browser_input && !open_input(&app, browser_input, browser_entry, browser_path))
        {
            marcus_input_destroy(browser_input);
            browser_show(app.browser, browser_path, app.resources, app.content);
        }
        now = (double)SDL_GetTicks();
        update_flight(&app, (now - previous_tick) / 1000.0);
        if (app.loaded)
        {
            if (!marcus_update(app.player, app.playing, app.loop, now - previous_tick, app.continue_click,
                               &app.playback))
            {
                fail(&app, marcus_error(app.player));
                refresh(&app);
                app.dirty = 1;
            }
            else
            {
                app.playing = app.playback.playing;
                if (app.playback.changed)
                {
                    refresh(&app);
                    app.dirty = 1;
                }
                if (app.playback.scene_changed)
                {
                    app.selected = -1;
                    app.row_scroll = app.object_scroll = 0;
                    fit_timeline(&app);
                }
            }
            if (frame_x(&app, app.info.frame) > app.grid_x + app.grid_width ||
                frame_x(&app, app.info.frame) < app.grid_x)
                app.timeline_start = app.info.frame;
        }
        app.continue_click = 0;
        app.movie_time_status = app.loaded ? marcus_movie_time_async(app.player, &app.movie_time) : -1;
        app.movie_time_available = app.movie_time_status == 1;
        update_movie_controls(&app, now, now - previous_tick);
        previous_tick = now;
        draw(&app);
        if (capture && frames == 3)
        {
            if (app.error[0] || (input && !app.loaded))
            {
                fprintf(stderr, "%s\n", app.error);
                result = 1;
            }
            if (capture && !screenshot(&app, capture))
                result = 1;
            app.running = 0;
        }
        SDL_RenderPresent(app.renderer);
        ++frames;
        SDL_Delay(10);
        completed_tick = SDL_GetTicksNS();
        record_frame(&app, (double)(completed_tick - frame_tick) / 1000000.0);
        frame_tick = completed_tick;
    }
cleanup:
    if (app.cursor_hidden)
        SDL_ShowCursor();
    release_camera(&app);
    if (result && !app.error[0])
        fprintf(stderr, "Marcus: %s\n", SDL_GetError());
    browser_destroy(app.browser);
    scene_organiser_destroy(app.scene_organiser);
    ui_destroy(&app.ui);
    SDL_DestroyTexture(app.picture);
    SDL_DestroyTexture(app.movie_picture);
    SDL_DestroyCursor(app.split_cursor);
    SDL_DestroyCursor(app.timeline_cursor);
    marcus_destroy(app.player);
    marcus_input_destroy(app.movie_input);
    marcus_input_destroy(app.zip_input);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return result;
}
