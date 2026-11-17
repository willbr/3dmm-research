#include "player.h"
#include "ui.h"
#include "settings.h"
#include "camera_guide.h"
#include "camera_controls.h"
#include "movie_browser.h"
#include "scene_organiser.h"
#include "background_browser.h"
#include "text_dialog.h"
#include "costume_dialog.h"
#include "sound_dialog.h"
#include "dialog_widgets.h"
#include "dialog_host.h"
#include "ribbon.h"
#include "camera_picker.h"
#include "storyboard_editor.h"
#include "storyboard.h"
#include "prop_gizmo.h"
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
#define RIBBON_DELTA (MARCUS_RIBBON_HEIGHT - 88)
#define VIEW_TOP (MARCUS_RIBBON_HEIGHT + 1)
#define CAMERA_BAR_Y (MARCUS_RIBBON_HEIGHT + 8)
#define OBJECT_LIST_TOP (MARCUS_RIBBON_HEIGHT + 38)
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
    MarcusUI ui;
    MarcusPlayer *player;
    MarcusBrowser *browser;
    MarcusSceneOrganiser *scene_organiser;
    MarcusBackgroundBrowser *background_browser;
    MarcusTextDialog *text_dialog;
    MarcusCostumeDialog *costume_dialog;
    MarcusSoundDialog *sound_dialog;
    MarcusCameraPicker *camera_picker;
    MarcusStoryboardEditor *storyboard_editor;
    MarcusPropGizmo *prop_gizmo;
    int prop_fields, prop_interacting;
    int ribbon_tab, modified;
    int binding_key_consumed;
    MarcusMovieInput *movie_input, *zip_input;
    int movie_entry, zip_selected;
    char zip_path[1024];
    MarcusInfo info;
    MarcusPlaybackState playback;
    MarcusMovieTime movie_time;
    int movie_time_available;
    int movie_time_status;
    MarcusCamera camera, flight;
    MarcusImage work_image, movie_image; /* Retained camera metadata; no borrowed pixels. */
    MarcusProjection scene_camera;
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
    int running, loaded, blank_movie, playing, loop, selected, authored, dirty, dragging, orbiting, panning;
    int orbit_selection, orbit_target;
    int continue_click, muted;
    int flying, fly_ready, camera_ready, fly_keys;
    int work_matched, work_needs_match;
    int row_scroll, width, height, list_selection;
    int resizing_objects;
    float object_split;
    int animation_expanded;
    char animation_hint[640];
    int sheet_mode, sound_scroll, selected_sound, selected_sound_track;
    int both_scroll, sound_inspector;
    int second_view, second_mode, resizing_view, movie_dirty;
    int movie_only, fullscreen;
    int movie_scrubbing, movie_resume, cursor_hidden;
    float movie_overlay_alpha;
    double movie_activity_tick, movie_scrub_seconds;
    int resizing_timeline;
    float timeline_height;
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

