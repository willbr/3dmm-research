#include "player.h"
#include "ui.h"
#include "settings.h"
#include "camera_guide.h"
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
#define MARCUS_FPS 6.0
#define FRAME_NAV_X 16
#define SCENE_NAV_X 432
#define NAV_STEP 32
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

typedef struct App
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *picture;
    MarcusUI ui;
    MarcusPlayer *player;
    MarcusInfo info;
    MarcusCamera camera, flight;
    MarcusProjection projection, scene_camera;
    MarcusCameraGuide camera_guide;
    MarcusShortcuts shortcuts, edited_shortcuts;
    char settings_path[1024], settings_message[256];
    int settings_open, binding_capture;
    char movie[1024], resources[1024], content[1024], error[1024];
    int running, loaded, playing, loop, selected, authored, dirty, dragging, orbiting;
    int flying, fly_ready, camera_ready, fly_keys;
    int row_scroll, object_scroll, width, height;
    float sidebar, viewport_bottom, timeline_y, grid_x, grid_width;
    float yaw, pitch, zoom, pixels_per_frame;
    double timeline_start, next_frame;
    Uint32 dialog_event;
} App;

static void release_fly(App *app)
{
    if (app->flying && app->window)
        SDL_SetWindowRelativeMouseMode(app->window, 0);
    app->flying = 0;
    app->fly_keys = 0;
}
static void view_mode(App *app, int mode)
{
    release_fly(app);
    if (mode == 2 && app->authored != 2)
    {
        app->flight = app->camera;
        app->fly_ready = app->camera_ready;
    }
    app->authored = mode;
    app->orbiting = app->dragging = 0;
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

static void fail(App *app, const char *message)
{
    SDL_strlcpy(app->error, message, sizeof(app->error));
    app->playing = 0;
    release_fly(app);
}
static void refresh(App *app)
{
    app->loaded = marcus_info(app->player, &app->info);
    if (app->selected >= app->info.object_count)
        app->selected = -1;
}
static void layout(App *app)
{
    SDL_GetRenderOutputSize(app->renderer, &app->width, &app->height);
    app->sidebar = (float)app->width - 300;
    app->timeline_y = (float)app->height - 300;
    app->viewport_bottom = app->timeline_y - 7;
    app->grid_x = 218;
    app->grid_width = app->sidebar - app->grid_x - 8;
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
    marcus_reset_framing(app->player);
    app->yaw = -18;
    app->pitch = 12;
    app->zoom = 1;
    app->fly_ready = 0;
    app->dirty = 1;
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
        release_fly(app);
        app->fly_ready = app->camera_ready = 0;
    }
    app->selected = app->info.object_count ? 0 : -1;
    app->row_scroll = app->object_scroll = 0;
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
static void open_movie(App *app, const char *path)
{
    char title[1200];
    app->playing = 0;
    release_fly(app);
    app->error[0] = 0;
    if (!marcus_open(app->player, path, app->resources, app->content))
    {
        fail(app, marcus_error(app->player));
        refresh(app);
        app->dirty = 1;
        return;
    }
    SDL_strlcpy(app->movie, path, sizeof(app->movie));
    SDL_snprintf(title, sizeof(title), "Marcus - %s", path);
    SDL_SetWindowTitle(app->window, title);
    refresh(app);
    app->selected = app->info.object_count ? 0 : -1;
    app->camera_ready = 0;
    app->row_scroll = app->object_scroll = 0;
    fit_timeline(app);
    reset_view(app);
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
    static const SDL_DialogFileFilter movies[] = {{"3D Movie Maker movies", "3mm"}, {"All files", "*"}};
    static const SDL_DialogFileFilter resources[] = {{"3DMM resources", "chk"}};
    release_fly(app);
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
        SDL_ShowOpenFileDialog(dialog_callback, request, app->window, kind == 0 ? movies : resources, kind == 0 ? 2 : 1,
                               kind == 0 ? NULL : app->resources, 0);
}
static void toggle_play(App *app)
{
    if (!app->loaded)
        return;
    if (!app->playing && app->info.frame == app->info.last_frame && app->info.scene == app->info.scene_count - 1)
    {
        if (!select_scene(app, 0))
            return;
    }
    app->playing = !app->playing;
    app->next_frame = (double)SDL_GetTicks() + 1000.0 / MARCUS_FPS;
}
static void advance(App *app)
{
    if (app->info.frame < app->info.last_frame)
        seek(app, app->info.frame + 1);
    else if (app->info.scene + 1 < app->info.scene_count)
        select_scene(app, app->info.scene + 1);
    else if (app->loop)
        select_scene(app, 0);
    else
        app->playing = 0;
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
        view_mode(app, (app->authored + 1) % 3);
    else if (action == MARCUS_RESET_VIEW)
    {
        reset_view(app);
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
static void open_settings(App *app)
{
    release_fly(app);
    app->playing = app->dragging = app->orbiting = 0;
    app->edited_shortcuts = app->shortcuts;
    app->binding_capture = -1;
    app->settings_open = 1;
}
static void cancel_settings(App *app)
{
    app->settings_open = 0;
    app->binding_capture = -1;
    app->settings_message[0] = 0;
}
static void apply_settings(App *app)
{
    if (!shortcuts_save(app->settings_path, &app->edited_shortcuts, app->settings_message,
                        sizeof(app->settings_message)))
        return;
    app->shortcuts = app->edited_shortcuts;
    cancel_settings(app);
}
static void settings_click(App *app, float x, float y)
{
    float left, top;
    left = app->width / 2.0f - 330;
    top = app->height / 2.0f - 310;
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
    else if (ui_inside(x, y, left + 24, top + 560, 148, 28))
    {
        shortcuts_defaults(&app->edited_shortcuts);
        app->binding_capture = -1;
        SDL_strlcpy(app->settings_message, "Defaults restored. Apply to save them.", sizeof(app->settings_message));
    }
    else if (ui_inside(x, y, left + 438, top + 560, 88, 28))
        cancel_settings(app);
    else if (ui_inside(x, y, left + 538, top + 560, 96, 28))
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
static void mouse_down(App *app, float x, float y, int button)
{
    int row, action;
    if (app->error[0])
    {
        if (ui_inside(x, y, app->width / 2.0f - 50, app->height / 2.0f + 86, 100, 28))
            app->error[0] = 0;
        return;
    }
    if (app->settings_open)
    {
        if (button == SDL_BUTTON_LEFT)
            settings_click(app, x, y);
        return;
    }
    if (button == SDL_BUTTON_RIGHT && app->loaded && app->authored == 2 && app->fly_ready &&
        ui_inside(x, y, 12, 132, app->sidebar - 24, app->viewport_bottom - 144))
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
    if (button != SDL_BUTTON_LEFT || app->flying)
        return;
    if (ui_inside(x, y, 16, 52, 112, 28))
        choose_file(app, 0);
    else if (ui_inside(x, y, 136, 52, 104, 28))
        choose_file(app, 1);
    else if (ui_inside(x, y, 248, 52, 104, 28))
        choose_file(app, 2);
    else if (ui_inside(x, y, 360, 52, 96, 28))
        open_settings(app);
    else if (!app->loaded)
    {
        if (ui_inside(x, y, app->sidebar / 2 - 65, 280, 130, 28))
            choose_file(app, 0);
    }
    else if (ui_inside(x, y, SCENE_NAV_X, app->timeline_y + 8, NAV_STEP * 4 - 4, 28))
    {
        action = (int)((x - SCENE_NAV_X) / NAV_STEP);
        if (x - SCENE_NAV_X - action * NAV_STEP >= 28)
            return;
        perform_action(app, MARCUS_FIRST_SCENE + action);
    }
    else if (ui_inside(x, y, app->sidebar - 312, 96, 84, 28))
        view_mode(app, 1);
    else if (ui_inside(x, y, app->sidebar - 220, 96, 72, 28))
        view_mode(app, 0);
    else if (ui_inside(x, y, app->sidebar - 140, 96, 60, 28))
        view_mode(app, 2);
    else if (ui_inside(x, y, app->sidebar - 72, 96, 56, 28))
        reset_view(app);
    else if (ui_inside(x, y, FRAME_NAV_X, app->timeline_y + 8, NAV_STEP * 4 - 4, 28))
    {
        action = (int)((x - FRAME_NAV_X) / NAV_STEP);
        if (x - FRAME_NAV_X - action * NAV_STEP >= 28)
            return;
        perform_action(app, MARCUS_FIRST_FRAME + action);
    }
    else if (ui_inside(x, y, 148, app->timeline_y + 8, 60, 28))
        toggle_play(app);
    else if (ui_inside(x, y, 216, app->timeline_y + 8, 60, 28))
        app->loop = !app->loop;
    else if (ui_inside(x, y, app->sidebar - 132, app->timeline_y + 8, 32, 28))
        zoom_timeline(app, 0.8f, app->grid_x + app->grid_width / 2);
    else if (ui_inside(x, y, app->sidebar - 94, app->timeline_y + 8, 32, 28))
        zoom_timeline(app, 1.25f, app->grid_x + app->grid_width / 2);
    else if (ui_inside(x, y, app->sidebar - 56, app->timeline_y + 8, 40, 28))
        fit_timeline(app);
    else if (ui_inside(x, y, app->sidebar + 1, 126, 298, 168))
    {
        row = (int)((y - 126) / ROW_HEIGHT) + app->object_scroll;
        if (row < app->info.object_count)
            app->selected = row;
    }
    else if (ui_inside(x, y, 0, app->timeline_y + 80, app->grid_x, app->height - app->timeline_y - 112))
    {
        row = (int)((y - app->timeline_y - 80) / ROW_HEIGHT) + app->row_scroll;
        if (row < app->info.object_count)
            app->selected = row;
    }
    else if (ui_inside(x, y, app->grid_x, app->timeline_y + 48, app->grid_width, app->height - app->timeline_y - 80))
    {
        app->playing = 0;
        app->dragging = 1;
        if (y >= app->timeline_y + 80)
        {
            row = (int)((y - app->timeline_y - 80) / ROW_HEIGHT) + app->row_scroll;
            if (row < app->info.object_count)
                app->selected = row;
        }
        seek(app, frame_at(app, x));
    }
    else if (!app->authored && ui_inside(x, y, 12, 132, app->sidebar - 24, app->viewport_bottom - 144))
        app->orbiting = 1;
}
static void key_down(App *app, SDL_Keycode key, SDL_Keymod modifiers)
{
    long step;
    int action;
    if (app->error[0])
    {
        if (key == SDLK_ESCAPE || key == SDLK_RETURN)
            app->error[0] = 0;
        return;
    }
    if (app->settings_open)
    {
        settings_key(app, key, modifiers);
        return;
    }
    if (key == SDLK_COMMA && (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)))
    {
        open_settings(app);
        return;
    }
    action = shortcuts_match(&app->shortcuts, key, modifiers);
    if (action >= 0)
    {
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
        if (app->flying)
        {
            release_fly(app);
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
    int rows, maximum_scroll;
    float x, y, delta;
    SDL_Keymod modifiers;
    if (event->type == SDL_EVENT_QUIT)
        app->running = 0;
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
                open_movie(app, app->movie);
        }
        SDL_free(request->path);
        SDL_free(request);
    }
    else if (event->type == SDL_EVENT_DROP_FILE && !app->settings_open)
        open_movie(app, event->drop.data);
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
        if (!app->flying)
        {
            app->ui.mouse_x = event->motion.x;
            app->ui.mouse_y = event->motion.y;
        }
        else
        {
            app->flight.yaw = (float)fmod(app->flight.yaw - event->motion.xrel * 0.15, 360);
            app->flight.pitch = (float)minimum(85, maximum(-85, app->flight.pitch + event->motion.yrel * 0.15));
            app->dirty = 1;
        }
        if (app->dragging)
            seek(app, frame_at(app, event->motion.x));
        if (app->orbiting)
        {
            app->yaw -= event->motion.xrel * 0.4f;
            app->pitch += event->motion.yrel * 0.4f;
            if (app->pitch > 85)
                app->pitch = 85;
            if (app->pitch < -85)
                app->pitch = -85;
            app->dirty = 1;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        mouse_down(app, event->button.x, event->button.y, event->button.button);
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event->button.button == SDL_BUTTON_RIGHT)
            release_fly(app);
        app->dragging = 0;
        app->orbiting = 0;
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && app->loaded && !app->error[0] && !app->settings_open)
    {
        x = event->wheel.mouse_x;
        y = event->wheel.mouse_y;
        delta = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            delta = -delta;
        modifiers = SDL_GetModState();
        if (app->flying || (app->authored == 2 && x < app->sidebar && y >= 132 && y < app->viewport_bottom))
            app->flight.move_speed = (float)minimum(10000, maximum(0.1, app->flight.move_speed * pow(1.25, delta)));
        else if (x >= app->sidebar && y >= 126 && y < 318)
        {
            maximum_scroll = app->info.object_count - 6;
            if (maximum_scroll < 0)
                maximum_scroll = 0;
            app->object_scroll -= (int)delta;
            if (app->object_scroll < 0)
                app->object_scroll = 0;
            if (app->object_scroll > maximum_scroll)
                app->object_scroll = maximum_scroll;
        }
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
                maximum_scroll = app->info.object_count - rows;
                if (maximum_scroll < 0)
                    maximum_scroll = 0;
                app->row_scroll -= (int)delta;
                if (app->row_scroll < 0)
                    app->row_scroll = 0;
                if (app->row_scroll > maximum_scroll)
                    app->row_scroll = maximum_scroll;
            }
        }
        else if (!app->authored && x < app->sidebar && y >= 132)
        {
            app->zoom *= delta > 0 ? 1.1f : 0.9f;
            if (app->zoom > 10)
                app->zoom = 10;
            if (app->zoom < 0.1f)
                app->zoom = 0.1f;
            app->dirty = 1;
        }
    }
    else if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        release_fly(app);
        app->dragging = app->orbiting = 0;
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
    ui_text(ui, 29, 13, "MARCUS", TEXT);
    ui_text(ui, 108, 13, "/  Movie player", MUTED);
    name = app->movie[0] ? app->movie : "Untitled session";
    slash = strrchr(name, '/');
    if (slash)
        name = slash + 1;
    slash = strrchr(name, '\\');
    if (slash)
        name = slash + 1;
    ui_label(ui, 300, 13, (float)app->width - 510, name, TEXT);
    ui_text(ui, (float)app->width - 160, 13, "VISUAL PREVIEW", ACCENT);
    ui_button(ui, 16, 52, 112, "Open movie", 0, 1);
    ui_button(ui, 136, 52, 104, "Resources...", 0, 1);
    ui_button(ui, 248, 52, 104, "Content...", 0, 1);
    ui_button(ui, 360, 52, 96, "Settings", app->settings_open, 1);
    ui_label(ui, 480, 58, app->sidebar - 496, "Drop a .3MM movie here to open it", MUTED);
    ui_line(ui, 0, 88, (float)app->width, 88, LINE);
}
static void draw_view(App *app)
{
    MarcusImage image;
    SDL_FRect destination;
    char label[256];
    int rendered;
    float area_width, area_height, scale;
    MarcusUI *ui;
    ui = &app->ui;
    ui_fill(ui, 0, 89, app->sidebar, app->viewport_bottom - 89, BG);
    ui_text(ui, 20, 102, "Camera viewport", MUTED);
    ui_button(ui, app->sidebar - 312, 96, 84, "Camera", app->authored == 1, app->loaded);
    ui_button(ui, app->sidebar - 220, 96, 72, "Orbit", app->authored == 0, app->loaded);
    ui_button(ui, app->sidebar - 140, 96, 60, "Fly", app->authored == 2, app->loaded);
    ui_button(ui, app->sidebar - 72, 96, 56, "Reset", 0, app->loaded);
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
        if (app->authored == 2)
        {
            rendered = 1;
            if (!app->fly_ready)
            {
                rendered = marcus_render(app->player, 960, 720, 0, app->yaw, app->pitch, app->zoom, &image);
                if (rendered)
                {
                    app->flight = image.camera;
                    app->fly_ready = 1;
                }
            }
            if (rendered)
                rendered = marcus_render_fly(app->player, 960, 720, &app->flight, &image);
        }
        else
            rendered =
                marcus_render(app->player, 960, 720, app->authored == 1, app->yaw, app->pitch, app->zoom, &image);
        if (rendered)
        {
            app->camera = image.camera;
            app->projection = image.projection;
            app->scene_camera = image.scene_camera;
            app->camera_ready = 1;
            if (!app->picture)
            {
                app->picture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                                 image.width, image.height);
                if (app->picture)
                {
                    SDL_SetTextureBlendMode(app->picture, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(app->picture, SDL_SCALEMODE_NEAREST);
                }
            }
            if (!app->picture || !SDL_UpdateTexture(app->picture, NULL, image.rgba, image.pitch))
                fail(app, SDL_GetError());
        }
        else
        {
            SDL_DestroyTexture(app->picture);
            app->picture = NULL;
            fail(app, marcus_error(app->player));
        }
    }
    area_width = app->sidebar - 32;
    area_height = app->viewport_bottom - 172;
    scale = area_width / 960;
    if (720 * scale > area_height)
        scale = area_height / 720;
    destination.w = 960 * scale;
    destination.h = 720 * scale;
    destination.x = (app->sidebar - destination.w) / 2;
    destination.y = 136 + (area_height - destination.h) / 2;
    ui_fill(ui, destination.x - 1, destination.y - 1, destination.w + 2, destination.h + 2, 0x111316);
    if (app->picture)
        SDL_RenderTexture(app->renderer, app->picture, NULL, &destination);
    memset(&app->camera_guide, 0, sizeof(app->camera_guide));
    if (app->authored != 1 && app->picture)
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
    if (app->flying)
    {
        ui_line(ui, app->sidebar / 2 - 6, destination.y + destination.h / 2, app->sidebar / 2 + 6,
                destination.y + destination.h / 2, ACCENT);
        ui_line(ui, app->sidebar / 2, destination.y + destination.h / 2 - 6, app->sidebar / 2,
                destination.y + destination.h / 2 + 6, ACCENT);
    }
    SDL_snprintf(label, sizeof(label), "%s %02lu    |    FRAME %.0f    |    6 FPS",
                 app->authored == 2 ? "FLY"
                 : app->authored    ? "CAMERA"
                                    : "ORBIT",
                 app->info.camera + 1, display_frame(app, app->info.frame));
    ui_text(ui, 20, app->viewport_bottom - 25, label, MUTED);
    if (!app->authored)
        ui_label(ui, app->sidebar - 415, app->viewport_bottom - 25, 395,
                 "Drag to orbit / scroll to zoom / C for camera", MUTED);
    else if (app->authored == 2)
    {
        SDL_snprintf(label, sizeof(label), "Hold RMB + WASD / Q E / Shift    Speed: %.1f",
                     (double)app->flight.move_speed);
        ui_label(ui, app->sidebar - 415, app->viewport_bottom - 25, 395, label, MUTED);
    }
}
static void property(App *app, float y, const char *label, const char *value)
{
    ui_text(&app->ui, app->sidebar + 18, y, label, MUTED);
    ui_label(&app->ui, app->sidebar + 120, y, 162, value, TEXT);
}
static void draw_sidebar(App *app)
{
    MarcusUI *ui;
    MarcusObject object;
    char label[192];
    int row, index;
    float y;
    unsigned long tint;
    ui = &app->ui;
    ui_fill(ui, app->sidebar, 89, 300, (float)app->height - 113, PANEL);
    ui_line(ui, app->sidebar, 89, app->sidebar, (float)app->height - 24, LINE);
    ui_text(ui, app->sidebar + 18, 103, "SCENE OBJECTS", MUTED);
    SDL_snprintf(label, sizeof(label), "%d", app->loaded ? app->info.object_count : 0);
    ui_text(ui, app->sidebar + 263, 103, label, MUTED);
    for (row = 0; row < 6 && app->loaded; ++row)
    {
        index = row + app->object_scroll;
        if (!marcus_object(app->player, index, &object))
            break;
        y = 126 + (float)(row * ROW_HEIGHT);
        if (index == app->selected)
        {
            ui_fill(ui, app->sidebar + 1, y, 298, ROW_HEIGHT, 0x34464c);
            ui_fill(ui, app->sidebar + 1, y, 3, ROW_HEIGHT, ACCENT);
        }
        tint = object.text ? 0xd2b780 : object.prop ? 0xb3a4d6 : ACCENT;
        ui_fill(ui, app->sidebar + 19, y + 10, 7, 7, tint);
        ui_label(ui, app->sidebar + 36, y + 5, 230, object.name, object.visible ? TEXT : MUTED);
        if (object.visible)
            ui_fill(ui, app->sidebar + 280, y + 11, 4, 4, ACCENT);
    }
    if (app->loaded && app->info.object_count > 6)
    {
        SDL_snprintf(label, sizeof(label), "%d-%d of %d  /  scroll for more", app->object_scroll + 1,
                     app->object_scroll + 6 < app->info.object_count ? app->object_scroll + 6 : app->info.object_count,
                     app->info.object_count);
        ui_text(ui, app->sidebar + 18, 300, label, MUTED);
    }
    ui_line(ui, app->sidebar, 326, (float)app->width, 326, LINE);
    ui_text(ui, app->sidebar + 18, 340, "OBJECT PROPERTIES", MUTED);
    if (!app->loaded || app->selected < 0 || !marcus_object(app->player, app->selected, &object))
    {
        ui_text(ui, app->sidebar + 18, 377, "Select an actor or prop", TEXT);
        ui_text(ui, app->sidebar + 18, 401, "in the object list or timeline.", MUTED);
        return;
    }
    ui_label(ui, app->sidebar + 18, 374, 264, object.name, TEXT);
    ui_text(ui, app->sidebar + 18, 400, object.text ? "3D TEXT" : object.prop ? "PROP" : "ACTOR", ACCENT);
    ui_line(ui, app->sidebar + 18, 435, (float)app->width - 18, 435, LINE);
    property(app, 450, "Visibility", object.visible ? "On stage" : "Off stage");
    ui_text(ui, app->sidebar + 18, 484, "WORLD POSITION", MUTED);
    SDL_snprintf(label, sizeof(label), "%.3f", (double)object.x);
    property(app, 511, "X", object.has_lifetime ? label : "--");
    SDL_snprintf(label, sizeof(label), "%.3f", (double)object.y);
    property(app, 537, "Y", object.has_lifetime ? label : "--");
    SDL_snprintf(label, sizeof(label), "%.3f", (double)object.z);
    property(app, 563, "Z", object.has_lifetime ? label : "--");
    ui_line(ui, app->sidebar + 18, 597, (float)app->width - 18, 597, LINE);
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
static void draw_timeline(App *app)
{
    MarcusUI *ui;
    MarcusObject object;
    MarcusScene scene;
    SDL_Rect clip;
    char label[320];
    int row, index, rows;
    float x, y, left, right;
    double frame, tick, end;
    unsigned long tint;
    ui = &app->ui;
    y = app->timeline_y;
    ui_fill(ui, 0, y, app->sidebar, (float)app->height - y - 24, PANEL);
    ui_line(ui, 0, y, app->sidebar, y, LINE);
    ui_navigation_button(ui, FRAME_NAV_X, y + 8, 0, app->loaded && app->info.frame > app->info.first_frame,
                         "First frame");
    ui_navigation_button(ui, FRAME_NAV_X + NAV_STEP, y + 8, 1, app->loaded && app->info.frame > app->info.first_frame,
                         "Previous frame");
    ui_navigation_button(ui, FRAME_NAV_X + NAV_STEP * 2, y + 8, 2,
                         app->loaded && app->info.frame < app->info.last_frame, "Next frame");
    ui_navigation_button(ui, FRAME_NAV_X + NAV_STEP * 3, y + 8, 3,
                         app->loaded && app->info.frame < app->info.last_frame, "Last frame");
    ui_button(ui, 148, y + 8, 60, app->playing ? "Pause" : "Play", app->playing, app->loaded);
    ui_button(ui, 216, y + 8, 60, "Loop", app->loop, app->loaded);
    ui_navigation_button(ui, SCENE_NAV_X, y + 8, 0, app->loaded && app->info.scene > 0, "First scene");
    ui_navigation_button(ui, SCENE_NAV_X + NAV_STEP, y + 8, 1, app->loaded && app->info.scene > 0, "Previous scene");
    ui_navigation_button(ui, SCENE_NAV_X + NAV_STEP * 2, y + 8, 2,
                         app->loaded && app->info.scene + 1 < app->info.scene_count, "Next scene");
    ui_navigation_button(ui, SCENE_NAV_X + NAV_STEP * 3, y + 8, 3,
                         app->loaded && app->info.scene + 1 < app->info.scene_count, "Last scene");
    if (app->loaded)
    {
        SDL_snprintf(label, sizeof(label), "Frame %.0f / %.0f", display_frame(app, app->info.frame),
                     display_frame(app, app->info.last_frame));
        ui_label(ui, 286, y + 14, 134, label, TEXT);
        if (marcus_scene(app->player, app->info.scene, &scene))
        {
            SDL_snprintf(label, sizeof(label), app->sidebar < 850 ? "%d / %d   %s" : "Scene %d / %d   %s",
                         app->info.scene + 1, app->info.scene_count, scene.name);
            ui_label(ui, 568, y + 14, app->sidebar - 716, label, TEXT);
        }
    }
    ui_button(ui, app->sidebar - 132, y + 8, 32, "-", 0, app->loaded);
    ui_button(ui, app->sidebar - 94, y + 8, 32, "+", 0, app->loaded);
    ui_button(ui, app->sidebar - 56, y + 8, 40, "Fit", 0, app->loaded);
    ui_fill(ui, 0, y + 48, app->sidebar, 32, 0x2c2f35);
    ui_text(ui, 18, y + 56, "OBJECT TRACKS", MUTED);
    rows = (int)((app->height - y - 112) / ROW_HEIGHT);
    for (row = 0; row < rows; ++row)
    {
        index = row + app->row_scroll;
        ui_fill(ui, 0, y + 80 + row * ROW_HEIGHT, app->sidebar, ROW_HEIGHT,
                app->loaded && index == app->selected ? 0x303f45
                : row % 2                             ? 0x23262b
                                                      : 0x272a30);
        ui_line(ui, 0, y + 80 + (row + 1) * ROW_HEIGHT, app->sidebar, y + 80 + (row + 1) * ROW_HEIGHT, 0x30333a);
        if (!app->loaded || index >= app->info.object_count)
            continue;
        if (!marcus_object(app->player, index, &object))
            continue;
        tint = object.text ? 0xd2b780 : object.prop ? 0xb3a4d6 : ACCENT;
        ui_fill(ui, 18, y + 90 + row * ROW_HEIGHT, 7, 7, tint);
        ui_label(ui, 35, y + 85 + row * ROW_HEIGHT, app->grid_x - 54, object.name, object.visible ? TEXT : MUTED);
    }
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
            index = row + app->row_scroll;
            if (index >= app->info.object_count || !marcus_object(app->player, index, &object))
                break;
            if (!object.has_lifetime)
                continue;
            left = frame_x(app, maximum((double)object.first_frame, app->info.first_frame));
            right = frame_x(app, minimum((double)object.last_frame, app->info.last_frame) + 1);
            if (right < app->grid_x || left > app->grid_x + app->grid_width || right <= left)
                continue;
            if (left < app->grid_x)
                left = app->grid_x;
            if (right > app->grid_x + app->grid_width)
                right = app->grid_x + app->grid_width;
            tint = object.text ? 0x7f704b : object.prop ? 0x67597f : 0x3d7372;
            ui_fill(ui, left + 1, y + 87 + row * ROW_HEIGHT, right - left - 1, 14, tint);
            ui_line(ui, left + 1, y + 87 + row * ROW_HEIGHT, right - 1, y + 87 + row * ROW_HEIGHT,
                    index == app->selected ? ACCENT : 0x819098);
            if (object.first_frame >= app->timeline_start)
                ui_fill(ui, left + 2, y + 91 + row * ROW_HEIGHT, 4, 6, TEXT);
        }
        x = frame_x(app, app->info.frame + 0.5);
        ui_line(ui, x, y + 48, x, (float)app->height - 32, 0xf28b76);
        ui_fill(ui, x - 4, y + 48, 9, 5, 0xf28b76);
    }
    SDL_SetRenderClipRect(app->renderer, NULL);
    ui_line(ui, app->grid_x, y + 48, app->grid_x, (float)app->height - 32, LINE);
    ui_line(ui, app->sidebar, y, app->sidebar, (float)app->height - 24, LINE);
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
static void draw_settings(App *app)
{
    float x, y, row_y;
    int row;
    char label[128];
    x = app->width / 2.0f - 330;
    y = app->height / 2.0f - 310;
    ui_fill(&app->ui, x - 1, y - 1, 662, 622, 0x606670);
    ui_fill(&app->ui, x, y, 660, 620, PANEL);
    ui_text(&app->ui, x + 24, y + 20, "Settings", TEXT);
    ui_button(&app->ui, x + 618, y + 16, 28, "x", 0, 1);
    ui_text(&app->ui, x + 24, y + 53, "KEYBOARD SHORTCUTS", ACCENT);
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
    ui_label(&app->ui, x + 24, y + 508, 610, app->settings_message, 0xf2ae9b);
    ui_text(&app->ui, x + 24, y + 534, "Flight movement keeps WASD while the right mouse button is held.", MUTED);
    ui_button(&app->ui, x + 24, y + 560, 148, "Restore defaults", 0, 1);
    ui_button(&app->ui, x + 438, y + 560, 88, "Cancel", 0, 1);
    ui_button(&app->ui, x + 538, y + 560, 96, "Apply", 1, 1);
}
static void draw(App *app)
{
    char previous[64], next[64], previous_scene[64], next_scene[64], play[64], view[64], hint[512];
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
    ui_fill(&app->ui, 0, (float)app->height - 24, (float)app->width, 24, 0x303339);
    ui_label(&app->ui, 16, (float)app->height - 21, (float)app->width - 235,
             app->ui.hint ? app->ui.hint
             : app->authored == 2
                 ? "Hold right mouse: look + WASD move   Q/E down/up   Shift faster   Wheel speed   Esc release"
                 : hint,
             MUTED);
    ui_text(&app->ui, (float)app->width - 194, (float)app->height - 21, "READ ONLY  /  NO AUDIO", ACCENT);
    if (app->settings_open)
        draw_settings(app);
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
    puts("Marcus - read-only 3D Movie Maker visual player\n"
         "Usage: marcus [movie.3mm] [--resources 3dmovie.chk] [--content-dir folder]\n"
         "              [--font font.ttf] [--scene N] [--frame N] [--object N] [--orbit|--fly]\n"
         "              [--screenshot output.bmp] [--show-settings]\n"
         "              [--settings-file shortcuts.cfg]\n"
         "Scenes and scene-relative frames start at 1; object indices start at 0. Sound is not yet supported.");
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
    const char *input, *font, *capture;
    int i, result, frames, requested_scene, requested_object, have_frame, show_settings;
    long requested_frame, parsed;
    double now, previous_tick;
    memset(&app, 0, sizeof(app));
    input = font = capture = NULL;
    result = have_frame = show_settings = 0;
    requested_scene = 1;
    requested_object = 0;
    requested_frame = 0;
    app.running = app.authored = app.dirty = 1;
    app.selected = -1;
    app.pixels_per_frame = 12;
    app.zoom = 1;
    find_defaults(&app);
    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            usage();
            return 0;
        }
        else if (!strcmp(argv[i], "--show-settings"))
            show_settings = 1;
        else if (!strcmp(argv[i], "--settings-file") && i + 1 < argc)
            SDL_strlcpy(app.settings_path, argv[++i], sizeof(app.settings_path));
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
        else if ((!strcmp(argv[i], "--scene") || !strcmp(argv[i], "--frame") || !strcmp(argv[i], "--object")) &&
                 i + 1 < argc)
        {
            if (!number(argv[i + 1], &parsed))
            {
                usage();
                return 2;
            }
            if (!strcmp(argv[i], "--scene"))
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
    SDL_SetWindowResizable(app.window, 1);
    SDL_SetWindowMinimumSize(app.window, 1080, 820);
    SDL_SetRenderVSync(app.renderer, 1);
    if (!ui_init(&app.ui, app.renderer, font))
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
        if (app.loaded && requested_scene != 1 && !select_scene(&app, requested_scene - 1))
            fail(&app, "Requested scene does not exist.");
        if (app.loaded && have_frame)
        {
            if (requested_frame < 1 || requested_frame > display_frame(&app, app.info.last_frame))
                fail(&app, "Requested frame is outside this scene.");
            else
                seek(&app, (long)((double)app.info.first_frame + requested_frame - 1));
        }
        if (app.loaded)
        {
            if (requested_object >= app.info.object_count && app.info.object_count)
                fail(&app, "Requested object does not exist.");
            else
                app.selected = app.info.object_count ? requested_object : -1;
        }
    }
    if (show_settings)
        open_settings(&app);
    frames = 0;
    previous_tick = (double)SDL_GetTicks();
    while (app.running)
    {
        layout(&app);
        while (SDL_PollEvent(&event))
        {
            SDL_ConvertEventToRenderCoordinates(app.renderer, &event);
            handle_event(&app, &event);
        }
        now = (double)SDL_GetTicks();
        update_flight(&app, (now - previous_tick) / 1000.0);
        previous_tick = now;
        if (app.playing && now >= app.next_frame)
        {
            advance(&app);
            app.next_frame += 1000.0 / MARCUS_FPS;
            if (app.next_frame < now)
                app.next_frame = now + 1000.0 / MARCUS_FPS;
            if (frame_x(&app, app.info.frame) > app.grid_x + app.grid_width ||
                frame_x(&app, app.info.frame) < app.grid_x)
                app.timeline_start = app.info.frame;
        }
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
    }
cleanup:
    release_fly(&app);
    if (result && !app.error[0])
        fprintf(stderr, "Marcus: %s\n", SDL_GetError());
    ui_destroy(&app.ui);
    SDL_DestroyTexture(app.picture);
    marcus_destroy(app.player);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return result;
}
