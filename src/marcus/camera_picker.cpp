#include "camera_picker.h"
#include "storyboard.h"
#include <algorithm>
#include <vector>
#include <string>

namespace { constexpr int columns = 3, perPage = 6; }
struct MarcusCameraPicker
{
    bool open = false, chosen = false;
    bool storyboard = false;
    int edit = 0;
    int selected = 0, original = 0, page = 0;
    long frame = 0;
    double displayFrame = 1;
    MarcusFramePreviews *previews = nullptr;
    std::vector<SDL_Texture *> textures;
    std::string error;
    ~MarcusCameraPicker() { Clear(); }
    void Clear()
    {
        marcus_frame_previews_destroy(previews); previews = nullptr;
        for (auto texture : textures) SDL_DestroyTexture(texture);
        textures.clear();
    }
    void Queue()
    {
        MarcusFramePosition positions[perPage];
        int count = 0;
        if (!textures[selected]) positions[count++] = {selected, frame};
        for (int index = page * perPage; index < std::min(int(textures.size()), (page + 1) * perPage); ++index)
            if (index != selected && !textures[index]) positions[count++] = {index, frame};
        marcus_frame_previews_request_positions(previews, positions, count);
    }
    void Select(int index)
    {
        selected = std::clamp(index, 0, int(textures.size()) - 1);
        const int nextPage = selected / perPage;
        if (nextPage != page)
        {
            page = nextPage;
            for (auto &texture : textures) { SDL_DestroyTexture(texture); texture = nullptr; }
        }
        Queue();
    }
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
        picker->page = picker->selected / perPage;
        picker->open = true;
        picker->Queue();
        return 1;
    }
    catch (...) { picker->Clear(); picker->open = false; return 0; }
}
int camera_picker_event(MarcusCameraPicker *picker, const SDL_Event *event, int width, int height)
{
    if (!picker || !picker->open) return 0;
    const float x = (width - 940) / 2.0f, y = (height - 570) / 2.0f;
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.key)
        {
        case SDLK_ESCAPE: picker->Close(false); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: picker->Close(true); break;
        case SDLK_LEFT: picker->Select(picker->selected - 1); break;
        case SDLK_RIGHT: picker->Select(picker->selected + 1); break;
        case SDLK_UP: picker->Select(picker->selected - columns); break;
        case SDLK_DOWN: picker->Select(picker->selected + columns); break;
        case SDLK_PAGEUP: picker->Select(picker->selected - perPage); break;
        case SDLK_PAGEDOWN: picker->Select(picker->selected + perPage); break;
        case SDLK_HOME: picker->Select(0); break;
        case SDLK_END: picker->Select(int(picker->textures.size()) - 1); break;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        const float delta = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        if (delta) picker->Select(picker->selected + (delta > 0 ? -perPage : perPage));
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        const float px = event->button.x, py = event->button.y;
        bool card = false;
        for (int cell = 0; cell < perPage; ++cell)
        {
            const int index = picker->page * perPage + cell;
            if (index >= int(picker->textures.size())) break;
            if (ui_inside(px, py, x + 24 + (cell % columns) * 300, y + 90 + (cell / columns) * 194, 284, 182))
            {
                picker->Select(index);
                if (event->button.clicks == 2) picker->Close(true);
                card = true; break;
            }
        }
        if (!card)
        {
            if (ui_inside(px, py, x + 24, y + 508, 92, 30)) picker->Select(std::max(0, picker->page - 1) * perPage);
            else if (ui_inside(px, py, x + 128, y + 508, 92, 30) && (picker->page + 1) * perPage < int(picker->textures.size()))
                picker->Select((picker->page + 1) * perPage);
            else if (ui_inside(px, py, x + 690, y + 508, 92, 30)) picker->Close(false);
            else if (ui_inside(px, py, x + 794, y + 508, 122, 30)) picker->Close(true);
            else if (ui_inside(px, py, x + 884, y + 18, 32, 28)) picker->Close(false);
            else if (picker->storyboard && ui_inside(px, py, x + 402, y + 508, 130, 30)) { picker->edit = 1; picker->Close(false); }
            else if (picker->storyboard && ui_inside(px, py, x + 542, y + 508, 136, 30)) { picker->edit = 2; picker->Close(false); }
        }
    }
    return event->type != SDL_EVENT_QUIT && event->type != SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}
