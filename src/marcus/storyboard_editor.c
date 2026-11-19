#include "storyboard_view.h"
#include "storyboard_editor_backend.h"
#include "storyboard.h"
#include "dialog_host.h"
#include "camera_controls.h"
#include "colour_picker.h"
#include "transform_gizmo.h"
#include <float.h>
#include <math.h>
#include <string.h>

struct MarcusStoryboardEditor
{
    MarcusPlayer *player;
    MarcusBoardEditorBackend *draft;
    MarcusBoardEditValues values;
    MarcusBoardPose work, light_view;
    SDL_FRect view, canvas, tools, bar, light_controls;
    SDL_Texture *texture;
    SDL_WindowID navigation_window;
    char *resources, *content;
    char error[512];
    unsigned long document, scene;
    long frame;
    int open, changed, pending, preview_dirty, grid_dirty;
    int painting, picking, looking, has_work, show_grid, inspect_lights;
    int angle, light, tab, colour, size, previous_x, previous_y, drag_field;
    int moving_light, light_gizmo_over, light_mode, camera_keys;
    float light_matrix[16], fly_speed, okhsl[3];
    unsigned pen_rgb, swatches[MARCUS_BOARD_SWATCH_COUNT];
    int ink_ready;
    unsigned long dither[64];
};

typedef struct BoardField
{
    const char *name;
    float *value;
    float low, high, step;
} BoardField;

static int minimum(int a, int b) { return a < b ? a : b; }
static int maximum(int a, int b) { return a > b ? a : b; }
static float clamp(float n, float low, float high) { return n < low ? low : n > high ? high : n; }
static float length2(float x, float y) { return (float)sqrt((double)x * x + (double)y * y); }
static int inside(float x, float y, const SDL_FRect *r) { return ui_inside(x, y, r->x, r->y, r->w, r->h); }
static void set_error(MarcusStoryboardEditor *e, const char *message)
{
    SDL_strlcpy(e->error, message, sizeof(e->error));
}

static int set_paths(MarcusStoryboardEditor *e, const char *resources, const char *content)
{
    char *r, *c;
    if (!resources) resources = "";
    if (!content) content = "";
    if (e->resources && strcmp(e->resources, resources) == 0 && strcmp(e->content, content) == 0) return 1;
    r = SDL_strdup(resources);
    c = SDL_strdup(content);
    if (!r || !c)
    {
        SDL_free(r);
        SDL_free(c);
        set_error(e, "Cannot copy storyboard resource paths.");
        return 0;
    }
    SDL_free(e->resources);
    SDL_free(e->content);
    e->resources = r;
    e->content = c;
    return 1;
}

static void select_rgb(MarcusStoryboardEditor *e, unsigned rgb)
{
    float next[3];
    int i;
    e->colour = -1;
    for (i = 0; i < e->values.colour_count; ++i)
        if (e->values.colours[i] == rgb) { e->colour = i; break; }
    marcus_rgb_okhsl(rgb, next);
    if (next[1] < .00001f) next[0] = e->okhsl[0];
    memcpy(e->okhsl, next, sizeof(next));
    e->pen_rgb = rgb;
    e->ink_ready = e->picking = 0;
}

static int resolve_colour(MarcusStoryboardEditor *e)
{
    const unsigned char *surfaces[MARCUS_BOARD_ANGLE_LIMIT];
    int i;
    if (e->colour >= 0) return 1;
    for (i = 0; i < e->values.angle_count; ++i) surfaces[i] = marcus_board_editor_pixels(e->draft, i);
    e->colour = marcus_board_colour_slot(e->values.colours, e->values.colour_count, e->pen_rgb,
                                         surfaces, e->values.angle_count);
    if (e->colour < 0)
    {
        set_error(e, "All 256 paint colours are in use. Pick an existing colour.");
        return 0;
    }
    if (e->colour == e->values.colour_count) ++e->values.colour_count;
    e->values.colours[e->colour] = e->pen_rgb;
    return 1;
}

static const MarcusBoardPose *display_pose(const MarcusStoryboardEditor *e)
{
    return e->tab == 2 && e->inspect_lights ? &e->light_view : &e->values.angles[e->angle];
}

static MarcusCamera *navigation_camera(MarcusStoryboardEditor *e)
{
    return e->tab == 2 ? &e->light_view.camera : &e->values.angles[e->angle].camera;
}

static void include_point(float low[3], float high[3], float x, float y, float z)
{
    float p[3];
    int i;
    p[0] = x; p[1] = y; p[2] = z;
    for (i = 0; i < 3; ++i)
    {
        if (p[i] < low[i]) low[i] = p[i];
        if (p[i] > high[i]) high[i] = p[i];
    }
}

