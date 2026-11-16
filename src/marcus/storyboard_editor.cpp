#include "storyboard_editor.h"
#include "storyboard.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr int sizes[] = {1, 4, 8, 16, 32};
constexpr const char *colourNames[] = {"Grey eraser", "Black", "White", "Soft blue", "Soft red", "Soft yellow", "Soft green"};
struct Field { const char *name; float *value; float low, high, step; };
void Grid(MarcusUI *ui, const MarcusBoardAngle &a, float x, float y)
{
    const double yaw = a.camera.yaw * .01745329252, pitch = a.camera.pitch * .01745329252, roll = a.roll * .01745329252;
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    const double right[3] = {cy * cr - sy * sp * sr, cp * sr, -sy * cr - cy * sp * sr};
    const double up[3] = {-cy * sr - sy * sp * cr, cp * cr, sy * sr - cy * sp * cr};
    const double back[3] = {sy * cp, sp, cy * cp};
    const double focal = 180 / std::tan(a.fov * .00872664626);
    const double centreX = std::round(a.camera.x / 10) * 10, centreZ = std::round(a.camera.z / 10) * 10;
    auto cameraPoint = [&](double wx, double wz) {
        const double p[3] = {wx - a.camera.x, -a.camera.y, wz - a.camera.z};
        std::array<double, 3> out = {};
        for (int i = 0; i < 3; ++i) { out[0] += p[i] * right[i]; out[1] += p[i] * up[i]; out[2] -= p[i] * back[i]; }
        return out;
    };
    const bool clipped = SDL_RenderClipEnabled(ui->renderer);
    SDL_Rect previous; SDL_GetRenderClipRect(ui->renderer, &previous);
    SDL_Rect clip = {int(x), int(y), 640, 360}; SDL_SetRenderClipRect(ui->renderer, &clip);
    auto line = [&](double x1, double z1, double x2, double z2) {
        auto p = cameraPoint(x1, z1), q = cameraPoint(x2, z2);
        const double nearClip = std::max(.1, double(a.nearClip));
        if (p[2] < nearClip && q[2] < nearClip) return;
        if (p[2] < nearClip) { const double t = (nearClip - p[2]) / (q[2] - p[2]); for (int i = 0; i < 3; ++i) p[i] += t * (q[i] - p[i]); }
        if (q[2] < nearClip) { const double t = (nearClip - q[2]) / (p[2] - q[2]); for (int i = 0; i < 3; ++i) q[i] += t * (p[i] - q[i]); }
        ui_line(ui, float(x + 320 + focal * p[0] / p[2]), float(y + 180 - focal * p[1] / p[2]),
            float(x + 320 + focal * q[0] / q[2]), float(y + 180 - focal * q[1] / q[2]), 0xadb3b6);
    };
    for (int i = -10; i <= 10; ++i)
    {
        line(centreX + i * 5, centreZ - 50, centreX + i * 5, centreZ + 50);
        line(centreX - 50, centreZ + i * 5, centreX + 50, centreZ + i * 5);
    }
    ui_line(ui, x + 314, y + 180, x + 326, y + 180, 0x6b7f84);
    ui_line(ui, x + 320, y + 174, x + 320, y + 186, 0x6b7f84);
    ui_text(ui, x + 12, y + 12, "Camera guide / ground grid (not painted)", 0x5b6570);
    SDL_SetRenderClipRect(ui->renderer, clipped ? &previous : nullptr);
}
}
struct MarcusStoryboardEditor
{
    MarcusPlayer *player = nullptr;
    MarcusStoryboard board;
    MarcusBoardAngle work;
    std::vector<MarcusStoryboard> undo;
    bool open = false, applied = false, newScene = false, painting = false, looking = false, previewDirty = true, hasWork = false;
    int angle = 0, light = 0, tab = 0, colour = 1, size = 8, previousX = -1, previousY = -1, dragField = -1;
    float dragStart = 0, dragValue = 0;
    std::string resources, content, error;
    SDL_Texture *texture = nullptr;
    std::vector<unsigned char> rgba;
    ~MarcusStoryboardEditor() { SDL_DestroyTexture(texture); }
    void Remember()
    {
        const size_t bytes = board.angles.size() * MarcusBoardWidth * MarcusBoardHeight;
        const size_t limit = std::max<size_t>(1, std::min<size_t>(24, 32 * 1024 * 1024 / bytes));
        if (undo.size() >= limit) undo.erase(undo.begin());
        undo.push_back(board);
    }
    void Undo()
    {
        if (undo.empty()) return;
        board = std::move(undo.back()); undo.pop_back();
        angle = std::min(angle, int(board.angles.size()) - 1); light = std::max(0, std::min(light, int(board.lights.size()) - 1));
        previewDirty = true;
    }
    void AddAngle()
    {
        if (board.angles.size() >= 64) return;
        Remember();
        auto next = board.angles[angle]; std::fill(next.pixels.begin(), next.pixels.end(), 0);
        board.angles.push_back(std::move(next)); angle = int(board.angles.size()) - 1;
        tab = 1; previewDirty = true;
    }
    std::vector<Field> Fields()
    {
        auto &a = board.angles[angle];
        if (tab == 1) return {{"Position X", &a.camera.x, -32700, 32700, .5f}, {"Position Y", &a.camera.y, -32700, 32700, .5f},
            {"Position Z", &a.camera.z, -32700, 32700, .5f}, {"Yaw", &a.camera.yaw, -360, 360, 1},
            {"Pitch", &a.camera.pitch, -89, 89, 1}, {"Roll", &a.roll, -180, 180, 1},
            {"FOV", &a.fov, 5, 150, 1}, {"Near clip", &a.nearClip, .01f, std::min(100.f, a.farClip - .01f), .1f},
            {"Far clip", &a.farClip, a.nearClip + .01f, 32700, 10}};
        if (tab != 2 || board.lights.empty()) return {};
        auto &l = board.lights[light];
        return {{"Position X", &l.x, -32700, 32700, .5f}, {"Position Y", &l.y, -32700, 32700, .5f},
            {"Position Z", &l.z, -32700, 32700, .5f}, {"Yaw", &l.yaw, -360, 360, 1},
            {"Pitch", &l.pitch, -89, 89, 1}, {"Intensity", &l.intensity, 0, 1, .05f}};
    }
    void Paint(float px, float py, float x, float y)
    {
        if (!ui_inside(px, py, x + 276, y + 100, 640, 360)) { previousX = previousY = -1; return; }
        const int ix = std::clamp(int((px - x - 276) * MarcusBoardWidth / 640), 0, MarcusBoardWidth - 1);
        const int iy = std::clamp(int((py - y - 100) * MarcusBoardHeight / 360), 0, MarcusBoardHeight - 1);
        MarcusBoardStroke(board.angles[angle], previousX < 0 ? ix : previousX, previousY < 0 ? iy : previousY, ix, iy, size, static_cast<unsigned char>(colour));
        previousX = ix; previousY = iy; previewDirty = true;
    }
    void Move(float forward, float right, float up)
    {
        auto &c = board.angles[angle].camera;
        const float yaw = c.yaw * .01745329252f, pitch = c.pitch * .01745329252f;
        c.x = std::clamp(c.x - std::sin(yaw) * std::cos(pitch) * forward + std::cos(yaw) * right, -32700.f, 32700.f);
        c.y = std::clamp(c.y - std::sin(pitch) * forward + up, -32700.f, 32700.f);
        c.z = std::clamp(c.z - std::cos(yaw) * std::cos(pitch) * forward - std::sin(yaw) * right, -32700.f, 32700.f);
        previewDirty = true;
    }
    void Apply()
    {
        painting = false; dragField = -1;
        if (MarcusApplyStoryboard(player, board, newScene, angle, resources.c_str(), content.c_str())) { applied = true; open = false; }
        else error = marcus_error(player);
    }
};

