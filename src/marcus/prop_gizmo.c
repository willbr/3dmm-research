#include "prop_gizmo.h"
#include "imgui_bindings.h"
#include "dialog_widgets.h"
#include "ui.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { EDIT_NONE, EDIT_GIZMO, EDIT_FIELD };

struct MarcusPropGizmo
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    ImGuiContext *context;
    MarcusPlayer *player;
    int object, scene, editing, changed, mode;
    long frame;
    unsigned long document, chunk;
    int enabled, cancel, finish, backend_ready, over;
    SDL_FRect control_rect;
    float original[3], value[3], applied[3];
    float position[3], left[9], right[9], anchor_offset[3];
    MarcusObjectShape shape, original_shape;
    char error[1024];
};

static const float unit_scale[3] = {1, 1, 1};
static const float origin[3] = {0, 0, 0};

static int finite_value(float value) { return value >= -FLT_MAX && value <= FLT_MAX; }
static float minimum(float a, float b) { return b < a ? b : a; }

static void multiply(const float a[9], const float b[9], float out[9])
{
    float result[9] = {0};
    int i, j, k;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            for (k = 0; k < 3; ++k) result[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
    memcpy(out, result, sizeof(result));
}

static int inverse(const float a[9], float out[9])
{
    float v[9], determinant;
    int i;
    v[0] = a[4] * a[8] - a[5] * a[7];
    v[3] = a[5] * a[6] - a[3] * a[8];
    v[6] = a[3] * a[7] - a[4] * a[6];
    determinant = a[0] * v[0] + a[1] * v[3] + a[2] * v[6];
    if (!finite_value(determinant) || fabs(determinant) < .001f) return 0;
    v[1] = a[2] * a[7] - a[1] * a[8];
    v[2] = a[1] * a[5] - a[2] * a[4];
    v[4] = a[0] * a[8] - a[2] * a[6];
    v[5] = a[2] * a[3] - a[0] * a[5];
    v[7] = a[1] * a[6] - a[0] * a[7];
    v[8] = a[0] * a[4] - a[1] * a[3];
    for (i = 0; i < 9; ++i) out[i] = v[i] / determinant;
    return 1;
}

static void transform_matrix(const float rotation[9], const float scale[3], const float position[3], float out[16])
{
    int i;
    memset(out, 0, 16 * sizeof(*out));
    out[15] = 1;
    for (i = 0; i < 9; ++i) out[(i / 3) * 4 + i % 3] = rotation[i] * scale[i / 3];
    memcpy(out + 12, position, 3 * sizeof(*out));
}

static void shape_angles(const MarcusObjectShape *shape, float value[3])
{
    float matrix[16], position[3], scale[3];
    transform_matrix(shape->rotation, unit_scale, origin, matrix);
    marcus_gizmo_decompose(matrix, position, value, scale);
}

static void rotation_basis(const float angles[3], float rotation[9])
{
    float matrix[16];
    int i;
    marcus_gizmo_compose(origin, angles, unit_scale, matrix);
    for (i = 0; i < 9; ++i) rotation[i] = matrix[(i / 3) * 4 + i % 3];
}

static void read_values(MarcusPropGizmo *gizmo)
{
    int i;
    if (gizmo->mode == MARCUS_GIZMO_ROTATE) shape_angles(&gizmo->shape, gizmo->value);
    else if (gizmo->mode == MARCUS_GIZMO_SCALE)
        for (i = 0; i < 3; ++i) gizmo->value[i] = gizmo->shape.scale[i] * 100;
    else memcpy(gizmo->value, gizmo->position, sizeof(gizmo->value));
}

static void value_limits(int mode, float *low, float *high)
{
    *low = mode == MARCUS_GIZMO_SCALE ? 1.f : mode == MARCUS_GIZMO_ROTATE ? -36000.f : -32700.f;
    *high = mode == MARCUS_GIZMO_SCALE ? 1000.f : mode == MARCUS_GIZMO_ROTATE ? 36000.f : 32700.f;
}

static void store_error(MarcusPropGizmo *gizmo)
{
    SDL_strlcpy(gizmo->error, marcus_error(gizmo->player), sizeof(gizmo->error));
}

static int same_context(const MarcusPropGizmo *gizmo)
{
    MarcusInfo info;
    MarcusObject prop;
    return gizmo->player && marcus_document(gizmo->player) == gizmo->document &&
           marcus_info(gizmo->player, &info) && info.scene == gizmo->scene && info.frame == gizmo->frame &&
           marcus_object(gizmo->player, gizmo->object, &prop) && prop.chunk == gizmo->chunk;
}

static void start_edit(MarcusPropGizmo *gizmo, int kind)
{
    if (gizmo->editing) return;
    memcpy(gizmo->original, gizmo->value, sizeof(gizmo->original));
    memcpy(gizmo->applied, gizmo->value, sizeof(gizmo->applied));
    gizmo->original_shape = gizmo->shape;
    gizmo->editing = kind;
}

static int restore(MarcusPropGizmo *gizmo)
{
    if (!gizmo->editing || !same_context(gizmo)) return 0;
    return (gizmo->mode ? marcus_preview_object_shape(gizmo->player, gizmo->object, &gizmo->original_shape) :
        marcus_preview_object_position(gizmo->player, gizmo->object, gizmo->original[0], gizmo->original[1],
                                       gizmo->original[2])) ? 1 : -1;
}

MarcusPropGizmo *prop_gizmo_create(SDL_Window *window, SDL_Renderer *renderer)
{
    MarcusPropGizmo *gizmo;
    gizmo = calloc(1, sizeof(*gizmo));
    if (!gizmo) return NULL;
    gizmo->window = window;
    gizmo->renderer = renderer;
    gizmo->object = gizmo->scene = -1;
    gizmo->context = igCreateContext(NULL);
    if (gizmo->context) gizmo->backend_ready = marcus_imgui_sdl_init(window, renderer);
    if (gizmo->backend_ready) return gizmo;
    prop_gizmo_destroy(gizmo);
    return NULL;
}

void prop_gizmo_destroy(MarcusPropGizmo *gizmo)
{
    if (!gizmo) return;
    if (gizmo->context)
    {
        igSetCurrentContext(gizmo->context);
        if (gizmo->backend_ready) marcus_imgui_sdl_shutdown();
        igDestroyContext(gizmo->context);
    }
    free(gizmo);
}

int prop_gizmo_event(MarcusPropGizmo *gizmo, const SDL_Event *event)
{
    float x, y;
    int left;
    const SDL_FRect *controls;
    if (!gizmo) return 0;
    igSetCurrentContext(gizmo->context);
    marcus_imgui_sdl_event(event);
    if (gizmo->editing && (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_WINDOW_RESIZED ||
                           event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)) gizmo->cancel = 1;
    if (!gizmo->enabled) return 0;
    if (event->type == SDL_EVENT_KEY_DOWN && gizmo->editing)
    {
        if (event->key.scancode == SDL_SCANCODE_ESCAPE ||
            (event->key.scancode == SDL_SCANCODE_Z && (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) &&
             !(event->key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) && !igGetIO_Nil()->WantTextInput))
            gizmo->cancel = 1;
        return 1;
    }
    if (event->type == SDL_EVENT_KEY_UP || event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_MOUSE_WHEEL)
        return gizmo->editing != 0;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        x = event->button.x;
        y = event->button.y;
        left = event->button.button == SDL_BUTTON_LEFT;
    }
    else if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        x = event->motion.x;
        y = event->motion.y;
        left = 1;
    }
    else return 0;
    controls = &gizmo->control_rect;
    return gizmo->editing || (left && (gizmo->over || ui_inside(x, y, controls->x, controls->y, controls->w, controls->h)));
}

