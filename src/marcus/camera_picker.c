#include "dialog_host.h"
#include "camera_picker.h"
#include "storyboard.h"
#include <stdlib.h>
#include <string.h>

enum { CAMERA_ANGLE_LIMIT = 1024, INITIAL_VISIBLE_ANGLES = 6 };

struct MarcusCameraPicker
{
    int open, chosen, storyboard, edit;
    int count, selected, original, first, end;
    long frame;
    double display_frame;
    MarcusFramePreviews *previews;
    SDL_Texture *textures[CAMERA_ANGLE_LIMIT];
    char error[512];
};

static void stop_previews(MarcusCameraPicker *picker)
{
    marcus_frame_previews_destroy(picker->previews);
    picker->previews = NULL;
}

static void clear_previews(MarcusCameraPicker *picker)
{
    int i;
    stop_previews(picker);
    for (i = 0; i < picker->count; ++i)
    {
        dialog_retire_texture(picker->textures[i]);
        picker->textures[i] = NULL;
    }
    picker->count = 0;
}

static int queue_angles(MarcusCameraPicker *picker)
{
    MarcusFramePosition positions[MARCUS_FRAME_PREVIEW_REQUEST_LIMIT];
    int i, count;
    count = 0;
    if (!picker->textures[picker->selected])
    {
        positions[count].scene = picker->selected;
        positions[count++].frame = picker->frame;
    }
    for (i = picker->first; i < picker->end && count < MARCUS_FRAME_PREVIEW_REQUEST_LIMIT; ++i)
        if (i != picker->selected && !picker->textures[i])
        {
            positions[count].scene = i;
            positions[count++].frame = picker->frame;
        }
    if (marcus_frame_previews_request_positions(picker->previews, positions, count)) return 1;
    SDL_strlcpy(picker->error, "Cannot queue camera thumbnails.", sizeof(picker->error));
    return 0;
}

MarcusCameraPicker *camera_picker_create(void)
{
    return calloc(1, sizeof(MarcusCameraPicker));
}

void camera_picker_destroy(MarcusCameraPicker *picker)
{
    if (!picker) return;
    clear_previews(picker);
    free(picker);
}

int camera_picker_show(MarcusCameraPicker *picker, MarcusPlayer *player)
{
    MarcusInfo info;
    if (!picker) return 0;
    clear_previews(picker);
    picker->open = picker->chosen = picker->edit = 0;
    picker->error[0] = '\0';
    if (!marcus_info(player, &info) || info.camera_count > CAMERA_ANGLE_LIMIT || info.camera >= info.camera_count)
        return 0;
    picker->previews = marcus_camera_previews_create(player);
    if (!picker->previews) return 0;
    picker->storyboard = marcus_is_storyboard(player);
    picker->count = (int)info.camera_count;
    picker->frame = info.frame;
    picker->display_frame = (double)info.frame - info.first_frame + 1;
    picker->selected = picker->original = (int)info.camera;
    picker->first = 0;
    picker->end = picker->count < INITIAL_VISIBLE_ANGLES ? picker->count : INITIAL_VISIBLE_ANGLES;
    if (!queue_angles(picker))
    {
        clear_previews(picker);
        return 0;
    }
    picker->open = 1;
    return 1;
}

int camera_picker_visible(const MarcusCameraPicker *picker) { return picker && picker->open; }

int camera_picker_take(MarcusCameraPicker *picker, unsigned long *camera)
{
    if (!picker || !picker->chosen || !camera) return 0;
    picker->chosen = 0;
    *camera = (unsigned long)picker->selected;
    return 1;
}

int camera_picker_take_storyboard(MarcusCameraPicker *picker, int *angle)
{
    int result;
    if (!picker || !picker->edit || !angle) return 0;
    result = picker->edit;
    picker->edit = 0;
    *angle = picker->selected;
    return result;
}

static void camera_state(void *model, MarcusDialogState *out)
{
    const MarcusCameraPicker *picker;
    picker = model;
    memset(out, 0, sizeof(*out));
    out->open = picker->open;
    out->kind = DIALOG_CAMERAS;
    out->count = picker->count;
    out->selected = picker->selected;
    out->current = picker->original;
    out->storyboard = picker->storyboard;
    SDL_strlcpy(out->title, "Camera angles", sizeof(out->title));
    SDL_strlcpy(out->confirm, "Use angle", sizeof(out->confirm));
    SDL_snprintf(out->status, sizeof(out->status), "Angle %d of %d / frame %.0f. Later camera changes stay in place.",
                 picker->selected + 1, picker->count, picker->display_frame);
    SDL_strlcpy(out->error, picker->error, sizeof(out->error));
}

static void camera_prepare(void *model, SDL_Renderer *renderer, int first, int end)
{
    MarcusCameraPicker *picker;
    MarcusImage image;
    SDL_Texture *texture;
    char error[512];
    int i, status;
    picker = model;
    if (!picker->open) return;
    if (first >= 0)
    {
        picker->first = first;
        picker->end = end < picker->count ? end : picker->count;
        queue_angles(picker);
        return;
    }
    for (i = 0; i < MARCUS_FRAME_PREVIEW_REQUEST_LIMIT; ++i)
    {
        error[0] = '\0';
        status = marcus_frame_previews_take(picker->previews, &image, error, sizeof(error));
        if (status < 0) SDL_strlcpy(picker->error, error, sizeof(picker->error));
        if (status <= 0) break;
        if (image.scene < 0 || image.scene >= picker->count) continue;
        texture = dialog_preview_texture(renderer, image.rgba, image.width, image.height, image.pitch);
        if (!texture) continue;
        dialog_retire_texture(picker->textures[image.scene]);
        picker->textures[image.scene] = texture;
    }
    for (i = 0; i < picker->count; ++i)
        if (i != picker->selected && (i < picker->first || i >= picker->end))
        {
            dialog_retire_texture(picker->textures[i]);
            picker->textures[i] = NULL;
        }
}

static int camera_item(void *model, SDL_Renderer *renderer, int index, MarcusDialogItem *out)
{
    const MarcusCameraPicker *picker;
    picker = model;
    (void)renderer;
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= picker->count) return 0;
    out->texture = picker->textures[index];
    SDL_snprintf(out->label, sizeof(out->label), "Angle %d%s", index + 1, index == picker->original ? " / current" : "");
    return 1;
}

static void camera_action(void *model, int command, int index, float value, const char *text)
{
    MarcusCameraPicker *picker;
    picker = model;
    (void)value;
    (void)text;
    if (!picker->open) return;
    switch (command)
    {
    case DIALOG_SELECT:
        picker->selected = index < 0 ? 0 : index >= picker->count ? picker->count - 1 : index;
        queue_angles(picker);
        return;
    case DIALOG_ACCEPT:
        picker->chosen = picker->selected != picker->original;
        break;
    case DIALOG_CANCEL:
        break;
    case DIALOG_EDIT_BOARD:
    case DIALOG_NEW_ANGLE:
        if (!picker->storyboard) return;
        picker->edit = command == DIALOG_EDIT_BOARD ? 1 : 2;
        break;
    default:
        return;
    }
    picker->open = 0;
    stop_previews(picker);
}

const MarcusDialogHost marcus_camera_dialog_host = {camera_state, camera_prepare, camera_item, camera_action};