static void frame_lights(MarcusStoryboardEditor *e)
{
    MarcusProjection projection;
    MarcusInfo info = {0};
    MarcusObject object = {0};
    MarcusBounds bounds = {0};
    const MarcusBoardLightValues *light;
    float low[3], high[3], *coordinates[3], distance;
    int i, corner;
    e->light_view = e->values.angles[e->angle];
    coordinates[0] = &e->light_view.camera.x;
    coordinates[1] = &e->light_view.camera.y;
    coordinates[2] = &e->light_view.camera.z;
    for (i = 0; i < 3; ++i) low[i] = high[i] = *coordinates[i];
    for (i = 0; i < e->values.light_count; ++i)
    {
        light = &e->values.lights[i];
        include_point(low, high, light->x, light->y, light->z);
    }
    if (marcus_info(e->player, &info))
        for (i = 0; i < info.object_count; ++i)
        {
            if (!marcus_object(e->player, i, &object) || !object.visible) continue;
            include_point(low, high, object.x, object.y, object.z);
            if (marcus_object_bounds(e->player, i, &bounds) && bounds.valid)
                for (corner = 0; corner < 24; corner += 3)
                    include_point(low, high, bounds.corners[corner], bounds.corners[corner + 1], bounds.corners[corner + 2]);
        }
    distance = length2(length2(high[0] - low[0], high[1] - low[1]), high[2] - low[2]) * 1.4f;
    if (distance < 20.f) distance = 20.f;
    e->light_view.camera.yaw = 35;
    e->light_view.camera.pitch = 25;
    e->light_view.roll = 0;
    e->light_view.fov = 60;
    e->light_view.near_clip = .1f;
    e->light_view.far_clip = 32700;
    marcus_board_projection(&e->light_view, &projection);
    for (i = 0; i < 3; ++i)
        *coordinates[i] = clamp((low[i] + high[i]) * .5f + projection.transform[6 + i] * distance, -32700.f, 32700.f);
    e->inspect_lights = e->preview_dirty = 1;
}

static int gesturing(const MarcusStoryboardEditor *e)
{
    return e->painting || e->looking || e->drag_field >= 0 || e->moving_light;
}

static void stop_light_gizmo(MarcusStoryboardEditor *e)
{
    if (e->moving_light) marcus_gizmo_enable(MARCUS_GIZMO_LIGHT, 0);
    e->moving_light = e->light_gizmo_over = 0;
    memset(&e->light_controls, 0, sizeof(e->light_controls));
}

static void stop_look(MarcusStoryboardEditor *e)
{
    SDL_Window *window;
    if (e->navigation_window)
    {
        window = SDL_GetWindowFromID(e->navigation_window);
        if (window) SDL_SetWindowRelativeMouseMode(window, 0);
    }
    e->navigation_window = 0;
    e->looking = e->camera_keys = 0;
}

static void reload(MarcusStoryboardEditor *e)
{
    MarcusInfo info = {0};
    MarcusScene current = {0};
    int scene_changed;
    stop_look(e);
    stop_light_gizmo(e);
    if (!marcus_info(e->player, &info) || !marcus_scene(e->player, info.scene, &current))
    {
        e->open = 0;
        return;
    }
    if (!marcus_board_editor_load(e->draft, e->player, &e->values, e->swatches))
    {
        set_error(e, SDL_GetError());
        e->open = 0;
        return;
    }
    scene_changed = e->scene != current.chunk;
    if (scene_changed) { e->fly_speed = 0; e->inspect_lights = 0; }
    e->document = marcus_document(e->player);
    e->scene = current.chunk;
    e->frame = info.frame;
    e->angle = e->values.angle_count - 1;
    if (info.camera < (unsigned long)e->angle) e->angle = (int)info.camera;
    e->light = minimum(e->light, maximum(0, e->values.light_count - 1));
    select_rgb(e, e->pen_rgb);
    e->pending = e->painting = 0;
    e->drag_field = -1;
    e->preview_dirty = 1;
    if (scene_changed && e->tab == 2) frame_lights(e);
}

static void commit(MarcusStoryboardEditor *e)
{
    MarcusInfo info = {0};
    int data_changed, camera_changed;
    if (!e->pending) return;
    if (!marcus_board_editor_stage(e->draft, &e->values, &data_changed))
    {
        set_error(e, SDL_GetError());
        return;
    }
    camera_changed = marcus_info(e->player, &info) && info.camera != (unsigned long)e->angle;
    if (data_changed)
    {
        if (!marcus_board_editor_apply(e->draft, e->player, e->angle, e->resources, e->content))
        {
            set_error(e, SDL_GetError());
            return;
        }
    }
    else if (camera_changed && !marcus_set_scene_camera(e->player, e->angle))
    {
        set_error(e, marcus_error(e->player));
        return;
    }
    marcus_board_editor_accept(e->draft);
    e->pending = 0;
    e->document = marcus_document(e->player);
    e->changed |= data_changed || camera_changed;
    e->error[0] = '\0';
    e->preview_dirty = 1;
}