void prop_gizmo_begin(MarcusPropGizmo *gizmo, MarcusPlayer *player, int object, int enabled, int width, int height)
{
    MarcusObject prop;
    MarcusInfo info;
    ImGuiIO *io;
    float x, y, output_x, output_y;
    int eligible;
    igSetCurrentContext(gizmo->context);
    marcus_gizmo_set_id(MARCUS_GIZMO_OBJECT);
    gizmo->changed = 0;
    gizmo->error[0] = '\0';
    eligible = enabled && player && marcus_object(player, object, &prop) && prop.visible && marcus_info(player, &info);
    if (gizmo->editing && (!eligible || player != gizmo->player || object != gizmo->object || !same_context(gizmo)))
    {
        gizmo->changed = player == gizmo->player ? restore(gizmo) : 0;
        if (gizmo->changed < 0) store_error(gizmo);
        gizmo->editing = EDIT_NONE;
        gizmo->cancel = 0;
        marcus_gizmo_enable(MARCUS_GIZMO_OBJECT, 0);
    }
    gizmo->enabled = eligible;
    gizmo->player = player;
    gizmo->object = object;
    gizmo->over = gizmo->finish = 0;
    if (eligible)
    {
        gizmo->document = marcus_document(player);
        gizmo->scene = info.scene;
        gizmo->frame = info.frame;
        gizmo->chunk = prop.chunk;
        gizmo->position[0] = prop.x;
        gizmo->position[1] = prop.y;
        gizmo->position[2] = prop.z;
        if (!gizmo->editing)
        {
            marcus_object_shape(player, object, &gizmo->shape, gizmo->left, gizmo->right);
            read_values(gizmo);
        }
    }
    marcus_imgui_sdl_new_frame();
    io = igGetIO_Nil();
    io->DisplaySize = dialog_vec((float)width, (float)height);
    io->DisplayFramebufferScale = dialog_vec(1, 1);
    SDL_GetMouseState(&x, &y);
    if (SDL_GetMouseFocus() == gizmo->window && SDL_RenderCoordinatesFromWindow(gizmo->renderer, x, y, &output_x, &output_y))
        ImGuiIO_AddMousePosEvent(io, output_x, output_y);
    igNewFrame();
    marcus_gizmo_new_frame();
    marcus_gizmo_enable(MARCUS_GIZMO_OBJECT, eligible && !gizmo->cancel);
}