static const char *sheet_name(int mode)
{
    return mode == SHEET_BOTH ? "Both" : mode == SHEET_SOUNDS ? "Sounds" : "Objects";
}
static void set_sheet_mode(App *app, int mode)
{

    app->sheet_mode = mode;

    if (mode != SHEET_BOTH)
        app->sound_inspector = mode == SHEET_SOUNDS;
}
static void show_object_tracks(App *app)
{

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
    storyboard_editor_release_camera(app->storyboard_editor);
    app->resizing_objects = 0;
    if (app->window && SDL_GetWindowRelativeMouseMode(app->window))
        SDL_SetWindowRelativeMouseMode(app->window, 0);
    app->flying = app->orbiting = app->panning = 0;
    app->fly_keys = 0;
}
static void view_mode(App *app, int mode)
{
    if (mode != 1) storyboard_editor_close(app->storyboard_editor);

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
static int orbit_selection_available(App *app)
{
    MarcusBounds bounds;
    return app->selected >= 0 && marcus_object_bounds(app->player, app->selected, &bounds) && bounds.valid;
}
static void choose_orbit_mode(App *app, int selection)
{
    view_mode(app, 0);
    app->orbit_selection = selection;
    app->orbit_target = -2;
    app->work_matched = app->work_needs_match = app->fly_ready = 0;
    app->zoom = 1;
    marcus_reset_framing(app->player);
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
    height = app->viewport_bottom - (148 + RIBBON_DELTA);
    scale = (float)minimum((right - left - 32) / resolution->width, height / resolution->height);
    rect.w = resolution->width * scale;
    rect.h = resolution->height * scale;
    rect.x = left + (right - left - rect.w) / 2;
    rect.y = 136 + RIBBON_DELTA + (height - rect.h) / 2;
    return rect;
}
static int inside_work(const App *app, float x, float y)
{
    float left;
    if (app->movie_only)
        return 0;
    left = work_left(app);
    return ui_inside(x, y, left + 12, 132 + RIBBON_DELTA, view_right(app) - left - 24, app->viewport_bottom - (144 + RIBBON_DELTA));
}
static int inside_movie(const App *app, float x, float y)
{
    if (app->movie_only)
        return ui_inside(x, y, 0, 0, (float)app->width, (float)app->height);
    return app->second_view ? ui_inside(x, y, 12, 132 + RIBBON_DELTA, app->split_x - 24, app->viewport_bottom - (144 + RIBBON_DELTA))
                            : app->authored == 1 && inside_work(app, x, y);
}
static void toggle_second_view(App *app)
{
    storyboard_editor_close(app->storyboard_editor);
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
    dialog_retire_texture(app->picture);
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

static void layout(App *app)
{
    int rows;
    float height;
    SDL_GetRenderOutputSize(app->renderer, &app->width, &app->height);
    app->sidebar = (float)app->width - 300;
    app->object_split = (float)maximum(OBJECT_LIST_TOP + ROW_HEIGHT + OBJECT_LIST_FOOTER,
                                      minimum(app->height - 184, app->object_split));
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
static void update_storyboard_layout(App *app)
{
    SDL_FRect view, canvas, tools, bar;
    float scale;
    if (app->movie_only || app->playing)
    {
        storyboard_editor_close(app->storyboard_editor);
        return;
    }
    storyboard_editor_sync(app->storyboard_editor, app->player, app->resources, app->content);
    if (!storyboard_editor_visible(app->storyboard_editor)) return;
    view.x = 0; view.y = VIEW_TOP;
    view.w = app->sidebar; view.h = app->viewport_bottom - VIEW_TOP;
    bar.x = 16; bar.y = VIEW_TOP + 8; bar.w = app->sidebar - 32; bar.h = 28;
    scale = (float)minimum((view.w - 32) / 544, maximum(1, view.h - 88) / 306);
    canvas.w = 544 * scale; canvas.h = 306 * scale;
    canvas.x = (view.w - canvas.w) / 2;
    canvas.y = VIEW_TOP + 56 + (view.h - 88 - canvas.h) / 2;
    tools.x = app->sidebar; tools.y = VIEW_TOP;
    tools.w = app->width - app->sidebar; tools.h = app->height - 24 - VIEW_TOP;
    storyboard_editor_layout(app->storyboard_editor, &view, &canvas, &tools, &bar);
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
    app->row_scroll = 0;
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
static void add_frame(App *app, int direction)
{
    char title[2200];
    release_camera(app);
    scene_organiser_close(app->scene_organiser);
    if (!marcus_add_frame(app->player, direction))
    {
        fail(app, marcus_error(app->player));
        return;
    }
    refresh(app);
    app->modified = 1;
    app->dirty = app->movie_dirty = 1;
    SDL_snprintf(title, sizeof(title), "Marcus - %s (unsaved changes)", app->movie[0] ? app->movie : "Untitled movie");
    SDL_SetWindowTitle(app->window, title);
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
        app->row_scroll = 0;
        fit_timeline(app);
    }
    app->dirty = 1;
    return 1;
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
    storyboard_editor_close(app->storyboard_editor);
    storyboard_editor_take(app->storyboard_editor);
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
    app->blank_movie = 0;
    app->modified = 0;
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
    app->row_scroll = 0;
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
static void new_movie(App *app)
{
    MarcusPlayer *player;
    player = marcus_create();
    if (!player)
    {
        fail(app, "Not enough memory to create a new movie.");
        return;
    }
    if (!marcus_set_playback_rate(player, app->shortcuts.playback_rate, app->shortcuts.playback_modifier))
    {
        fail(app, marcus_error(player));
        marcus_destroy(player);
        return;
    }
    marcus_set_muted(player, app->muted);
    storyboard_editor_close(app->storyboard_editor);
    storyboard_editor_take(app->storyboard_editor);
    scene_organiser_close(app->scene_organiser);
    finish_movie_scrub(app);
    release_camera(app);
    app->playing = 0;
    marcus_destroy(app->player);
    app->player = player;
    marcus_input_destroy(app->movie_input);
    app->movie_input = NULL;
    cancel_zip(app);
    app->movie[0] = app->zip_path[0] = app->error[0] = app->animation_hint[0] = 0;
    app->movie_entry = 0;
    app->blank_movie = 1;
    app->modified = app->ribbon_tab = 0;
    memset(&app->info, 0, sizeof(app->info));
    memset(&app->playback, 0, sizeof(app->playback));
    memset(&app->movie_time, 0, sizeof(app->movie_time));
    memset(&app->work_image, 0, sizeof(app->work_image));
    memset(&app->movie_image, 0, sizeof(app->movie_image));
    memset(&app->scene_camera, 0, sizeof(app->scene_camera));
    dialog_retire_texture(app->picture);
    dialog_retire_texture(app->movie_picture);
    app->picture = app->movie_picture = NULL;
    app->selected = app->selected_sound = app->selected_sound_track = -1;
    app->row_scroll = app->sound_scroll = app->both_scroll = 0;
    app->animation_expanded = app->continue_click = 0;
    app->resizing_view = app->resizing_timeline = 0;
    app->timeline_start = 0;
    app->pixels_per_frame = 24;
    refresh(app);
    reset_view(app);
    if (app->movie_only)
        editor_view(app);
    SDL_SetWindowTitle(app->window, "Marcus - Untitled movie");
}
static void confirm_zip(App *app)
{
    if (app->zip_input && open_input(app, app->zip_input, app->zip_selected, app->zip_path))
        app->zip_input = NULL;
}
static void open_movie(App *app, const char *path)
{
    MarcusMovieInput *input;

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
        app->zip_selected = 0;
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
    static const SDL_DialogFileFilter resources[] = {{"3DMM palette and shading", "chk"}};
    static const SDL_DialogFileFilter movies[] = {{"Marcus movie copy", "3mm"}};
    static const SDL_DialogFileFilter bundles[] = {{"V3DMM movie bundle", "vmm"}};
    release_camera(app);
    if (kind == 0)
    {
        app->playing = 0;

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
    if (kind == 4)
    {
        if (marcus_requires_vmm(app->player))
            SDL_ShowSaveFileDialog(dialog_callback, request, app->window, bundles, 1, "Storyboard.vmm");
        else SDL_ShowSaveFileDialog(dialog_callback, request, app->window, movies, 1, "Movie.3mm");
    }
    else if (kind == 2)
        SDL_ShowOpenFolderDialog(dialog_callback, request, app->window, app->content, 0);
    else
        SDL_ShowOpenFileDialog(dialog_callback, request, app->window, resources, 1, app->resources, 0);
}
static void choose_background(App *app)
{
    app->playing = 0;
    release_camera(app);
    finish_movie_scrub(app);

    scene_organiser_close(app->scene_organiser);
    if (!background_browser_show(app->background_browser, app->player, app->resources, app->content))
        fail(app, marcus_error(app->player)[0] ? marcus_error(app->player) : "No backgrounds found. Choose your original 3DMM Content folder.");
}
static void choose_storyboard(App *app, int new_scene, int angle)
{
    app->playing = 0;
    release_camera(app);
    finish_movie_scrub(app);

    scene_organiser_close(app->scene_organiser);
    if (app->movie_only) editor_view(app);
    if (!storyboard_editor_show(app->storyboard_editor, app->player, new_scene, angle,
            &app->work_image, app->fly_ready && !app->work_needs_match ? app->flight.move_speed : 0,
            app->resources, app->content))
        fail(app, marcus_error(app->player)[0] ? marcus_error(app->player) : "Cannot enter storyboard painting.");
    else
    {
        app->second_view = 0;
        view_mode(app, 1);
        app->blank_movie = 0;
        refresh(app);
        if (new_scene) fit_timeline(app);
        SDL_SetCursor(SDL_GetDefaultCursor());
        update_storyboard_layout(app);
    }
}
static void choose_object(App *app)
{
    if (!app->loaded) return;
    finish_movie_scrub(app);
    app->playing = 0;
    release_camera(app);

    scene_organiser_close(app->scene_organiser);
    if (!background_browser_show_objects(app->background_browser, app->player, app->resources, app->content))
        fail(app, marcus_error(app->player)[0] ? marcus_error(app->player) : "No objects found. Choose your original 3DMM Content folder.");
}
static int selected_text(App *app)
{
    MarcusObject object;
    return app->loaded && app->selected >= 0 && marcus_object(app->player, app->selected, &object) && object.text;
}
static void edit_text(App *app)
{
    if (!selected_text(app)) return;
    finish_movie_scrub(app);
    app->playing = 0;
    release_camera(app);

    scene_organiser_close(app->scene_organiser);
    if (!text_dialog_edit(app->text_dialog, app->player, app->window, app->content, app->resources, app->selected))
        fail(app, marcus_error(app->player)[0] ? marcus_error(app->player) : "Cannot edit this 3D text object.");
}
static void choose_text(App *app)
{
    if (!app->loaded) return;
    finish_movie_scrub(app);
    app->playing = 0;
    release_camera(app);

    scene_organiser_close(app->scene_organiser);
    if (!text_dialog_show(app->text_dialog, app->player, app->window, app->content, app->resources))
        fail(app, marcus_error(app->player)[0] ? marcus_error(app->player) : "No 3D fonts found. Choose your original 3DMM Content folder.");
}
static void choose_camera_angle(App *app)
{
    if (!app->loaded || !app->info.camera_count) return;
    finish_movie_scrub(app);
    app->playing = 0;
    release_camera(app);

    scene_organiser_close(app->scene_organiser);
    if (!camera_picker_show(app->camera_picker, app->player))
        fail(app, "Cannot open the camera angle chooser.");
}
static void toggle_play(App *app)
{
    if (!app->loaded)
        return;
    storyboard_editor_close(app->storyboard_editor);
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

static void perform_action(App *app, int action)
{
    long target;
    int scene, direction;
    if (action == MARCUS_OPEN)
    {
        choose_file(app, 0);
        return;
    }
    if (action == MARCUS_NEW)
    {
        new_movie(app);
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
static int frame_has_sound(App *app)
{
    MarcusSound sound;
    int index;
    /* Retail marks cue frames, including stop/silence cues, rather than every
     * frame covered by a playing clip or a sound carried from another scene. */
    for (index = 0; index < app->info.sound_count; ++index)
        if (marcus_sound(app->player, index, &sound) && !sound.carried && sound.frame == app->info.frame)
            return 1;
    return 0;
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

    app->resizing_view = app->resizing_timeline = 0;
    app->playing = app->dragging = app->orbiting = app->panning = 0;
    app->edited_shortcuts = app->shortcuts;
    app->binding_capture = -1;
    app->rendering_settings = 0;
    app->settings_open = 1;
    app->binding_key_consumed = 0;
}
static void normalize_camera_options(int renderer, MarcusRenderOptions *view)
{
    /* 3DMM materials already use Gouraud shading in the authored mode. */
    if (view->display_mode == MARCUS_DISPLAY_SMOOTH) view->display_mode = MARCUS_DISPLAY_TEXTURED;
    if (!marcus_renderer_is_gpu(renderer))
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
    if (!marcus_set_playback_rate(app->player, app->shortcuts.playback_rate, app->shortcuts.playback_modifier))
    {
        SDL_strlcpy(app->settings_message, marcus_error(app->player), sizeof(app->settings_message));
        return;
    }
    refresh(app);
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
    rows = (int)maximum(1, (app->height - app->timeline_y - 112) / ROW_HEIGHT);
    track_row = index + (app->sheet_mode == SHEET_BOTH ? 1 : 0);
    scroll = sheet_scroll(app);
    if (track_row < *scroll)
        *scroll = track_row;
    if (track_row >= *scroll + rows)
        *scroll = track_row - rows + 1;
}
static void ribbon_action(App *app, int action)
{
    MarcusObject object;
    int index, step, match;
    if (action == RIBBON_NEW_MOVIE) new_movie(app);
    else if (action == RIBBON_OPEN) choose_file(app, 0);
    else if (action == RIBBON_SETTINGS) open_settings(app);
    else if (action == RIBBON_NEW_SCENE) choose_background(app);
    else if (action == RIBBON_STORYBOARD)
    {
        if (storyboard_editor_visible(app->storyboard_editor)) storyboard_editor_close(app->storyboard_editor);
        else choose_storyboard(app, !marcus_is_storyboard(app->player), -1);
    }
    else if (action == RIBBON_SAVE_COPY) choose_file(app, 4);
    else if (action == RIBBON_NEW_OBJECT) choose_object(app);
    else if (action == RIBBON_NEW_TEXT) choose_text(app);
    else if (action == RIBBON_EDIT_TEXT) edit_text(app);
    else if (action == RIBBON_CAMERAS) choose_camera_angle(app);
    else if (action == RIBBON_CAMERA_GUIDES)
    {
        app->shortcuts.show_cameras = !app->shortcuts.show_cameras;
        if (!shortcuts_save(app->settings_path, &app->shortcuts, app->settings_message, sizeof(app->settings_message)))
            fail(app, app->settings_message);
    }
    else if (action == RIBBON_SCENES || action == RIBBON_FRAMES)
    {
        app->playing = 0;
        release_camera(app);
        if (action == RIBBON_SCENES) scene_organiser_show(app->scene_organiser, app->player);
        else if (!scene_organiser_show_frames(app->scene_organiser, app->player)) fail(app, "Cannot open the frame organiser.");
    }
    else if (action == RIBBON_PREVIOUS) perform_action(app, MARCUS_PREV_SCENE);
    else if (action == RIBBON_NEXT) perform_action(app, MARCUS_NEXT_SCENE);
    else if (action == RIBBON_OBJECTS) set_sheet_mode(app, SHEET_OBJECTS);
    else if (action == RIBBON_BOTH) set_sheet_mode(app, SHEET_BOTH);
    else if (action == RIBBON_SOUNDS) set_sheet_mode(app, SHEET_SOUNDS);
    else if (action == RIBBON_MUTE)
    {
        app->muted = !app->muted;
        marcus_set_muted(app->player, app->muted);
    }
    else if (action == RIBBON_RESET_MIX) marcus_reset_sound_mix(app->player);
    else if (action == RIBBON_ACTOR || action == RIBBON_PROP || action == RIBBON_WORDS)
    {
        for (step = 1; step <= app->info.object_count; ++step)
        {
            index = (app->selected + step) % app->info.object_count;
            if (!marcus_object(app->player, index, &object)) continue;
            match = action == RIBBON_WORDS ? object.text : action == RIBBON_PROP ? object.prop && !object.text
                                                                                 : !object.prop && !object.text;
            if (match)
            {
                app->selected = index;
                show_object_tracks(app);
                *sheet_scroll(app) = index;
                app->dirty = app->movie_dirty = 1;
                break;
            }
        }
    }
}
static void mouse_down(App *app, float x, float y, int button)
{
    if (app->error[0] || app->settings_open || app->zip_input ||
        igGetIO_Nil()->WantCaptureMouse ||
        igIsPopupOpen_Str(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    if (app->movie_only) {
        if (button == SDL_BUTTON_LEFT && app->playing && app->playback.wait == 1 && inside_movie(app, x, y)) app->continue_click = 1;
        return;
    }
    if (button == SDL_BUTTON_LEFT && app->playing && app->playback.wait == 1 && inside_movie(app, x, y)) { app->continue_click = 1; return; }
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
        app->fly_keys = marcus_fly_held_keys();
        return;
    }

    if (button != SDL_BUTTON_LEFT || app->flying || app->orbiting || app->panning) return;
    if (ui_inside(x, y, app->grid_x, app->timeline_y + 48, app->grid_width - 12, app->height - app->timeline_y - 80)) {
        app->playing = 0; app->dragging = 1;
        if (y < app->timeline_y + 80 || !select_timeline_row(app, x, y, 1)) seek(app, frame_at(app, x));
    }
    else if (inside_movie(app, x, y) || inside_work(app, x, y)) pick_object(app, x, y);
}
static void undo_edit(App *app)
{
    MarcusObject selected, object;
    char title[2200];
    int had_selection, index;
    if (!marcus_can_undo(app->player)) return;
    had_selection = marcus_object(app->player, app->selected, &selected);
    app->playing = app->continue_click = 0;
    release_camera(app);
    finish_movie_scrub(app);
    scene_organiser_close(app->scene_organiser);
    if (!marcus_undo(app->player))
    {
        fail(app, marcus_error(app->player));
        return;
    }
    memset(&app->info, 0, sizeof(app->info));
    refresh(app);
    app->modified = marcus_modified(app->player);
    app->blank_movie = !app->loaded && !app->movie[0];
    app->selected = app->selected_sound = app->selected_sound_track = -1;
    if (had_selection)
        for (index = 0; index < app->info.object_count; ++index)
            if (marcus_object(app->player, index, &object) && object.actor_id == selected.actor_id)
            {
                app->selected = index;
                break;
            }
    app->row_scroll = app->sound_scroll = app->both_scroll = 0;

    app->animation_hint[0] = 0;
    memset(&app->playback, 0, sizeof(app->playback));
    memset(&app->work_image, 0, sizeof(app->work_image));
    memset(&app->movie_image, 0, sizeof(app->movie_image));
    dialog_retire_texture(app->picture);
    dialog_retire_texture(app->movie_picture);
    app->picture = app->movie_picture = NULL;
    if (app->loaded)
    {
        fit_timeline(app);
    }
    if (app->movie_only) editor_view(app);
    app->dirty = app->movie_dirty = 1;
    SDL_snprintf(title, sizeof(title), "Marcus - %s%s", app->movie[0] ? app->movie : "Untitled movie",
                 app->modified ? " (unsaved changes)" : "");
    SDL_SetWindowTitle(app->window, title);
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

    if (key == SDLK_COMMA && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)))
    {
        if (app->movie_only)
            open_rendering_settings(app, 0);
        else
            open_settings(app);
        return;
    }
    if (key == SDLK_Z && (shortcuts_modifiers(modifiers) == MARCUS_MOD_CTRL ||
                          shortcuts_modifiers(modifiers) == MARCUS_MOD_GUI))
    {
        undo_edit(app);
        return;
    }
    if (key == SDLK_S && (shortcuts_modifiers(modifiers) == MARCUS_MOD_CTRL ||
                          shortcuts_modifiers(modifiers) == MARCUS_MOD_GUI))
    {
        if (app->loaded) choose_file(app, 4);
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
static void update_flight(App *app, double seconds)
{
    if (!app->flying || !app->fly_ready || app->error[0])
        return;
    if (marcus_fly_step(&app->flight, app->fly_keys, seconds))
    {
        app->work_matched = 0;
        app->dirty = 1;
    }
}
static void handle_event(App *app, const SDL_Event *event)
{
    DialogRequest *request;
    SDL_FRect viewport;
    int rows, maximum_scroll, *scroll, gizmo_event;
    float x, y, delta;
    SDL_Keymod modifiers;
    update_storyboard_layout(app);
    gizmo_event = prop_gizmo_event(app->prop_gizmo, event);
    if (event->type == SDL_EVENT_QUIT)
        app->running = 0;
    else if ((app->error[0] || app->settings_open || app->zip_input) && dialog_input_event(event))
    {
        if (app->settings_open && app->binding_capture >= 0 && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
        {
            app->binding_key_consumed = 1;
            settings_key(app, event->key.key, event->key.mod);
        }
    }
    else if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_TEXT_EDITING) &&
        ui_keyboard_busy() && !app->flying && !app->orbiting && !storyboard_editor_capturing(app->storyboard_editor)) { }
    else if (!app->error[0] && camera_picker_event(app->camera_picker, event, app->width, app->height))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
    else if (!app->error[0] && sound_dialog_event(app->sound_dialog, event))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
    else if (!app->error[0] && costume_dialog_event(app->costume_dialog, event))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
    else if (!app->error[0] && text_dialog_event(app->text_dialog, event))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
    else if (!app->error[0] && background_browser_event(app->background_browser, event, app->width, app->height))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
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
        else if (request->kind == 4)
        {
            if (!marcus_save_copy(app->player, request->path)) fail(app, marcus_error(app->player));
            else
            {
                char title[1200];
                SDL_snprintf(title, sizeof(title), "Marcus - Saved copy: %s", request->path);
                SDL_SetWindowTitle(app->window, title);
            }
        }
        else if (request->kind == 0)
            open_movie(app, request->path);
        else
        {
            if (request->kind == 1)
                SDL_strlcpy(app->resources, request->path, sizeof(app->resources));
            else
                SDL_strlcpy(app->content, request->path, sizeof(app->content));
            if (app->modified)
            {
                if (!marcus_reload(app->player, app->resources, app->content)) fail(app, marcus_error(app->player));
                refresh(app);
                reset_view(app);
            }
            else if (app->movie[0])
                open_input(app, app->movie_input, app->movie_entry, app->movie);
            background_browser_prepare(app->background_browser, app->player, app->resources, app->content);
        }
        SDL_free(request->path);
        SDL_free(request);
    }
    else if (!app->error[0] && !app->settings_open && !app->zip_input &&
             storyboard_editor_event(app->storyboard_editor, event))
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION && !storyboard_editor_flying(app->storyboard_editor))
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
    }
    else if (gizmo_event)
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
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
        if (app->flying && marcus_fly_key(event->key.scancode))
            app->fly_keys |= marcus_fly_key(event->key.scancode);
        else
            key_down(app, event->key.key, event->key.mod);
    }
    else if (event->type == SDL_EVENT_KEY_UP)
        app->fly_keys &= ~marcus_fly_key(event->key.scancode);
    else if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
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

        }
        else
        {
            if (marcus_fly_look(&app->flight, event->motion.xrel, event->motion.yrel))
            {
                app->work_matched = 0;
                app->dirty = 1;
            }
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
            app->resizing_objects = 0;

            app->dragging = 0;
            app->resizing_view = 0;
            app->resizing_timeline = 0;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && (app->movie_only || app->movie_scrubbing))
        movie_activity(app);
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
            marcus_fly_speed(&app->flight, delta);
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
    const char *name, *slash;
    name = app->movie[0] ? app->movie : "Untitled movie";
    slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    slash = strrchr(name, '\\');
    if (slash) name = slash + 1;
    ribbon_action(app, ribbon_draw(&app->ui, app->width, &app->ribbon_tab, name, app->loaded, app->modified, app->muted,
                app->info.camera_count, app->shortcuts.show_cameras, selected_text(app)));
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
            dialog_retire_texture(*texture);
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
    ui_clip(&clip);
    for (i = 0; i < box.line_count; ++i)
    {
        edge = &box.lines[i];
        ui_line(&app->ui, destination->x + edge->x1 * destination->w, destination->y + edge->y1 * destination->h,
                destination->x + edge->x2 * destination->w, destination->y + edge->y2 * destination->h, bounds.colour);
    }
    ui_unclip();
}
static void record_camera_render(App *app, int work, Uint64 started)
{
    CameraPerformance *performance;
    performance = &app->camera_performance[work];
    performance->pending_ms += (double)(SDL_GetTicksNS() - started) / 1000000.0;
    performance->pending_rendered = 1;
}
static void draw_camera_bar(App *app, int movie)
{
    float left, right;
    int work;
    left = movie ? 0 : work_left(app); right = movie ? app->split_x : view_right(app);
    work = !movie && (app->second_view || app->authored != 1);
    if (ui_panel(movie ? "Movie camera toolbar" : "View toolbar", left, CAMERA_BAR_Y, right - left, 32, 1, 0)) {
        if (ui_button_at("Settings", left + 8, CAMERA_BAR_Y + 2, 68, 26, 0, 1, "Camera rendering settings")) open_rendering_settings(app, work);
        igSetCursorScreenPos(dialog_vec(left + 84, CAMERA_BAR_Y + 6));
        igTextUnformatted(work ? "Work camera" : "Movie camera", NULL);
        if (!movie) {
            if (!app->second_view && ui_button_at("Camera", right - 312, CAMERA_BAR_Y + 2, 84, 26, app->authored == 1, app->loaded, "Movie camera")) view_mode(app, 1);
            igSetCursorScreenPos(dialog_vec(right - 220, CAMERA_BAR_Y + 2)); igSetNextItemWidth(72);
            if (igBeginCombo("##Orbit target", "Orbit", 0)) {
                if (igSelectable_Bool("Scene centre", !app->orbit_selection, 0, dialog_vec(0, 0))) choose_orbit_mode(app, 0);
                igBeginDisabled(!orbit_selection_available(app));
                if (igSelectable_Bool("Selection", app->orbit_selection, 0, dialog_vec(0, 0))) choose_orbit_mode(app, 1);
                igEndDisabled(); igEndCombo();
            }
            if (ui_button_at("Fly", right - 140, CAMERA_BAR_Y + 2, 60, 26, app->authored == 2, app->loaded, "Fly camera")) view_mode(app, 2);
            if (ui_button_at("Reset", right - 72, CAMERA_BAR_Y + 2, 56, 26, 0, app->loaded, "Match the movie camera")) reset_camera_view(app);
        }
    }
    ui_panel_end();
}
static void draw_movie_view(App *app)
{
    const MarcusRenderResolution *resolution;
    MarcusImage image;
    SDL_FRect destination;
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
            ui_image(&app->ui, app->movie_picture, &destination);
        return;
    }
    draw_camera_bar(app, 1);
    ui_fill(&app->ui, destination.x - 1, destination.y - 1, destination.w + 2, destination.h + 2, 0x111316);
    if (app->movie_picture)
    {
        ui_image(&app->ui, app->movie_picture, &destination);
        draw_selection(app, &app->movie_image, &destination);
    }
    ui_fill(&app->ui, app->split_x - 1, VIEW_TOP, 2, app->viewport_bottom - VIEW_TOP, LINE);
    ui_fill(&app->ui, app->split_x - 2, (app->viewport_bottom + VIEW_TOP) / 2 - 15, 4, 30, MUTED);
}
static void draw_view_strip(App *app)
{
    float x, h;
    x = view_right(app); h = app->viewport_bottom - VIEW_TOP;
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, dialog_vec(0, 0));
    if (ui_panel("Camera split toggle", x, VIEW_TOP, VIEW_STRIP_WIDTH, h, 1, 0)) {
        if (igButton(app->second_view ? ">" : "<", dialog_vec(VIEW_STRIP_WIDTH, h))) toggle_second_view(app);
        if (igIsItemHovered(0)) igSetTooltip("%s work camera", app->second_view ? "Collapse" : "Expand");
    }
    ui_panel_end();
    if (app->second_view) {
        if (ui_panel("Camera divider", app->split_x - 4, VIEW_TOP, 8, h, 1, 0)) {
            igInvisibleButton("Resize cameras", dialog_vec(8, h), 0); app->resizing_view = igIsItemActive();
            if (igIsItemHovered(0) || app->resizing_view) igSetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (app->resizing_view) { app->split_x = (float)maximum(330, minimum(view_right(app) - 330, app->split_x + igGetIO_Nil()->MouseDelta.x)); app->split_fraction = app->split_x / view_right(app); }
        }
        ui_panel_end();
    }
    igPopStyleVar(1);
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
static void draw_camera_guides(App *app, const SDL_FRect *destination)
{
    MarcusProjection camera;
    MarcusCameraGuide guide;
    SDL_Rect clip;
    SDL_FRect labels[64], label_rect;
    unsigned long index, colour;
    int pass, line, label_count, placed, active;
    float x, y;
    char label[64];
    const MarcusGuideLine *segment;
    if (!app->loaded || app->authored == 1 || !app->picture) return;
    clip.x = (int)destination->x;
    clip.y = (int)destination->y;
    clip.w = (int)destination->w;
    clip.h = (int)destination->h;
    label_count = 0;
    ui_clip(&clip);
    /* Draw the active camera last so its highlight remains visible. */
    for (pass = 0; pass < 2; ++pass)
        for (index = 0; index < app->info.camera_count; ++index)
        {
            active = index == app->info.camera;
            /* Looking through the active camera must not draw its own guide
             * over the shot. The visibility toggle only affects other cameras. */
            if (active != pass || (active && app->work_matched) ||
                (!active && !app->shortcuts.show_cameras)) continue;
            if (active) camera = app->scene_camera;
            else if (!marcus_camera_projection(app->player, index, &camera)) continue;
            camera_guide_build_image(&app->work_image, &camera, &guide);
            colour = active ? 0xefbd78 : 0x78b7c4;
            for (line = 0; line < guide.line_count; ++line)
            {
                segment = &guide.lines[line];
                ui_line(&app->ui, destination->x + segment->x1 * destination->w, destination->y + segment->y1 * destination->h,
                        destination->x + segment->x2 * destination->w, destination->y + segment->y2 * destination->h, colour);
            }
            if (!guide.has_label) continue;
            x = destination->x + guide.label_x * destination->w;
            y = destination->y + guide.label_y * destination->h;
            label_rect.x = (float)maximum(destination->x + 4, minimum(x + 8, destination->x + destination->w - 100));
            label_rect.y = (float)maximum(destination->y + 4, minimum(y - 22, destination->y + destination->h - 20));
            label_rect.w = 96;
            label_rect.h = 18;
            for (placed = 0; placed < label_count; ++placed)
                if (label_rect.x < labels[placed].x + labels[placed].w && label_rect.x + label_rect.w > labels[placed].x &&
                    label_rect.y < labels[placed].y + labels[placed].h && label_rect.y + label_rect.h > labels[placed].y)
                {
                    label_rect.y = labels[placed].y + labels[placed].h;
                    if (label_rect.y + label_rect.h > destination->y + destination->h) break;
                    placed = -1;
                }
            label_rect.y = (float)minimum(label_rect.y, destination->y + destination->h - 20);
            if (label_count < 64) labels[label_count++] = label_rect;
            ui_fill(&app->ui, x - 2, y - 2, 5, 5, colour);
            ui_fill(&app->ui, label_rect.x - 2, label_rect.y, label_rect.w, label_rect.h, BG);
            SDL_snprintf(label, sizeof(label), "Camera %02lu", index + 1);
            ui_text(&app->ui, label_rect.x, label_rect.y + 1, label, colour);
        }
    ui_unclip();
}
static void draw_view(App *app)
{
    const MarcusRenderResolution *resolution;
    MarcusImage image;
    SDL_FRect destination;
    int rendered, render_width, render_height, work, target;
    float left, width, right;
    MarcusUI *ui;
    Uint64 started;
    ui = &app->ui;
    update_storyboard_layout(app);
    if (storyboard_editor_visible(app->storyboard_editor))
    {
        ui_fill(ui, 0, VIEW_TOP, app->sidebar, app->viewport_bottom - VIEW_TOP, BG);
        storyboard_editor_draw(app->storyboard_editor, ui);
        ui_text(ui, 16, app->viewport_bottom - 22, "Painted background / no scenery depth", MUTED);
        return;
    }
    ui_fill(ui, 0, VIEW_TOP, app->sidebar, app->viewport_bottom - VIEW_TOP, BG);
    draw_view_strip(app);
    draw_camera_bar(app, 0);
    left = work_left(app);
    right = view_right(app);
    width = right - left;
    work = app->second_view || app->authored != 1;
    if (app->loaded)
    {
        target = app->orbit_selection && app->selected >= 0 && app->selected < app->info.object_count
                     ? app->selected : -1;
        if (target != app->orbit_target)
        {
            app->orbit_target = target;
            app->dirty = 1;
            if (app->orbit_selection && !app->authored)
                app->work_matched = app->work_needs_match = 0;
        }
        if (!marcus_set_orbit_target(app->player, target))
            fail(app, marcus_error(app->player));
    }
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
    if (!app->loaded) {
        if (ui_panel("Start a movie", 32, VIEW_TOP + 100, app->sidebar - 64, 200, 1, 0)) {
            igTextUnformatted(app->blank_movie ? "Untitled movie" : "A new view of your movies", NULL);
            igTextWrapped("Choose a background for your first scene, or open a movie to explore.");
            if (igButton("New scene", dialog_vec(160, 32))) choose_background(app);
            igTextDisabled("Settings > Content locations chooses your original 3DMM files.");
        }
        ui_panel_end(); return;
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
            app->scene_camera = image.scene_camera;
            app->camera_ready = 1;
            if (!update_picture(app, &app->picture, &image))
                fail(app, SDL_GetError());
        }
        else
        {
            dialog_retire_texture(app->picture);
            app->picture = NULL;
            fail(app, marcus_error(app->player));
        }
    }
    destination = viewport_rect(app, 0);
    ui_fill(ui, destination.x - 1, destination.y - 1, destination.w + 2, destination.h + 2, 0x111316);
    if (app->picture)
        ui_image(&app->ui, app->picture, &destination);
    draw_camera_guides(app, &destination);
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
        if (app->resizing_view || ui_inside(ui->mouse_x, ui->mouse_y, app->split_x - 4, VIEW_TOP, 8,
                                           app->viewport_bottom - VIEW_TOP))
            ui->hint = "Drag to resize the movie and work camera views";
    }
}
static void apply_prop_changes(App *app)
{
    int moved;
    moved = prop_gizmo_update(app->prop_gizmo);
    if (!moved) return;
    app->prop_interacting = 1; app->dirty = app->movie_dirty = 1;
    if (moved < 0) fail(app, prop_gizmo_error(app->prop_gizmo));
    else if (moved == 2) { app->modified = 1; marcus_info(app->player, &app->info); }
}
static void draw_sound_properties(App *app)
{
    MarcusSound sound;
    MarcusSoundTrack track;
    MarcusWaveformInfo waveform;
    MarcusWaveformColumn peak;
    igSeparatorText("Sound properties");
    if (!marcus_sound(app->player, app->selected_sound, &sound)) {
        if (marcus_sound_track(app->player, app->selected_sound_track, &track)) igText("%s / %d cues", track.name, track.cue_count);
        else igTextDisabled("Select a sound cue or waveform.");
        return;
    }
    igTextWrapped("%s", sound.name);
    igText("%s / %s", sound.style == MARCUS_SOUND_MUSIC ? "Music" : sound.style == MARCUS_SOUND_SPEECH ? "Speech" : "Sound effect", sound.owner);
    igBeginDisabled(app->playing);
    if (igButton("Edit sound...", dialog_vec(-1, 0))) sound_dialog_open(app->sound_dialog, app->player, app->window, app->selected_sound);
    igEndDisabled();
    if (sound.carried) igText("From scene %d / frame %.0f", sound.origin_scene + 1, sound.origin_frame);
    else igText("Cue frame: %.0f", display_frame(app, sound.frame));
    igText("Volume: %.1f%%", sound.volume * 100.0 / 65536.0);
    igText("Loop: %s / Queued: %s", sound.loop ? "yes" : "no", sound.queue ? "yes" : "no");
    igText("Silence: %s", sound.silent ? "yes" : "no");
    if (sound.motion) igText("Trigger: cel %ld", sound.motion_cel);
    else igTextUnformatted("Trigger: frame cue", NULL);
    igText("Sound: %ld : %08lX / child %lu", sound.source_id, sound.chunk, sound.child);
    if (marcus_sound_waveform(app->player, app->selected_sound, 0, 1, 1, &peak, &waveform)) {
        if (waveform.kind == MARCUS_WAVEFORM_SAMPLED) igText("Duration: %.3f s (one play)", waveform.seconds);
        else igTextWrapped("%s", waveform.message);
    }
    igTextDisabled(sound.carried ? "Continues from an earlier scene." : "Scrubbing is silent.");
}
static void draw_object_properties(App *app)
{
    MarcusObject object;
    ImVec2 cursor;
    int editable;
    igSeparatorText("Object properties");
    if (!marcus_object(app->player, app->selected, &object)) { igTextDisabled("Select an object in the list or timeline."); return; }
    igTextWrapped("%s", object.name);
    igTextUnformatted(object.text ? "3D text" : object.prop ? "Prop" : "Actor", NULL);
    if (object.text) { if (igButton("Edit 3D text", dialog_vec(-1, 0))) edit_text(app); }
    else {
        cursor = igGetCursorScreenPos();
        costume_dialog_button(app->costume_dialog, app->player, app->selected, cursor.x, cursor.y,
            object.visible && !app->playing && !app->settings_open && !app->error[0]);
    }
    igText("Visibility: %s", object.visible ? "On stage" : "Off stage");
    igSeparatorText(prop_gizmo_mode(app->prop_gizmo) == 1 ? "Rotation (degrees)" : prop_gizmo_mode(app->prop_gizmo) == 2 ? "Size (percent)" : "World position");
    editable = prop_gizmo_properties(app->prop_gizmo); app->prop_fields = editable;
    if (editable) apply_prop_changes(app);
    else if (object.has_lifetime) igText("X %.3f / Y %.3f / Z %.3f", (double)object.x, (double)object.y, (double)object.z);
    else igTextDisabled("No entrance / off stage");
    igSeparator(); igText("Actor ID: %ld", object.actor_id);
    igText("Action / cel: %ld / %ld", object.action, object.cel);
    if (object.last_frame < app->info.first_frame || object.first_frame > app->info.last_frame) igTextUnformatted("Lifetime: outside scene", NULL);
    else igText("Lifetime: %.0f - %.0f", display_frame(app, maximum(object.first_frame, app->info.first_frame)), display_frame(app, minimum(object.last_frame, app->info.last_frame)));
    igText("Template: %ld : %08lX", object.source_id, object.template_chunk);
}
static void draw_sidebar(App *app)
{
    MarcusObject object;
    ImGuiListClipper *clip;
    int i, reveal;
    reveal = app->selected != app->list_selection;
    if (ui_panel("Scene objects", app->sidebar, VIEW_TOP, 300, app->object_split - VIEW_TOP - 4, 0, 1)) {
        igText("Scene objects / %d", app->loaded ? app->info.object_count : 0);
        if (igBeginChild_Str("Object list", dialog_vec(0, 0), 0, 0)) {
            clip = ImGuiListClipper_ImGuiListClipper();
            if (clip) {
                ImGuiListClipper_Begin(clip, app->loaded ? app->info.object_count : 0, ROW_HEIGHT);
                if (reveal && app->selected >= 0) ImGuiListClipper_IncludeItemByIndex(clip, app->selected);
                while (ImGuiListClipper_Step(clip)) for (i = clip->DisplayStart; i < clip->DisplayEnd; ++i) {
                    if (!marcus_object(app->player, i, &object)) continue;
                    igPushID_Int(i);
                    if (igSelectable_Bool(object.name, i == app->selected, 0, dialog_vec(0, ROW_HEIGHT - igGetStyle()->ItemSpacing.y))) { app->selected = i; show_object_tracks(app); }
                    if (reveal && i == app->selected) igSetScrollHereY(.5f);
                    if (igIsItemHovered(0)) igSetTooltip("%s / %s", object.text ? "3D text" : object.prop ? "Prop" : "Actor", object.visible ? "On stage" : "Off stage");
                    igPopID();
                }
                ImGuiListClipper_destroy(clip);
            }
        }
        igEndChild();
    }
    ui_panel_end();
    app->list_selection = app->selected;
    if (ui_panel("Object list divider", app->sidebar, app->object_split - 4, 300, 8, 1, 0)) {
        igSetCursorScreenPos(dialog_vec(app->sidebar, app->object_split - 4)); igInvisibleButton("Resize properties", dialog_vec(300, 8), 0);
        app->resizing_objects = igIsItemActive();
        if (igIsItemHovered(0) || app->resizing_objects) igSetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (app->resizing_objects) { app->object_split += igGetIO_Nil()->MouseDelta.y; layout(app); }
    }
    ui_panel_end();
    if (ui_panel("Inspector", app->sidebar, app->object_split + 4, 300, app->height - 28 - app->object_split, 0, 1)) {
        if (app->sheet_mode == SHEET_SOUNDS || (app->sheet_mode == SHEET_BOTH && app->sound_inspector)) draw_sound_properties(app);
        else draw_object_properties(app);
    }
    ui_panel_end();
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
    double seconds, low, high;
    int changed;
    rect = movie_scrubber_rect(app); low = 0; high = app->movie_time.duration_seconds;
    seconds = app->movie_scrubbing ? app->movie_scrub_seconds : app->movie_time.position_seconds;
    igSetCursorScreenPos(dialog_vec(rect.x, rect.y + (rect.h - 20) / 2)); igSetNextItemWidth(rect.w);
    igBeginDisabled(!available || high <= 0);
    changed = igSliderScalar("##Movie position", ImGuiDataType_Double, &seconds, &low, &high, "", ImGuiSliderFlags_AlwaysClamp);
    if (igIsItemActivated()) { release_camera(app); app->movie_resume = app->playing; app->playing = 0; app->movie_scrubbing = 1; movie_activity(app); }
    if (changed) seek_movie(app, seconds);
    if (igIsItemDeactivated()) finish_movie_scrub(app);
    if (igIsItemHovered(0)) igSetTooltip("%.2f / %.2f seconds / drag to seek across scenes", seconds, high);
    igEndDisabled();
}
static void open_organiser(App *app, int frames)
{
    app->playing = 0; release_camera(app); app->dragging = 0;
    if (frames) { if (!scene_organiser_show_frames(app->scene_organiser, app->player)) fail(app, "Cannot open the frame organiser."); }
    else scene_organiser_show(app->scene_organiser, app->player);
}
static void toggle_mute(App *app) { app->muted = !app->muted; marcus_set_muted(app->player, app->muted); }
static void draw_transport(App *app)
{
    static const char *nav[] = {"|<", "<", ">", ">|"};
    char label[96];
    int i, enabled, extending, mode;
    float y;
    y = app->timeline_y;
    if (ui_panel("Transport", 0, y, app->sidebar, 80, 1, 0)) {
        extending = app->loaded && !app->playing && (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
        for (i = 0; i < 4; ++i) {
            enabled = app->loaded && (i < 2 ? app->info.frame > app->info.first_frame : app->info.frame < app->info.last_frame);
            if ((i == 1 || i == 2) && extending) enabled = 1;
            SDL_snprintf(label, sizeof(label), "%s###Frame%d", nav[i], i);
            if (ui_button_at(label, FRAME_NAV_X + NAV_STEP * i, y + FRAME_NAV_Y, 28, 20, 0, enabled, "Frame navigation / Ctrl at an end adds a frame")) {
                if (extending && ((i == 1 && app->info.frame == app->info.first_frame) || (i == 2 && app->info.frame == app->info.last_frame))) add_frame(app, i == 1 ? -1 : 1);
                else perform_action(app, MARCUS_FIRST_FRAME + i);
            }
            enabled = app->loaded && (i < 2 ? app->info.scene > 0 : app->info.scene + 1 < app->info.scene_count);
            SDL_snprintf(label, sizeof(label), "%s###Scene%d", nav[i], i);
            if (ui_button_at(label, SCENE_NAV_X + NAV_STEP * i, y + SCENE_NAV_Y, 28, 20, 0, enabled, "Scene navigation")) perform_action(app, MARCUS_FIRST_SCENE + i);
        }
        if (app->loaded) SDL_snprintf(label, sizeof(label), "Frame %.0f / %.0f###Frames", display_frame(app, app->info.frame), display_frame(app, app->info.last_frame));
        else SDL_strlcpy(label, "Frame --###Frames", sizeof(label));
        if (ui_button_at(label, 16, y + 4, 164, 20, 0, app->loaded, "Open frame organiser")) open_organiser(app, 1);
        if (app->loaded) SDL_snprintf(label, sizeof(label), "Scene %d / %d###Scenes", app->info.scene + 1, app->info.scene_count);
        else SDL_strlcpy(label, "Scene --###Scenes", sizeof(label));
        if (ui_button_at(label, 16, y + 26, 164, 20, 0, app->loaded, "Open scene organiser")) open_organiser(app, 0);
        if (ui_button_at(app->playback.wait == 1 ? "Continue" : app->playing ? "Pause" : "Play", PLAY_X, y + 2, 48, 44, 0, app->loaded, "Play / pause / Shift opens movie view")) {
            if (igGetIO_Nil()->KeyShift) { app->movie_only = app->movie_dirty = 1; movie_activity(app); if (!app->playing || app->playback.wait == 1) toggle_play(app); }
            else toggle_play(app);
        }
        if (ui_button_at("Loop", LOOP_X, y + 4, 60, 20, app->loop, app->loaded, "Repeat this scene")) app->loop = !app->loop;
        if (ui_button_at(app->muted ? "Unmute" : "Mute", LOOP_X, y + 26, 60, 20, app->muted, 1, "Mute movie audio")) toggle_mute(app);
        if (ui_button_at("-###Zoom out", app->sidebar - 132, y + 2, 32, 44, 0, app->loaded, "Zoom timeline out")) zoom_timeline(app, .8f, app->grid_x + app->grid_width / 2);
        if (ui_button_at("+###Zoom in", app->sidebar - 94, y + 2, 32, 44, 0, app->loaded, "Zoom timeline in")) zoom_timeline(app, 1.25f, app->grid_x + app->grid_width / 2);
        if (ui_button_at("Fit", app->sidebar - 56, y + 2, 40, 44, 0, app->loaded, "Fit scene frames")) fit_timeline(app);
        if (app->movie_time_available) {
            format_time(label, sizeof(label), app->movie_time.position_seconds, app->movie_time.duration_seconds >= 3600);
            igSetCursorScreenPos(dialog_vec(LOOP_X + 72, y + 5)); igTextUnformatted(label, NULL);
            format_time(label, sizeof(label), app->movie_time.duration_seconds, app->movie_time.duration_seconds >= 3600);
            igSetCursorScreenPos(dialog_vec(LOOP_X + 72, y + 27)); igTextUnformatted(label, NULL);
        }
        draw_movie_scrubber(app, app->movie_time_available);
        igSetCursorScreenPos(dialog_vec(10, y + 50)); igSetNextItemWidth(106); mode = app->sheet_mode;
        if (igBeginCombo("##Track types", sheet_name(mode), 0)) {
            for (i = 0; i < 3; ++i) if (igSelectable_Bool(sheet_name(i), mode == i, 0, dialog_vec(0, 0))) set_sheet_mode(app, i);
            igEndCombo();
        }
        if (ui_button_at("Reset mix", 124, y + 50, 80, 24, 0, app->loaded && marcus_sound_mix_active(app->player), "Clear temporary layer solos and mutes")) marcus_reset_sound_mix(app->player);
    }
    ui_panel_end();
}
static void draw_track_labels(App *app, int rows)
{
    MarcusObject object;
    MarcusSoundTrack track;
    int row, kind, index, lane, selected, limit;
    int *scroll;
    float top;
    char label[320];
    scroll = sheet_scroll(app); top = app->timeline_y + 80;
    if (ui_panel("Track labels", 0, top, app->grid_x, (float)(rows * ROW_HEIGHT), 1, 0)) {
        for (row = 0; row < rows; ++row) {
            kind = sheet_row(app, row + *scroll, &index, &lane);
            if (!app->loaded || !kind) continue;
            if (kind == SHEET_SOUND_ROW && !marcus_sound_track(app->player, index, &track)) continue;
            if (kind == SHEET_OBJECT_ROW && !marcus_object(app->player, index, &object)) continue;
            /* Cursor positioning must be followed by an item, including when
             * the movie has fewer tracks than the panel has visible rows. */
            igPushID_Int(row); igSetCursorScreenPos(dialog_vec(8, top + row * ROW_HEIGHT + 2));
            if (kind == SHEET_OBJECT_HEADER || kind == SHEET_SOUND_HEADER) igTextUnformatted(kind == SHEET_OBJECT_HEADER ? "Objects" : "Sounds", NULL);
            else if (kind == SHEET_SOUND_ROW) {
                if (igSelectable_Bool(track.name, app->selected_sound_track == index && app->sound_inspector, 0, dialog_vec(app->grid_x - 76, 22))) { app->selected_sound_track = index; app->selected_sound = -1; app->sound_inspector = 1; }
                if (ui_button_at("S", app->grid_x - 58, top + row * ROW_HEIGHT + 2, 24, 22, track.solo, 1, "Solo layer across scenes")) marcus_set_sound_track_mix(app->player, index, track.muted, !track.solo);
                if (ui_button_at("M", app->grid_x - 30, top + row * ROW_HEIGHT + 2, 24, 22, track.muted, 1, "Mute layer across scenes")) marcus_set_sound_track_mix(app->player, index, !track.muted, track.solo);
            }
            else if (kind == SHEET_OBJECT_ROW) {
                if (lane) igTextDisabled("    %s", lane == 1 ? "Transform" : lane == 2 ? "Appearance" : lane == 3 ? "Timing" : "Visibility");
                else {
                    selected = app->selected == index;
                    if (igSmallButton(selected && app->animation_expanded ? "-" : "+")) { app->animation_expanded = selected ? !app->animation_expanded : 1; app->selected = index; app->sound_inspector = 0; }
                    igSameLine(30, 0); SDL_snprintf(label, sizeof(label), "%s###Object", object.name);
                    if (igSelectable_Bool(label, selected && !app->sound_inspector, 0, dialog_vec(app->grid_x - 38, 22))) { app->selected = index; app->sound_inspector = 0; }
                }
            }
            igPopID();
        }
    }
    ui_panel_end();
    limit = sheet_row_count(app) - rows; if (limit < 0) limit = 0;
    if (ui_panel("Track scroll", app->sidebar - 14, top, 14, (float)(rows * ROW_HEIGHT), 1, 0)) {
        igSetCursorScreenPos(dialog_vec(app->sidebar - 14, top));
        igVSliderInt("##Scroll tracks", dialog_vec(14, (float)(rows * ROW_HEIGHT)), scroll, 0, limit, "", 0);
    }
    ui_panel_end();
}
static void draw_timeline(App *app)
{
    MarcusUI *ui;
    MarcusObject object;
    MarcusSound sound;
    SDL_Rect clip;
    char label[320];
    int row, index, rows, selected_row, lane, kind, scroll, active;
    float x, y, left;
    double frame, tick, end;
    unsigned long tint;
    ui = &app->ui;
    y = app->timeline_y;
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, dialog_vec(0, 0));
    if (ui_panel("Timeline divider", 0, app->viewport_bottom, app->sidebar, TIMELINE_HANDLE_HEIGHT, 0, 0)) {
        igInvisibleButton("Resize timeline", dialog_vec(app->sidebar, TIMELINE_HANDLE_HEIGHT), 0);
        app->resizing_timeline = igIsItemActive();
        if (igIsItemHovered(0) || app->resizing_timeline) igSetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (app->resizing_timeline) { app->timeline_height -= igGetIO_Nil()->MouseDelta.y; layout(app); }
    }
    ui_panel_end(); igPopStyleVar(1);
    ui_fill(ui, 0, y, app->sidebar, (float)app->height - y - 24, PANEL);
    ui_line(ui, 0, y, app->sidebar, y, LINE);
    draw_transport(app);
    rows = (int)((app->height - y - 112) / ROW_HEIGHT);
    scroll = *sheet_scroll(app);
    draw_track_labels(app, rows); scroll = *sheet_scroll(app);
    for (row = 0; row < rows; ++row) {
        kind = sheet_row(app, row + scroll, &index, &lane);
        selected_row = kind == SHEET_SOUND_ROW ? app->selected_sound_track : app->selected;
        active = app->sheet_mode != SHEET_BOTH || app->sound_inspector == (kind == SHEET_SOUND_ROW);
        ui_fill(ui, 0, y + 80 + row * ROW_HEIGHT, app->sidebar, ROW_HEIGHT,
            app->loaded && index >= 0 && index == selected_row && active ? 0x303f45 : row % 2 ? 0x23262b : 0x272a30);
        ui_line(ui, 0, y + 80 + (row + 1) * ROW_HEIGHT, app->sidebar, y + 80 + (row + 1) * ROW_HEIGHT, 0x30333a);
    }
    if (app->loaded && app->sheet_mode == SHEET_SOUNDS && !app->info.sound_count)
        ui_text(ui, app->grid_x + 18, y + 94, "No authored sound cues in this scene.", MUTED);
    clip.x = (int)app->grid_x;
    clip.y = (int)y + 48;
    clip.w = (int)app->grid_width;
    clip.h = app->height - (int)y - 80;
    ui_clip(&clip);
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
        /* The thumb's one-pixel grip starts four pixels from its left edge.
         * Snap both grip and marker to the same filled pixel column. */
        x = floorf(frame_x(app, app->info.frame + 0.5));
        ui_fill(ui, x, y + 48, 1, (float)app->height - y - 80, 0x6d77d0);
        ui_fill(ui, x - 3, y + 48, 7, 18, frame_has_sound(app) ? 0xe4b575 : 0x9c9cff);
    }
    ui_unclip();
    ui_line(ui, app->grid_x, y + 48, app->grid_x, (float)app->height - 32, LINE);
    ui_line(ui, app->sidebar, y, app->sidebar, (float)app->height - 24, LINE);
}

