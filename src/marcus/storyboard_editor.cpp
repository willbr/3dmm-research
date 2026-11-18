#include "storyboard_editor.h"
#include "storyboard_view.h"
#include "dialog_host.h"
#include "storyboard.h"
#include "storyboard_tools.h"
#include "camera_controls.h"
#include "transform_gizmo.h"
#include "colour_picker.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
struct Field { const char *name; float *value; float low, high, step; };
bool Inside(float x, float y, const SDL_FRect &r) { return ui_inside(x, y, r.x, r.y, r.w, r.h) != 0; }

float SegmentDistance(float x, float y, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    const float t = length > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / length, 0.f, 1.f) : 0;
    return std::hypot(x - ax - t * dx, y - ay - t * dy);
}
void BuildGrid(const std::vector<std::array<double, 4>> &lines, const SDL_FRect &r, const MarcusBoardAngle &angle,
    float opacity, std::vector<SDL_Vertex> &vertices, std::vector<int> &indices)
{
    vertices.clear(); indices.clear();
    const MarcusBoardGridFade fade(angle, r.w, r.h);
    for (const auto &line : lines)
    {
        const double dx = (line[2] - line[0]) * r.w, dy = (line[3] - line[1]) * r.h;
        const double length = std::hypot(dx, dy);
        if (length < .001) continue;
        const float nx = float(-dy / (2 * length)), ny = float(dx / (2 * length));
        const int steps = std::max(1, int(std::ceil(length / 8)));
        auto point = [&](int step) {
            const double t = double(step) / steps;
            return std::array<double, 2>{line[0] + (line[2] - line[0]) * t, line[1] + (line[3] - line[1]) * t};
        };
        auto a = point(0);
        float alphaA = fade.Opacity(a[0], a[1]) * opacity;
        for (int step = 1; step <= steps; ++step)
        {
            const auto b = point(step);
            const float alphaB = fade.Opacity(b[0], b[1]) * opacity;
            if (alphaA > 0 || alphaB > 0)
            {
                const float ax = float(r.x + a[0] * r.w), ay = float(r.y + a[1] * r.h);
                const float bx = float(r.x + b[0] * r.w), by = float(r.y + b[1] * r.h);
                const SDL_FColor ca = {173 / 255.f, 179 / 255.f, 182 / 255.f, alphaA};
                const SDL_FColor cb = {ca.r, ca.g, ca.b, alphaB};
                const int first = int(vertices.size());
                vertices.push_back({{ax + nx, ay + ny}, ca, {0, 0}});
                vertices.push_back({{ax - nx, ay - ny}, ca, {0, 0}});
                vertices.push_back({{bx + nx, by + ny}, cb, {0, 0}});
                vertices.push_back({{bx - nx, by - ny}, cb, {0, 0}});
                for (int i : {0, 1, 2, 2, 1, 3}) indices.push_back(first + i);
            }
            a = b; alphaA = alphaB;
        }
    }
}

}
struct MarcusStoryboardEditor
{
    MarcusPlayer *player = nullptr;
    MarcusStoryboard board;
    MarcusBoardAngle work, lightView;
    bool inspectLights = false, showGrid = true;
    bool movingLight = false, lightGizmoOver = false;
    int lightMode = 0;
    float lightMatrix[16] = {};
    SDL_FRect lightControls = {};
    std::vector<float> depth;
    std::vector<std::array<double, 4>> gridLines;
    std::vector<SDL_Vertex> gridVertices;
    std::vector<int> gridIndices;
    bool gridStyleDirty = true;
    float gridOpacity = -1;
    SDL_FRect view = {}, canvas = {}, tools = {}, bar = {};
    bool open = false, changed = false, painting = false, picking = false, looking = false, pending = false, previewDirty = true, hasWork = false;
    int angle = 0, light = 0, tab = 0, colour = 3, size = 8, previousX = -1, previousY = -1, dragField = -1;
    float dragStart = 0, dragValue = 0;
    unsigned long document = 0, scene = 0;
    int cameraKeys = 0;
    SDL_WindowID navigationWindow = 0;
    float flySpeed = 0;
    long frame = 0;
    std::string resources, content, error;
    std::vector<unsigned char> baseline, rgba;
    MarcusBoardPaintColours paintColours = MarcusBoardColours;
    MarcusBoardPalette scenePalette = {};
    bool inkReady = false;
    std::array<float, 3> okhsl = {};
    unsigned penRgb = MarcusBoardColours[3];
    unsigned inkRgb = 0;
    MarcusBoardInk selectedInk;
    SDL_Texture *texture = nullptr;
    ~MarcusStoryboardEditor() { StopLook(); dialog_retire_texture(texture); }

