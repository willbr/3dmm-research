#include "storyboard_editor_backend.h"
#include "storyboard.h"
#include "storyboard_draft.h"
#include "storyboard_tools.h"
#include "colour_picker.h"
#include <algorithm>
#include <cmath>

struct MarcusBoardEditorBackend
{
    MarcusBoardDraft paint{};
    std::vector<unsigned char> fallback;
    MarcusBoardPaintColours paintColours = MarcusBoardColours;
    MarcusBoardPalette palette = {};
    std::vector<float> depth;
    std::vector<std::array<double, 4>> gridLines;
    std::vector<SDL_Vertex> gridVertices;
    std::vector<int> gridIndices;
    ~MarcusBoardEditorBackend() { marcus_board_draft_dispose(&paint); }
};

namespace
{
int Error(const char *message)
{
    SDL_SetError("%s", message);
    return 0;
}

MarcusBoardAngle AngleFor(const MarcusBoardPose &pose)
{
    return {pose.camera, pose.roll, pose.fov, pose.near_clip, pose.far_clip, {}};
}

}

extern "C"
{
MarcusBoardEditorBackend *marcus_board_editor_backend_create(void)
try { return new MarcusBoardEditorBackend; }
catch (const std::exception &e) { Error(e.what()); return nullptr; }

void marcus_board_editor_backend_destroy(MarcusBoardEditorBackend *draft) { delete draft; }

int marcus_board_editor_new_scene(MarcusPlayer *player, const char *resources, const char *content)
try
{
    if (!MarcusApplyStoryboard(player, MarcusStoryboard{}, true, 0, resources, content)) return Error(marcus_error(player));
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

int marcus_board_editor_load(MarcusBoardEditorBackend *draft, MarcusPlayer *player, MarcusBoardEditValues *values,
                            unsigned swatches[MARCUS_BOARD_SWATCH_COUNT])
try
{
    MarcusStoryboard board;
    MarcusBoardWire wire{};
    if (!MarcusGetStoryboard(player, board)) return Error("Cannot load the selected storyboard.");
    auto colours = MarcusBoardColours;
    MarcusBoardPalette palette{};
    MarcusGetStoryboardColours(player, colours);
    MarcusGetStoryboardPalette(player, palette);
    if (!MarcusBoardView(board, wire) || !marcus_board_draft_load(&draft->paint, &wire))
        return Error("Cannot copy the selected storyboard.");
    draft->palette = palette;
    draft->paintColours = std::move(colours);
    *values = wire.values;
    std::copy_n(draft->paintColours.begin(), MARCUS_BOARD_SWATCH_COUNT, swatches);
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

unsigned char *marcus_board_editor_pixels(MarcusBoardEditorBackend *draft, int angle)
{
    return marcus_board_draft_pixels(&draft->paint, angle);
}

int marcus_board_editor_append_surface(MarcusBoardEditorBackend *draft)
{
    return marcus_board_draft_append(&draft->paint) ? 1 : Error("Cannot add a storyboard paint surface.");
}

void marcus_board_editor_default_light(MarcusBoardLightValues *light)
{
    *light = MarcusBoardLightValuesOf(MarcusBoardLight{});
}

int marcus_board_editor_stage(MarcusBoardEditorBackend *draft, const MarcusBoardEditValues *values, int *changed)
{
    return marcus_board_draft_stage(&draft->paint, values, changed) ? 1 : Error("Cannot stage the storyboard settings.");
}

int marcus_board_editor_apply(MarcusBoardEditorBackend *draft, MarcusPlayer *player, int angle,
                             const char *resources, const char *content)
try
{
    MarcusBoardWire wire;
    if (!marcus_board_draft_staged(&draft->paint, &wire)) return Error("No valid storyboard settings were staged.");
    if (!MarcusApplyStoryboard(player, MarcusBoardFromWire(wire), false, angle, resources, content))
        return Error(marcus_error(player));
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

void marcus_board_editor_accept(MarcusBoardEditorBackend *draft) { marcus_board_draft_accept(&draft->paint); }

int marcus_board_editor_bake(MarcusBoardEditorBackend *draft, int angle, const MarcusBoardPose *pose, unsigned char colour)
try
{
    auto pixels = marcus_board_draft_pixels(&draft->paint, angle);
    if (!pixels) return Error("Cannot find the storyboard paint surface.");
    MarcusBoardBakeGrid(AngleFor(*pose), pixels, colour);
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

int marcus_board_editor_can_bake(const MarcusBoardPose *pose)
try { return !MarcusBoardGridLines(AngleFor(*pose)).empty(); }
catch (const std::exception &e) { return Error(e.what()); }

unsigned marcus_board_editor_okhsl(const float colour[3]) { return MarcusOkHSL(colour[0], colour[1], colour[2]); }
void marcus_board_editor_to_okhsl(unsigned rgb, float colour[3])
{
    const auto hsl = MarcusToOkHSL(rgb);
    std::copy(hsl.begin(), hsl.end(), colour);
}

void marcus_board_editor_dither(const MarcusBoardEditorBackend *draft, unsigned rgb, unsigned long pixels[64])
{
    const auto ink = MarcusBoardMatchColour(draft->palette, rgb);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 16; ++x)
            pixels[y * 16 + x] = draft->palette[MarcusBoardPaintIndex(ink, x, y)];
}

void marcus_board_editor_light_widget(const MarcusProjection *projection, const MarcusBoardLightValues *light, float height,
                                      MarcusCameraGuide *shape, MarcusCameraGuide *direction, float centre[3], int *visible)
{
    const auto widget = MarcusBoardBuildLightWidget(*projection, MarcusBoardLightFromValues(*light), height);
    *shape = widget.shape;
    *direction = widget.direction;
    *visible = widget.visible;
    std::copy(widget.centre.begin(), widget.centre.end(), centre);
}

int marcus_board_editor_render(MarcusBoardEditorBackend *draft, MarcusPlayer *player, const MarcusBoardEditValues *values,
                               int angle, const MarcusBoardPose *inspection, const unsigned char **rgba)
try
{
    MarcusBoardWire wire;
    if (!marcus_board_draft_view(&draft->paint, values, &wire) || angle < 0 || angle >= values->angle_count)
        return Error("Storyboard settings are out of range.");
    const auto view = AngleFor(inspection ? *inspection : values->angles[angle]);
    MarcusImage image = {};
    if (MarcusRenderStoryboardView(player, wire, angle, image, &draft->depth, inspection))
        *rgba = image.rgba;
    else
    {
        draft->depth.clear();
        draft->fallback.resize(MARCUS_BOARD_PIXELS * 4);
        marcus_board_fallback_rgba(wire.pixels[angle], draft->paintColours.data(), draft->paintColours.size(),
                                   &wire.values, draft->fallback.data());
        *rgba = draft->fallback.data();
    }
    draft->gridLines = MarcusBoardVisibleGridLines(view, draft->depth, MARCUS_BOARD_WIDTH, MARCUS_BOARD_HEIGHT);
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

int marcus_board_editor_build_grid(MarcusBoardEditorBackend *draft, const MarcusBoardPose *pose, const SDL_FRect *rect)
try
{
    auto &vertices = draft->gridVertices;
    auto &indices = draft->gridIndices;
    vertices.clear();
    indices.clear();
    const auto &r = *rect;
    const MarcusBoardGridFade fade(AngleFor(*pose), r.w, r.h);
    for (const auto &line : draft->gridLines)
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
        float alphaA = fade.Opacity(a[0], a[1]);
        for (int step = 1; step <= steps; ++step)
        {
            const auto b = point(step);
            const float alphaB = fade.Opacity(b[0], b[1]);
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
            a = b;
            alphaA = alphaB;
        }
    }
    return 1;
}
catch (const std::exception &e)
{
    draft->gridVertices.clear();
    draft->gridIndices.clear();
    return Error(e.what());
}

void marcus_board_editor_grid(const MarcusBoardEditorBackend *draft, const SDL_Vertex **vertices, const int **indices, int *count)
{
    *vertices = draft->gridVertices.data();
    *indices = draft->gridIndices.data();
    *count = static_cast<int>(draft->gridIndices.size());
}
}