static void finish(MarcusStoryboardEditor *e)
{
    stop_look(e);
    stop_light_gizmo(e);
    e->painting = 0;
    e->drag_field = -1;
    commit(e);
}

static int flight_speed(MarcusStoryboardEditor *e)
{
    MarcusCamera matched = {0};
    if (e->fly_speed <= 0)
    {
        if (!marcus_match_camera(e->player, MARCUS_BOARD_WIDTH, MARCUS_BOARD_HEIGHT, &matched))
        {
            set_error(e, marcus_error(e->player));
            return 0;
        }
        e->fly_speed = matched.move_speed;
    }
    navigation_camera(e)->move_speed = e->fly_speed;
    return 1;
}

static void start_look(MarcusStoryboardEditor *e, SDL_WindowID id)
{
    SDL_Window *window;
    finish(e);
    if (e->tab == 2 && !e->inspect_lights)
    {
        e->light_view = e->values.angles[e->angle];
        e->inspect_lights = 1;
    }
    window = SDL_GetWindowFromID(id);
    if (!window || !flight_speed(e)) return;
    if (!SDL_SetWindowRelativeMouseMode(window, 1))
    {
        set_error(e, SDL_GetError());
        return;
    }
    e->navigation_window = id;
    e->camera_keys = marcus_fly_held_keys();
    e->looking = 1;
}

static void select_angle(MarcusStoryboardEditor *e, int index)
{
    finish(e);
    e->angle = minimum(maximum(index, 0), e->values.angle_count - 1);
    e->pending = e->preview_dirty = 1;
    commit(e);
}

static void add_angle(MarcusStoryboardEditor *e)
{
    int next = e->values.angle_count;
    if (next >= MARCUS_BOARD_ANGLE_LIMIT) return;
    finish(e);
    if (!marcus_board_editor_append_surface(e->draft))
    {
        set_error(e, SDL_GetError());
        return;
    }
    e->values.angles[next] = e->values.angles[e->angle];
    ++e->values.angle_count;
    e->angle = next;
    e->pending = e->preview_dirty = 1;
    commit(e);
}

static float segment_distance(float x, float y, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    float t = length > 0 ? clamp(((x - ax) * dx + (y - ay) * dy) / length, 0.f, 1.f) : 0;
    return length2(x - ax - t * dx, y - ay - t * dy);
}

static int hit_light(const MarcusStoryboardEditor *e, float px, float py)
{
    MarcusProjection projection;
    MarcusCameraGuide guides[2];
    const SDL_FRect *canvas = &e->canvas;
    float nx, ny, centre[3], distance, edge, best = 14.f;
    int pass, i, guide, line, visible, index = -1;
    if (canvas->w <= 0 || canvas->h <= 0) return -1;
    marcus_board_projection(display_pose(e), &projection);
    nx = (px - canvas->x) / canvas->w;
    ny = (py - canvas->y) / canvas->h;
    /* Prefer the selected marker when newly added lights share a position. */
    for (pass = 0; pass < e->values.light_count; ++pass)
    {
        i = pass == 0 ? e->light : pass <= e->light ? pass - 1 : pass;
        marcus_board_editor_light_widget(&projection, &e->values.lights[i], canvas->h,
                                         &guides[0], &guides[1], centre, &visible);
        distance = visible ? length2((nx - centre[0]) * canvas->w, (ny - centre[1]) * canvas->h) : 14.f;
        for (guide = 0; guide < 2; ++guide)
            for (line = 0; line < guides[guide].line_count; ++line)
            {
                edge = segment_distance(px, py,
                    canvas->x + guides[guide].lines[line].x1 * canvas->w, canvas->y + guides[guide].lines[line].y1 * canvas->h,
                    canvas->x + guides[guide].lines[line].x2 * canvas->w, canvas->y + guides[guide].lines[line].y2 * canvas->h);
                if (edge < 7 && edge < distance) distance = edge;
            }
        if (distance < best) { best = distance; index = i; }
    }
    return index;
}

static void set_field(BoardField *field, const char *name, float *value, float low, float high, float step)
{
    field->name = name;
    field->value = value;
    field->low = low;
    field->high = high;
    field->step = step;
}