static void apply_matrix(MarcusPropGizmo *gizmo, const float matrix[16])
{
    float position[3], angles[3], scale[3], inverse_left[9], inverse_right[9], basis[9];
    int i;
    if (!gizmo->mode)
    {
        for (i = 0; i < 3; ++i) gizmo->value[i] = matrix[12 + i] - gizmo->anchor_offset[i];
        return;
    }
    marcus_gizmo_decompose(matrix, position, angles, scale);
    if (gizmo->mode == MARCUS_GIZMO_ROTATE && inverse(gizmo->left, inverse_left) && inverse(gizmo->right, inverse_right))
    {
        rotation_basis(angles, basis);
        multiply(inverse_left, basis, basis);
        multiply(basis, inverse_right, gizmo->shape.rotation);
        shape_angles(&gizmo->shape, gizmo->value);
    }
    else if (gizmo->mode == MARCUS_GIZMO_SCALE)
        for (i = 0; i < 3; ++i) gizmo->value[i] = scale[i] * 100;
}

void prop_gizmo_view(MarcusPropGizmo *gizmo, const MarcusImage *image, const SDL_FRect *viewport)
{
    SDL_FRect rect;
    MarcusGizmoResult result;
    float rotation[9], matrix[16], anchor[3], display_shift[2] = {0, 0};
    float aspect, size, inset_x, inset_y;
    int previous_mode, i;
    if (!gizmo->enabled || gizmo->cancel || gizmo->editing == EDIT_FIELD || !image ||
        image->document != gizmo->document || image->scene != gizmo->scene || image->frame != gizmo->frame ||
        !image->projection.valid) return;
    rect = *viewport;
    if (rect.w <= 0 || rect.h <= 0) return;
    if (image->authored_camera)
    {
        aspect = image->projection.aspect;
        if (rect.w / rect.h > aspect)
        {
            size = rect.h * aspect;
            rect.x += (rect.w - size) / 2;
            rect.w = size;
        }
        else
        {
            size = rect.w / aspect;
            rect.y += (rect.h - size) / 2;
            rect.h = size;
        }
    }
    gizmo->control_rect.x = rect.x + 8;
    gizmo->control_rect.y = rect.y + 8;
    gizmo->control_rect.w = 196;
    gizmo->control_rect.h = 28;
    previous_mode = gizmo->mode;
    gizmo->over = ui_transform_controls("Object tools", &gizmo->control_rect, &gizmo->mode, 3, gizmo->editing);
    if (previous_mode != gizmo->mode) read_values(gizmo);

    multiply(gizmo->left, gizmo->shape.rotation, rotation);
    multiply(rotation, gizmo->right, rotation);
    transform_matrix(rotation, gizmo->mode ? gizmo->shape.scale : unit_scale,
                     gizmo->mode ? gizmo->position : gizmo->value, matrix);
    inset_x = minimum(.25f, 112.f / rect.w);
    inset_y = minimum(.25f, 112.f / rect.h);
    if (!gizmo->editing)
    {
        if (!camera_gizmo_anchor(&image->projection, matrix + 12, inset_x, inset_y, anchor)) return;
        for (i = 0; i < 3; ++i) gizmo->anchor_offset[i] = anchor[i] - matrix[12 + i];
    }
    for (i = 0; i < 3; ++i) matrix[12 + i] += gizmo->anchor_offset[i];
    camera_gizmo_display_shift(&image->projection, matrix + 12, inset_x, inset_y, display_shift);
    result = marcus_gizmo_manipulate(MARCUS_GIZMO_OBJECT, &image->projection, &rect, gizmo->mode,
                                    matrix, minimum(.1f, 160.f / rect.w), display_shift);
    if (result.active || result.changed) start_edit(gizmo, EDIT_GIZMO);
    if (result.changed) apply_matrix(gizmo, matrix);
    gizmo->over |= result.over;
    if (gizmo->over && !gizmo->editing)
        ui_tooltip(gizmo->mode == MARCUS_GIZMO_ROTATE ? "Drag a ring to rotate. Ctrl snaps to 15 degrees. Escape cancels." :
            gizmo->mode == MARCUS_GIZMO_SCALE ? "Drag an axis to resize, or the centre for uniform size. Ctrl snaps to 10%. Escape cancels." :
            "Drag an axis or plane to move the object. Ctrl snaps to 1 unit. Escape cancels.");
    if (gizmo->editing == EDIT_GIZMO && !result.active) gizmo->finish = 1;
}