    unsigned ColourRGB() const { return penRgb; }
    void SelectRGB(unsigned rgb)
    {
        const auto found = std::find(board.colours.begin(), board.colours.end(), rgb);
        colour = found == board.colours.end() ? -1 : int(found - board.colours.begin());
        auto next = MarcusToOkHSL(rgb);
        if (next[1] < .00001f) next[0] = okhsl[0];
        okhsl = next; penRgb = rgb;
        inkReady = false; picking = false;
    }
    bool ResolveColour()
    {
        if (colour >= 0) return true;
        colour = MarcusBoardAddColour(board, ColourRGB());
        if (colour >= 0) return true;
        error = "All 256 paint colours are in use. Pick an existing colour."; return false;
    }

    const MarcusBoardAngle &DisplayAngle() const { return tab == 2 && inspectLights ? lightView : board.angles[angle]; }
    MarcusCamera &NavigationCamera() { return tab == 2 ? lightView.camera : board.angles[angle].camera; }
    void FrameLights()
    {
        lightView = board.angles[angle];
        std::array<float, 3> low = {lightView.camera.x, lightView.camera.y, lightView.camera.z}, high = low;
        auto include = [&](float x, float y, float z) {
            const float p[] = {x, y, z};
            for (int i = 0; i < 3; ++i) { low[i] = std::min(low[i], p[i]); high[i] = std::max(high[i], p[i]); }
        };
        for (const auto &l : board.lights) include(l.x, l.y, l.z);
        MarcusInfo info = {};
        if (marcus_info(player, &info)) for (int i = 0; i < info.object_count; ++i)
        {
            MarcusObject object = {}; MarcusBounds bounds = {};
            if (marcus_object(player, i, &object) && object.visible)
            {
                include(object.x, object.y, object.z);
                if (marcus_object_bounds(player, i, &bounds) && bounds.valid)
                    for (int corner = 0; corner < 8; ++corner) include(bounds.corners[corner * 3], bounds.corners[corner * 3 + 1], bounds.corners[corner * 3 + 2]);
            }
        }
        const float distance = std::max(20.f, std::hypot(std::hypot(high[0] - low[0], high[1] - low[1]), high[2] - low[2]) * 1.4f);
        lightView.camera.yaw = 35; lightView.camera.pitch = 25; lightView.roll = 0;
        lightView.fov = 60; lightView.nearClip = .1f; lightView.farClip = 32700;
        const auto projection = MarcusBoardProjection(lightView);
        float *coordinates[] = {&lightView.camera.x, &lightView.camera.y, &lightView.camera.z};
        for (int i = 0; i < 3; ++i) *coordinates[i] = std::clamp((low[i] + high[i]) * .5f + projection.transform[6 + i] * distance, -32700.f, 32700.f);
        inspectLights = previewDirty = true;
    }
    bool Gesture() const { return painting || looking || dragField >= 0 || movingLight; }
    void StopLightGizmo()
    {
        if (movingLight) marcus_gizmo_enable(MARCUS_GIZMO_LIGHT, 0);
        movingLight = lightGizmoOver = false; lightControls = {};
    }
    void StopLook()
    {
        if (navigationWindow)
            if (auto window = SDL_GetWindowFromID(navigationWindow)) SDL_SetWindowRelativeMouseMode(window, false);
        navigationWindow = 0; looking = false; cameraKeys = 0;
    }
    bool FlightSpeed()
    {
        if (flySpeed <= 0)
        {
            MarcusCamera matched = {};
            if (!marcus_match_camera(player, MarcusBoardWidth, MarcusBoardHeight, &matched))
            { error = marcus_error(player); return false; }
            flySpeed = matched.move_speed;
        }
        NavigationCamera().move_speed = flySpeed;
        return true;
    }
    void StartLook(SDL_WindowID id)
    {
        Finish();
        if (tab == 2 && !inspectLights) { lightView = board.angles[angle]; inspectLights = true; }
        auto window = SDL_GetWindowFromID(id);
        if (!window || !FlightSpeed()) return;
        if (!SDL_SetWindowRelativeMouseMode(window, true)) { error = SDL_GetError(); return; }
        navigationWindow = id; cameraKeys = marcus_fly_held_keys(); looking = true;
    }
    void Reload()
    {
        StopLook();
        StopLightGizmo();
        const auto selected = ColourRGB();
        MarcusInfo info = {}; MarcusScene current = {};
        if (!marcus_info(player, &info) || !marcus_scene(player, info.scene, &current) || !MarcusGetStoryboard(player, board))
        { open = false; return; }
        const bool sceneChanged = scene != current.chunk;
        if (sceneChanged) { flySpeed = 0; inspectLights = false; }
        document = marcus_document(player); scene = current.chunk; frame = info.frame;
        angle = std::min(int(info.camera), int(board.angles.size()) - 1);
        light = std::clamp(light, 0, std::max(0, int(board.lights.size()) - 1));
        baseline = MarcusEncodeBoard(board);
        MarcusGetStoryboardColours(player, paintColours);
        MarcusGetStoryboardPalette(player, scenePalette);
        SelectRGB(selected);
        pending = painting = picking = looking = false; dragField = -1; cameraKeys = 0;
        previewDirty = true;
        if (sceneChanged && tab == 2) FrameLights();
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
        StopLook(); StopLightGizmo(); painting = false; dragField = -1;
        Commit();
    }
    void SelectAngle(int index)
    {
        Finish();
        angle = std::clamp(index, 0, int(board.angles.size()) - 1);
        pending = previewDirty = true; Commit();
    }
    int HitLight(float px, float py) const
    {
        const auto projection = MarcusBoardProjection(DisplayAngle());
        const float nx = (px - canvas.x) / canvas.w, ny = (py - canvas.y) / canvas.h;
        float best = 14;
        int index = -1;
        // Prefer the selected marker when newly added lights share a position.
        for (int pass = 0; pass < int(board.lights.size()); ++pass)
        {
            const int i = pass == 0 ? light : pass <= light ? pass - 1 : pass;
            const auto widget = MarcusBoardBuildLightWidget(projection, board.lights[i], canvas.h);
            float distance = widget.visible ? std::hypot((nx - widget.centre[0]) * canvas.w, (ny - widget.centre[1]) * canvas.h) : 14;
            for (const auto *guide : {&widget.shape, &widget.direction}) for (int line = 0; line < guide->line_count; ++line)
            {
                const auto &p = guide->lines[line];
                const float edge = SegmentDistance(px, py, canvas.x + p.x1 * canvas.w, canvas.y + p.y1 * canvas.h,
                    canvas.x + p.x2 * canvas.w, canvas.y + p.y2 * canvas.h);
                if (edge < 7) distance = std::min(distance, edge);
            }
            if (distance < best) { best = distance; index = i; }
        }
        return index;
    }