static int fields(MarcusStoryboardEditor *e, BoardField out[9])
{
    MarcusBoardPose *a = &e->values.angles[e->angle];
    MarcusBoardLightValues *l = &e->values.lights[e->light];
    float near_limit;
    if (e->tab == 1)
    {
        near_limit = a->far_clip - .01f;
        set_field(out, "Position X", &a->camera.x, -32700, 32700, .5f);
        set_field(out + 1, "Position Y", &a->camera.y, -32700, 32700, .5f);
        set_field(out + 2, "Position Z", &a->camera.z, -32700, 32700, .5f);
        set_field(out + 3, "Yaw", &a->camera.yaw, -360, 360, 1);
        set_field(out + 4, "Pitch", &a->camera.pitch, -90, 90, 1);
        set_field(out + 5, "Roll", &a->roll, -180, 180, 1);
        set_field(out + 6, "FOV", &a->fov, 5, 150, 1);
        set_field(out + 7, "Near clip", &a->near_clip, .01f, near_limit < 100.f ? near_limit : 100.f, .1f);
        set_field(out + 8, "Far clip", &a->far_clip, a->near_clip + .01f, 32700, 10);
        return 9;
    }
    if (e->tab != 2 || !e->values.light_count) return 0;
    set_field(out, "Position X", &l->x, -32700, 32700, .5f);
    set_field(out + 1, "Position Y", &l->y, -32700, 32700, .5f);
    set_field(out + 2, "Position Z", &l->z, -32700, 32700, .5f);
    set_field(out + 3, "Yaw", &l->yaw, -360, 360, 1);
    set_field(out + 4, "Pitch", &l->pitch, -89, 89, 1);
    set_field(out + 5, "Intensity", &l->intensity, 0, 1, .05f);
    return 6;
}

static int canvas_point(const MarcusStoryboardEditor *e, float x, float y, int *ix, int *iy)
{
    const SDL_FRect *r = &e->canvas;
    if (r->w <= 0 || r->h <= 0 || !inside(x, y, r)) return 0;
    *ix = minimum(maximum((int)((x - r->x) * MARCUS_BOARD_WIDTH / r->w), 0), MARCUS_BOARD_WIDTH - 1);
    *iy = minimum(maximum((int)((y - r->y) * MARCUS_BOARD_HEIGHT / r->h), 0), MARCUS_BOARD_HEIGHT - 1);
    return 1;
}

static void pick(MarcusStoryboardEditor *e, float x, float y)
{
    const unsigned char *pixels;
    int ix, iy;
    if (!canvas_point(e, x, y, &ix, &iy)) return;
    /* Sample the logical ink independently of dithering and foreground props. */
    pixels = marcus_board_editor_pixels(e->draft, e->angle);
    select_rgb(e, e->values.colours[pixels[iy * MARCUS_BOARD_WIDTH + ix]]);
}

static void paint(MarcusStoryboardEditor *e, float x, float y)
{
    int ix, iy;
    if (!canvas_point(e, x, y, &ix, &iy))
    {
        e->previous_x = e->previous_y = -1;
        return;
    }
    if (!resolve_colour(e)) return;
    marcus_board_stroke(marcus_board_editor_pixels(e->draft, e->angle),
        e->previous_x < 0 ? ix : e->previous_x, e->previous_y < 0 ? iy : e->previous_y,
        ix, iy, e->size, (unsigned char)e->colour);
    e->previous_x = ix;
    e->previous_y = iy;
    e->pending = e->preview_dirty = 1;
}

MarcusStoryboardEditor *storyboard_editor_create(void)
{
    MarcusStoryboardEditor *e = SDL_calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->draft = marcus_board_editor_backend_create();
    if (!e->draft) { SDL_free(e); return NULL; }
    e->show_grid = e->preview_dirty = e->grid_dirty = 1;
    e->colour = 3;
    e->size = 8;
    e->pen_rgb = marcus_board_swatches[3];
    e->previous_x = e->previous_y = e->drag_field = -1;
    return e;
}

void storyboard_editor_destroy(MarcusStoryboardEditor *e)
{
    if (!e) return;
    stop_look(e);
    dialog_retire_texture(e->texture);
    marcus_board_editor_backend_destroy(e->draft);
    SDL_free(e->resources);
    SDL_free(e->content);
    SDL_free(e);
}

int storyboard_editor_visible(const MarcusStoryboardEditor *e) { return e && e->open; }
int storyboard_editor_flying(const MarcusStoryboardEditor *e) { return e && e->looking; }
int storyboard_editor_capturing(const MarcusStoryboardEditor *e) { return e && (e->painting || e->looking || e->moving_light); }
void storyboard_editor_release_camera(MarcusStoryboardEditor *e) { if (e && e->looking) finish(e); }