extern "C" {
MarcusStoryboardEditor *storyboard_editor_create(void) { try { return new MarcusStoryboardEditor; } catch (...) { return nullptr; } }
void storyboard_editor_destroy(MarcusStoryboardEditor *e) { delete e; }
int storyboard_editor_visible(const MarcusStoryboardEditor *e) { return e && e->open; }
int storyboard_editor_show(MarcusStoryboardEditor *e, MarcusPlayer *player, int newScene, int angle,
    const MarcusImage *work, const char *resources, const char *content)
{
    if (!e) return 0;
    try
    {
        e->player = player; e->newScene = newScene != 0; e->applied = false;
        e->board = MarcusStoryboard{};
        if (!newScene && !MarcusGetStoryboard(player, e->board)) return 0;
        MarcusInfo info = {}; marcus_info(player, &info);
        e->angle = newScene ? 0 : std::clamp(angle < 0 ? int(info.camera) : angle, 0, int(e->board.angles.size()) - 1);
        e->hasWork = work && work->projection.valid && !work->projection.orthographic;
        if (e->hasWork)
        {
            e->work = MarcusBoardAngle{};
            e->work.camera = work->camera;
            e->work.camera.yaw = std::remainder(e->work.camera.yaw, 360.f);
            e->work.camera.pitch = std::clamp(e->work.camera.pitch, -89.f, 89.f);
            const float yaw = e->work.camera.yaw * .01745329252f, pitch = e->work.camera.pitch * .01745329252f;
            const auto *right = work->projection.transform;
            const float cosine = right[0] * std::cos(yaw) - right[2] * std::sin(yaw);
            const float sine = -right[0] * std::sin(yaw) * std::sin(pitch) + right[1] * std::cos(pitch) - right[2] * std::cos(yaw) * std::sin(pitch);
            e->work.roll = std::atan2(sine, cosine) * 57.2957795f;
            e->work.fov = std::clamp(work->projection.vertical_fov, 5.f, 150.f);
            e->work.nearClip = std::clamp(work->projection.near_clip, .01f, 100.f);
            e->work.farClip = std::clamp(work->projection.far_clip, e->work.nearClip + .01f, 32700.f);
        }
        e->resources = resources; e->content = content; e->error.clear(); e->undo.clear();
        e->painting = e->looking = false; e->dragField = -1; e->tab = e->light = 0;
        if (angle == -2) e->AddAngle();
        e->previewDirty = e->open = true;
        return 1;
    }
    catch (...) { e->open = false; return 0; }
}
int storyboard_editor_event(MarcusStoryboardEditor *e, const SDL_Event *event, int width, int height)
{
    if (!storyboard_editor_visible(e)) return 0;
    const float x = (width - 940) / 2.f, y = (height - 570) / 2.f;
    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        const auto key = event->key.key;
        const bool movement = e->tab == 1 && !(event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT)) &&
            (key == SDLK_W || key == SDLK_S || key == SDLK_A || key == SDLK_D || key == SDLK_Q || key == SDLK_E);
        if (event->key.repeat && !movement) return 1;
        if (key == SDLK_ESCAPE) { e->open = e->painting = e->looking = false; e->dragField = -1; }
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) e->Apply();
        else if (key == SDLK_Z && (event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) { e->painting = false; e->dragField = -1; e->Undo(); }
        else if (key >= SDLK_1 && key <= SDLK_6) e->colour = int(key - SDLK_1) + 1;
        else if (movement)
        {
            const float step = event->key.mod & SDL_KMOD_SHIFT ? 2.f : .5f;
            if (!event->key.repeat) e->Remember();
            e->Move(step * ((key == SDLK_W) - (key == SDLK_S)), step * ((key == SDLK_D) - (key == SDLK_A)), step * ((key == SDLK_E) - (key == SDLK_Q)));
        }
        else if (key == SDLK_E) e->colour = 0;
        else if (key == SDLK_LEFTBRACKET) e->size = std::max(1, e->size / 2);
        else if (key == SDLK_RIGHTBRACKET) e->size = std::min(64, e->size * 2);
        else if (key == SDLK_LEFT || key == SDLK_RIGHT)
        { e->angle = std::clamp(e->angle + (key == SDLK_LEFT ? -1 : 1), 0, int(e->board.angles.size()) - 1); e->previewDirty = true; }
    }
    else if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (e->painting) e->Paint(event->motion.x, event->motion.y, x, y);
        else if (e->looking)
        {
            auto &c = e->board.angles[e->angle].camera;
            c.yaw = std::remainder(c.yaw - event->motion.xrel * .2f, 360.f);
            c.pitch = std::clamp(c.pitch + event->motion.yrel * .2f, -89.f, 89.f);
            e->previewDirty = true;
        }
        else if (e->dragField >= 0)
        {
            auto fields = e->Fields();
            if (e->dragField < int(fields.size()))
            {
                auto &f = fields[e->dragField];
                const float scale = SDL_GetModState() & SDL_KMOD_SHIFT ? .02f : .2f;
                *f.value = std::clamp(e->dragValue + (event->motion.x - e->dragStart) * f.step * scale, f.low, f.high);
                e->previewDirty = true;
            }
        }
    }
    else if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    { e->painting = e->looking = false; e->dragField = -1; }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_RIGHT && e->tab == 1 &&
        ui_inside(event->button.x, event->button.y, x + 276, y + 100, 640, 360))
    { e->Remember(); e->looking = true; }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL && e->tab == 1 && event->wheel.y != 0)
    { e->Remember(); e->Move(event->wheel.y * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -.5f : .5f), 0, 0); }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        const float px = event->button.x, py = event->button.y;
        if (ui_inside(px, py, x + 884, y + 18, 32, 28) || ui_inside(px, py, x + 690, y + 518, 92, 30)) e->open = false;
        else if (ui_inside(px, py, x + 794, y + 518, 122, 30)) e->Apply();
        else if (ui_inside(px, py, x + 276, y + 58, 42, 28)) { e->angle = std::max(0, e->angle - 1); e->previewDirty = true; }
        else if (ui_inside(px, py, x + 324, y + 58, 42, 28)) { e->angle = std::min(int(e->board.angles.size()) - 1, e->angle + 1); e->previewDirty = true; }
        else if (ui_inside(px, py, x + 652, y + 58, 122, 28)) e->AddAngle();
        else if (ui_inside(px, py, x + 784, y + 58, 132, 28) && e->hasWork)
        {
            e->Remember(); auto pixels = std::move(e->board.angles[e->angle].pixels);
            e->board.angles[e->angle] = e->work; e->board.angles[e->angle].pixels = std::move(pixels); e->previewDirty = true;
        }
        else if (ui_inside(px, py, x + 24, y + 76, 224, 30)) e->tab = std::min(2, int((px - x - 24) / 76));
        else if (e->tab == 0)
        {
            for (int c = 1; c <= 6; ++c)
                if (ui_inside(px, py, x + 24 + ((c - 1) % 3) * 76, y + 126 + ((c - 1) / 3) * 68, 68, 58)) e->colour = c;
            for (int s = 0; s < 5; ++s)
                if (ui_inside(px, py, x + 24 + s * 45, y + 300, 40, 30)) e->size = sizes[s];
            if (ui_inside(px, py, x + 24, y + 346, 224, 30)) e->colour = 0;
            if (ui_inside(px, py, x + 24, y + 390, 106, 30)) { e->Remember(); std::fill(e->board.angles[e->angle].pixels.begin(), e->board.angles[e->angle].pixels.end(), 0); e->previewDirty = true; }
            if (ui_inside(px, py, x + 140, y + 390, 108, 30)) e->Undo();
            if (ui_inside(px, py, x + 276, y + 100, 640, 360))
            { e->Remember(); e->painting = true; e->previousX = e->previousY = -1; e->Paint(px, py, x, y); }
        }
        else
        {
            if (e->tab == 2)
            {
                if (ui_inside(px, py, x + 24, y + 118, 40, 28)) e->light = std::max(0, e->light - 1);
                if (ui_inside(px, py, x + 70, y + 118, 40, 28)) e->light = std::max(0, std::min(int(e->board.lights.size()) - 1, e->light + 1));
                if (ui_inside(px, py, x + 116, y + 118, 62, 28) && e->board.lights.size() < 16)
                { e->Remember(); e->board.lights.push_back(MarcusBoardLight{}); e->light = int(e->board.lights.size()) - 1; e->previewDirty = true; }
                if (ui_inside(px, py, x + 184, y + 118, 64, 28) && !e->board.lights.empty())
                { e->Remember(); e->board.lights.erase(e->board.lights.begin() + e->light); e->light = std::max(0, e->light - 1); e->previewDirty = true; }
                if (ui_inside(px, py, x + 24, y + 156, 224, 28) && !e->board.lights.empty())
                { e->Remember(); e->board.lights[e->light].point ^= 1; e->previewDirty = true; }
            }
            auto fields = e->Fields();
            for (int i = 0; i < int(fields.size()); ++i)
            {
                if (!ui_inside(px, py, x + 112, y + (e->tab == 1 ? 124 : 200) + i * 32, 136, 26)) continue;
                e->Remember(); auto &f = fields[i];
                if (px >= x + 204) *f.value = std::clamp(*f.value + (px >= x + 226 ? 1 : -1) * f.step, f.low, f.high);
                else { e->dragField = i; e->dragStart = px; e->dragValue = *f.value; }
                e->previewDirty = true; break;
            }
        }
    }
    return event->type != SDL_EVENT_QUIT && event->type != SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}