    void AddAngle()
    {
        if (board.angles.size() >= 64) return;
        Finish();
        auto next = board.angles[angle]; std::fill(next.pixels.begin(), next.pixels.end(), 0);
        board.angles.push_back(std::move(next)); angle = int(board.angles.size()) - 1;
        pending = previewDirty = true; Commit();
    }
    std::vector<Field> Fields()
    {
        auto &a = board.angles[angle];
        if (tab == 1) return {{"Position X", &a.camera.x, -32700, 32700, .5f}, {"Position Y", &a.camera.y, -32700, 32700, .5f},
            {"Position Z", &a.camera.z, -32700, 32700, .5f}, {"Yaw", &a.camera.yaw, -360, 360, 1},
            {"Pitch", &a.camera.pitch, -90, 90, 1}, {"Roll", &a.roll, -180, 180, 1},
            {"FOV", &a.fov, 5, 150, 1}, {"Near clip", &a.nearClip, .01f, std::min(100.f, a.farClip - .01f), .1f},
            {"Far clip", &a.farClip, a.nearClip + .01f, 32700, 10}};
        if (tab != 2 || board.lights.empty()) return {};
        auto &l = board.lights[light];
        return {{"Position X", &l.x, -32700, 32700, .5f}, {"Position Y", &l.y, -32700, 32700, .5f},
            {"Position Z", &l.z, -32700, 32700, .5f}, {"Yaw", &l.yaw, -360, 360, 1},
            {"Pitch", &l.pitch, -89, 89, 1}, {"Intensity", &l.intensity, 0, 1, .05f}};
    }
    bool CanvasPoint(float x, float y, int &ix, int &iy) const
    {
        if (canvas.w <= 0 || canvas.h <= 0 || !Inside(x, y, canvas)) return false;
        ix = std::clamp(int((x - canvas.x) * MarcusBoardWidth / canvas.w), 0, MarcusBoardWidth - 1);
        iy = std::clamp(int((y - canvas.y) * MarcusBoardHeight / canvas.h), 0, MarcusBoardHeight - 1);
        return true;
    }
    void Pick(float x, float y)
    {
        int ix, iy;
        if (!CanvasPoint(x, y, ix, iy)) return;
        // Sample the logical ink, so dithering and foreground props cannot
        // change the selected drawing colour or create a movie undo entry.
        SelectRGB(board.colours[board.angles[angle].pixels[iy * MarcusBoardWidth + ix]]);
    }
    void Paint(float x, float y)
    {
        int ix, iy;
        if (!CanvasPoint(x, y, ix, iy)) { previousX = previousY = -1; return; }
        if (!ResolveColour()) return;
        MarcusBoardStroke(board.angles[angle], previousX < 0 ? ix : previousX, previousY < 0 ? iy : previousY, ix, iy, size, static_cast<unsigned char>(colour));
        previousX = ix; previousY = iy; pending = previewDirty = true;
    }
};