void storyboard_editor_update(MarcusStoryboardEditor *e, double seconds)
{
    if (storyboard_editor_visible(e) && e->looking && marcus_fly_step(navigation_camera(e), e->camera_keys, seconds))
    {
        e->preview_dirty = 1;
        if (e->tab == 1) e->pending = 1;
    }
}

void storyboard_editor_gizmo(MarcusStoryboardEditor *e, int enabled)
{
    MarcusBoardLightValues *light;
    MarcusProjection projection;
    MarcusGizmoResult result;
    if (!enabled || !storyboard_editor_visible(e) || e->tab != 2 || e->looking ||
        e->painting || e->drag_field >= 0 || !e->values.light_count)
    {
        if (e && e->moving_light) reload(e);
        else if (e) stop_light_gizmo(e);
        marcus_gizmo_enable(MARCUS_GIZMO_LIGHT, 0);
        return;
    }
    light = &e->values.lights[e->light];
    if (light->point) e->light_mode = 0;
    e->light_controls.x = e->canvas.x + 8;
    e->light_controls.y = e->canvas.y + 8;
    e->light_controls.w = light->point ? 64.f : 130.f;
    e->light_controls.h = 28;
    e->light_gizmo_over = ui_transform_controls("Light tools", &e->light_controls, &e->light_mode,
                                                light->point ? 1 : 2, e->moving_light);
    if (!e->moving_light) marcus_board_light_matrix(light, e->light_matrix);
    marcus_board_projection(display_pose(e), &projection);
    result = marcus_gizmo_manipulate(MARCUS_GIZMO_LIGHT, &projection, &e->canvas, e->light_mode, e->light_matrix, .1f, NULL);
    e->light_gizmo_over = e->light_gizmo_over || result.over;
    if (result.active || result.changed) e->moving_light = 1;
    if (result.changed && marcus_board_light_from_matrix(light, e->light_matrix, e->light_mode == 1, light))
    {
        e->pending = e->preview_dirty = 1;
        if (!e->light_mode)
        {
            e->light_matrix[12] = light->x;
            e->light_matrix[13] = light->y;
            e->light_matrix[14] = light->z;
        }
    }
    if (e->light_gizmo_over && !e->moving_light)
        ui_tooltip(e->light_mode ? "Drag a ring to aim the light. Ctrl snaps to 15 degrees. Escape cancels."
            : "Drag an axis, plane or centre to move the light. Ctrl snaps to 1 unit. Escape cancels.");
    if (e->moving_light && !result.active) finish(e);
}

void storyboard_editor_close(MarcusStoryboardEditor *e)
{
    if (!storyboard_editor_visible(e)) return;
    finish(e);
    e->picking = e->open = 0;
}

void storyboard_editor_layout(MarcusStoryboardEditor *e, const SDL_FRect *view, const SDL_FRect *canvas,
    const SDL_FRect *tools, const SDL_FRect *bar)
{
    if (!e) return;
    if (e->canvas.x != canvas->x || e->canvas.y != canvas->y || e->canvas.w != canvas->w || e->canvas.h != canvas->h)
        e->grid_dirty = 1;
    e->view = *view;
    e->canvas = *canvas;
    e->tools = *tools;
    e->bar = *bar;
}

void storyboard_editor_sync(MarcusStoryboardEditor *e, MarcusPlayer *player, const char *resources, const char *content)
{
    MarcusInfo info = {0};
    MarcusScene current = {0};
    if (!storyboard_editor_visible(e)) return;
    if (e->player != player || !marcus_info(player, &info) || !marcus_scene(player, info.scene, &current) ||
        !marcus_is_storyboard(player))
    {
        stop_look(e);
        stop_light_gizmo(e);
        e->open = 0;
        return;
    }
    if (!set_paths(e, resources, content)) return;
    if (current.chunk != e->scene || info.frame != e->frame ||
        info.camera != (unsigned long)e->angle || marcus_document(player) != e->document)
    {
        if (gesturing(e) && current.chunk == e->scene && info.frame == e->frame) e->preview_dirty = 1;
        else reload(e);
    }
}

int storyboard_editor_show(MarcusStoryboardEditor *e, MarcusPlayer *player, int new_scene, int angle,
    const MarcusImage *work, float fly_speed, const char *resources, const char *content)
{
    if (!e) return 0;
    storyboard_editor_close(e);
    e->player = player;
    e->changed = 0;
    e->error[0] = '\0';
    if (!set_paths(e, resources, content)) return 0;
    if (new_scene && !marcus_board_editor_new_scene(player, e->resources, e->content))
    {
        set_error(e, SDL_GetError());
        return 0;
    }
    e->open = 1;
    reload(e);
    if (!e->open) return 0;
    e->fly_speed = fly_speed;
    e->changed = new_scene != 0;
    e->has_work = marcus_board_pose_from_view(work, &e->work);
    e->tab = e->light = e->inspect_lights = e->picking = 0;
    if (new_scene) select_rgb(e, marcus_board_swatches[3]);
    if (angle == -2) add_angle(e);
    else if (angle >= 0) select_angle(e, angle);
    return 1;
}

