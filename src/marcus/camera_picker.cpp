#include "dialog_host.h"
#include "camera_picker.h"
#include "storyboard.h"
#include <algorithm>
#include <exception>
#include <vector>
#include <string>

struct MarcusCameraPicker
{
    bool open = false, chosen = false;
    bool storyboard = false;
    int edit = 0;
    int selected = 0, original = 0;
    long frame = 0;
    double displayFrame = 1;
    MarcusFramePreviews *previews = nullptr;
    std::vector<SDL_Texture *> textures;
    std::string error;
    ~MarcusCameraPicker() { Clear(); }
    void Clear()
    {
        marcus_frame_previews_destroy(previews); previews = nullptr;
        for (auto texture : textures) dialog_retire_texture(texture);
        textures.clear();
    }
    int first = 0, last = 6;
    void Queue()
    {
        MarcusFramePosition positions[32]; int count = 0;
        if (selected >= 0 && selected < int(textures.size()) && !textures[selected]) positions[count++] = {selected, frame};
        for (int i = first; i < std::min(last, int(textures.size())) && count < 32; ++i)
            if (i != selected && !textures[i]) positions[count++] = {i, frame};
        marcus_frame_previews_request_positions(previews, positions, count);
    }
    void Select(int index) { selected = std::clamp(index, 0, int(textures.size()) - 1); Queue(); }
    void Close(bool confirm)
    {
        chosen = confirm && selected != original;
        open = false;
        marcus_frame_previews_destroy(previews); previews = nullptr;
    }
};
extern "C" {
MarcusCameraPicker *camera_picker_create(void)
{
    try { return new MarcusCameraPicker; } catch (...) { return nullptr; }
}
void camera_picker_destroy(MarcusCameraPicker *picker) { delete picker; }
int camera_picker_show(MarcusCameraPicker *picker, MarcusPlayer *player)
{
    if (!picker) return 0;
    picker->Clear();
    picker->open = picker->chosen = false;
    picker->edit = 0; picker->storyboard = marcus_is_storyboard(player) != 0;
    picker->error.clear();
    MarcusInfo info = {};
    if (!marcus_info(player, &info) || !info.camera_count || info.camera_count > 1024) return 0;
    try
    {
        picker->textures.resize(info.camera_count, nullptr);
        picker->previews = marcus_camera_previews_create(player);
        if (!picker->previews) return 0;
        picker->frame = info.frame;
        picker->displayFrame = double(info.frame) - info.first_frame + 1;
        picker->original = static_cast<int>(info.camera);
        picker->selected = picker->original;
        picker->first = 0; picker->last = 6;
        picker->open = true;
        picker->Queue();
        return 1;
    }
    catch (...) { picker->Clear(); picker->open = false; return 0; }
}

int camera_picker_take(MarcusCameraPicker *picker, unsigned long *camera)
{
    if (!picker || !picker->chosen || !camera) return 0;
    picker->chosen = false;
    *camera = static_cast<unsigned long>(picker->selected);
    return 1;
}
int camera_picker_take_storyboard(MarcusCameraPicker *picker, int *angle)
{
    if (!picker || !picker->edit || !angle) return 0;
    const int result = picker->edit; picker->edit = 0; *angle = picker->selected; return result;
}
}

int camera_picker_visible(const MarcusCameraPicker *value) { return value && value->open; }

namespace {
void CameraState(void *p, MarcusDialogState *out)
{
    auto &s = *static_cast<MarcusCameraPicker *>(p); *out = {};
    out->open = s.open; out->kind = DIALOG_CAMERAS; out->count = int(s.textures.size());
    out->selected = s.selected; out->current = s.original; out->storyboard = s.storyboard;
    SDL_strlcpy(out->title, "Camera angles", sizeof(out->title)); SDL_strlcpy(out->confirm, "Use angle", sizeof(out->confirm));
    SDL_snprintf(out->status, sizeof(out->status), "Angle %d of %d / frame %.0f. Later camera changes stay in place.", s.selected + 1, out->count, s.displayFrame);
    SDL_strlcpy(out->error, s.error.c_str(), sizeof(out->error));
}
void CameraPrepare(void *p, SDL_Renderer *renderer, int first, int last)
{
    auto &s = *static_cast<MarcusCameraPicker *>(p); if (!s.open) return;
    try {
    if (first >= 0) { s.first = first; s.last = last; s.Queue(); return; }
    MarcusImage image = {}; char error[512] = {};
    for (int n = 0; n < 32; ++n) {
        int status = marcus_frame_previews_take(s.previews, &image, error, sizeof(error));
        if (status < 0) s.error = error; if (status <= 0) break;
        const int index = image.scene; if (index < 0 || index >= int(s.textures.size()) || !image.rgba) continue;
        auto surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32, const_cast<unsigned char *>(image.rgba), image.pitch);
        if (surface) { dialog_retire_texture(s.textures[index]); s.textures[index] = SDL_CreateTextureFromSurface(renderer, surface); SDL_DestroySurface(surface); }
    }
    for (int i = 0; i < int(s.textures.size()); ++i) if (i != s.selected && (i < s.first || i >= s.last))
    { dialog_retire_texture(s.textures[i]); s.textures[i] = nullptr; }
    } catch (const std::exception &e) { s.error = e.what(); }
}
int CameraItem(void *p, SDL_Renderer *, int index, MarcusDialogItem *out)
{
    auto &s = *static_cast<MarcusCameraPicker *>(p); *out = {};
    if (index < 0 || index >= int(s.textures.size())) return 0;
    out->texture = s.textures[index]; SDL_snprintf(out->label, sizeof(out->label), "Angle %d%s", index + 1, index == s.original ? " / current" : ""); return 1;
}
void CameraAction(void *p, int command, int index, float, const char *)
{
    auto &s = *static_cast<MarcusCameraPicker *>(p);
    if (command == DIALOG_SELECT) s.Select(index);
    else if (command == DIALOG_ACCEPT) s.Close(true);
    else if (command == DIALOG_CANCEL) s.Close(false);
    else if (command == DIALOG_EDIT_BOARD || command == DIALOG_NEW_ANGLE) { s.edit = command == DIALOG_EDIT_BOARD ? 1 : 2; s.Close(false); }
}
}
extern "C" const MarcusDialogHost marcus_camera_dialog_host = {CameraState, CameraPrepare, CameraItem, CameraAction};