void storyboard_editor_draw(MarcusStoryboardEditor *e, MarcusUI *ui, int width, int height)
{
    if (!storyboard_editor_visible(e)) return;
    const float x = (width - 940) / 2.f, y = (height - 570) / 2.f;
    if (e->previewDirty)
    {
        MarcusImage image = {};
        if (!e->newScene && MarcusRenderStoryboard(e->player, e->board, e->angle, image))
            e->rgba.assign(image.rgba, image.rgba + size_t(image.pitch) * image.height);
        else
        {
            e->rgba.resize(MarcusBoardWidth * MarcusBoardHeight * 4);
            const auto &pixels = e->board.angles[e->angle].pixels;
            for (size_t i = 0; i < pixels.size(); ++i)
            {
                const auto rgb = MarcusBoardColours[pixels[i]];
                e->rgba[i * 4] = static_cast<unsigned char>(rgb >> 16); e->rgba[i * 4 + 1] = static_cast<unsigned char>(rgb >> 8);
                e->rgba[i * 4 + 2] = static_cast<unsigned char>(rgb); e->rgba[i * 4 + 3] = 255;
            }
        }
        if (!e->texture) e->texture = SDL_CreateTexture(ui->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, MarcusBoardWidth, MarcusBoardHeight);
        if (e->texture) { SDL_SetTextureScaleMode(e->texture, SDL_SCALEMODE_NEAREST); SDL_UpdateTexture(e->texture, nullptr, e->rgba.data(), MarcusBoardWidth * 4); }
        e->previewDirty = false;
    }
    ui_fill(ui, 0, 0, float(width), float(height), 0x121519);
    ui_fill(ui, x - 1, y - 1, 942, 572, 0x5b6570); ui_fill(ui, x, y, 940, 570, 0x25282d);
    ui_heading(ui, x + 24, y + 22, e->newScene ? "NEW STORYBOARD SCENE" : "STORYBOARD", 0x78cbc4);
    ui_button(ui, x + 884, y + 18, 32, "x", 0, 1);
    for (int t = 0; t < 3; ++t) ui_button_sized(ui, x + 24 + t * 76, y + 76, 72, 30, t == 0 ? "Paint" : t == 1 ? "Camera" : "Lights", e->tab == t, 1);
    char label[192];
    ui_button_sized(ui, x + 276, y + 58, 42, 28, "<", 0, e->angle > 0);
    ui_button_sized(ui, x + 324, y + 58, 42, 28, ">", 0, e->angle + 1 < int(e->board.angles.size()));
    SDL_snprintf(label, sizeof(label), "Angle %d of %d", e->angle + 1, int(e->board.angles.size()));
    ui_text(ui, x + 380, y + 65, label, 0xdde0e5);
    ui_button_sized(ui, x + 652, y + 58, 122, 28, "New angle", 0, e->board.angles.size() < 64);
    ui_button_sized(ui, x + 784, y + 58, 132, 28, "Use work camera", 0, e->hasWork);
    ui_fill(ui, x + 275, y + 99, 642, 362, 0x5b6570);
    if (e->texture) { SDL_FRect destination = {x + 276, y + 100, 640, 360}; SDL_RenderTexture(ui->renderer, e->texture, nullptr, &destination); }
    if (e->tab == 1) Grid(ui, e->board.angles[e->angle], x + 276, y + 100);
    if (e->tab == 0)
    {
        for (int c = 1; c <= 6; ++c)
        {
            const float left = x + 24 + ((c - 1) % 3) * 76, top = y + 126 + ((c - 1) / 3) * 68;
            ui_fill(ui, left - 2, top - 2, 70, 60, e->colour == c ? 0x78cbc4 : 0x424952);
            ui_fill(ui, left, top, 66, 34, MarcusBoardColours[c]);
            ui_label(ui, left + 2, top + 41, 66, colourNames[c], 0xdde0e5);
        }
        SDL_snprintf(label, sizeof(label), "Crunchy brush / %d px", e->size);
        ui_heading(ui, x + 24, y + 276, label, 0x969da8);
        for (int s = 0; s < 5; ++s)
        { SDL_snprintf(label, sizeof(label), "%d", sizes[s]); ui_button_sized(ui, x + 24 + s * 45, y + 300, 40, 30, label, e->size == sizes[s], 1); }
        ui_button_sized(ui, x + 24, y + 346, 224, 30, "Grey eraser (E)", e->colour == 0, 1);
        ui_button_sized(ui, x + 24, y + 390, 106, 30, "Clear angle", 0, 1);
        ui_button_sized(ui, x + 140, y + 390, 108, 30, "Undo", 0, !e->undo.empty());
        ui_text(ui, x + 24, y + 442, "1-6 colours / [ ] brush size", 0x969da8);
        ui_text(ui, x + 24, y + 464, "Ctrl+Z / Cmd+Z to undo", 0x969da8);
    }
    else
    {
        if (e->tab == 2)
        {
            ui_button_sized(ui, x + 24, y + 118, 40, 28, "<", 0, e->light > 0);
            ui_button_sized(ui, x + 70, y + 118, 40, 28, ">", 0, e->light + 1 < int(e->board.lights.size()));
            ui_button_sized(ui, x + 116, y + 118, 62, 28, "Add", 0, e->board.lights.size() < 16);
            ui_button_sized(ui, x + 184, y + 118, 64, 28, "Delete", 0, !e->board.lights.empty());
            const bool any = !e->board.lights.empty();
            SDL_snprintf(label, sizeof(label), "Light %d / %d: %s", any ? e->light + 1 : 0, int(e->board.lights.size()), any && e->board.lights[e->light].point ? "Point" : "Directional");
            ui_button_sized(ui, x + 24, y + 156, 224, 28, label, 0, any);
        }
        auto fields = e->Fields();
        for (int i = 0; i < int(fields.size()); ++i)
        {
            const float top = y + (e->tab == 1 ? 124 : 200) + i * 32;
            const auto &f = fields[i];
            ui_text(ui, x + 24, top + 6, f.name, 0xdde0e5);
            ui_fill(ui, x + 112, top, 86, 26, 0x1c1e22);
            SDL_snprintf(label, sizeof(label), "%.2f", *f.value); ui_label(ui, x + 118, top + 6, 76, label, 0xdde0e5);
            ui_button_sized(ui, x + 204, top, 20, 26, "-", 0, 1); ui_button_sized(ui, x + 228, top, 20, 26, "+", 0, 1);
        }
        ui_text(ui, x + 24, y + 442, "Drag values / Shift for fine", 0x969da8);
        ui_text(ui, x + 24, y + 464, e->tab == 1 ? "RMB look / WASD + Q E move" : "Lights affect 3D actors / props.", 0x969da8);
    }
    ui_label(ui, x + 276, y + 476, 640, "Flat background / no scenery depth. Actors remain in front of the drawing.", 0x969da8);
    ui_label(ui, x + 24, y + 524, 648, e->error.empty() ? "Enter applies / Escape cancels / Save copy from Scenes to keep your blockout." : e->error.c_str(), e->error.empty() ? 0x969da8 : 0xe4a1a1);
    ui_button_sized(ui, x + 690, y + 518, 92, 30, "Cancel", 0, 1);
    ui_button_sized(ui, x + 794, y + 518, 122, 30, e->newScene ? "Add scene" : "Apply", 1, 1);
}
int storyboard_editor_take(MarcusStoryboardEditor *e) { if (!e || !e->applied) return 0; e->applied = false; return 1; }
}