static int key_down(MarcusStoryboardEditor *e, const SDL_KeyboardEvent *event)
{
    SDL_Keycode key = event->key;
    int fly_key = marcus_fly_key(event->scancode);
    int command = event->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT);
    if (e->looking && fly_key) { e->camera_keys |= fly_key; return 1; }
    if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL ||
        key == SDLK_LALT || key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI) return gesturing(e);
    if (key == SDLK_ESCAPE && !event->repeat)
    {
        if (e->picking) e->picking = 0;
        else if (e->looking) finish(e);
        else if (gesturing(e)) reload(e);
        else storyboard_editor_close(e);
        return 1;
    }
    if (key == SDLK_Z && command && gesturing(e)) { reload(e); return 1; }
    if (e->moving_light) return 1;
    if (command) { finish(e); return 0; }
    if (e->looking) return 0;
    if (!event->repeat)
    {
        if (key >= SDLK_1 && key <= SDLK_9) { select_rgb(e, marcus_board_swatches[key - SDLK_1 + 1]); return 1; }
        if (key == SDLK_0) { select_rgb(e, marcus_board_swatches[0]); return 1; }
        if (key == SDLK_I && e->tab == 0) { finish(e); e->picking = !e->picking; return 1; }
        if (key == SDLK_LEFTBRACKET) { e->size = maximum(1, e->size / 2); return 1; }
        if (key == SDLK_RIGHTBRACKET) { e->size = minimum(64, e->size * 2); return 1; }
    }
    finish(e);
    return 0;
}

static int pointer_down(MarcusStoryboardEditor *e, const SDL_MouseButtonEvent *event)
{
    float px = event->x, py = event->y;
    int index;
    if (e->looking || e->moving_light) return 1;
    if (!inside(px, py, &e->view) && !inside(px, py, &e->tools)) { finish(e); return 0; }
    if (!inside(px, py, &e->canvas)) return 1;
    if (event->button == SDL_BUTTON_LEFT && e->tab == 0)
    {
        if (e->picking || (SDL_GetModState() & SDL_KMOD_ALT)) pick(e, px, py);
        else
        {
            e->painting = 1;
            e->previous_x = e->previous_y = -1;
            paint(e, px, py);
        }
    }
    else if (event->button == SDL_BUTTON_LEFT && e->tab == 2)
    {
        if (e->light_gizmo_over || inside(px, py, &e->light_controls)) return 1;
        finish(e);
        index = hit_light(e, px, py);
        if (index >= 0) e->light = index;
    }
    else if (event->button == SDL_BUTTON_RIGHT && (e->tab == 1 || e->tab == 2)) start_look(e, event->windowID);
    return 1;
}

int storyboard_editor_event(MarcusStoryboardEditor *e, const SDL_Event *event)
{
    MarcusCamera *camera;
    int fly_key;
    if (!storyboard_editor_visible(e)) return 0;
    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN:
        return key_down(e, &event->key);
    case SDL_EVENT_KEY_UP:
        if (!e->looking) return 0;
        fly_key = marcus_fly_key(event->key.scancode);
        e->camera_keys &= ~fly_key;
        return fly_key != 0;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (e->moving_light) reload(e);
        else finish(e);
        return 0;
    case SDL_EVENT_MOUSE_MOTION:
        if (e->moving_light) return 1;
        if (e->painting) { paint(e, event->motion.x, event->motion.y); return 1; }
        if (e->looking)
        {
            if (marcus_fly_look(navigation_camera(e), event->motion.xrel, event->motion.yrel))
            {
                e->preview_dirty = 1;
                if (e->tab == 1) e->pending = 1;
            }
            return 1;
        }
        return inside(event->motion.x, event->motion.y, &e->view) || inside(event->motion.x, event->motion.y, &e->tools);
    case SDL_EVENT_MOUSE_BUTTON_UP:
        /* ImGuizmo samples the final pointer release in the shared render frame. */
        if (e->moving_light) return 1;
        if ((e->looking && event->button.button == SDL_BUTTON_RIGHT) ||
            (e->painting && event->button.button == SDL_BUTTON_LEFT)) { finish(e); return 1; }
        return 0;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e->moving_light) return 1;
        if (!e->looking && !inside(event->wheel.mouse_x, event->wheel.mouse_y, &e->canvas))
            return inside(event->wheel.mouse_x, event->wheel.mouse_y, &e->view) ||
                   inside(event->wheel.mouse_x, event->wheel.mouse_y, &e->tools);
        if ((e->tab == 1 || e->tab == 2) && event->wheel.y != 0 && flight_speed(e))
        {
            camera = navigation_camera(e);
            marcus_fly_speed(camera, event->wheel.y * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1));
            e->fly_speed = camera->move_speed;
        }
        return 1;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return pointer_down(e, &event->button);
    default:
        return 0;
    }
}

