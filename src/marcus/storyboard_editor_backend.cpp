#include "storyboard_editor_backend.h"
#include "storyboard.h"
#include "storyboard_tools.h"
#include "colour_picker.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

struct MarcusBoardEditorBackend
{
    MarcusStoryboard board;
    std::vector<unsigned char> baseline, staged, fallback;
    MarcusBoardPaintColours paintColours = MarcusBoardColours;
    MarcusBoardPalette palette = {};
    std::vector<float> depth;
    std::vector<std::array<double, 4>> gridLines;
    std::vector<SDL_Vertex> gridVertices;
    std::vector<int> gridIndices;
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

void WritePose(MarcusBoardAngle &angle, const MarcusBoardPose &pose)
{
    angle.camera = pose.camera;
    angle.roll = pose.roll;
    angle.fov = pose.fov;
    angle.nearClip = pose.near_clip;
    angle.farClip = pose.far_clip;
}

void ImportValues(MarcusStoryboard &board, const MarcusBoardEditValues &values)
{
    if (values.angle_count < 1 || values.angle_count > MARCUS_BOARD_ANGLE_LIMIT ||
        static_cast<std::size_t>(values.angle_count) != board.angles.size() ||
        values.light_count < 0 || values.light_count > MARCUS_BOARD_LIGHT_LIMIT ||
        values.colour_count < MARCUS_BOARD_SWATCH_COUNT || values.colour_count > MARCUS_BOARD_COLOUR_LIMIT)
        throw std::runtime_error("Storyboard settings are out of range.");
    board.colours.assign(values.colours, values.colours + values.colour_count);
    board.lights.resize(values.light_count);
    for (int i = 0; i < values.light_count; ++i) board.lights[i] = MarcusBoardLightFromValues(values.lights[i]);
    for (int i = 0; i < values.angle_count; ++i) WritePose(board.angles[i], values.angles[i]);
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
    if (!MarcusGetStoryboard(player, board)) return Error("Cannot load the selected storyboard.");
    auto baseline = MarcusEncodeBoard(board);
    auto colours = MarcusBoardColours;
    MarcusGetStoryboardColours(player, colours);
    MarcusGetStoryboardPalette(player, draft->palette);
    draft->board = std::move(board);
    draft->baseline = std::move(baseline);
    draft->paintColours = std::move(colours);
    values->angle_count = static_cast<int>(draft->board.angles.size());
    values->light_count = static_cast<int>(draft->board.lights.size());
    values->colour_count = static_cast<int>(draft->board.colours.size());
    for (int i = 0; i < values->angle_count; ++i) values->angles[i] = MarcusBoardPoseOf(draft->board.angles[i]);
    for (int i = 0; i < values->light_count; ++i) values->lights[i] = MarcusBoardLightValuesOf(draft->board.lights[i]);
    std::copy(draft->board.colours.begin(), draft->board.colours.end(), values->colours);
    std::copy_n(draft->paintColours.begin(), MARCUS_BOARD_SWATCH_COUNT, swatches);
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

unsigned char *marcus_board_editor_pixels(MarcusBoardEditorBackend *draft, int angle)
{
    return draft->board.angles[angle].pixels.data();
}

int marcus_board_editor_append_surface(MarcusBoardEditorBackend *draft)
try { draft->board.angles.emplace_back(); return 1; }
catch (const std::exception &e) { return Error(e.what()); }

void marcus_board_editor_default_light(MarcusBoardLightValues *light)
{
    *light = MarcusBoardLightValuesOf(MarcusBoardLight{});
}

int marcus_board_editor_stage(MarcusBoardEditorBackend *draft, const MarcusBoardEditValues *values, int *changed)
try
{
    ImportValues(draft->board, *values);
    draft->staged = MarcusEncodeBoard(draft->board);
    *changed = draft->staged != draft->baseline;
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

int marcus_board_editor_apply(MarcusBoardEditorBackend *draft, MarcusPlayer *player, int angle,
                             const char *resources, const char *content)
try
{
    if (!MarcusApplyStoryboard(player, draft->board, false, angle, resources, content)) return Error(marcus_error(player));
    return 1;
}
catch (const std::exception &e) { return Error(e.what()); }

void marcus_board_editor_accept(MarcusBoardEditorBackend *draft) { draft->baseline.swap(draft->staged); }

int marcus_board_editor_bake(MarcusBoardEditorBackend *draft, int angle, const MarcusBoardPose *pose, unsigned char colour)
try
{
    auto &shot = draft->board.angles[angle];
    WritePose(shot, *pose);
    MarcusBoardBakeGrid(shot, colour);
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
    ImportValues(draft->board, *values);
    const auto view = AngleFor(inspection ? *inspection : values->angles[angle]);
    MarcusImage image = {};
    if (MarcusRenderStoryboard(player, draft->board, angle, image, &draft->depth, inspection ? &view : nullptr))
        *rgba = image.rgba;
    else
    {
        draft->depth.clear();
        draft->fallback.resize(MARCUS_BOARD_PIXELS * 4);
        const auto &pixels = draft->board.angles[angle].pixels;
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            const auto index = pixels[i];
            const auto rgb = index < draft->paintColours.size() ? draft->paintColours[index] : draft->board.colours[index];
            draft->fallback[i * 4] = static_cast<unsigned char>(rgb >> 16);
            draft->fallback[i * 4 + 1] = static_cast<unsigned char>(rgb >> 8);
            draft->fallback[i * 4 + 2] = static_cast<unsigned char>(rgb);
            draft->fallback[i * 4 + 3] = 255;
        }
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
