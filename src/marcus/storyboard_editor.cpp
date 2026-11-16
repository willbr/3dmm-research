#include "storyboard_editor.h"
#include "storyboard.h"
#include "storyboard_tools.h"
#include "camera_controls.h"
#include "transform_gizmo.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr int sizes[] = {1, 4, 8, 16, 32};
constexpr const char *colourNames[] = {"Canvas grey", "Black", "White", "Soft blue", "Soft red", "Soft yellow", "Soft green", "Dark grey", "Grey", "Light grey"};
constexpr int swatches[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
static_assert(sizeof(colourNames) / sizeof(*colourNames) == MarcusBoardColourCount, "Paint labels match the palette");
struct Field { const char *name; float *value; float low, high, step; };
bool Inside(float x, float y, const SDL_FRect &r) { return ui_inside(x, y, r.x, r.y, r.w, r.h) != 0; }
void Guide(MarcusUI *ui, const MarcusCameraGuide &guide, const SDL_FRect &r, unsigned colour, bool outlined = false)
{
    if (outlined)
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
        {
            if (!dx && !dy) continue;
            auto shifted = r; shifted.x += dx; shifted.y += dy;
            Guide(ui, guide, shifted, 0x15191f);
        }
    for (int i = 0; i < guide.line_count; ++i)
    {
        const auto &line = guide.lines[i];
        ui_line(ui, r.x + line.x1 * r.w, r.y + line.y1 * r.h, r.x + line.x2 * r.w, r.y + line.y2 * r.h, colour);
    }
}
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
void Grid(MarcusUI *ui, const std::vector<SDL_Vertex> &vertices, const std::vector<int> &indices,
    const SDL_FRect &r, bool lightTools)
{
    const bool clipped = SDL_RenderClipEnabled(ui->renderer);
    SDL_Rect previous; SDL_GetRenderClipRect(ui->renderer, &previous);
    SDL_Rect clip = {int(r.x), int(r.y), int(r.w), int(r.h)}; SDL_SetRenderClipRect(ui->renderer, &clip);
    SDL_BlendMode blend; SDL_GetRenderDrawBlendMode(ui->renderer, &blend);
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    if (!indices.empty()) SDL_RenderGeometry(ui->renderer, nullptr, vertices.data(), int(vertices.size()), indices.data(), int(indices.size()));
    SDL_SetRenderDrawBlendMode(ui->renderer, blend);
    ui_line(ui, r.x + r.w / 2 - 6, r.y + r.h / 2, r.x + r.w / 2 + 6, r.y + r.h / 2, 0x6b7f84);
    ui_line(ui, r.x + r.w / 2, r.y + r.h / 2 - 6, r.x + r.w / 2, r.y + r.h / 2 + 6, 0x6b7f84);
    ui_text(ui, r.x + 12, r.y + (lightTools ? 44 : 12), "Perspective guide / Bake grid to paint it", 0x5b6570);
    SDL_SetRenderClipRect(ui->renderer, clipped ? &previous : nullptr);
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
    bool extended = false, hsvDirty = true, inkReady = false;
    std::array<float, 3> hsv = {210, .3f, .85f};
    int hsvDrag = 0;
    unsigned inkRgb = 0;
    MarcusBoardInk selectedInk;
    SDL_Texture *hsvTexture = nullptr;
    SDL_Texture *texture = nullptr;
    ~MarcusStoryboardEditor() { StopLook(); SDL_DestroyTexture(texture); SDL_DestroyTexture(hsvTexture); }
    float PaintOffset() const { return extended ? std::clamp(tools.h - 628, 48.f, 128.f) : 0.f; }
    SDL_FRect ColourCell(int c) const
    {
        return {tools.x + 16 + (c % (extended ? 5 : 3)) * (extended ? 54.f : 90.f),
            tools.y + 104 + (c / (extended ? 5 : 3)) * (extended ? 34.f : 48.f), extended ? 48.f : 84.f, extended ? 28.f : 40.f};
    }
    SDL_FRect ExtendedButton() const
    { return {tools.x + 16 + (extended ? 0 : 90.f), tools.y + (extended ? 176.f : 248.f), extended ? 264.f : 174.f, extended ? 28.f : 40.f}; }
    SDL_FRect SaturationValue() const { return {tools.x + 16, tools.y + 240, 200, PaintOffset() + 4}; }
    SDL_FRect HueStrip() const { return {tools.x + 232, tools.y + 240, 24, PaintOffset() + 4}; }
    unsigned ColourRGB() const
    { return colour >= 0 && colour < int(board.colours.size()) ? board.colours[colour] : MarcusBoardHSV(hsv[0], hsv[1], hsv[2]); }
    void SelectRGB(unsigned rgb)
    {
        const auto found = std::find(board.colours.begin(), board.colours.end(), rgb);
        colour = found == board.colours.end() ? -1 : int(found - board.colours.begin());
        hsv = MarcusBoardToHSV(rgb); hsvDirty = true; inkReady = false; picking = false;
    }
    bool ResolveColour()
    {
        if (colour >= 0) return true;
        colour = MarcusBoardAddColour(board, ColourRGB());
        if (colour >= 0) return true;
        error = "All 256 paint colours are in use. Pick an existing colour."; return false;
    }
    void MoveHSV(float px, float py)
    {
        if (hsvDrag == 1)
        {
            const auto r = SaturationValue();
            hsv[1] = std::clamp((px - r.x) / (r.w - 1), 0.f, 1.f);
            hsv[2] = 1 - std::clamp((py - r.y) / (r.h - 1), 0.f, 1.f);
        }
        else if (hsvDrag == 2)
        {
            const auto r = HueStrip(); hsv[0] = 360 * std::clamp((py - r.y) / (r.h - 1), 0.f, 1.f); hsvDirty = true;
        }
        const auto rgb = MarcusBoardHSV(hsv[0], hsv[1], hsv[2]);
        const auto found = std::find(board.colours.begin(), board.colours.end(), rgb);
        colour = found == board.colours.end() ? -1 : int(found - board.colours.begin());
        inkReady = false; picking = false;
    }
    void DrawHSV(MarcusUI *ui)
    {
        const auto sv = SaturationValue(), hue = HueStrip();
        if (!hsvTexture) hsvTexture = SDL_CreateTexture(ui->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 200, 132);
        if (hsvTexture && hsvDirty)
        {
            std::vector<unsigned char> pixels(200 * 132 * 4);
            for (int py = 0; py < 132; ++py) for (int px = 0; px < 200; ++px)
            {
                const auto rgb = MarcusBoardHSV(hsv[0], px / 199.f, 1 - py / 131.f);
                auto *p = pixels.data() + (py * 200 + px) * 4;
                p[0] = static_cast<unsigned char>(rgb >> 16); p[1] = static_cast<unsigned char>(rgb >> 8);
                p[2] = static_cast<unsigned char>(rgb); p[3] = 255;
            }
            SDL_UpdateTexture(hsvTexture, nullptr, pixels.data(), 200 * 4); hsvDirty = false;
        }
        for (int i = 0; i < 16; ++i)
            ui_fill(ui, tools.x + 16 + i * 16, tools.y + 212, 14, 18, MarcusBoardHSV(i * 22.5f, .65f, .9f));
        if (hsvTexture) SDL_RenderTexture(ui->renderer, hsvTexture, nullptr, &sv);
        for (int py = 0; py < int(hue.h); ++py) ui_fill(ui, hue.x, hue.y + py, hue.w, 1, MarcusBoardHSV(py * 360.f / (hue.h - 1), 1, 1));
        const float cx = sv.x + hsv[1] * (sv.w - 1), cy = sv.y + (1 - hsv[2]) * (sv.h - 1);
        ui_fill(ui, cx - 4, cy - 1, 9, 3, 0); ui_fill(ui, cx - 1, cy - 4, 3, 9, 0);
        ui_line(ui, cx - 3, cy, cx + 3, cy, 0xffffff); ui_line(ui, cx, cy - 3, cx, cy + 3, 0xffffff);
        const float hy = hue.y + hsv[0] / 360 * (hue.h - 1);
        ui_fill(ui, hue.x - 2, hy - 2, hue.w + 4, 5, 0); ui_fill(ui, hue.x, hy - 1, hue.w, 3, 0xffffff);
        const auto rgb = ColourRGB();
        if (!inkReady || inkRgb != rgb) { selectedInk = MarcusBoardMatchColour(scenePalette, rgb); inkRgb = rgb; inkReady = true; }
        const float previewY = tools.y + 256 + PaintOffset();
        ui_fill(ui, sv.x, previewY, 64, 24, rgb);
        for (int py = 0; py < 6; ++py) for (int px = 0; px < 48; ++px)
            ui_fill(ui, sv.x + 72 + px * 4, previewY + py * 4, 4, 4, scenePalette[MarcusBoardPaintIndex(selectedInk, px, py)]);
        char text[96]; SDL_snprintf(text, sizeof(text), "H %.0f / S %.0f%% / V %.0f%% / #%06X", hsv[0], hsv[1] * 100, hsv[2] * 100, rgb);
        ui_label(ui, sv.x, tools.y + 286 + PaintOffset(), 264, text, 0xdde0e5);
        if (Inside(ui->mouse_x, ui->mouse_y, sv)) ui->hint = "Drag: saturation left/right, value up/down";
        else if (Inside(ui->mouse_x, ui->mouse_y, hue)) ui->hint = "Drag to change hue";
        else if (ui_inside(ui->mouse_x, ui->mouse_y, sv.x, previewY, 264, 24))
            ui->hint = "Left: chosen colour / Right: the dithered paint used in the background";
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
        if (movingLight) MarcusGizmo::Enable(MarcusGizmo::Light, false);
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
        SelectRGB(selected); hsvDrag = 0;
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
        StopLook(); StopLightGizmo(); painting = false; dragField = -1; hsvDrag = 0;
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
    void DrawLights(MarcusUI *ui)
    {
        const auto projection = MarcusBoardProjection(DisplayAngle());
        SDL_Rect previous; SDL_GetRenderClipRect(ui->renderer, &previous);
        const bool clipped = SDL_RenderClipEnabled(ui->renderer);
        SDL_Rect clip = {int(canvas.x), int(canvas.y), int(canvas.w), int(canvas.h)};
        SDL_SetRenderClipRect(ui->renderer, &clip);
        MarcusCameraGuide camera = {};
        if (inspectLights)
        {
            const auto shot = MarcusBoardProjection(board.angles[angle]);
            camera_guide_build(&projection, &shot, &camera); Guide(ui, camera, canvas, 0x78b7c4);
        }
        // Keep the selected light and its label above nearby markers.
        for (int pass = 0; pass < 2; ++pass) for (int i = 0; i < int(board.lights.size()); ++i)
        {
            const bool selected = i == light;
            if (selected != (pass == 1)) continue;
            const auto widget = MarcusBoardBuildLightWidget(projection, board.lights[i], canvas.h);
            const unsigned colour = selected ? 0xffd77a : 0xc4a76d;
            Guide(ui, widget.shape, canvas, colour, true); Guide(ui, widget.direction, canvas, colour, true);
            if (widget.visible)
            {
                const auto &p = widget.centre;
                const float x = canvas.x + p[0] * canvas.w, y = canvas.y + p[1] * canvas.h;
                if (!selected) { ui_fill(ui, x - 3, y - 3, 7, 7, 0x15191f); ui_fill(ui, x - 2, y - 2, 5, 5, colour); }
                char label[64]; SDL_snprintf(label, sizeof(label), "%s light %d", board.lights[i].point ? "Point" : "Directional", i + 1);
                float width = 12;
                for (const unsigned char *p = reinterpret_cast<const unsigned char *>(label); *p; ++p)
                    width += ui->font ? ui->glyphs[*p - 32].advance : 8;
                const float left = std::clamp(x + 12, canvas.x + 4, std::max(canvas.x + 4, canvas.x + canvas.w - width - 4));
                const float top = std::clamp(y - 32, canvas.y + 40, std::max(canvas.y + 40, canvas.y + canvas.h - 26));
                ui_fill(ui, left, top, width, 22, selected ? colour : 0x707984);
                ui_fill(ui, left + 1, top + 1, width - 2, 20, 0x20252c);
                ui_text(ui, left + 6, top + 3, label, selected ? 0xffe09a : 0xe4c27d);
            }
        }
        SDL_SetRenderClipRect(ui->renderer, clipped ? &previous : nullptr);
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
void storyboard_editor_release_camera(MarcusStoryboardEditor *e) { if (e && e->looking) e->Finish(); }
void storyboard_editor_update(MarcusStoryboardEditor *e, double seconds)
{
    if (storyboard_editor_visible(e) && e->looking && marcus_fly_step(&e->NavigationCamera(), e->cameraKeys, seconds))
    { e->previewDirty = true; if (e->tab == 1) e->pending = true; }
}
void storyboard_editor_gizmo(MarcusStoryboardEditor *e, int enabled)
{
    const bool eligible = enabled && storyboard_editor_visible(e) && e->tab == 2 && !e->looking &&
        !e->painting && e->dragField < 0 && !e->hsvDrag && !e->board.lights.empty();
    if (!eligible)
    {
        if (e && e->movingLight) e->Reload();
        if (e) { e->lightGizmoOver = false; e->lightControls = {}; }
        MarcusGizmo::Enable(MarcusGizmo::Light, false);
        return;
    }
    auto &light = e->board.lights[e->light];
    if (light.point) e->lightMode = 0;
    e->lightControls = {e->canvas.x + 8, e->canvas.y + 8, light.point ? 64.f : 130.f, 28};
    e->lightGizmoOver = MarcusGizmo::Controls("Light tools", e->lightControls, e->lightMode, light.point ? 1 : 2, e->movingLight);
    if (!e->movingLight) MarcusBoardLightTransform(light, e->lightMatrix);
    const auto result = MarcusGizmo::Manipulate(MarcusGizmo::Light, MarcusBoardProjection(e->DisplayAngle()), e->canvas, e->lightMode, e->lightMatrix);
    e->lightGizmoOver |= result.over;
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
        ImGui::SetTooltip(e->lightMode ? "Drag a ring to aim the light. Ctrl snaps to 15 degrees. Escape cancels."
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
    const float x = e->tools.x + 16, y = e->tools.y, paintOffset = e->PaintOffset();
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
        if (e->hsvDrag) { e->MoveHSV(event->motion.x, event->motion.y); return 1; }
        if (e->movingLight) return 1;
        if (e->painting) { e->Paint(event->motion.x, event->motion.y); return 1; }
        if (e->looking)
        {
            if (marcus_fly_look(&e->NavigationCamera(), event->motion.xrel, event->motion.yrel))
            { e->previewDirty = true; if (e->tab == 1) e->pending = true; }
            return 1;
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
    // ImGuizmo samples the final pointer/release before the shared render frame commits.
    if (e->movingLight && (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL)) return 1;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ((e->looking && event->button.button == SDL_BUTTON_RIGHT) ||
         ((e->painting || e->dragField >= 0 || e->hsvDrag) && event->button.button == SDL_BUTTON_LEFT))) { e->Finish(); return 1; }
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
    if (event->button.button != SDL_BUTTON_LEFT) return 1;
    e->Finish();
    const float right = e->bar.x + e->bar.w;
    if (ui_inside(px, py, e->tools.x + e->tools.w - 80, y + 12, 64, 28)) { storyboard_editor_close(e); return 1; }
    if (ui_inside(px, py, e->bar.x + 140, e->bar.y, 32, 28)) { e->SelectAngle(e->angle - 1); return 1; }
    if (ui_inside(px, py, e->bar.x + 180, e->bar.y, 32, 28)) { e->SelectAngle(e->angle + 1); return 1; }
    if (ui_inside(px, py, right - 354, e->bar.y, 96, 28)) { e->showGrid = !e->showGrid; return 1; }
    if (ui_inside(px, py, right - 250, e->bar.y, 112, 28)) { e->AddAngle(); return 1; }
    if (ui_inside(px, py, right - 130, e->bar.y, 130, 28) && e->hasWork)
    {
        auto pixels = std::move(e->board.angles[e->angle].pixels);
        e->board.angles[e->angle] = e->work; e->board.angles[e->angle].pixels = std::move(pixels);
        e->pending = true; e->Commit(); return 1;
    }
    for (int t = 0; t < 3; ++t)
        if (ui_inside(px, py, x + t * 90, y + 52, 84, 30))
        {
            if (t == 2 && e->tab != 2 && !e->inspectLights) e->FrameLights();
            e->tab = t; e->picking = false; e->previewDirty = true; return 1;
        }
    if (e->tab == 0)
    {
        for (int c = 0; c < int(MarcusBoardColourCount); ++c)
            if (Inside(px, py, e->ColourCell(c))) { e->SelectRGB(MarcusBoardColours[swatches[c]]); return 1; }
        if (Inside(px, py, e->ExtendedButton())) { e->extended = !e->extended; return 1; }
        if (e->extended)
        {
            for (int i = 0; i < 16; ++i)
                if (ui_inside(px, py, x + i * 16, y + 212, 14, 18)) { e->SelectRGB(MarcusBoardHSV(i * 22.5f, .65f, .9f)); return 1; }
            if (Inside(px, py, e->SaturationValue())) { e->hsvDrag = 1; e->MoveHSV(px, py); return 1; }
            if (Inside(px, py, e->HueStrip())) { e->hsvDrag = 2; e->MoveHSV(px, py); return 1; }
        }
        for (int s = 0; s < 5; ++s)
            if (ui_inside(px, py, x + s * 54, y + 332 + paintOffset, 48, 30)) e->size = sizes[s];
        if (ui_inside(px, py, x, y + 380 + paintOffset, 264, 30)) { e->picking = !e->picking; return 1; }
        if (e->colour != 0 && ui_inside(px, py, x, y + 424 + paintOffset, 264, 30) && !MarcusBoardGridLines(e->board.angles[e->angle]).empty() && e->ResolveColour())
        { MarcusBoardBakeGrid(e->board.angles[e->angle], static_cast<unsigned char>(e->colour)); e->pending = true; e->Commit(); }
        if (ui_inside(px, py, x, y + 468 + paintOffset, 126, 30))
        { std::fill(e->board.angles[e->angle].pixels.begin(), e->board.angles[e->angle].pixels.end(), 0); e->pending = true; e->Commit(); }
        if (ui_inside(px, py, x + 138, y + 468 + paintOffset, 126, 30) && marcus_can_undo(e->player))
        { if (marcus_undo(e->player)) { e->Reload(); e->changed = true; } else e->error = marcus_error(e->player); }
    }
    else
    {
        if (e->tab == 2)
        {
            if (ui_inside(px, py, x, y + 394, 126, 30)) e->FrameLights();
            if (ui_inside(px, py, x + 138, y + 394, 126, 30))
            { if (e->inspectLights) e->inspectLights = false; else e->FrameLights(); e->previewDirty = true; }
            if (ui_inside(px, py, x, y + 104, 40, 28)) e->light = std::max(0, e->light - 1);
            if (ui_inside(px, py, x + 48, y + 104, 40, 28)) e->light = std::max(0, std::min(int(e->board.lights.size()) - 1, e->light + 1));
            if (ui_inside(px, py, x + 96, y + 104, 76, 28) && e->board.lights.size() < 16)
            { e->board.lights.push_back(MarcusBoardLight{}); e->light = int(e->board.lights.size()) - 1; e->pending = true; e->FrameLights(); }
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
        if (!e->texture) e->texture = SDL_CreateTexture(ui->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, MarcusBoardWidth, MarcusBoardHeight);
        if (e->texture) { SDL_SetTextureScaleMode(e->texture, SDL_SCALEMODE_NEAREST); SDL_UpdateTexture(e->texture, nullptr, e->rgba.data(), MarcusBoardWidth * 4); }
        e->previewDirty = false;
    }
    const float x = e->tools.x + 16, y = e->tools.y, right = e->bar.x + e->bar.w, paintOffset = e->PaintOffset();
    ui_fill(ui, e->tools.x, y, e->tools.w, e->tools.h, 0x25282d);
    ui_heading(ui, x, y + 20, "STORYBOARD TOOLS", 0x78cbc4);
    ui_button_sized(ui, e->tools.x + e->tools.w - 80, y + 12, 64, 28, "Done", 0, 1);
    for (int t = 0; t < 3; ++t) ui_button_sized(ui, x + t * 90, y + 52, 84, 30, t == 0 ? "Paint" : t == 1 ? "Camera" : "Lights", e->tab == t, 1);
    ui_heading(ui, e->bar.x, e->bar.y + 8, "STORYBOARD", 0x78cbc4);
    char label[192];
    ui_button_sized(ui, e->bar.x + 140, e->bar.y, 32, 28, "<", 0, e->angle > 0);
    ui_button_sized(ui, e->bar.x + 180, e->bar.y, 32, 28, ">", 0, e->angle + 1 < int(e->board.angles.size()));
    SDL_snprintf(label, sizeof(label), "Angle %d of %d", e->angle + 1, int(e->board.angles.size()));
    ui_label(ui, e->bar.x + 226, e->bar.y + 8, std::max(0.f, e->bar.w - 588), label, 0xdde0e5);
    ui_button_sized(ui, right - 354, e->bar.y, 96, 28, e->showGrid ? "Grid: on" : "Grid: off", e->showGrid, 1);
    ui_button_sized(ui, right - 250, e->bar.y, 112, 28, "New angle", 0, e->board.angles.size() < 64);
    ui_button_sized(ui, right - 130, e->bar.y, 130, 28, "Use work camera", 0, e->hasWork);
    ui_fill(ui, e->canvas.x - 1, e->canvas.y - 1, e->canvas.w + 2, e->canvas.h + 2, 0x5b6570);
    if (e->texture) SDL_RenderTexture(ui->renderer, e->texture, nullptr, &e->canvas);
    if (e->showGrid)
    {
        if (e->gridStyleDirty || e->gridOpacity != ui->opacity)
        {
            BuildGrid(e->gridLines, e->canvas, e->DisplayAngle(), ui->opacity, e->gridVertices, e->gridIndices);
            e->gridStyleDirty = false; e->gridOpacity = ui->opacity;
        }
        Grid(ui, e->gridVertices, e->gridIndices, e->canvas, e->tab == 2);
    }
    if (e->tab == 2) e->DrawLights(ui);
    if (e->tab == 0)
    {
        for (int c = 0; c < int(MarcusBoardColourCount); ++c)
        {
            const int ink = swatches[c];
            const auto r = e->ColourCell(c);
            ui_fill(ui, r.x - 2, r.y - 2, r.w + 4, r.h + 4, e->colour == ink ? 0x78cbc4 : 0x424952);
            ui_fill(ui, r.x, r.y, r.w, e->extended ? r.h : 24, e->paintColours[ink]);
            if (!e->extended) ui_label(ui, r.x + 4, r.y + 27, r.w - 8, colourNames[ink], 0xdde0e5);
            if (Inside(ui->mouse_x, ui->mouse_y, r)) ui->hint = colourNames[ink];
        }
        const auto more = e->ExtendedButton();
        ui_button_sized(ui, more.x, more.y, more.w, more.h, e->extended ? "Hide extended palette" : "Extended palette", e->extended || e->colour < 0 || e->colour >= int(MarcusBoardColourCount), 1);
        if (e->extended) e->DrawHSV(ui);
        SDL_snprintf(label, sizeof(label), "Crunchy brush / %d px", e->size);
        ui_heading(ui, x, y + 308 + paintOffset, label, 0x969da8);
        for (int s = 0; s < 5; ++s)
        { SDL_snprintf(label, sizeof(label), "%d", sizes[s]); ui_button_sized(ui, x + s * 54, y + 332 + paintOffset, 48, 30, label, e->size == sizes[s], 1); }
        ui_button_sized(ui, x, y + 380 + paintOffset, 264, 30, "Pick colour (I)", e->picking, 1);
        const bool enabled = e->colour != 0 && !MarcusBoardGridLines(e->board.angles[e->angle]).empty();
        SDL_snprintf(label, sizeof(label), "Bake grid (%s)", e->colour >= 0 && e->colour < int(MarcusBoardColourCount) ? colourNames[e->colour] : "Custom");
        ui_button_sized(ui, x, y + 424 + paintOffset, 264, 30, label, 0, enabled);
        if (ui_inside(ui->mouse_x, ui->mouse_y, x, y + 424 + paintOffset, 264, 30))
            ui->hint = "Bake this angle's grid in the selected colour (1-9). Undo removes it; no depth is added.";
        ui_button_sized(ui, x, y + 468 + paintOffset, 126, 30, "Clear angle", 0, 1);
        ui_button_sized(ui, x + 138, y + 468 + paintOffset, 126, 30, "Undo", 0, marcus_can_undo(e->player));
        ui_text(ui, x, y + 520 + paintOffset, "1-9 colours / 0 canvas grey", 0x969da8);
        ui_text(ui, x, y + 544 + paintOffset, "[ ] size / Alt+click to pick", 0x969da8);
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
            ui_button_sized(ui, x, y + 394, 126, 30, "Frame lights", 0, 1);
            ui_button_sized(ui, x + 138, y + 394, 126, 30, e->inspectLights ? "Shot view" : "3D light view", 0, 1);
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
        ui_text(ui, x, y + 440, e->tab == 1 ? "Drag values / Shift for fine" : "Move / Rotate / Ctrl to snap", 0x969da8);
        ui_text(ui, x, y + 464, e->tab == 1 ? "Hold RMB / WASD + Q E / Shift" : "RMB + WASD to inspect", 0x969da8);
        if (e->tab == 2) ui_text(ui, x, y + 488, "Escape cancels / Ctrl+Z undo", 0x969da8);
        if (e->tab == 1)
        {
            SDL_snprintf(label, sizeof(label), "Wheel: flight speed%s", e->flySpeed > 0 ? "" : " / RMB to start");
            if (e->flySpeed > 0) SDL_snprintf(label, sizeof(label), "Wheel: flight speed / %.2f units/s", e->flySpeed);
            ui_label(ui, x, y + 488, 264, label, 0x969da8);
        }
    }
    const float footer = y + (e->tab == 0 ? 588 + paintOffset : 512);
    ui_text(ui, x, footer, "Edits join the movie as you work.", 0x78cbc4);
    ui_text(ui, x, footer + 24, "Save copy / Ctrl+S to keep them.", 0x969da8);
    if (!e->error.empty()) ui_label(ui, x, footer + 56, 264, e->error.c_str(), 0xe4a1a1);
    if (e->looking || Inside(ui->mouse_x, ui->mouse_y, e->canvas))
        ui->hint = e->tab == 0 ? (e->picking ? "Click the background to pick its paint colour / Escape cancels the picker"
            : "Paint / 1-9 colours / 0 canvas grey / I or Alt+click to pick / [ ] size / Ctrl+Z undo")
            : e->tab == 1 ? "Hold right mouse: look + WASD move / Q E down/up / Shift faster / Wheel speed / Escape releases"
            : "Select a light / Move: axes, planes or centre / Rotate: aim with rings / Ctrl snaps / RMB + WASD to inspect";
}
int storyboard_editor_take(MarcusStoryboardEditor *e) { if (!e || !e->changed) return 0; e->changed = false; return 1; }
}