void storyboard_editor_prepare(MarcusStoryboardEditor *e, SDL_Renderer *renderer)
{
    const unsigned char *rgba;
    const MarcusBoardPose *inspection;
    if (!storyboard_editor_visible(e)) return;
    if (e->preview_dirty)
    {
        inspection = e->tab == 2 && e->inspect_lights ? &e->light_view : NULL;
        if (!marcus_board_editor_render(e->draft, e->player, &e->values, e->angle, inspection, &rgba))
        {
            set_error(e, SDL_GetError());
            return;
        }
        e->grid_dirty = 1;
        if (!e->texture)
            e->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                           MARCUS_BOARD_WIDTH, MARCUS_BOARD_HEIGHT);
        if (e->texture)
        {
            SDL_SetTextureScaleMode(e->texture, SDL_SCALEMODE_NEAREST);
            SDL_UpdateTexture(e->texture, NULL, rgba, MARCUS_BOARD_WIDTH * 4);
        }
        e->preview_dirty = 0;
    }
    if (e->show_grid && e->grid_dirty)
    {
        if (marcus_board_editor_build_grid(e->draft, display_pose(e), &e->canvas)) e->grid_dirty = 0;
        else set_error(e, SDL_GetError());
    }
}

static void light_overlays(const MarcusStoryboardEditor *e, MarcusBoardEditorState *out)
{
    MarcusProjection projection, shot;
    MarcusBoardLightOverlay *overlay;
    const MarcusBoardLightValues *light;
    float centre[3];
    int i;
    marcus_board_projection(display_pose(e), &projection);
    if (e->inspect_lights)
    {
        marcus_board_projection(&e->values.angles[e->angle], &shot);
        camera_guide_build(&projection, &shot, &out->camera_guide);
    }
    for (i = 0; i < out->lights; ++i)
    {
        overlay = &out->overlays[i];
        light = &e->values.lights[i];
        marcus_board_editor_light_widget(&projection, light, e->canvas.h, &overlay->shape, &overlay->direction,
                                         centre, &overlay->visible);
        overlay->selected = i == e->light;
        overlay->x = e->canvas.x + centre[0] * e->canvas.w;
        overlay->y = e->canvas.y + centre[1] * e->canvas.h;
        SDL_snprintf(overlay->label, sizeof(overlay->label), "%s light %d", light->point ? "Point" : "Directional", i + 1);
    }
}

void storyboard_editor_state(MarcusStoryboardEditor *e, MarcusBoardEditorState *out)
{
    BoardField source[9];
    MarcusBoardField *field;
    int i;
    memset(out, 0, sizeof(*out));
    if (!storyboard_editor_visible(e)) return;
    out->open = 1;
    out->tab = e->tab;
    out->angle = e->angle;
    out->angles = e->values.angle_count;
    out->light = e->light;
    out->lights = e->values.light_count;
    out->grid = e->show_grid;
    out->point = out->lights && e->values.lights[e->light].point;
    out->has_work = e->has_work;
    out->inspecting = e->inspect_lights;
    out->picking = e->picking;
    out->brush = e->size;
    out->can_undo = marcus_can_undo(e->player);
    out->editing = e->drag_field;
    out->can_bake = e->colour != 0 && marcus_board_editor_can_bake(&e->values.angles[e->angle]);
    out->canvas = e->canvas;
    out->tools = e->tools;
    out->bar = e->bar;
    out->texture = e->texture;
    marcus_board_editor_grid(e->draft, &out->vertices, &out->indices, &out->index_count);
    out->rgb = e->pen_rgb;
    memcpy(out->okhsl, e->okhsl, sizeof(out->okhsl));
    for (i = 0; i < MARCUS_BOARD_SWATCH_COUNT; ++i) out->swatches[i] = e->swatches[i];
    if (!e->ink_ready)
    {
        marcus_board_editor_dither(e->draft, out->rgb, e->dither);
        e->ink_ready = 1;
    }
    memcpy(out->dither, e->dither, sizeof(out->dither));
    out->field_count = fields(e, source);
    for (i = 0; i < out->field_count; ++i)
    {
        field = &out->fields[i];
        SDL_strlcpy(field->name, source[i].name, sizeof(field->name));
        field->value = *source[i].value;
        field->low = source[i].low;
        field->high = source[i].high;
        field->step = source[i].step;
    }
    if (e->tab == 2) light_overlays(e, out);
    SDL_strlcpy(out->error, e->error, sizeof(out->error));
}

