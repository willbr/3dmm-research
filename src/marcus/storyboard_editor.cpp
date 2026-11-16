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
bool Inside(float x, float y, const SDL_FRect &r) { return ui_inside(x, y, r.x, r.y, r.w, r.h) != 0; }
unsigned MoveKey(SDL_Keycode key)
{
    switch (key) { case SDLK_W: return 1; case SDLK_S: return 2; case SDLK_A: return 4; case SDLK_D: return 8;
        case SDLK_Q: return 16; case SDLK_E: return 32; default: return 0; }
}
void Grid(MarcusUI *ui, const MarcusBoardAngle &angle, const SDL_FRect &r)
{
    const bool clipped = SDL_RenderClipEnabled(ui->renderer);
    SDL_Rect previous; SDL_GetRenderClipRect(ui->renderer, &previous);
    SDL_Rect clip = {int(r.x), int(r.y), int(r.w), int(r.h)}; SDL_SetRenderClipRect(ui->renderer, &clip);
    for (const auto &line : MarcusBoardGridLines(angle))
        ui_line(ui, float(r.x + line[0] * r.w), float(r.y + line[1] * r.h),
            float(r.x + line[2] * r.w), float(r.y + line[3] * r.h), 0xadb3b6);
    ui_line(ui, r.x + r.w / 2 - 6, r.y + r.h / 2, r.x + r.w / 2 + 6, r.y + r.h / 2, 0x6b7f84);
    ui_line(ui, r.x + r.w / 2, r.y + r.h / 2 - 6, r.x + r.w / 2, r.y + r.h / 2 + 6, 0x6b7f84);
    ui_text(ui, r.x + 12, r.y + 12, "Perspective guide / Bake grid to paint it", 0x5b6570);
    SDL_SetRenderClipRect(ui->renderer, clipped ? &previous : nullptr);
}
}
struct MarcusStoryboardEditor
{
    MarcusPlayer *player = nullptr;
    MarcusStoryboard board;
    MarcusBoardAngle work;
    SDL_FRect view = {}, canvas = {}, tools = {}, bar = {};
    bool open = false, changed = false, painting = false, looking = false, pending = false, previewDirty = true, hasWork = false;
    int angle = 0, light = 0, tab = 0, colour = 1, size = 8, previousX = -1, previousY = -1, dragField = -1;
    float dragStart = 0, dragValue = 0;
    unsigned long document = 0, scene = 0;
    unsigned cameraKeys = 0;
    long frame = 0;
    std::string resources, content, error;
    std::vector<unsigned char> baseline, rgba;
    SDL_Texture *texture = nullptr;
    ~MarcusStoryboardEditor() { SDL_DestroyTexture(texture); }
    bool Gesture() const { return painting || looking || dragField >= 0 || cameraKeys != 0; }
    void Reload()
    {
        MarcusInfo info = {}; MarcusScene current = {};
        if (!marcus_info(player, &info) || !marcus_scene(player, info.scene, &current) || !MarcusGetStoryboard(player, board))
        { open = false; return; }
        document = marcus_document(player); scene = current.chunk; frame = info.frame;
        angle = std::min(int(info.camera), int(board.angles.size()) - 1);
        light = std::clamp(light, 0, std::max(0, int(board.lights.size()) - 1));
        baseline = MarcusEncodeBoard(board);
        pending = painting = looking = false; dragField = -1; cameraKeys = 0;
        previewDirty = true;
    }
    bool Commit()
    {
        if (!pending) return true;
        auto bytes = MarcusEncodeBoard(board);
        MarcusInfo info = {};
        const bool dataChanged = bytes != baseline;
        const bool cameraChanged = marcus_info(player, &info) && info.camera != static_cast<unsigned long>(angle);
        const bool ok = dataChanged ? MarcusApplyStoryboard(player, board, false, angle, resources.c_str(), content.c_str())
                                   : !cameraChanged || marcus_set_scene_camera(player, angle);
        if (!ok) { error = marcus_error(player); return false; }
        baseline = std::move(bytes); pending = false;
        document = marcus_document(player);
        changed |= dataChanged || cameraChanged;
        error.clear(); previewDirty = true;
        return true;
    }
    void Finish()
    {
        painting = looking = false; dragField = -1; cameraKeys = 0;
        Commit();
    }
    void SelectAngle(int index)
    {
        Finish();
        angle = std::clamp(index, 0, int(board.angles.size()) - 1);
        pending = previewDirty = true; Commit();
    }
    void AddAngle()
    {
        if (board.angles.size() >= 64) return;
        Finish();
        auto next = board.angles[angle]; std::fill(next.pixels.begin(), next.pixels.end(), 0);
        board.angles.push_back(std::move(next)); angle = int(board.angles.size()) - 1;
        tab = 1; pending = previewDirty = true; Commit();
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
    void Paint(float x, float y)
    {
        if (!Inside(x, y, canvas)) { previousX = previousY = -1; return; }
        const int ix = std::clamp(int((x - canvas.x) * MarcusBoardWidth / canvas.w), 0, MarcusBoardWidth - 1);
        const int iy = std::clamp(int((y - canvas.y) * MarcusBoardHeight / canvas.h), 0, MarcusBoardHeight - 1);
        MarcusBoardStroke(board.angles[angle], previousX < 0 ? ix : previousX, previousY < 0 ? iy : previousY, ix, iy, size, static_cast<unsigned char>(colour));
        previousX = ix; previousY = iy; pending = previewDirty = true;
    }
    void Move(float forward, float right, float up)
    {
        auto &c = board.angles[angle].camera;
        const float yaw = c.yaw * .01745329252f, pitch = c.pitch * .01745329252f;
        c.x = std::clamp(c.x - std::sin(yaw) * std::cos(pitch) * forward + std::cos(yaw) * right, -32700.f, 32700.f);
        c.y = std::clamp(c.y - std::sin(pitch) * forward + up, -32700.f, 32700.f);
        c.z = std::clamp(c.z - std::cos(yaw) * std::cos(pitch) * forward - std::sin(yaw) * right, -32700.f, 32700.f);
        pending = previewDirty = true;
    }
};

extern "C" {
MarcusStoryboardEditor *storyboard_editor_create(void) { try { return new MarcusStoryboardEditor; } catch (...) { return nullptr; } }
void storyboard_editor_destroy(MarcusStoryboardEditor *e) { delete e; }
int storyboard_editor_visible(const MarcusStoryboardEditor *e) { return e && e->open; }
void storyboard_editor_close(MarcusStoryboardEditor *e) { if (e && e->open) { e->Finish(); e->open = false; } }
void storyboard_editor_layout(MarcusStoryboardEditor *e, const SDL_FRect *view, const SDL_FRect *canvas, const SDL_FRect *tools, const SDL_FRect *bar)
{ if (e) { e->view = *view; e->canvas = *canvas; e->tools = *tools; e->bar = *bar; } }
void storyboard_editor_sync(MarcusStoryboardEditor *e, MarcusPlayer *player, const char *resources, const char *content)
{
    if (!storyboard_editor_visible(e)) return;
    if (e->player != player) { e->open = false; return; }
    e->resources = resources; e->content = content;
    MarcusInfo info = {}; MarcusScene current = {};
    if (!marcus_info(player, &info) || !marcus_scene(player, info.scene, &current) || !marcus_is_storyboard(player))
    { e->open = false; return; }
    if (current.chunk != e->scene || info.frame != e->frame || info.camera != static_cast<unsigned long>(e->angle) ||
        marcus_document(player) != e->document)
    {
        if (e->Gesture() && current.chunk == e->scene && info.frame == e->frame) { e->previewDirty = true; return; }
        e->Reload();
    }
}
int storyboard_editor_show(MarcusStoryboardEditor *e, MarcusPlayer *player, int newScene, int angle,
    const MarcusImage *work, const char *resources, const char *content)
{
    if (!e) return 0;
    try
    {
        storyboard_editor_close(e);
        e->player = player; e->changed = false; e->error.clear();
        e->resources = resources; e->content = content;
        if (newScene && !MarcusApplyStoryboard(player, MarcusStoryboard{}, true, 0, resources, content)) return 0;
        e->open = true; e->Reload();
        if (!e->open) return 0;
        e->changed = newScene != 0;
        e->hasWork = work && work->projection.valid && !work->projection.orthographic;
        if (e->hasWork)
        {
            e->work = MarcusBoardAngle{}; e->work.camera = work->camera;
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
        e->tab = e->light = 0;
        if (angle == -2) e->AddAngle();
        else if (angle >= 0) e->SelectAngle(angle);
        return 1;
    }
    catch (const std::exception &error) { e->error = error.what(); e->open = false; return 0; }
}
int storyboard_editor_event(MarcusStoryboardEditor *e, const SDL_Event *event)
{
    if (!storyboard_editor_visible(e)) return 0;
    const float x = e->tools.x + 16, y = e->tools.y;
    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        const auto key = event->key.key;
        if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL ||
            key == SDLK_LALT || key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI) return e->Gesture();
        const bool command = event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT);
        if (key == SDLK_ESCAPE && !event->key.repeat)
        {
            if (e->Gesture()) e->Reload(); else storyboard_editor_close(e);
            return 1;
        }
        if (key == SDLK_Z && command && e->Gesture()) { e->Reload(); return 1; }
        if (command) { e->Finish(); return 0; }
        const bool movement = e->tab == 1 && (key == SDLK_W || key == SDLK_S || key == SDLK_A || key == SDLK_D || key == SDLK_Q || key == SDLK_E);
        if (movement)
        {
            if (e->painting || e->dragField >= 0) return 1;
            const float step = event->key.mod & SDL_KMOD_SHIFT ? 2.f : .5f;
            e->cameraKeys |= MoveKey(key);
            e->Move(step * ((key == SDLK_W) - (key == SDLK_S)), step * ((key == SDLK_D) - (key == SDLK_A)), step * ((key == SDLK_E) - (key == SDLK_Q)));
            return 1;
        }
        if (!event->key.repeat)
        {
            if (key >= SDLK_1 && key <= SDLK_6) { e->colour = int(key - SDLK_1) + 1; return 1; }
            if (key == SDLK_E) { e->colour = 0; return 1; }
            if (key == SDLK_LEFTBRACKET) { e->size = std::max(1, e->size / 2); return 1; }
            if (key == SDLK_RIGHTBRACKET) { e->size = std::min(64, e->size * 2); return 1; }
        }
        e->Finish(); return 0;
    }
    if (event->type == SDL_EVENT_KEY_UP && (e->cameraKeys & MoveKey(event->key.key)))
    {
        e->cameraKeys &= ~MoveKey(event->key.key);
        if (!e->Gesture()) e->Commit();
        return 1;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) { e->Finish(); return 0; }
    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (e->painting) { e->Paint(event->motion.x, event->motion.y); return 1; }
        if (e->looking)
        {
            auto &c = e->board.angles[e->angle].camera;
            c.yaw = std::remainder(c.yaw - event->motion.xrel * .2f, 360.f);
            c.pitch = std::clamp(c.pitch + event->motion.yrel * .2f, -89.f, 89.f);
            e->pending = e->previewDirty = true; return 1;
        }
        if (e->dragField >= 0)
        {
            auto fields = e->Fields();
            if (e->dragField < int(fields.size()))
            {
                auto &f = fields[e->dragField];
                const float scale = SDL_GetModState() & SDL_KMOD_SHIFT ? .02f : .2f;
                *f.value = std::clamp(e->dragValue + (event->motion.x - e->dragStart) * f.step * scale, f.low, f.high);
                e->pending = e->previewDirty = true;
            }
            return 1;
        }
        return Inside(event->motion.x, event->motion.y, e->view) || Inside(event->motion.x, event->motion.y, e->tools);
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && e->Gesture()) { e->Finish(); return 1; }
    if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        if (!Inside(event->wheel.mouse_x, event->wheel.mouse_y, e->canvas))
            return Inside(event->wheel.mouse_x, event->wheel.mouse_y, e->view) || Inside(event->wheel.mouse_x, event->wheel.mouse_y, e->tools);
        if (e->tab == 1 && event->wheel.y != 0)
        { e->Move(event->wheel.y * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -.5f : .5f), 0, 0); e->Commit(); }
        return 1;
    }
    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return 0;
    const float px = event->button.x, py = event->button.y;
    if (!Inside(px, py, e->view) && !Inside(px, py, e->tools)) { e->Finish(); return 0; }
    if (Inside(px, py, e->canvas))
    {
        if (event->button.button == SDL_BUTTON_LEFT && e->tab == 0)
        { e->painting = true; e->previousX = e->previousY = -1; e->Paint(px, py); }
        else if (event->button.button == SDL_BUTTON_RIGHT && e->tab == 1) e->looking = true;
        return 1;
    }
    if (event->button.button != SDL_BUTTON_LEFT) return 1;
    e->Finish();
    const float right = e->bar.x + e->bar.w;
    if (ui_inside(px, py, e->tools.x + e->tools.w - 80, y + 12, 64, 28)) { storyboard_editor_close(e); return 1; }
    if (ui_inside(px, py, e->bar.x + 140, e->bar.y, 32, 28)) { e->SelectAngle(e->angle - 1); return 1; }
    if (ui_inside(px, py, e->bar.x + 180, e->bar.y, 32, 28)) { e->SelectAngle(e->angle + 1); return 1; }
    if (ui_inside(px, py, right - 250, e->bar.y, 112, 28)) { e->AddAngle(); return 1; }
    if (ui_inside(px, py, right - 130, e->bar.y, 130, 28) && e->hasWork)
    {
        auto pixels = std::move(e->board.angles[e->angle].pixels);
        e->board.angles[e->angle] = e->work; e->board.angles[e->angle].pixels = std::move(pixels);
        e->pending = true; e->Commit(); return 1;
    }
    for (int t = 0; t < 3; ++t)
        if (ui_inside(px, py, x + t * 90, y + 52, 84, 30)) { e->tab = t; return 1; }
    if (e->tab == 0)
    {
        for (int c = 1; c <= 6; ++c)
            if (ui_inside(px, py, x + ((c - 1) % 3) * 90, y + 104 + ((c - 1) / 3) * 72, 84, 62)) e->colour = c;
        for (int s = 0; s < 5; ++s)
            if (ui_inside(px, py, x + s * 54, y + 284, 48, 30)) e->size = sizes[s];
        if (ui_inside(px, py, x, y + 332, 264, 30)) e->colour = 0;
        if (ui_inside(px, py, x, y + 378, 126, 30))
        { std::fill(e->board.angles[e->angle].pixels.begin(), e->board.angles[e->angle].pixels.end(), 0); e->pending = true; e->Commit(); }
        if (ui_inside(px, py, x + 138, y + 378, 126, 30) && marcus_can_undo(e->player))
        { if (marcus_undo(e->player)) { e->Reload(); e->changed = true; } else e->error = marcus_error(e->player); }
    }
    else
    {
        if (e->tab == 1 && e->colour > 0 && ui_inside(px, py, x, y + 394, 264, 30) && !MarcusBoardGridLines(e->board.angles[e->angle]).empty())
        { MarcusBoardBakeGrid(e->board.angles[e->angle], static_cast<unsigned char>(e->colour)); e->pending = true; e->Commit(); e->tab = 0; }
        if (e->tab == 2)
        {
            if (ui_inside(px, py, x, y + 104, 40, 28)) e->light = std::max(0, e->light - 1);
            if (ui_inside(px, py, x + 48, y + 104, 40, 28)) e->light = std::max(0, std::min(int(e->board.lights.size()) - 1, e->light + 1));
            if (ui_inside(px, py, x + 96, y + 104, 76, 28) && e->board.lights.size() < 16)
            { e->board.lights.push_back(MarcusBoardLight{}); e->light = int(e->board.lights.size()) - 1; e->pending = true; }
            if (ui_inside(px, py, x + 180, y + 104, 84, 28) && !e->board.lights.empty())
            { e->board.lights.erase(e->board.lights.begin() + e->light); e->light = std::max(0, e->light - 1); e->pending = true; }
            if (ui_inside(px, py, x, y + 144, 264, 28) && !e->board.lights.empty())
            { e->board.lights[e->light].point ^= 1; e->pending = true; }
        }
        auto fields = e->Fields();
        for (int i = 0; i < int(fields.size()); ++i)
        {
            if (!ui_inside(px, py, x + 104, y + (e->tab == 1 ? 104 : 192) + i * 30, 160, 26)) continue;
            auto &f = fields[i];
            if (px >= x + 212) { *f.value = std::clamp(*f.value + (px >= x + 240 ? 1 : -1) * f.step, f.low, f.high); e->pending = true; }
            else { e->dragField = i; e->dragStart = px; e->dragValue = *f.value; }
            break;
        }
        if (e->dragField < 0) e->Commit();
    }
    return 1;
}
void storyboard_editor_draw(MarcusStoryboardEditor *e, MarcusUI *ui)
{
    if (!storyboard_editor_visible(e)) return;
    if (e->previewDirty)
    {
        MarcusImage image = {};
        if (MarcusRenderStoryboard(e->player, e->board, e->angle, image))
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
    const float x = e->tools.x + 16, y = e->tools.y, right = e->bar.x + e->bar.w;
    ui_fill(ui, e->tools.x, y, e->tools.w, e->tools.h, 0x25282d);
    ui_heading(ui, x, y + 20, "STORYBOARD TOOLS", 0x78cbc4);
    ui_button_sized(ui, e->tools.x + e->tools.w - 80, y + 12, 64, 28, "Done", 0, 1);
    for (int t = 0; t < 3; ++t) ui_button_sized(ui, x + t * 90, y + 52, 84, 30, t == 0 ? "Paint" : t == 1 ? "Camera" : "Lights", e->tab == t, 1);
    ui_heading(ui, e->bar.x, e->bar.y + 8, "STORYBOARD", 0x78cbc4);
    char label[192];
    ui_button_sized(ui, e->bar.x + 140, e->bar.y, 32, 28, "<", 0, e->angle > 0);
    ui_button_sized(ui, e->bar.x + 180, e->bar.y, 32, 28, ">", 0, e->angle + 1 < int(e->board.angles.size()));
    SDL_snprintf(label, sizeof(label), "Angle %d of %d", e->angle + 1, int(e->board.angles.size()));
    ui_label(ui, e->bar.x + 226, e->bar.y + 8, std::max(0.f, e->bar.w - 484), label, 0xdde0e5);
    ui_button_sized(ui, right - 250, e->bar.y, 112, 28, "New angle", 0, e->board.angles.size() < 64);
    ui_button_sized(ui, right - 130, e->bar.y, 130, 28, "Use work camera", 0, e->hasWork);
    ui_fill(ui, e->canvas.x - 1, e->canvas.y - 1, e->canvas.w + 2, e->canvas.h + 2, 0x5b6570);
    if (e->texture) SDL_RenderTexture(ui->renderer, e->texture, nullptr, &e->canvas);
    if (e->tab == 1) Grid(ui, e->board.angles[e->angle], e->canvas);
    if (e->tab == 0)
    {
        for (int c = 1; c <= 6; ++c)
        {
            const float left = x + ((c - 1) % 3) * 90, top = y + 104 + ((c - 1) / 3) * 72;
            ui_fill(ui, left - 2, top - 2, 88, 66, e->colour == c ? 0x78cbc4 : 0x424952);
            ui_fill(ui, left, top, 84, 38, MarcusBoardColours[c]);
            ui_label(ui, left + 4, top + 46, 76, colourNames[c], 0xdde0e5);
        }
        SDL_snprintf(label, sizeof(label), "Crunchy brush / %d px", e->size);
        ui_heading(ui, x, y + 258, label, 0x969da8);
        for (int s = 0; s < 5; ++s)
        { SDL_snprintf(label, sizeof(label), "%d", sizes[s]); ui_button_sized(ui, x + s * 54, y + 284, 48, 30, label, e->size == sizes[s], 1); }
        ui_button_sized(ui, x, y + 332, 264, 30, "Grey eraser (E)", e->colour == 0, 1);
        ui_button_sized(ui, x, y + 378, 126, 30, "Clear angle", 0, 1);
        ui_button_sized(ui, x + 138, y + 378, 126, 30, "Undo", 0, marcus_can_undo(e->player));
        ui_text(ui, x, y + 440, "1-6 colours / [ ] brush size", 0x969da8);
        ui_text(ui, x, y + 464, "Ctrl+Z / Cmd+Z to undo", 0x969da8);
    }
    else
    {
        if (e->tab == 2)
        {
            ui_button_sized(ui, x, y + 104, 40, 28, "<", 0, e->light > 0);
            ui_button_sized(ui, x + 48, y + 104, 40, 28, ">", 0, e->light + 1 < int(e->board.lights.size()));
            ui_button_sized(ui, x + 96, y + 104, 76, 28, "Add", 0, e->board.lights.size() < 16);
            ui_button_sized(ui, x + 180, y + 104, 84, 28, "Delete", 0, !e->board.lights.empty());
            const bool any = !e->board.lights.empty();
            SDL_snprintf(label, sizeof(label), "Light %d / %d: %s", any ? e->light + 1 : 0, int(e->board.lights.size()), any && e->board.lights[e->light].point ? "Point" : "Directional");
            ui_button_sized(ui, x, y + 144, 264, 28, label, 0, any);
        }
        auto fields = e->Fields();
        for (int i = 0; i < int(fields.size()); ++i)
        {
            const float top = y + (e->tab == 1 ? 104 : 192) + i * 30;
            const auto &f = fields[i];
            ui_text(ui, x, top + 6, f.name, 0xdde0e5);
            ui_fill(ui, x + 104, top, 100, 26, 0x1c1e22);
            SDL_snprintf(label, sizeof(label), "%.2f", *f.value); ui_label(ui, x + 110, top + 6, 88, label, 0xdde0e5);
            ui_button_sized(ui, x + 212, top, 24, 26, "-", 0, 1); ui_button_sized(ui, x + 240, top, 24, 26, "+", 0, 1);
        }
        if (e->tab == 1)
        {
            const bool enabled = e->colour > 0 && !MarcusBoardGridLines(e->board.angles[e->angle]).empty();
            SDL_snprintf(label, sizeof(label), "Bake grid (%s)", colourNames[e->colour]);
            ui_button_sized(ui, x, y + 394, 264, 30, label, 0, enabled);
            if (ui_inside(ui->mouse_x, ui->mouse_y, x, y + 394, 264, 30))
                ui->hint = "Bake this angle's grid in the selected colour (1-6). Undo removes it; no depth is added.";
        }
        ui_text(ui, x, y + 440, "Drag values / Shift for fine", 0x969da8);
        ui_text(ui, x, y + 464, e->tab == 1 ? "RMB look / WASD + Q E move" : "Lights affect 3D actors / props.", 0x969da8);
    }
    ui_text(ui, x, y + 512, "Edits join the movie as you work.", 0x78cbc4);
    ui_text(ui, x, y + 536, "Save copy / Ctrl+S to keep them.", 0x969da8);
    if (!e->error.empty()) ui_label(ui, x, y + 568, 264, e->error.c_str(), 0xe4a1a1);
    if (Inside(ui->mouse_x, ui->mouse_y, e->canvas))
        ui->hint = e->tab == 0 ? "Paint the background / 1-6 colours / E eraser / [ ] size / Ctrl+Z undo / Escape finishes"
            : e->tab == 1 ? "Camera: right-drag to look / WASD + Q E move / wheel forward and back" : "Lights affect actors and props across this scene's camera angles";
}
int storyboard_editor_take(MarcusStoryboardEditor *e) { if (!e || !e->changed) return 0; e->changed = false; return 1; }
}
