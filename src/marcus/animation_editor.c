#include "animation_editor.h"
#include "camera_guide.h"
#include "dialog_widgets.h"
#include "ui.h"

struct MarcusAnimationEditor
{
    unsigned long document;
    long frame;
    int object, scene, ready, action_count, action, cel, freeze, replace;
    int armed, dragging, count;
    float position[3], tweak[3], anchor[3];
    ImVec2 pointer_start;
    MarcusPathPoint points[MARCUS_PATH_MAX_POINTS];
    ImVec2 stroke[MARCUS_PATH_MAX_POINTS + 1];
};

MarcusAnimationEditor *animation_editor_create(void)
{
    MarcusAnimationEditor *editor;
    editor = (MarcusAnimationEditor *)SDL_calloc(1, sizeof(*editor));
    if (editor) editor->replace = 1;
    return editor;
}
void animation_editor_destroy(MarcusAnimationEditor *editor) { SDL_free(editor); }
int animation_editor_recording(const MarcusAnimationEditor *editor) { return editor && editor->armed; }
static void cancel_path(MarcusAnimationEditor *editor)
{
    editor->armed = editor->dragging = editor->count = 0;
}
int animation_editor_event(MarcusAnimationEditor *editor, const SDL_Event *event)
{
    if (!animation_editor_recording(editor)) return 0;
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_WINDOW_RESIZED)
        cancel_path(editor);
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
    {
        cancel_path(editor);
        return 1;
    }
    return dialog_input_event(event);
}
static void choose_action(MarcusAnimationEditor *editor, const MarcusPlayer *player)
{
    MarcusObjectAction action;
    int i;
    if (!marcus_object_action(player, editor->action, &action)) return;
    igSetNextItemWidth(-1);
    if (igBeginCombo("##Action", action.name, 0))
    {
        for (i = 0; i < editor->action_count; ++i)
        {
            if (!marcus_object_action(player, i, &action)) continue;
            igPushID_Int(i);
            if (igSelectable_Bool(action.name, i == editor->action, 0, dialog_vec(0, 0)))
            {
                editor->action = i;
                editor->cel = 1;
            }
            if (i == editor->action) igSetItemDefaultFocus();
            igPopID();
        }
        igEndCombo();
    }
    if (!marcus_object_action(player, editor->action, &action)) return;
    igSetNextItemWidth(94);
    if (igInputInt("Start cel", &editor->cel, 1, 0, 0))
        editor->cel = SDL_clamp(editor->cel, 1, SDL_max(1, action.cels));
    igSameLine(0, 12);
    dialog_checkbox("Freeze pose", &editor->freeze);
}
int animation_editor_properties(MarcusAnimationEditor *editor, MarcusPlayer *player, int object,
                                const MarcusObject *value, int enabled)
{
    MarcusInfo info;
    unsigned long document;
    int result;
    if (!marcus_info(player, &info)) return 0;
    document = marcus_document(player);
    if (!editor->ready || editor->document != document || editor->object != object || editor->scene != info.scene)
    {
        cancel_path(editor);
        editor->ready = 1;
        editor->document = document;
        editor->object = object;
        editor->scene = info.scene;
        editor->frame = info.frame;
        editor->action_count = marcus_object_action_count(player, object);
        editor->action = (int)SDL_clamp(value->action, 0, SDL_max(0, editor->action_count - 1));
        editor->cel = 1;
        editor->freeze = 0;
    }
    if (editor->frame != info.frame)
    {
        cancel_path(editor);
        editor->frame = info.frame;
        editor->action = (int)SDL_clamp(value->action, 0, SDL_max(0, editor->action_count - 1));
        editor->cel = 1;
    }
    igSeparatorText("Animation");
    if (editor->armed)
    {
        igTextWrapped("Drag in the viewport to draw the path. Release to apply.");
        if (igButton("Cancel path", dialog_vec(-1, 0))) cancel_path(editor);
        return 0;
    }
    result = 0;
    igBeginDisabled(!enabled || !value->visible);
    if (editor->action_count > 0)
    {
        choose_action(editor, player);
        if (igButton("Apply action", dialog_vec(-1, 0)))
            result = marcus_set_object_action(player, object, editor->action, editor->cel - 1, editor->freeze) ? 1 : -1;
    }
    if (!result)
    {
        dialog_checkbox("Replace remaining path", &editor->replace);
        if (igButton("Draw path", dialog_vec(105, 0)))
        {
            editor->armed = 1;
            editor->position[0] = value->x;
            editor->position[1] = value->y;
            editor->position[2] = value->z;
        }
        igSameLine(0, 8);
        if (igButton("Edit path point", dialog_vec(132, 0)))
        {
            editor->tweak[0] = value->x;
            editor->tweak[1] = value->y;
            editor->tweak[2] = value->z;
            igOpenPopup_Str("Path point", 0);
        }
        if (igBeginPopup("Path point", 0))
        {
            igInputFloat3("World XYZ", editor->tweak, "%.3f", 0);
            if (igButton("Set point", dialog_vec(0, 0)))
            {
                if (editor->tweak[0] != value->x || editor->tweak[1] != value->y || editor->tweak[2] != value->z)
                    result = marcus_tweak_object_path(player, object, editor->tweak[0] - value->x,
                                 editor->tweak[1] - value->y, editor->tweak[2] - value->z) ? 1 : -1;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
    }
    igEndDisabled();
    return result;
}
static SDL_FRect picture_rect(const MarcusImage *image, const SDL_FRect *viewport)
{
    SDL_FRect rect;
    float width, height;
    rect = *viewport;
    if (!image->authored_camera) return rect;
    width = (float)image->width;
    height = width / image->projection.aspect;
    if (height > image->height)
    {
        height = (float)image->height;
        width = height * image->projection.aspect;
    }
    rect.w *= width / image->width;
    rect.h *= height / image->height;
    rect.x += (viewport->w - rect.w) * .5f;
    rect.y += (viewport->h - rect.h) * .5f;
    return rect;
}
int animation_editor_draw_path(MarcusAnimationEditor *editor, MarcusPlayer *player, const MarcusImage *image,
                               const SDL_FRect *viewport)
{
    MarcusInfo info;
    SDL_FRect rect;
    ImVec2 mouse, base, previous;
    float projected[3], world[3], x, y;
    int result;
    if (!editor->armed) return 0;
    if (marcus_document(player) != editor->document || !marcus_info(player, &info) ||
        info.scene != editor->scene || info.frame != editor->frame)
    {
        cancel_path(editor);
        return 0;
    }
    if (image->width <= 0 || image->height <= 0 || viewport->w <= 0 || viewport->h <= 0 ||
        !camera_project_point(&image->projection, editor->position, projected))
    {
        cancel_path(editor);
        return -2;
    }
    rect = picture_rect(image, viewport);
    result = 0;
    if (ui_panel("Draw object path", rect.x, rect.y, rect.w, rect.h, 1, 0))
    {
        igInvisibleButton("##path surface", dialog_vec(rect.w, rect.h), 0);
        if (igIsItemHovered(0) || igIsItemActive()) igSetMouseCursor(ImGuiMouseCursor_ResizeAll);
        mouse = igGetMousePos();
        x = SDL_clamp((mouse.x - rect.x) / rect.w, 0.f, 1.f);
        y = SDL_clamp((mouse.y - rect.y) / rect.h, 0.f, 1.f);
        base = dialog_vec(rect.x + projected[0] * rect.w, rect.y + projected[1] * rect.h);
        editor->stroke[0] = base;
        if (igIsItemActivated())
        {
            editor->pointer_start = dialog_vec(x, y);
            editor->dragging = camera_world_at_depth(&image->projection, x, y, projected[2], editor->anchor);
        }
        else if (editor->dragging && (igIsItemActive() || igIsItemDeactivated()) &&
                 camera_world_at_depth(&image->projection, x, y, projected[2], world))
        {
            mouse.x = base.x + (x - editor->pointer_start.x) * rect.w;
            mouse.y = base.y + (y - editor->pointer_start.y) * rect.h;
            previous = editor->stroke[editor->count];
            if (editor->count < MARCUS_PATH_MAX_POINTS &&
                (SDL_fabsf(mouse.x - previous.x) >= 3 || SDL_fabsf(mouse.y - previous.y) >= 3))
            {
                editor->points[editor->count].x = world[0] - editor->anchor[0];
                editor->points[editor->count].y = world[1] - editor->anchor[1];
                editor->points[editor->count].z = world[2] - editor->anchor[2];
                editor->stroke[++editor->count] = mouse;
            }
        }
        if (editor->count)
            ImDrawList_AddPolyline(igGetWindowDrawList(), editor->stroke, editor->count + 1, 0xffc4cb78U, 0, 2);
        if (editor->dragging && igIsItemDeactivated())
        {
            if (editor->count)
                result = marcus_record_object_path(player, editor->object, editor->points,
                                                   editor->count, editor->replace) ? 1 : -1;
            cancel_path(editor);
        }
    }
    ui_panel_end();
    return result;
}