void storyboard_editor_command(MarcusStoryboardEditor *e, int command, int index, float value, const float *colour)
{
    BoardField source[9];
    MarcusBoardLightValues *light;
    if (!storyboard_editor_visible(e)) return;
    switch (command)
    {
    case BOARD_DONE:
        storyboard_editor_close(e);
        break;
    case BOARD_TAB:
        if (index < 0 || index > 2) break;
        finish(e);
        if (index == 2 && e->tab != 2 && !e->inspect_lights) frame_lights(e);
        e->tab = index;
        e->picking = 0;
        e->preview_dirty = 1;
        break;
    case BOARD_ANGLE:
        select_angle(e, index);
        break;
    case BOARD_NEW_ANGLE:
        add_angle(e);
        break;
    case BOARD_USE_WORK:
        if (!e->has_work) break;
        finish(e);
        e->values.angles[e->angle] = e->work;
        e->pending = 1;
        commit(e);
        break;
    case BOARD_GRID:
        e->show_grid = !e->show_grid;
        break;
    case BOARD_HSL:
        if (colour)
        {
            select_rgb(e, marcus_okhsl_rgb(colour));
            memcpy(e->okhsl, colour, sizeof(e->okhsl));
        }
        break;
    case BOARD_SWATCH:
        if (index >= 0 && index < MARCUS_BOARD_SWATCH_COUNT) select_rgb(e, marcus_board_swatches[index]);
        break;
    case BOARD_PICK:
        finish(e);
        e->picking = !e->picking;
        break;
    case BOARD_BRUSH:
        e->size = minimum(maximum(index, 1), 64);
        break;
    case BOARD_BAKE:
        finish(e);
        if (e->colour == 0 || !resolve_colour(e)) break;
        if (!marcus_board_editor_bake(e->draft, e->angle, &e->values.angles[e->angle], (unsigned char)e->colour))
        {
            set_error(e, SDL_GetError());
            break;
        }
        e->pending = 1;
        commit(e);
        break;
    case BOARD_CLEAR:
        finish(e);
        memset(marcus_board_editor_pixels(e->draft, e->angle), 0, MARCUS_BOARD_PIXELS);
        e->pending = 1;
        commit(e);
        break;
    case BOARD_UNDO:
        finish(e);
        if (marcus_undo(e->player)) { reload(e); e->changed = 1; }
        else set_error(e, marcus_error(e->player));
        break;
    case BOARD_LIGHT:
        finish(e);
        e->light = minimum(maximum(index, 0), maximum(0, e->values.light_count - 1));
        break;
    case BOARD_LIGHT_ADD:
        finish(e);
        if (e->values.light_count >= MARCUS_BOARD_LIGHT_LIMIT) break;
        e->light = e->values.light_count++;
        marcus_board_editor_default_light(&e->values.lights[e->light]);
        e->pending = 1;
        frame_lights(e);
        commit(e);
        break;
    case BOARD_LIGHT_REMOVE:
        finish(e);
        if (!e->values.light_count) break;
        light = &e->values.lights[e->light];
        --e->values.light_count;
        if (e->light < e->values.light_count)
            memmove(light, light + 1, (e->values.light_count - e->light) * sizeof(*light));
        e->light = maximum(0, e->light - 1);
        e->pending = 1;
        commit(e);
        break;
    case BOARD_POINT:
        finish(e);
        if (!e->values.light_count) break;
        e->values.lights[e->light].point ^= 1;
        e->pending = 1;
        commit(e);
        break;
    case BOARD_FRAME_LIGHTS:
        finish(e);
        frame_lights(e);
        break;
    case BOARD_SHOT_VIEW:
        finish(e);
        if (e->inspect_lights) e->inspect_lights = 0;
        else frame_lights(e);
        e->preview_dirty = 1;
        break;
    case BOARD_FIELD_BEGIN:
        finish(e);
        e->drag_field = index;
        break;
    case BOARD_FIELD_SET:
        if (index < 0 || index >= fields(e, source) || !(value >= -FLT_MAX && value <= FLT_MAX)) break;
        *source[index].value = clamp(value, source[index].low, source[index].high);
        e->pending = e->preview_dirty = 1;
        break;
    case BOARD_FIELD_END:
        finish(e);
        break;
    case BOARD_FIELD_CANCEL:
        reload(e);
        break;
    }
}

int storyboard_editor_take(MarcusStoryboardEditor *e)
{
    if (!e || !e->changed) return 0;
    e->changed = 0;
    return 1;
}