void prop_gizmo_field_state(MarcusPropGizmo *gizmo, MarcusPropFields *out)
{
    memset(out, 0, sizeof(*out));
    if (!gizmo) return;
    out->enabled = gizmo->enabled && !gizmo->cancel;
    out->mode = gizmo->mode;
    out->dragging_gizmo = gizmo->editing == EDIT_GIZMO;
    value_limits(gizmo->mode, &out->low, &out->high);
    memcpy(out->value, gizmo->value, sizeof(out->value));
}

void prop_gizmo_field_edit(MarcusPropGizmo *gizmo, int axis, float value, int begin, int finish)
{
    if (!gizmo || !gizmo->enabled || gizmo->cancel || axis < 0 || axis > 2 || gizmo->editing == EDIT_GIZMO) return;
    if (begin || value != gizmo->value[axis]) start_edit(gizmo, EDIT_FIELD);
    if (finite_value(value)) gizmo->value[axis] = value;
    if (finish && gizmo->editing == EDIT_FIELD) gizmo->finish = 1;
}

static void clamp_values(MarcusPropGizmo *gizmo)
{
    float low, high, value;
    int axis;
    value_limits(gizmo->mode, &low, &high);
    for (axis = 0; axis < 3; ++axis)
    {
        value = gizmo->value[axis];
        gizmo->value[axis] = !finite_value(value) ? gizmo->applied[axis] : value < low ? low : value > high ? high : value;
    }
    if (gizmo->mode == MARCUS_GIZMO_SCALE)
        for (axis = 0; axis < 3; ++axis) gizmo->shape.scale[axis] = gizmo->value[axis] / 100;
    if (gizmo->mode == MARCUS_GIZMO_ROTATE && gizmo->editing == EDIT_FIELD)
        rotation_basis(gizmo->value, gizmo->shape.rotation);
}