void camera_picker_draw(MarcusCameraPicker *picker, MarcusUI *ui, int width, int height)
{
    if (!picker || !picker->open) return;
    MarcusImage image = {};
    char failure[512] = {};
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        const int status = marcus_frame_previews_take(picker->previews, &image, failure, sizeof(failure));
        if (status < 0) picker->error = failure;
        if (status <= 0) break;
        const int index = image.scene;
        if (index / perPage != picker->page || index < 0 || index >= int(picker->textures.size())) continue;
        auto surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
            const_cast<unsigned char *>(image.rgba), image.pitch);
        if (surface)
        {
            SDL_DestroyTexture(picker->textures[index]);
            picker->textures[index] = SDL_CreateTextureFromSurface(ui->renderer, surface);
            SDL_DestroySurface(surface);
        }
    }
    const float x = (width - 940) / 2.0f, y = (height - 570) / 2.0f;
    ui_fill(ui, 0, 0, float(width), float(height), 0x121519);
    ui_fill(ui, x - 1, y - 1, 942, 572, 0x5b6570);
    ui_fill(ui, x, y, 940, 570, 0x25282d);
    ui_heading(ui, x + 24, y + 22, "CAMERA ANGLES", 0x78cbc4);
    char label[160];
    SDL_snprintf(label, sizeof(label), "Choose an angle for frame %.0f. Later camera changes stay in place.", picker->displayFrame);
    ui_text(ui, x + 24, y + 58, label, 0xdde0e5);
    ui_button(ui, x + 884, y + 18, 32, "x", 0, 1);
    for (int cell = 0; cell < perPage; ++cell)
    {
        const int index = picker->page * perPage + cell;
        if (index >= int(picker->textures.size())) break;
        const float left = x + 24 + (cell % columns) * 300, top = y + 90 + (cell / columns) * 194;
        const bool selected = index == picker->selected;
        const bool hovered = ui_inside(ui->mouse_x, ui->mouse_y, left, top, 284, 182);
        ui_fill(ui, left - 1, top - 1, 286, 184, selected ? 0x78cbc4 : hovered ? 0x697580 : 0x424952);
        ui_fill(ui, left, top, 284, 182, selected ? 0x375e64 : 0x1c1e22);
        ui_fill(ui, left + 8, top + 8, 268, 148, 0x101216);
        if (picker->textures[index])
        {
            float w, h;
            SDL_GetTextureSize(picker->textures[index], &w, &h);
            const float scale = std::min(268 / w, 148 / h);
            SDL_FRect destination = {left + 8 + (268 - w * scale) / 2, top + 8 + (148 - h * scale) / 2, w * scale, h * scale};
            SDL_RenderTexture(ui->renderer, picker->textures[index], nullptr, &destination);
        }
        else ui_text(ui, left + 68, top + 72, picker->error.empty() ? "Loading preview..." : "Preview unavailable", 0x969da8);
        SDL_snprintf(label, sizeof(label), "Angle %d%s", index + 1, index == picker->original ? "  /  current" : "");
        ui_text(ui, left + 12, top + 162, label, 0xdde0e5);
    }
    ui_label(ui, x + 24, y + 482, 892, picker->error.empty() ? "Select a thumbnail. Enter applies; Escape cancels." : picker->error.c_str(), 0x969da8);
    ui_button_sized(ui, x + 24, y + 508, 92, 30, "Previous", 0, picker->page > 0);
    ui_button_sized(ui, x + 128, y + 508, 92, 30, "Next", 0, (picker->page + 1) * perPage < int(picker->textures.size()));
    SDL_snprintf(label, sizeof(label), "Angle %d of %d", picker->selected + 1, int(picker->textures.size()));
    ui_label(ui, x + 244, y + 516, 150, label, 0x969da8);
    if (picker->storyboard)
    {
        ui_button_sized(ui, x + 402, y + 508, 130, 30, "Edit storyboard", 0, 1);
        ui_button_sized(ui, x + 542, y + 508, 136, 30, "New angle", 0, 1);
    }
    ui_button_sized(ui, x + 690, y + 508, 92, 30, "Cancel", 0, 1);
    ui_button_sized(ui, x + 794, y + 508, 122, 30, "Use angle", 1, 1);
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