static void draw_error(App *app)
{
    int open, dismissed;
    open = 1;
    if (!dialog_begin("Unable to display this movie###MarcusError", &open, app->width, app->height, 680, 300)) {
        if (!open) app->error[0] = 0;
        return;
    }
    igTextWrapped("%s", app->error);
    igSpacing();
    dismissed = igButton("Dismiss", dialog_vec(120, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0) || igShortcut_Nil(ImGuiKey_Enter, 0);
    dialog_end(dismissed || !open);
    if (dismissed || !open) app->error[0] = 0;
}
static void draw_debug_panel(App *app, float x, float y, int camera, int frozen);
static void draw_rendering_settings(App *app)
{
    static const char *renderers[] = {"BRender CPU C", "BRender compute", "SDL GPU", "BRender CPU ASM x86"};
    static const char *aa[] = {"Off", "2x SSAA", "4x SSAA"};
    static const char *filters[] = {"Nearest", "Bilinear"};
    static const char *axes[] = {"Free", "Front", "Side", "Top"};
    const char *resolutions[MARCUS_RESOLUTION_COUNT];
    MarcusRenderOptions *view;
    int *renderer, *resolution, *overlay, gpu, i, index, count;
    float history[DEBUG_HISTORY], fps_history[DEBUG_HISTORY], maximum_ms, maximum_fps;
    int work;
    work = app->settings_work_camera;
    renderer = work ? &app->edited_shortcuts.work_renderer : &app->edited_shortcuts.renderer;
    resolution = work ? &app->edited_shortcuts.work_render_resolution : &app->edited_shortcuts.render_resolution;
    overlay = work ? &app->edited_shortcuts.work_debug_overlay : &app->edited_shortcuts.movie_debug_overlay;
    view = work ? &app->edited_shortcuts.work_view : &app->edited_shortcuts.movie_view;
    igTextUnformatted(work ? "Work camera" : "Movie camera", NULL);
    if (igButton("Back to settings", dialog_vec(160, 0))) { app->rendering_settings = 0; return; }
    igSeparator();
    igSetNextItemWidth(320);
    if (igCombo_Str_arr("Renderer", renderer, renderers, MARCUS_RENDERER_COUNT, -1)) normalize_camera_options(*renderer, view);
    gpu = marcus_renderer_is_gpu(*renderer);
    for (i = 0; i < MARCUS_RESOLUTION_COUNT; ++i) resolutions[i] = marcus_render_resolution(i)->label;
    igSetNextItemWidth(320); igCombo_Str_arr("Resolution", resolution, resolutions, MARCUS_RESOLUTION_COUNT, -1);
    igSetNextItemWidth(320);
    if (igBeginCombo("Display mode", marcus_display_mode_name(view->display_mode), 0)) {
        for (i = 0; i < MARCUS_DISPLAY_COUNT; ++i) {
            if (i == MARCUS_DISPLAY_SMOOTH || (!gpu && i >= MARCUS_DISPLAY_DEPTH)) continue;
            if (igSelectable_Bool(marcus_display_mode_name(i), view->display_mode == i, 0, dialog_vec(0, 0))) view->display_mode = i;
        }
        igEndCombo();
    }
    igBeginDisabled(!gpu);
    dialog_checkbox("Wire overlay", &view->wire_overlay);
    igSetNextItemWidth(320); igCombo_Str_arr("Anti-aliasing", &view->antialias, aa, 3, -1);
    igSetNextItemWidth(320); igCombo_Str_arr("Texture filter", &view->texture_filter, filters, 2, -1);
    igEndDisabled();
    if (work) {
        dialog_checkbox("Orthographic projection", &view->orthographic);
        igSetNextItemWidth(320);
        if (igCombo_Str_arr("View preset", &view->axis_view, axes, 4, -1) && view->axis_view) view->orthographic = 1;
    }
    dialog_checkbox("Show camera FPS and frame time", overlay);
    igSeparatorText("Captured performance");
    igText("Last render: %.2f ms", app->settings_performance.last_ms);
    maximum_ms = 1; maximum_fps = 60; count = app->settings_frame_count;
    for (i = 0; i < count; ++i) {
        index = (app->settings_frame_next - count + i + DEBUG_HISTORY) % DEBUG_HISTORY;
        history[i] = (float)app->settings_performance.render_ms[index];
        fps_history[i] = (float)app->settings_performance.fps[index];
        if (history[i] > maximum_ms) maximum_ms = history[i];
        if (fps_history[i] > maximum_fps) maximum_fps = fps_history[i];
    }
    igPlotLines_FloatPtr("Render FPS", fps_history, count, 0, NULL, 0, maximum_fps, dialog_vec(480, 90), sizeof(float));
    igPlotLines_FloatPtr("Render time (ms)", history, count, 0, NULL, 0, maximum_ms, dialog_vec(480, 90), sizeof(float));
    igTextDisabled("Captured before opening settings. Playback is paused.");
}
static void draw_settings(App *app)
{
    static const char *rates[] = {"Original", "6 FPS", "12 FPS", "24 FPS", "Uncapped"};
    static const char *modifiers[] = {"Normal (1x)", "Win98 -> XP (1.35805x)", "XP -> Win98 (0.918485x)"};
    char binding[128];
    int open, row, cancel, apply;
    open = app->settings_open; cancel = apply = 0;
    if (!dialog_begin("Settings###MarcusSettings", &open, app->width, app->height, 960, 0)) {
        if (!open) cancel_settings(app);
        return;
    }
    if (igBeginChild_Str("Settings contents", dialog_vec(0, -70), 0, 0)) {
        if (app->rendering_settings) draw_rendering_settings(app);
        else {
            igSeparatorText("Content locations");
            if (igButton("Palette and shading...", dialog_vec(220, 0))) { app->binding_capture = -1; choose_file(app, 1); }
            igSameLine(0, 10);
            if (igButton("3DMM content folder...", dialog_vec(220, 0))) { app->binding_capture = -1; choose_file(app, 2); }
            igTextWrapped("%s", app->resources); igTextWrapped("%s", app->content);
            igSeparatorText("Keyboard shortcuts");
            igTextWrapped(app->binding_capture >= 0 ? "Press a key. Escape cancels capture; Backspace clears the binding." : "Choose a binding to change it.");
            if (igBeginTable("Bindings", 2, ImGuiTableFlags_SizingStretchSame, dialog_vec(0, 0), 0)) {
                for (row = 0; row < MARCUS_ACTION_COUNT; ++row) {
                    igPushID_Int(row); igTableNextColumn(); igTextUnformatted(shortcuts_action_name(row), NULL); igTableNextColumn();
                    shortcuts_label(&app->edited_shortcuts.binding[row], binding, sizeof(binding));
                    if (igButton(row == app->binding_capture ? "Press a key..." : binding, dialog_vec(-1, 0))) {
                        app->binding_capture = row; app->settings_message[0] = 0;
                    }
                    igPopID();
                }
                igEndTable();
            }
            igSeparatorText("Playback");
            dialog_checkbox("Pause playback on focus loss", &app->edited_shortcuts.pause_on_focus_loss);
            igSetNextItemWidth(300); igCombo_Str_arr("Movie frame rate", &app->edited_shortcuts.playback_rate, rates, 5, -1);
            igSetNextItemWidth(300); igCombo_Str_arr("Compatibility speed modifier", &app->edited_shortcuts.playback_modifier, modifiers, 3, -1);
            igSeparatorText("Rendering");
            if (igButton("Movie camera settings...", dialog_vec(260, 0))) {
                app->rendering_settings = 1; app->settings_work_camera = 0;
                app->settings_performance = app->camera_performance[0];
                app->settings_frame_next = app->frame_next; app->settings_frame_count = app->frame_count;
            }
            igSameLine(0, 10);
            if (igButton("Work camera settings...", dialog_vec(260, 0))) {
                app->rendering_settings = 1; app->settings_work_camera = 1;
                app->settings_performance = app->camera_performance[1];
                app->settings_frame_next = app->frame_next; app->settings_frame_count = app->frame_count;
            }
            dialog_checkbox("App debug overlay (FPS and frame time)", &app->edited_shortcuts.debug_overlay);
            if (igButton("Restore defaults", dialog_vec(180, 0))) {
                shortcuts_defaults(&app->edited_shortcuts); app->binding_capture = -1;
                SDL_strlcpy(app->settings_message, "Defaults restored. Apply to save them.", sizeof(app->settings_message));
            }
        }
    }
    igEndChild(); igTextWrapped("%s", app->settings_message); igSeparator();
    cancel = igButton("Cancel", dialog_vec(100, 0)); igSameLine(0, 10);
    apply = igButton("Apply", dialog_vec(120, 0));
    if (app->binding_capture < 0 && !app->binding_key_consumed) {
        if (igShortcut_Nil(ImGuiKey_Escape, 0)) cancel = 1;
        if (!igIsAnyItemActive() && igShortcut_Nil(ImGuiKey_Enter, 0)) apply = 1;
    }
    app->binding_key_consumed = 0;
    if (cancel || !open) cancel_settings(app);
    else if (apply) apply_settings(app);
    dialog_end(!app->settings_open);
}
static void draw_zip(App *app)
{
    ImGuiListClipper *clip;
    MarcusMovieEntry member;
    char label[1152];
    int open, row, count, accept, cancel;
    open = app->zip_input != NULL; accept = cancel = 0;
    if (!dialog_begin("Choose a movie from this ZIP###MarcusZip", &open, app->width, app->height, 1000, 720)) {
        if (!open) cancel_zip(app);
        return;
    }
    igTextWrapped("%s", app->zip_path); count = marcus_input_count(app->zip_input);
    if (igBeginChild_Str("ZIP movies", dialog_vec(0, -68), 0, 0)) {
        clip = ImGuiListClipper_ImGuiListClipper();
        if (clip) {
            ImGuiListClipper_Begin(clip, count, igGetTextLineHeightWithSpacing());
            while (ImGuiListClipper_Step(clip)) for (row = clip->DisplayStart; row < clip->DisplayEnd; ++row) {
                if (!marcus_input_entry(app->zip_input, row, &member)) continue;
                igPushID_Int(row); SDL_snprintf(label, sizeof(label), "%s / %.1f MiB", member.name, member.bytes / (1024 * 1024));
                if (igSelectable_Bool(label, row == app->zip_selected, ImGuiSelectableFlags_AllowDoubleClick, dialog_vec(0, 0))) {
                    zip_select(app, row); if (igIsMouseDoubleClicked_Nil(0)) accept = 1;
                }
                igPopID();
            }
            ImGuiListClipper_destroy(clip);
        }
    }
    igEndChild(); igText("%d of %d movies", app->zip_selected + 1, count); igSeparator();
    cancel = igButton("Cancel", dialog_vec(100, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0);
    igSameLine(0, 10); igBeginDisabled(!count);
    if (igButton("Open", dialog_vec(120, 0)) || (count && igShortcut_Nil(ImGuiKey_Enter, 0))) accept = 1;
    igEndDisabled(); dialog_end(cancel || accept || !open);
    if (cancel || !open) cancel_zip(app); else if (accept) confirm_zip(app);
}

static void draw_movie_controls(App *app)
{
    SDL_FRect rect;
    int available;
    if (app->movie_overlay_alpha <= 0) return;
    rect = movie_controls_rect(app); available = app->movie_time_available;
    igPushStyleVar_Float(ImGuiStyleVar_Alpha, app->movie_overlay_alpha);
    if (ui_panel("Movie transport", rect.x, rect.y, rect.w, rect.h, 0, 0)) {
        igText("%s / Scene %d of %d", app->playback.wait == 1 ? "Continue" : app->playing ? "Playing" : "Paused", app->info.scene + 1, app->info.scene_count);
        draw_movie_scrubber(app, available);
        if (ui_button_at("< Scene", rect.x + 24, rect.y + 86, 76, 28, 0, app->info.scene > 0, NULL)) perform_action(app, MARCUS_PREV_SCENE);
        if (ui_button_at(app->playback.wait == 1 ? "Continue" : app->playing ? "Pause" : "Play", rect.x + 108, rect.y + 86, 80, 28, 0, app->loaded, NULL)) toggle_play(app);
        if (ui_button_at("Scene >", rect.x + 196, rect.y + 86, 76, 28, 0, app->info.scene + 1 < app->info.scene_count, NULL)) perform_action(app, MARCUS_NEXT_SCENE);
        if (ui_button_at("-10 s", rect.x + 280, rect.y + 86, 60, 28, 0, available, NULL)) seek_movie(app, app->movie_time.position_seconds - 10);
        if (ui_button_at("+10 s", rect.x + 348, rect.y + 86, 60, 28, 0, available, NULL)) seek_movie(app, app->movie_time.position_seconds + 10);
        if (ui_button_at(app->muted ? "Unmute" : "Mute", rect.x + 416, rect.y + 86, 68, 28, app->muted, 1, NULL)) toggle_mute(app);
        if (ui_button_at("Settings", rect.x + rect.w - 300, rect.y + 86, 88, 28, 0, 1, NULL)) open_rendering_settings(app, 0);
        if (ui_button_at(app->fullscreen ? "Windowed" : "Fullscreen", rect.x + rect.w - 204, rect.y + 86, 96, 28, 0, 1, NULL)) set_fullscreen(app, !app->fullscreen);
        if (ui_button_at("Editor", rect.x + rect.w - 100, rect.y + 86, 84, 28, 0, 1, NULL)) editor_view(app);
    }
    ui_panel_end(); igPopStyleVar(1);
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
static void draw_debug_panel(App *app, float x, float y, int camera, int frozen)
{
    float fps[DEBUG_HISTORY], ms[DEBUG_HISTORY], max_fps, max_ms;
    const CameraPerformance *performance;
    int i, index, count, next;
    char identity[64];
    performance = camera < 0 ? NULL : frozen ? &app->settings_performance : &app->camera_performance[camera];
    count = frozen ? app->settings_frame_count : app->frame_count; next = frozen ? app->settings_frame_next : app->frame_next;
    max_fps = 60; max_ms = 20;
    for (i = 0; i < count; ++i) {
        index = (next - count + i + DEBUG_HISTORY) % DEBUG_HISTORY;
        ms[i] = (float)(performance ? performance->render_ms[index] : app->frame_ms[index]);
        fps[i] = (float)(performance ? performance->fps[index] : app->frame_ms[index] > 0 ? 1000 / app->frame_ms[index] : 0);
        if (ms[i] > max_ms) max_ms = ms[i]; if (fps[i] > max_fps) max_fps = fps[i];
    }
    SDL_snprintf(identity, sizeof(identity), "Performance %d", camera);
    if (ui_panel(identity, x, y, 304, 212, 0, 0)) {
        igTextUnformatted(camera < 0 ? "App / display" : camera ? "Work camera" : "Movie camera", NULL);
        igPlotLines_FloatPtr("FPS", fps, count, 0, NULL, 0, max_fps, dialog_vec(240, 60), sizeof(float));
        igPlotLines_FloatPtr("ms", ms, count, 0, NULL, 0, max_ms, dialog_vec(240, 60), sizeof(float));
        if (performance) igText("Last render: %.2f ms", performance->last_ms);
    }
    ui_panel_end();
}
static void draw_debug_overlay(App *app)
{
    float y;
    y = app->movie_only ? 24.0f : 132.0f + RIBBON_DELTA;
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
        costume_dialog_draw(app->costume_dialog, &app->ui, app->width, app->height);
        sound_dialog_draw(app->sound_dialog, app->player, &app->ui, app->width, app->height);
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
                 "%s / %s  Frame    %s / %s  Scene    %s  Play / pause    %s  View    Ctrl+Z  Undo    Ctrl+,  Settings", previous, next,
                 previous_scene, next_scene, play, view);
    app->ui.hint = NULL;
    ui_fill(&app->ui, 0, 0, (float)app->width, (float)app->height, BG);
    draw_header(app);
    if (app->ribbon_tab == 2 && !app->settings_open && !app->zip_input && !app->error[0] &&
        !browser_visible(app->browser) && !background_browser_visible(app->background_browser) &&
        !text_dialog_visible(app->text_dialog) && !costume_dialog_visible(app->costume_dialog) &&
        !camera_picker_visible(app->camera_picker) && !scene_organiser_visible(app->scene_organiser) &&
        !storyboard_editor_visible(app->storyboard_editor))
        sound_dialog_toolbar(app->sound_dialog, app->player, app->window, app->selected_sound,
            464, 78, app->loaded && !app->playing);
    draw_timeline(app);
    if (!storyboard_editor_visible(app->storyboard_editor)) draw_sidebar(app);
    draw_view(app);

    if (ui_panel("Editor status", 0, (float)app->height - 24, (float)app->width, 24, 1, 0)) {
        igTextUnformatted(app->ui.hint ? app->ui.hint : hint, NULL);
        igSameLine((float)app->width - 210, -1);
        igTextUnformatted(app->playback.scrolling ? "Scrolling caption" : app->playback.transition ? "Scene transition" :
            app->playback.wait == 1 ? "Click / Space to continue" : app->playback.wait == 2 ? "Waiting for sound" :
            app->playback.wait == 3 ? "Timed pause" : app->modified ? "Unsaved movie" : "Movie playback", NULL);
    }
    ui_panel_end();
    draw_debug_overlay(app);
    if (app->settings_open)
        draw_settings(app);
    if (app->zip_input)
        draw_zip(app);
    scene_organiser_draw(app->scene_organiser, &app->ui, app->width, app->height);
    browser_draw(app->browser, &app->ui, app->width, app->height);
    background_browser_draw(app->background_browser, &app->ui, app->width, app->height);
    text_dialog_draw(app->text_dialog, &app->ui, app->width, app->height);
    costume_dialog_draw(app->costume_dialog, &app->ui, app->width, app->height);
    sound_dialog_draw(app->sound_dialog, app->player, &app->ui, app->width, app->height);
    camera_picker_draw(app->camera_picker, &app->ui, app->width, app->height);
    if (app->error[0])
        draw_error(app);
}
/* Sample the controls against the displayed camera, then update the world
 * before either viewport renders. Only the overlay submission happens later. */
static void update_prop_gizmo(App *app)
{
    SDL_FRect viewport;
    int enabled;
    enabled = app->loaded && !app->playing && !app->movie_only && !app->error[0] && !app->settings_open &&
        !app->zip_input && !browser_visible(app->browser) && !background_browser_visible(app->background_browser) && !text_dialog_visible(app->text_dialog) &&
        !costume_dialog_visible(app->costume_dialog) && !sound_dialog_visible(app->sound_dialog) && !camera_picker_visible(app->camera_picker) && !scene_organiser_visible(app->scene_organiser) &&
        !igIsPopupOpen_Str(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        !app->flying && !app->orbiting && !app->panning &&
        !app->resizing_view && !app->resizing_objects && !app->resizing_timeline && !app->dragging;
    prop_gizmo_begin(app->prop_gizmo, app->player, app->selected,
        enabled && !storyboard_editor_visible(app->storyboard_editor),
        app->width, app->height);
    storyboard_editor_gizmo(app->storyboard_editor, enabled);
    if (app->picture)
    {
        viewport = viewport_rect(app, 0);
        prop_gizmo_view(app->prop_gizmo, &app->work_image, &viewport);
    }
    app->prop_interacting = prop_gizmo_active(app->prop_gizmo);
    apply_prop_changes(app);
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
    puts("Marcus - 3D Movie Maker player\n"
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
    if (!marcus_set_playback_rate(app.player, app.shortcuts.playback_rate, app.shortcuts.playback_modifier))
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
    SDL_SetRenderVSync(app.renderer, 1);
    if (!ui_init(&app.ui, app.renderer, font))
    {
        result = 1;
        goto cleanup;
    }
    app.prop_gizmo = prop_gizmo_create(app.window, app.renderer);
    if (!app.prop_gizmo)
    {
        result = 1;
        goto cleanup;
    }
    ui_configure(font);
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
    app.background_browser = background_browser_create();
    if (!app.background_browser)
    {
        result = 1;
        goto cleanup;
    }
    app.text_dialog = text_dialog_create();
    if (!app.text_dialog)
    {
        result = 1;
        goto cleanup;
    }
    app.costume_dialog = costume_dialog_create();
    if (!app.costume_dialog)
    {
        result = 1;
        goto cleanup;
    }
    app.sound_dialog = sound_dialog_create();
    if (!app.sound_dialog)
    {
        result = 1;
        goto cleanup;
    }
    app.camera_picker = camera_picker_create();
    if (!app.camera_picker)
    {
        result = 1;
        goto cleanup;
    }
    app.dialog_event = SDL_RegisterEvents(1);
    app.storyboard_editor = storyboard_editor_create();
    if (!app.storyboard_editor)
    {
        result = 1;
        goto cleanup;
    }
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
    background_browser_prepare(app.background_browser, app.player, app.resources, app.content);
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
        {
            unsigned long background;
            if (background_browser_take(app.background_browser, &background))
            {
                if (background == MARCUS_BACKGROUND_STORYBOARD)
                    choose_storyboard(&app, 1, -1);
                else if (!marcus_add_scene(app.player, background, app.resources, app.content))
                {
                    fail(&app, marcus_error(app.player));
                    refresh(&app);
                    app.dirty = 1;
                }
                else
                {
                    app.modified = 1;
                    app.blank_movie = 0;
                    app.ribbon_tab = 0;
                    app.selected = app.selected_sound = app.selected_sound_track = -1;
                    app.row_scroll = app.sound_scroll = app.both_scroll = 0;
                    refresh(&app);
                    reset_view(&app);
                    fit_timeline(&app);
                    SDL_SetWindowTitle(app.window, app.movie[0] ? "Marcus - Movie (unsaved changes)" : "Marcus - Untitled movie (unsaved)");
                }
            }
        }
        {
            unsigned long prop;
            int object, changed, actor, edited;
            edited = 0;
            object = text_dialog_take(app.text_dialog, &edited);
            if (object < 0)
            {
                object = costume_dialog_take(app.costume_dialog);
                if (object >= 0) edited = 1;
            }
            changed = object >= 0;
            if (background_browser_take_object(app.background_browser, &prop, &actor))
            {
                changed = 1;
                object = actor ? marcus_add_actor(app.player, prop) : marcus_add_prop(app.player, prop);
            }
            if (changed)
            {
                if (object < 0) fail(&app, marcus_error(app.player));
                refresh(&app);
                if (object >= 0)
                {
                    app.modified = 1;
                    app.selected = object;
                    app.selected_sound = app.selected_sound_track = -1;

                    show_object_tracks(&app);
                    *sheet_scroll(&app) = object;
                    if (!edited) { view_mode(&app, 1); reset_view(&app); }
                    SDL_SetWindowTitle(app.window, app.movie[0] ? "Marcus - Movie (unsaved changes)" : "Marcus - Untitled movie (unsaved)");
                }
                app.dirty = app.movie_dirty = 1;
            }
        }
        {
            unsigned long camera;
            int storyboard_angle, edit_storyboard;
            edit_storyboard = camera_picker_take_storyboard(app.camera_picker, &storyboard_angle);
            if (edit_storyboard) choose_storyboard(&app, 0, edit_storyboard == 2 ? -2 : storyboard_angle);
            if (camera_picker_take(app.camera_picker, &camera))
            {
                scene_organiser_close(app.scene_organiser);
                if (!marcus_set_scene_camera(app.player, camera)) fail(&app, marcus_error(app.player));
                else
                {
                    app.modified = 1;
                    app.selected = app.selected_sound = app.selected_sound_track = -1;
                    view_mode(&app, 1);
                    SDL_SetWindowTitle(app.window, app.movie[0] ? "Marcus - Movie (unsaved changes)" : "Marcus - Untitled movie (unsaved)");
                }
                refresh(&app);
                reset_view(&app);
            }
        }
        if (sound_dialog_take(app.sound_dialog, &app.selected_sound))
        {
            MarcusSound sound;
            refresh(&app);
            app.modified = marcus_modified(app.player);
            app.selected_sound_track = marcus_sound(app.player, app.selected_sound, &sound) ? sound.track : -1;
            if (app.sheet_mode == SHEET_OBJECTS) set_sheet_mode(&app, SHEET_SOUNDS);
            app.sound_inspector = 1;
            app.dirty = app.movie_dirty = 1;
            SDL_SetWindowTitle(app.window, app.movie[0] ? "Marcus - Movie (unsaved changes)" : "Marcus - Untitled movie (unsaved)");
        }
        if (storyboard_editor_take(app.storyboard_editor))
        {
            MarcusInfo previous_info;
            previous_info = app.info;
            app.modified = marcus_modified(app.player);
            refresh(&app);
            app.blank_movie = !app.loaded && !app.movie[0];
            if (app.info.scene != previous_info.scene || app.info.first_frame != previous_info.first_frame ||
                app.info.last_frame != previous_info.last_frame) fit_timeline(&app);
            app.dirty = app.movie_dirty = 1;
            SDL_SetWindowTitle(app.window, app.modified ? "Marcus - Storyboard (unsaved changes)" : "Marcus - Storyboard");
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
        if (!app.error[0]) storyboard_editor_update(app.storyboard_editor, (now - previous_tick) / 1000.0);
        else storyboard_editor_release_camera(app.storyboard_editor);
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
                    app.row_scroll = 0;
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
        update_storyboard_layout(&app);
        update_prop_gizmo(&app);
        draw(&app);
        prop_gizmo_end(app.prop_gizmo);
        dialog_release_textures();
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
    text_dialog_destroy(app.text_dialog);
    costume_dialog_destroy(app.costume_dialog);
    sound_dialog_destroy(app.sound_dialog);
    background_browser_destroy(app.background_browser);
    camera_picker_destroy(app.camera_picker);
    storyboard_editor_destroy(app.storyboard_editor);
    scene_organiser_destroy(app.scene_organiser);
    dialog_release_textures();
    prop_gizmo_destroy(app.prop_gizmo);
    ui_destroy(&app.ui);
    dialog_retire_texture(app.picture);
    dialog_retire_texture(app.movie_picture);
    marcus_destroy(app.player);
    marcus_input_destroy(app.movie_input);
    marcus_input_destroy(app.zip_input);
    dialog_release_textures();
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return result;
}