static int commit_edit(MarcusPropGizmo *gizmo)
{
    int axis;
    for (axis = 0; axis < 3; ++axis)
        if (fabs(gizmo->value[axis] - gizmo->original[axis]) > .00002f) break;
    if (axis == 3) return restore(gizmo);
    if (gizmo->mode ? marcus_set_object_shape(gizmo->player, gizmo->object, &gizmo->shape) :
        marcus_move_object(gizmo->player, gizmo->object, gizmo->value[0], gizmo->value[1], gizmo->value[2])) return 2;
    store_error(gizmo);
    restore(gizmo);
    return -1;
}

int prop_gizmo_update(MarcusPropGizmo *gizmo)
{
    int result;
    marcus_gizmo_set_id(MARCUS_GIZMO_OBJECT);
    result = gizmo->changed;
    gizmo->changed = 0;
    if (gizmo->editing)
    {
        clamp_values(gizmo);
        if (gizmo->cancel)
        {
            result = restore(gizmo);
            memcpy(gizmo->value, gizmo->original, sizeof(gizmo->value));
            gizmo->shape = gizmo->original_shape;
            gizmo->editing = EDIT_NONE;
            marcus_gizmo_enable(MARCUS_GIZMO_OBJECT, 0);
        }
        else if (gizmo->finish)
        {
            result = commit_edit(gizmo);
            gizmo->editing = EDIT_NONE;
        }
        else if (gizmo->value[0] != gizmo->applied[0] || gizmo->value[1] != gizmo->applied[1] ||
                 gizmo->value[2] != gizmo->applied[2])
        {
            if (gizmo->mode ? marcus_preview_object_shape(gizmo->player, gizmo->object, &gizmo->shape) :
                marcus_preview_object_position(gizmo->player, gizmo->object, gizmo->value[0], gizmo->value[1], gizmo->value[2]))
            {
                memcpy(gizmo->applied, gizmo->value, sizeof(gizmo->applied));
                result = 1;
            }
            else
            {
                store_error(gizmo);
                restore(gizmo);
                gizmo->editing = EDIT_NONE;
                result = -1;
                marcus_gizmo_enable(MARCUS_GIZMO_OBJECT, 0);
            }
        }
    }
    if (result < 0 && !gizmo->error[0]) store_error(gizmo);
    gizmo->cancel = 0;
    return result;
}

int prop_gizmo_active(const MarcusPropGizmo *gizmo) { return gizmo && gizmo->editing != EDIT_NONE; }
int prop_gizmo_mode(const MarcusPropGizmo *gizmo) { return gizmo ? gizmo->mode : MARCUS_GIZMO_MOVE; }
void prop_gizmo_end(MarcusPropGizmo *gizmo)
{
    igRender();
    marcus_imgui_sdl_draw(igGetDrawData(), gizmo->renderer);
}

const char *prop_gizmo_error(const MarcusPropGizmo *gizmo) { return gizmo ? gizmo->error : "Object controls are unavailable."; }