extern "C" {
MarcusStoryboardEditor *storyboard_editor_create(void) { try { return new MarcusStoryboardEditor; } catch (...) { return nullptr; } }
void storyboard_editor_destroy(MarcusStoryboardEditor *e) { delete e; }
int storyboard_editor_visible(const MarcusStoryboardEditor *e) { return e && e->open; }
int storyboard_editor_flying(const MarcusStoryboardEditor *e) { return e && e->looking; }
int storyboard_editor_capturing(const MarcusStoryboardEditor *e) { return e && (e->painting || e->looking || e->movingLight); }
void storyboard_editor_release_camera(MarcusStoryboardEditor *e) { if (e && e->looking) e->Finish(); }
void storyboard_editor_update(MarcusStoryboardEditor *e, double seconds)
{
    if (storyboard_editor_visible(e) && e->looking && marcus_fly_step(&e->NavigationCamera(), e->cameraKeys, seconds))
    { e->previewDirty = true; if (e->tab == 1) e->pending = true; }
}
void storyboard_editor_gizmo(MarcusStoryboardEditor *e, int enabled)
{
    const bool eligible = enabled && storyboard_editor_visible(e) && e->tab == 2 && !e->looking &&
        !e->painting && e->dragField < 0 && !e->board.lights.empty();
    if (!eligible)
    {
        if (e && e->movingLight) e->Reload();
        if (e) { e->lightGizmoOver = false; e->lightControls = {}; }
        marcus_gizmo_enable(MARCUS_GIZMO_LIGHT, 0);
        return;
    }
    auto &light = e->board.lights[e->light];
    if (light.point) e->lightMode = 0;
    e->lightControls = {e->canvas.x + 8, e->canvas.y + 8, light.point ? 64.f : 130.f, 28};
    e->lightGizmoOver = ui_transform_controls("Light tools", &e->lightControls, &e->lightMode, light.point ? 1 : 2,
                                              e->movingLight) != 0;
    if (!e->movingLight) MarcusBoardLightTransform(light, e->lightMatrix);
    const auto projection = MarcusBoardProjection(e->DisplayAngle());
    const auto result = marcus_gizmo_manipulate(MARCUS_GIZMO_LIGHT, &projection, &e->canvas, e->lightMode,
                                              e->lightMatrix, .1f, nullptr);
    e->lightGizmoOver = e->lightGizmoOver || result.over != 0;
    if (result.active || result.changed) e->movingLight = true;
    if (result.changed)
    {
        MarcusBoardLight next;
        if (MarcusBoardLightFromTransform(light, e->lightMatrix, e->lightMode == 1, next))
        {
            light = next; e->pending = e->previewDirty = true;
            if (!e->lightMode) { e->lightMatrix[12] = light.x; e->lightMatrix[13] = light.y; e->lightMatrix[14] = light.z; }
        }
    }
    if (e->lightGizmoOver && !e->movingLight)
        ui_tooltip(e->lightMode ? "Drag a ring to aim the light. Ctrl snaps to 15 degrees. Escape cancels."
            : "Drag an axis, plane or centre to move the light. Ctrl snaps to 1 unit. Escape cancels.");
    if (e->movingLight && !result.active) e->Finish();
}
void storyboard_editor_close(MarcusStoryboardEditor *e) { if (e && e->open) { e->Finish(); e->picking = e->open = false; } }
void storyboard_editor_layout(MarcusStoryboardEditor *e, const SDL_FRect *view, const SDL_FRect *canvas, const SDL_FRect *tools, const SDL_FRect *bar)
{
    if (e)
    {
        if (e->canvas.x != canvas->x || e->canvas.y != canvas->y || e->canvas.w != canvas->w || e->canvas.h != canvas->h) e->gridStyleDirty = true;
        e->view = *view; e->canvas = *canvas; e->tools = *tools; e->bar = *bar;
    }
}
void storyboard_editor_sync(MarcusStoryboardEditor *e, MarcusPlayer *player, const char *resources, const char *content)
{
    if (!storyboard_editor_visible(e)) return;
    if (e->player != player) { e->StopLook(); e->open = false; return; }
    e->resources = resources; e->content = content;
    MarcusInfo info = {}; MarcusScene current = {};
    if (!marcus_info(player, &info) || !marcus_scene(player, info.scene, &current) || !marcus_is_storyboard(player))
    { e->StopLook(); e->open = false; return; }
    if (current.chunk != e->scene || info.frame != e->frame || info.camera != static_cast<unsigned long>(e->angle) ||
        marcus_document(player) != e->document)
    {
        if (e->Gesture() && current.chunk == e->scene && info.frame == e->frame) { e->previewDirty = true; return; }
        e->Reload();
    }
}
int storyboard_editor_show(MarcusStoryboardEditor *e, MarcusPlayer *player, int newScene, int angle,
    const MarcusImage *work, float flySpeed, const char *resources, const char *content)
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
        e->flySpeed = flySpeed;
        e->changed = newScene != 0;
        e->hasWork = work && work->projection.valid && !work->projection.orthographic;
        if (e->hasWork)
        {
            e->work = MarcusBoardAngle{}; e->work.camera = work->camera;
            e->work.camera.yaw = std::remainder(e->work.camera.yaw, 360.f);
            e->work.camera.pitch = std::clamp(e->work.camera.pitch, -90.f, 90.f);
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
        e->inspectLights = false;
        e->picking = false;
        if (newScene) e->SelectRGB(MarcusBoardColours[3]);
        if (angle == -2) e->AddAngle();
        else if (angle >= 0) e->SelectAngle(angle);
        return 1;
    }
    catch (const std::exception &error) { e->error = error.what(); e->open = false; return 0; }
}
int storyboard_editor_event(MarcusStoryboardEditor *e, const SDL_Event *event)
{
    if (!storyboard_editor_visible(e)) return 0;
    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        const auto key = event->key.key;
        const int flyKey = marcus_fly_key(event->key.scancode);
        if (e->looking && flyKey) { e->cameraKeys |= flyKey; return 1; }
        if (key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LCTRL || key == SDLK_RCTRL ||
            key == SDLK_LALT || key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI) return e->Gesture();
        const bool command = event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT);
        if (key == SDLK_ESCAPE && !event->key.repeat)
        {
            if (e->picking) e->picking = false;
            else if (e->looking) e->Finish();
            else if (e->Gesture()) e->Reload(); else storyboard_editor_close(e);
            return 1;
        }
        if (key == SDLK_Z && command && e->Gesture()) { e->Reload(); return 1; }
        if (e->movingLight) return 1;
        if (command) { e->Finish(); return 0; }
        if (e->looking) return 0;
        if (!event->key.repeat)
        {
            if (key >= SDLK_1 && key <= SDLK_9) { e->SelectRGB(MarcusBoardColours[int(key - SDLK_1) + 1]); return 1; }
            if (key == SDLK_0) { e->SelectRGB(MarcusBoardColours[0]); return 1; }
            if (key == SDLK_I && e->tab == 0) { e->Finish(); e->picking = !e->picking; return 1; }
            if (key == SDLK_LEFTBRACKET) { e->size = std::max(1, e->size / 2); return 1; }
            if (key == SDLK_RIGHTBRACKET) { e->size = std::min(64, e->size * 2); return 1; }
        }
        e->Finish(); return 0;
    }
    if (event->type == SDL_EVENT_KEY_UP && e->looking && marcus_fly_key(event->key.scancode))
    {
        e->cameraKeys &= ~marcus_fly_key(event->key.scancode);
        return 1;
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) { if (e->movingLight) e->Reload(); else e->Finish(); return 0; }
    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (e->movingLight) return 1;
        if (e->painting) { e->Paint(event->motion.x, event->motion.y); return 1; }
        if (e->looking)
        {
            if (marcus_fly_look(&e->NavigationCamera(), event->motion.xrel, event->motion.yrel))
            { e->previewDirty = true; if (e->tab == 1) e->pending = true; }
            return 1;
        }

        return Inside(event->motion.x, event->motion.y, e->view) || Inside(event->motion.x, event->motion.y, e->tools);
    }
    // ImGuizmo samples the final pointer/release before the shared render frame commits.
    if (e->movingLight && (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL)) return 1;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ((e->looking && event->button.button == SDL_BUTTON_RIGHT) ||
         (e->painting && event->button.button == SDL_BUTTON_LEFT))) { e->Finish(); return 1; }
    if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        if (!e->looking && !Inside(event->wheel.mouse_x, event->wheel.mouse_y, e->canvas))
            return Inside(event->wheel.mouse_x, event->wheel.mouse_y, e->view) || Inside(event->wheel.mouse_x, event->wheel.mouse_y, e->tools);
        if ((e->tab == 1 || e->tab == 2) && event->wheel.y != 0 && e->FlightSpeed())
        {
            auto &camera = e->NavigationCamera();
            marcus_fly_speed(&camera, event->wheel.y * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1));
            e->flySpeed = camera.move_speed;
        }
        return 1;
    }
    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN) return 0;
    if (e->looking || e->movingLight) return 1;
    const float px = event->button.x, py = event->button.y;
    if (!Inside(px, py, e->view) && !Inside(px, py, e->tools)) { e->Finish(); return 0; }
    if (Inside(px, py, e->canvas))
    {
        if (event->button.button == SDL_BUTTON_LEFT && e->tab == 2 &&
            (e->lightGizmoOver || Inside(px, py, e->lightControls))) return 1;
        if (event->button.button == SDL_BUTTON_LEFT && e->tab == 0)
        {
            if (e->picking || (SDL_GetModState() & SDL_KMOD_ALT)) e->Pick(px, py);
            else { e->painting = true; e->previousX = e->previousY = -1; e->Paint(px, py); }
        }
        else if (event->button.button == SDL_BUTTON_LEFT && e->tab == 2)
        {
            e->Finish();
            const int index = e->HitLight(px, py);
            if (index >= 0) e->light = index;
        }
        else if (event->button.button == SDL_BUTTON_RIGHT && (e->tab == 1 || e->tab == 2)) e->StartLook(event->button.windowID);
        return 1;
    }
    return 1;
}
void storyboard_editor_prepare(MarcusStoryboardEditor *e, SDL_Renderer *renderer)
{
    if (!storyboard_editor_visible(e)) return;
    try {
        if (e->previewDirty)
        {
            MarcusImage image = {};
            if (MarcusRenderStoryboard(e->player, e->board, e->angle, image, &e->depth, e->tab == 2 && e->inspectLights ? &e->lightView : nullptr))
                e->rgba.assign(image.rgba, image.rgba + size_t(image.pitch) * image.height);
            else
            {
                e->depth.clear();
                e->rgba.resize(MarcusBoardWidth * MarcusBoardHeight * 4);
                const auto &pixels = e->board.angles[e->angle].pixels;
                for (size_t i = 0; i < pixels.size(); ++i)
                {
                    const auto rgb = pixels[i] < e->paintColours.size() ? e->paintColours[pixels[i]] : e->board.colours[pixels[i]];
                    e->rgba[i * 4] = static_cast<unsigned char>(rgb >> 16); e->rgba[i * 4 + 1] = static_cast<unsigned char>(rgb >> 8);
                    e->rgba[i * 4 + 2] = static_cast<unsigned char>(rgb); e->rgba[i * 4 + 3] = 255;
                }
            }
            e->gridLines = MarcusBoardVisibleGridLines(e->DisplayAngle(), e->depth, MarcusBoardWidth, MarcusBoardHeight);
            e->gridStyleDirty = true;
            if (!e->texture) e->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, MarcusBoardWidth, MarcusBoardHeight);
            if (e->texture) { SDL_SetTextureScaleMode(e->texture, SDL_SCALEMODE_NEAREST); SDL_UpdateTexture(e->texture, nullptr, e->rgba.data(), MarcusBoardWidth * 4); }
            e->previewDirty = false;
        }
        if (e->showGrid && e->gridStyleDirty) {
            BuildGrid(e->gridLines, e->canvas, e->DisplayAngle(), 1, e->gridVertices, e->gridIndices);
            e->gridStyleDirty = false;
        }
    } catch (const std::exception &error) { e->error = error.what(); }
}
void storyboard_editor_state(MarcusStoryboardEditor *e, MarcusBoardEditorState *out)
{
    *out = {}; if (!storyboard_editor_visible(e)) return;
    try {
        out->open = true; out->tab = e->tab; out->angle = e->angle; out->angles = int(e->board.angles.size());
        out->light = e->light; out->lights = int(e->board.lights.size()); out->grid = e->showGrid;
        out->point = out->lights && e->board.lights[e->light].point; out->has_work = e->hasWork;
        out->inspecting = e->inspectLights; out->picking = e->picking; out->brush = e->size;
        out->can_undo = marcus_can_undo(e->player); out->editing = e->dragField;
        out->can_bake = e->colour != 0 && !MarcusBoardGridLines(e->board.angles[e->angle]).empty();
        out->canvas = e->canvas; out->tools = e->tools; out->bar = e->bar; out->texture = e->texture;
        out->vertices = e->gridVertices.data(); out->indices = e->gridIndices.data(); out->index_count = int(e->gridIndices.size());
        out->rgb = e->ColourRGB();
        out->colour[0] = ((out->rgb >> 16) & 255) / 255.f; out->colour[1] = ((out->rgb >> 8) & 255) / 255.f; out->colour[2] = (out->rgb & 255) / 255.f;
        std::copy(e->okhsl.begin(), e->okhsl.end(), out->okhsl);
        for (int i = 0; i < 10; ++i) out->swatches[i] = e->paintColours[i];
        if (!e->inkReady || e->inkRgb != out->rgb) { e->selectedInk = MarcusBoardMatchColour(e->scenePalette, out->rgb); e->inkRgb = out->rgb; e->inkReady = true; }
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 16; ++x) out->dither[y * 16 + x] = e->scenePalette[MarcusBoardPaintIndex(e->selectedInk, x, y)];
        const auto fields = e->Fields(); out->field_count = int(fields.size());
        for (int i = 0; i < out->field_count; ++i) {
            auto &field = out->fields[i]; SDL_strlcpy(field.name, fields[i].name, sizeof(field.name));
            field.value = *fields[i].value; field.low = fields[i].low; field.high = fields[i].high; field.step = fields[i].step;
        }
        if (e->tab == 2) {
            const auto projection = MarcusBoardProjection(e->DisplayAngle());
            if (e->inspectLights) { const auto shot = MarcusBoardProjection(e->board.angles[e->angle]); camera_guide_build(&projection, &shot, &out->camera_guide); }
            for (int i = 0; i < out->lights; ++i) {
                const auto widget = MarcusBoardBuildLightWidget(projection, e->board.lights[i], e->canvas.h);
                auto &overlay = out->overlays[i]; overlay.shape = widget.shape; overlay.direction = widget.direction;
                overlay.selected = i == e->light; overlay.visible = widget.visible;
                overlay.x = e->canvas.x + widget.centre[0] * e->canvas.w; overlay.y = e->canvas.y + widget.centre[1] * e->canvas.h;
                SDL_snprintf(overlay.label, sizeof(overlay.label), "%s light %d", e->board.lights[i].point ? "Point" : "Directional", i + 1);
            }
        }
        SDL_strlcpy(out->error, e->error.c_str(), sizeof(out->error));
    } catch (const std::exception &error) { SDL_strlcpy(out->error, error.what(), sizeof(out->error)); }
}
void storyboard_editor_command(MarcusStoryboardEditor *e, int command, int index, float value, const float *colour)
{
    if (!storyboard_editor_visible(e)) return;
    try {
        switch (command) {
        case BOARD_DONE: storyboard_editor_close(e); break;
        case BOARD_TAB:
            if (index < 0 || index > 2) break;
            e->Finish(); if (index == 2 && e->tab != 2 && !e->inspectLights) e->FrameLights();
            e->tab = index; e->picking = false; e->previewDirty = true; break;
        case BOARD_ANGLE: e->SelectAngle(index); break;
        case BOARD_NEW_ANGLE: e->AddAngle(); break;
        case BOARD_USE_WORK:
            if (e->hasWork) { e->Finish(); auto pixels = std::move(e->board.angles[e->angle].pixels);
                e->board.angles[e->angle] = e->work; e->board.angles[e->angle].pixels = std::move(pixels); e->pending = true; e->Commit(); }
            break;
        case BOARD_GRID: e->showGrid = !e->showGrid; break;
        case BOARD_RGB:
            if (colour) { unsigned rgb = 0; for (int i = 0; i < 3; ++i) rgb = (rgb << 8) | unsigned(std::clamp(colour[i], 0.f, 1.f) * 255 + .5f); e->SelectRGB(rgb); } break;
        case BOARD_HSL: if (colour) e->SelectRGB(MarcusOkHSL(colour[0], colour[1], colour[2])); break;
        case BOARD_SWATCH: if (index >= 0 && index < int(MarcusBoardColourCount)) e->SelectRGB(MarcusBoardColours[index]); break;
        case BOARD_PICK: e->Finish(); e->picking = !e->picking; break;
        case BOARD_BRUSH: e->size = std::clamp(index, 1, 64); break;
        case BOARD_BAKE:
            e->Finish(); if (e->colour != 0 && e->ResolveColour()) { MarcusBoardBakeGrid(e->board.angles[e->angle], static_cast<unsigned char>(e->colour)); e->pending = true; e->Commit(); } break;
        case BOARD_CLEAR:
            e->Finish(); std::fill(e->board.angles[e->angle].pixels.begin(), e->board.angles[e->angle].pixels.end(), 0); e->pending = true; e->Commit(); break;
        case BOARD_UNDO:
            e->Finish(); if (marcus_undo(e->player)) { e->Reload(); e->changed = true; } else e->error = marcus_error(e->player); break;
        case BOARD_LIGHT:
            e->Finish(); e->light = std::clamp(index, 0, std::max(0, int(e->board.lights.size()) - 1)); break;
        case BOARD_LIGHT_ADD:
            e->Finish(); if (e->board.lights.size() < 16) { e->board.lights.push_back(MarcusBoardLight{}); e->light = int(e->board.lights.size()) - 1; e->pending = true; e->FrameLights(); e->Commit(); } break;
        case BOARD_LIGHT_REMOVE:
            e->Finish(); if (!e->board.lights.empty()) { e->board.lights.erase(e->board.lights.begin() + e->light); e->light = std::max(0, e->light - 1); e->pending = true; e->Commit(); } break;
        case BOARD_POINT:
            e->Finish(); if (!e->board.lights.empty()) { e->board.lights[e->light].point ^= 1; e->pending = true; e->Commit(); } break;
        case BOARD_FRAME_LIGHTS: e->Finish(); e->FrameLights(); break;
        case BOARD_SHOT_VIEW: e->Finish(); if (e->inspectLights) e->inspectLights = false; else e->FrameLights(); e->previewDirty = true; break;
        case BOARD_FIELD_BEGIN: e->Finish(); e->dragField = index; break;
        case BOARD_FIELD_SET: {
            auto fields = e->Fields(); if (index < 0 || index >= int(fields.size()) || !std::isfinite(value)) break;
            *fields[index].value = std::clamp(value, fields[index].low, fields[index].high); e->pending = e->previewDirty = true; break; }
        case BOARD_FIELD_END: e->Finish(); break;
        case BOARD_FIELD_CANCEL: e->Reload(); break;
        }
    } catch (const std::exception &error) { e->error = error.what(); }
}

int storyboard_editor_take(MarcusStoryboardEditor *e) { if (!e || !e->changed) return 0; e->changed = false; return 1; }
}
