// Headless authoring/native round-trip checks. No application window or UI input.
#include "storyboard.h"
#include "storyboard_tools.h"
#include "colour_picker.h"
#include "mm/preview.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>

extern bool MarcusRenderLocalWorld(MarcusPlayer *, const MM::PreviewView &, MM::PreviewImage &, MM::Error &);

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    auto player = std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)>(marcus_create(), marcus_destroy);
    auto check = [&](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(std::string(message) + ": " + marcus_error(player.get()));
    };
    try
    {
        // Colour math and picker geometry only; these checks use no UI input.
        for (unsigned red = 0; red <= 255; red += 17)
            for (unsigned green = 0; green <= 255; green += 17)
                for (unsigned blue = 0; blue <= 255; blue += 17)
                {
                    const auto hsl = MarcusToOkHSL(red << 16 | green << 8 | blue);
                    check(std::isfinite(hsl[0]) && hsl[0] >= 0 && hsl[0] < 360 &&
                        std::isfinite(hsl[1]) && hsl[1] >= 0 && hsl[1] <= 1 &&
                        std::isfinite(hsl[2]) && hsl[2] >= 0 && hsl[2] <= 1, "Finite OkHSL conversion");
                    const auto rgb = MarcusOkHSL(hsl[0], hsl[1], hsl[2]);
                    check(std::abs(int(rgb >> 16) - int(red)) <= 1 &&
                        std::abs(int((rgb >> 8) & 255) - int(green)) <= 1 &&
                        std::abs(int(rgb & 255) - int(blue)) <= 1, "OkHSL preserves sRGB colours");
                }
        const auto redHSL = MarcusToOkHSL(0xff0000), greenHSL = MarcusToOkHSL(0x00ff00), blueHSL = MarcusToOkHSL(0x0000ff);
        check(std::abs(redHSL[0] - 29.23f) < .02f && std::abs(greenHSL[0] - 142.50f) < .02f &&
            std::abs(blueHSL[0] - 264.05f) < .02f, "OkHSL uses perceptual primary hues");
        check(MarcusOkHSL(180, 1, 0) == 0 && MarcusOkHSL(180, 1, 1) == 0xffffff &&
            MarcusOkHSL(-60, .7f, .5f) == MarcusOkHSL(300, .7f, .5f), "OkHSL endpoints and hue wrapping");
        for (int lightness = 1; lightness < 20; ++lightness)
            for (int saturation = 0; saturation <= 20; ++saturation)
            {
                const float s = saturation / 20.f, l = lightness / 20.f;
                const auto point = MarcusOkHSLTrianglePoint(s, l);
                const auto picked = MarcusOkHSLTrianglePick(point[0], point[1]);
                check(MarcusOkHSLTriangleContains(point[0], point[1]) &&
                    std::abs(picked[0] - s) < .00001f && std::abs(picked[1] - l) < .00001f,
                    "Triangle selection preserves saturation and lightness");
            }
        for (const auto sl : {std::array<float, 2>{0, 0}, {0, 1}, {1, .5f}})
        {
            const auto point = MarcusOkHSLTrianglePoint(sl[0], sl[1]);
            const auto picked = MarcusOkHSLTrianglePick(point[0], point[1]);
            check(std::abs(picked[0] - sl[0]) < .00001f && std::abs(picked[1] - sl[1]) < .00001f,
                "Triangle vertices select black, white and saturated hue");
        }
        for (const auto point : {std::array<float, 2>{-2, 0}, {2, 2}, {0, -2}})
        {
            check(!MarcusOkHSLTriangleContains(point[0], point[1]), "Triangle excludes its surrounding gap");
            const auto sl = MarcusOkHSLTrianglePick(point[0], point[1]);
            check(sl[0] >= 0 && sl[0] <= 1 && sl[1] >= 0 && sl[1] <= 1,
                "Dragging outside the triangle clamps to its edges");
        }
        const auto triangle = MarcusOkHSLTrianglePixels(210, 64);
        check(triangle.size() == 64 * 64 * 4 && triangle[3] == 0, "Picker texture has a transparent exterior");
        const auto centreSL = MarcusOkHSLTrianglePick(1 / 64.f, 1 / 64.f);
        const auto centreRGB = MarcusOkHSL(210, centreSL[0], centreSL[1]);
        const auto centre = triangle.data() + (32 * 64 + 32) * 4;
        check(centre[3] == 255 && (unsigned(centre[0]) << 16 | unsigned(centre[1]) << 8 | centre[2]) == centreRGB,
            "Picker texture displays the selected OkHSL colour");
        check(MarcusOkHSLTrianglePixels(0, 0).empty() && MarcusOkHSLTrianglePixels(0, 1025).empty(), "Bound picker texture sizes");
        MarcusStoryboard board;
        check(MarcusBoardValid(board), "Default storyboard");
        {
            check(marcus_add_scene(player.get(), 11, argv[1], argv[2]), "Create retail black-scene comparison");
            MM::Archive retailData; MM::Error readError;
            check(retailData.Open((std::filesystem::u8path(argv[2]) / "bkgds.3cn").u8string(), readError), "Open retail insertion data");
            auto cameraPoints = [&](unsigned slot) {
                const auto &chunks = retailData.Chunks();
                const auto background = std::find_if(chunks.begin(), chunks.end(), [](const auto &c) {
                    return c.id.type == 0x424b4744 && c.id.number == 11;
                });
                check(background != chunks.end(), "Read black background definition");
                const auto child = std::find_if(background->children.begin(), background->children.end(), [&](const auto &c) {
                    return c.present && c.slot == slot && c.target.type == 0x43414d20;
                });
                check(child != background->children.end(), "Read black camera slot");
                const auto camera = std::find_if(chunks.begin(), chunks.end(), [&](const auto &c) { return c.id == child->target; });
                MM::PayloadPage payload;
                check(camera != chunks.end() && retailData.ReadPayload(camera - chunks.begin(), 0, 4096, payload, readError), "Decode retail camera points");
                const auto &b = payload.bytes;
                check(b.size() >= 76 && (b.size() - 76) % 12 == 0 && b[0] == 1 && b[1] == 0, "Retail camera point layout");
                std::vector<std::array<float, 3>> points;
                for (size_t at = 16; at < b.size(); at = at == 16 ? 76 : at + 12)
                {
                    std::array<float, 3> point;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const size_t p = at + axis * 4;
                        const auto word = std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) |
                            (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24);
                        point[axis] = static_cast<std::int32_t>(word) / 65536.f;
                    }
                    points.push_back(point);
                }
                return points;
            };
            const auto retailPoints = cameraPoints(0);
            check(retailPoints.size() == 3 && retailPoints[0] != retailPoints[1] && retailPoints[1] != retailPoints[2],
                "Black scene supplies three distinct insertion anchors");
            auto atPoint = [](const MarcusObject &object, const std::array<float, 3> &point) {
                return std::abs(object.x - point[0]) < .01f && std::abs(object.y - point[1]) < .01f && std::abs(object.z - point[2]) < .01f;
            };
            MarcusProjection retailCamera = {};
            check(marcus_camera_projection(player.get(), 0, &retailCamera), "Read retail black camera");
            MarcusProp cube = {};
            const int count = marcus_prop_count(player.get(), argv[2]);
            bool found = false;
            for (int i = 0; i < count; ++i)
            {
                MarcusProp prop = {}; check(marcus_prop(player.get(), i, &prop), "Read comparison prop");
                if (std::string(prop.name).find("Cube") != std::string::npos || std::string(prop.name).find("cube") != std::string::npos)
                { cube = prop; found = true; break; }
            }
            check(found, "Retail cube is available");
            int index = marcus_add_prop(player.get(), cube.chunk);
            MarcusObject retailCube = {};
            check(index >= 0 && marcus_object(player.get(), index, &retailCube), "Insert cube into retail black scene");
            std::printf("Retail cube placement: %.5f %.5f %.5f\n", retailCube.x, retailCube.y, retailCube.z);
            check(atPoint(retailCube, retailPoints[0]), "Retail insertion starts at the default point");
            MarcusObject inserted = {};
            for (int step = 1; step < 5; ++step)
            {
                MarcusImage thumbnail = {}; MarcusProjection metadata = {};
                check(marcus_camera_thumbnail(player.get(), 1, &thumbnail) && marcus_camera_projection(player.get(), 2, &metadata) &&
                    marcus_seek(player.get(), 1), "Read camera previews and seek without consuming insertion anchors");
                check(marcus_add_prop(player.get(), 0xffffffffUL) < 0, "Reject an unavailable prop without consuming a point");
                index = marcus_add_prop(player.get(), cube.chunk);
                check(index >= 0 && marcus_object(player.get(), index, &inserted) && atPoint(inserted, retailPoints[step % 3]),
                    "Retail insertions cycle default, extra one, extra two, default");
            }
            check(marcus_move_object(player.get(), index, inserted.x + 5, inserted.y, inserted.z), "Move the newly inserted cube away");
            index = marcus_add_prop(player.get(), cube.chunk);
            check(index >= 0 && marcus_object(player.get(), index, &inserted) && atPoint(inserted, retailPoints[1]),
                "A completed manual move releases the last insertion anchor for reuse");
            check(marcus_set_scene_camera(player.get(), 1), "Switch the authored camera");
            index = marcus_add_prop(player.get(), cube.chunk);
            check(index >= 0 && marcus_object(player.get(), index, &inserted) && atPoint(inserted, cameraPoints(1)[0]),
                "Switching cameras starts at that camera's default point");
            check(marcus_set_scene_camera(player.get(), 0), "Return to the original camera");
            index = marcus_add_prop(player.get(), cube.chunk);
            check(index >= 0 && marcus_object(player.get(), index, &inserted) && atPoint(inserted, retailPoints[0]),
                "Returning to a camera restarts its insertion sequence");
            const auto initial = MarcusBoardProjection(board.angles[0]);
            for (int i = 0; i < 12; ++i)
                check(std::abs(initial.transform[i] - retailCamera.transform[i]) < .0002f, "Default storyboard camera matches retail black pose");
            check(std::abs(initial.vertical_fov - retailCamera.vertical_fov) < .001f && initial.near_clip == retailCamera.near_clip &&
                initial.far_clip == retailCamera.far_clip, "Default storyboard lens and clipping match retail black");
            check(MarcusApplyStoryboard(player.get(), board, true, 0, argv[1], argv[2]), "Create default storyboard comparison");
            index = marcus_add_prop(player.get(), cube.chunk);
            MarcusObject storyboardCube = {};
            check(index >= 0 && marcus_object(player.get(), index, &storyboardCube) &&
                std::abs(retailCube.x - storyboardCube.x) < .01f && std::abs(retailCube.y - storyboardCube.y) < .01f &&
                std::abs(retailCube.z - storyboardCube.z) < .01f, "Storyboard cube insertion matches retail black placement");
            std::vector<float> depths;
            MarcusImage preview = {};
            check(MarcusRenderStoryboard(player.get(), board, 0, preview, &depths) &&
                std::count_if(depths.begin(), depths.end(), [](float d) { return std::isfinite(d); }) > 100,
                "A freshly inserted cube is visible through the default storyboard shot");
            for (int step = 1; step <= 3; ++step)
            {
                index = marcus_add_prop(player.get(), cube.chunk);
                check(index >= 0 && marcus_object(player.get(), index, &inserted) && atPoint(inserted, retailPoints[step % 3]),
                    "Generated storyboard cameras retain and cycle all three retail insertion anchors");
            }
            for (float pitch : {0.f, -35.f, 75.f})
            {
                auto changed = board;
                changed.angles[0].camera.pitch = pitch; changed.angles[0].camera.yaw = 40;
                changed.angles[0].roll = 15; changed.angles[0].fov = 20;
                check(MarcusApplyStoryboard(player.get(), changed, false, 0, argv[1], argv[2]), "Set a moved storyboard camera");
                const auto projection = MarcusBoardProjection(changed.angles[0]);
                for (int step = 0; step < 3; ++step)
                {
                    index = marcus_add_prop(player.get(), cube.chunk);
                    float projected[3], world[3];
                    check(index >= 0 && marcus_object(player.get(), index, &inserted), "Insert into moved storyboard shot");
                    world[0] = inserted.x; world[1] = inserted.y; world[2] = inserted.z;
                    check(camera_project_point(&projection, world, projected), "Every insertion anchor stays in frame after camera rotation and lens changes");
                }
            }
            // Keep the rest of the serialization/export fixture a single fresh scene.
            player.reset(); player.reset(marcus_create());
        }
        {
            MarcusBoardAngle view;
            view.camera = {0, 10, 40, 0, 0, 5}; view.nearClip = .1f;
            const auto projection = MarcusBoardProjection(view);
            MarcusBoardLight lamp = {0, 10, 0, 45, 20, .8f, 0};
            const auto widget = MarcusBoardBuildLightWidget(projection, lamp, 600);
            check(widget.visible && std::abs(widget.centre[0] - .5f) < .0001f && std::abs(widget.centre[1] - .5f) < .0001f,
                "Project light selection markers through the scene camera");
            MarcusBoardLight moved;
            float matrix[16];
            MarcusBoardLightTransform(lamp, matrix);
            check(matrix[12] == lamp.x && matrix[13] == lamp.y && matrix[14] == lamp.z &&
                std::abs(matrix[9] - std::sin(lamp.pitch * .01745329252f)) < .00001f,
                "Light matrix matches the native camera-to-world pitch convention");
            matrix[12] += 5; matrix[13] -= 3; matrix[14] += 9;
            check(MarcusBoardLightFromTransform(lamp, matrix, false, moved) && moved.x == 5 && moved.y == 7 && moved.z == 9 &&
                moved.yaw == lamp.yaw && moved.pitch == lamp.pitch && moved.intensity == lamp.intensity,
                "ImGuizmo translation preserves light direction and brightness");
            matrix[12] = 40000; matrix[13] = -40000;
            check(MarcusBoardLightFromTransform(lamp, matrix, false, moved) && moved.x == 32700 && moved.y == -32700,
                "Gizmo movement saturates at native world limits");
            matrix[12] = std::numeric_limits<float>::infinity();
            check(!MarcusBoardLightFromTransform(lamp, matrix, false, moved), "Nonfinite transforms cannot corrupt a light");
            for (float yaw : {-170.f, 0.f, 45.f, 179.f}) for (float pitch : {-89.f, 0.f, 35.f, 89.f})
            {
                auto turned = lamp; turned.yaw = yaw; turned.pitch = pitch;
                turned.x = 123; turned.y = 456; turned.z = 789;
                MarcusBoardLightTransform(turned, matrix);
                check(MarcusBoardLightFromTransform(lamp, matrix, true, moved) && std::abs(moved.yaw - yaw) < .001f &&
                    std::abs(moved.pitch - pitch) < .001f && moved.x == lamp.x && moved.y == lamp.y && moved.z == lamp.z,
                    "Rotation rings round-trip yaw/pitch while preserving the light position");
            }
            matrix[8] = matrix[10] = 0; matrix[9] = 1;
            check(MarcusBoardLightFromTransform(lamp, matrix, true, moved) && moved.pitch == 89,
                "Vertical rotation respects the native light pitch limit");
            matrix[9] = 0;
            check(!MarcusBoardLightFromTransform(lamp, matrix, true, moved), "Degenerate orientation is rejected");
            lamp.point = 1;
            const auto point = MarcusBoardBuildLightWidget(projection, lamp, 600);
            MarcusBoardLightTransform(lamp, matrix);
            check(point.shape.line_count == 24 && point.direction.line_count == 0 &&
                !MarcusBoardLightFromTransform(lamp, matrix, true, moved), "Point light has a spherical marker and no rotation");
            lamp.z = 50;
            check(!MarcusBoardBuildLightWidget(projection, lamp, 600).visible, "Lights behind the viewer are not pickable");
            view.camera = {25, 24, 40, 30, 15, 5}; view.roll = 17;
            lamp = {0, 10, 0, 45, 20, .8f, 0};
            const auto angled = MarcusBoardProjection(view);
            const auto angledWidget = MarcusBoardBuildLightWidget(angled, lamp, 600);
            check(angledWidget.visible, "Light markers remain visible through a pitched, yawed and rolled camera");
            std::vector<float> clear(MarcusBoardWidth * MarcusBoardHeight, std::numeric_limits<float>::infinity());
            const auto full = MarcusBoardVisibleGridLines(view, clear, MarcusBoardWidth, MarcusBoardHeight);
            check(!full.empty(), "Ground grid is visible against an infinite backdrop");
            std::fill(clear.begin(), clear.end(), .1f);
            check(MarcusBoardVisibleGridLines(view, clear, MarcusBoardWidth, MarcusBoardHeight).empty(),
                "Foreground geometry occludes every live grid segment behind it");
            for (int y = 0; y < MarcusBoardHeight; ++y) for (int x = 0; x < MarcusBoardWidth; ++x)
                clear[y * MarcusBoardWidth + x] = x < MarcusBoardWidth / 2 ? .1f : std::numeric_limits<float>::infinity();
            const auto half = MarcusBoardVisibleGridLines(view, clear, MarcusBoardWidth, MarcusBoardHeight);
            check(!half.empty() && std::all_of(half.begin(), half.end(), [](const auto &line) {
                return line[0] >= .498 && line[2] >= .498;
            }), "Live grid segments split at the foreground silhouette");
            MarcusBoardAngle level;
            level.camera.pitch = 0;
            const MarcusBoardGridFade fade(level, 544, 306);
            check(fade.Opacity(.5, .5) == 0 && fade.Opacity(.5, .49) == 0 && fade.Opacity(.5, .51) == 0,
                "The horizon and subpixel distant grid disappear instead of accumulating a solid band");
            float previous = 0;
            for (double y : {.51, .58, .64, .72, .9})
            {
                const float alpha = fade.Opacity(.5, y);
                check(std::isfinite(alpha) && alpha >= previous && alpha >= 0 && alpha <= 1,
                    "Grid opacity increases smoothly from the horizon toward the foreground");
                previous = alpha;
            }
            check(fade.Opacity(.5, .64) > 0 && fade.Opacity(.5, .64) < 1 && fade.Opacity(.5, .9) == 1,
                "Grid fade retains a transparent middle distance and solid readable foreground");
            const MarcusBoardGridFade large(level, 1088, 612);
            check(large.Opacity(.5, .64) > fade.Opacity(.5, .64), "Fade responds to displayed pixel spacing when the viewport grows");
            const float ground[] = {0, 0, -150};
            float flatPoint[3], rolledPoint[3];
            auto levelProjection = MarcusBoardProjection(level);
            check(camera_project_point(&levelProjection, ground, flatPoint), "Project a middle-distance ground point");
            auto rolled = level; rolled.roll = 37;
            auto rolledProjection = MarcusBoardProjection(rolled);
            check(camera_project_point(&rolledProjection, ground, rolledPoint) &&
                std::abs(MarcusBoardGridFade(rolled, 544, 306).Opacity(rolledPoint[0], rolledPoint[1]) -
                    fade.Opacity(flatPoint[0], flatPoint[1])) < .0001f, "Grid fading follows the rolled camera's ground plane");
            check(MarcusBoardGridFade(level, 0, 306).Opacity(.5, .9) == 0, "An empty viewport has no grid opacity");
        }
        for (float pitch : {-90.f, 90.f})
        {
            auto vertical = board; vertical.angles[0].camera.pitch = pitch;
            check(MarcusBoardValid(vertical) && MarcusDecodeBoard(MarcusEncodeBoard(vertical)).angles[0].camera.pitch == pitch,
                "Full Fly pitch range survives storyboard serialization");
            const auto verticalGrid = MarcusBoardGridLines(vertical.angles[0]);
            check(std::all_of(verticalGrid.begin(), verticalGrid.end(), [](const auto &line) {
                return std::all_of(line.begin(), line.end(), [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; });
            }), "Vertical camera grid remains finite and clipped");
        }
        MarcusBoardStroke(board.angles[0], 0, 0, 543, 305, 8, 3);
        check(board.angles[0].pixels[0] == 3 && board.angles[0].pixels.back() == 3 && board.angles[0].pixels[100] == 0, "Clipped, continuous opaque strokes");
        MarcusBoardStroke(board.angles[0], 270, 152, 270, 152, 16, 0);
        check(board.angles[0].pixels[152 * 544 + 270] == 0, "Canvas grey covers existing marks");
        const auto originalPainting = board.angles[0].pixels;
        const auto grid = MarcusBoardGridLines(board.angles[0]);
        check(!grid.empty() && std::all_of(grid.begin(), grid.end(), [](const auto &line) {
            return std::all_of(line.begin(), line.end(), [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; });
        }), "Perspective grid is clipped to the canvas");
        MarcusBoardBakeGrid(board.angles[0], 6);
        check(std::count(board.angles[0].pixels.begin(), board.angles[0].pixels.end(), 6) > 500 &&
            std::equal(originalPainting.begin(), originalPainting.begin() + 544 * 76, board.angles[0].pixels.begin()),
            "Bake the ground grid while retaining the drawing above the horizon");
        auto rebaked = board.angles[0]; MarcusBoardBakeGrid(rebaked, 6);
        check(rebaked.pixels == board.angles[0].pixels, "Baking the same grid twice is stable");
        {
            MarcusBoardAngle baked;
            baked.camera.pitch = 0;
            for (int y = 0; y < MarcusBoardHeight; ++y) for (int x = 0; x < MarcusBoardWidth; ++x)
                baked.pixels[y * MarcusBoardWidth + x] = (x / 20 + y / 20) % 2 ? 3 : 4;
            const auto original = baked.pixels;
            auto mask = baked; std::fill(mask.pixels.begin(), mask.pixels.end(), 0);
            for (const auto &line : MarcusBoardGridLines(baked))
                MarcusBoardStroke(mask, int(std::lround(line[0] * (MarcusBoardWidth - 1))), int(std::lround(line[1] * (MarcusBoardHeight - 1))),
                    int(std::lround(line[2] * (MarcusBoardWidth - 1))), int(std::lround(line[3] * (MarcusBoardHeight - 1))), 1, 1);
            MarcusBoardBakeGrid(baked, 6);
            const MarcusBoardGridFade fade(baked, MarcusBoardWidth, MarcusBoardHeight);
            int faded = 0, solid = 0, partialInk = 0, partialBackground = 0;
            for (int y = 0; y < MarcusBoardHeight; ++y) for (int x = 0; x < MarcusBoardWidth; ++x)
            {
                const size_t i = size_t(y) * MarcusBoardWidth + x;
                if (!mask.pixels[i]) { check(baked.pixels[i] == original[i], "Grid baking retains paint outside the lines"); continue; }
                const float opacity = fade.Opacity((x + .5) / MarcusBoardWidth, (y + .5) / MarcusBoardHeight);
                if (opacity == 0) { ++faded; check(baked.pixels[i] == original[i], "The baked horizon retains the underlying painting"); }
                else if (opacity == 1) { ++solid; check(baked.pixels[i] == 6, "Nearby baked grid lines retain the full selected ink"); }
                else if (opacity > .2f && opacity < .8f)
                {
                    if (opacity >= .5f)
                    { ++partialInk; check(baked.pixels[i] == 6, "Readable middle-distance lines have no stippled gaps"); }
                    else
                    { ++partialBackground; check(baked.pixels[i] == original[i], "Crowded distant lines are omitted without scattered speckles"); }
                }
            }
            check(faded > 500 && solid > 500 && partialInk > 20 && partialBackground > 20,
                "Baking retains continuous lines with a clear cutoff before the crowded horizon");
            auto again = baked; MarcusBoardBakeGrid(again, 6);
            check(again.pixels == baked.pixels, "Rebaking retains the same solid grid and distant cutoff");
        }
        auto turned = board.angles[0]; turned.camera.yaw = 37; turned.camera.pitch = 24; turned.roll = -18; turned.fov = 5; turned.nearClip = .01f;
        const auto turnedGrid = MarcusBoardGridLines(turned);
        check(!turnedGrid.empty() && turnedGrid != grid && std::all_of(turnedGrid.begin(), turnedGrid.end(), [](const auto &line) {
            return std::all_of(line.begin(), line.end(), [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; });
        }), "Moved, rolled and zoomed grids remain clipped");
        auto legacy = MarcusEncodeBoard(board);
        legacy.erase(legacy.begin() + 20, legacy.begin() + 24 + board.colours.size() * 4); legacy[0] = 1;
        check(MarcusEncodeBoard(MarcusDecodeBoard(legacy)) == MarcusEncodeBoard(board), "Read version-one storyboards with the original colour indices");
        for (unsigned char grey = 7; grey < MarcusBoardColourCount; ++grey)
        {
            MarcusBoardStroke(board.angles[0], 30 + grey * 20, 40, 30 + grey * 20, 40, 8, grey);
            check(board.angles[0].pixels[40 * MarcusBoardWidth + 30 + grey * 20] == grey, "Paint each additional grey");
        }
        auto versionTwo = MarcusEncodeBoard(board);
        versionTwo.erase(versionTwo.begin() + 20, versionTwo.begin() + 24 + board.colours.size() * 4); versionTwo[0] = 2;
        check(MarcusEncodeBoard(MarcusDecodeBoard(versionTwo)) == MarcusEncodeBoard(board), "Read version-two storyboard greys");
        check(MarcusBoardHSV(0, 1, 1) == 0xff0000 && MarcusBoardHSV(120, 1, 1) == 0x00ff00 &&
            MarcusBoardHSV(240, 1, 1) == 0x0000ff && MarcusBoardHSV(360, 1, 1) == 0xff0000 &&
            MarcusBoardHSV(77, 0, 1) == 0xffffff && MarcusBoardHSV(77, 1, 0) == 0, "HSV primaries, hue wrap, white and black");
        for (auto rgb : {0x2357a5U, 0xffffffU, 0U, 0x949494U, 0x9ebfd9U})
        {
            const auto hsv = MarcusBoardToHSV(rgb);
            check(MarcusBoardHSV(hsv[0], hsv[1], hsv[2]) == rgb, "RGB/HSV round trip keeps the selected paint colour");
        }
        auto fullPalette = board;
        for (unsigned i = MarcusBoardColourCount; i < 256; ++i) fullPalette.colours.push_back(0x123400 + i);
        for (unsigned i = 0; i < 256; ++i) fullPalette.angles[0].pixels[i] = static_cast<unsigned char>(i);
        const auto fullColours = fullPalette.colours;
        check(MarcusBoardAddColour(fullPalette, 0xabcdef) == -1 && fullPalette.colours == fullColours,
            "A full paint palette cannot recolour existing strokes");
        fullPalette.angles[0].pixels[200] = 0;
        check(MarcusBoardAddColour(fullPalette, 0xabcdef) == 200 && fullPalette.colours.size() == 256,
            "An unused custom colour slot can be reused safely");
        const auto customRgb = MarcusBoardHSV(280, .8f, .7f);
        const int customInk = MarcusBoardAddColour(board, customRgb);
        check(customInk == int(MarcusBoardColourCount) && MarcusBoardAddColour(board, customRgb) == customInk &&
            board.colours.size() == MarcusBoardColourCount + 1, "Reuse an existing custom colour without consuming another slot");
        MarcusBoardStroke(board.angles[0], 500, 24, 520, 24, 8, static_cast<unsigned char>(customInk));
        board.angles.emplace_back();
        // Keep this prop/lighting fixture close to the ground, independent of the new-scene camera height.
        board.angles[1].camera.x = 7; board.angles[1].camera.y = 5; board.angles[1].camera.yaw = -25; board.angles[1].camera.pitch = 12;
        board.angles[1].roll = 8; board.angles[1].fov = 62;
        std::fill(board.angles[1].pixels.begin(), board.angles[1].pixels.end(), 5);
        board.lights.push_back({2, 8, 4, 10, -20, .6f, 1});
        const auto bytes = MarcusEncodeBoard(board);
        check(MarcusEncodeBoard(MarcusDecodeBoard(bytes)) == bytes, "Lossless versioned editor data");
        auto broken = bytes; broken.pop_back();
        bool rejected = false;
        try { MarcusDecodeBoard(broken); } catch (const std::exception &) { rejected = true; }
        check(rejected, "Reject truncated editor data");
        auto invalid = board; invalid.angles[0].fov = std::numeric_limits<float>::quiet_NaN();
        check(!MarcusBoardValid(invalid), "Reject nonfinite camera");
        invalid = board; invalid.angles[0].pixels[0] = static_cast<unsigned char>(board.colours.size());
        check(!MarcusBoardValid(invalid), "Reject invalid paint colour");
        invalid = board; invalid.colours.back() = 0x1000000;
        check(!MarcusBoardValid(invalid), "Reject invalid custom RGB values");
        check(MarcusApplyStoryboard(player.get(), board, true, 0, argv[1], argv[2]), "Create blank storyboard movie");
        MarcusInfo info = {};
        check(marcus_info(player.get(), &info) && info.scene_count == 1 && info.camera_count == 2 && info.camera == 0 && info.object_count == 0 && info.sound_count == 0 && info.frame == 1, "Blank scene admission");
        // Viewport tools commit completed gestures to the ordinary movie history.
        auto painted = board;
        for (int stroke = 0; stroke < 3; ++stroke)
        {
            const auto revision = marcus_document(player.get());
            MarcusBoardStroke(painted.angles[0], 400, 30 + stroke * 12, 460, 30 + stroke * 12, 4, 1);
            check(MarcusApplyStoryboard(player.get(), painted, false, 0, argv[1], argv[2]) &&
                marcus_document(player.get()) != revision && marcus_info(player.get(), &info) && info.frame == 1 && info.camera == 0,
                "A completed stroke commits without moving the timeline");
            MarcusStoryboard current;
            check(MarcusGetStoryboard(player.get(), current) && MarcusEncodeBoard(current) == MarcusEncodeBoard(painted),
                "Movie state immediately contains a completed stroke");
        }
        for (int stroke = 0; stroke < 3; ++stroke) check(marcus_undo(player.get()), "Undo individual viewport strokes");
        MarcusStoryboard restored;
        check(MarcusGetStoryboard(player.get(), restored) && MarcusEncodeBoard(restored) == bytes,
            "Movie undo restores the original canvas after multiple strokes");
        MarcusProjection projection;
        check(marcus_camera_projection(player.get(), 1, &projection) && std::abs(projection.transform[9] - 7) < .001 &&
            std::abs(projection.transform[7] - std::sin(12 * .01745329252)) < .001 && std::abs(projection.vertical_fov - 62) < .01, "Native camera pose, pitch convention and FOV");
        MM::PreviewView view; view.width = 544; view.height = 306; view.includeDepth = true; view.renderAtOutputResolution = true;
        MM::PreviewImage image; MM::Error error;
        check(MarcusRenderLocalWorld(player.get(), view, image, error), "Render storyboard image");
        check(image.depth.size() == 544 * 306 && std::all_of(image.depth.begin(), image.depth.end(), [](float d) { return std::isinf(d) && d > 0; }), "Every storyboard depth is infinity");
        check(marcus_add_frame(player.get(), 1) && marcus_set_scene_camera(player.get(), 1), "Author second camera cut");
        check(marcus_seek(player.get(), 1) && marcus_info(player.get(), &info) && info.camera == 0 &&
            marcus_seek(player.get(), 2) && marcus_info(player.get(), &info) && info.camera == 1, "Camera cut survives seeking");
        auto edited = board; edited.angles[1].fov = 70; edited.angles[0].pixels[1000] = 4;
        MarcusBoardBakeGrid(edited.angles[1], 3);
        check(MarcusApplyStoryboard(player.get(), edited, false, 1, argv[1], argv[2]) &&
            marcus_camera_projection(player.get(), 1, &projection) && std::abs(projection.vertical_fov - 70) < .01, "Edit camera and painting");
        check(marcus_undo(player.get()) && marcus_camera_projection(player.get(), 1, &projection) && std::abs(projection.vertical_fov - 62) < .01, "Undo restores native scenery");
        MarcusStoryboard undone;
        check(MarcusGetStoryboard(player.get(), undone) && MarcusEncodeBoard(undone) == bytes, "Undo restores the painting before a grid bake");
        check(marcus_reload(player.get(), argv[1], argv[2]) && marcus_is_storyboard(player.get()), "Reload retains custom backgrounds");
        check(marcus_seek(player.get(), 2), "Return to second cut");
        int props = marcus_prop_count(player.get(), argv[2]);
        check(props > 0, "Prop catalogue available");
        MarcusProp prop = {}; check(marcus_prop(player.get(), 0, &prop), "Read prop");
        for (int i = 0; i < props; ++i)
        {
            MarcusProp candidate = {}; marcus_prop(player.get(), i, &candidate);
            if (std::string(candidate.name).find("Ball") != std::string::npos) { prop = candidate; break; }
        }
        std::printf("Storyboard prop: %s\n", prop.name); std::fflush(stdout);
        check(marcus_add_prop(player.get(), prop.chunk) >= 0, "Block out with 3D props");
        MarcusObject object; check(marcus_object(player.get(), 0, &object), "Read placed object");
        check(MarcusRenderLocalWorld(player.get(), view, image, error) &&
            std::any_of(image.depth.begin(), image.depth.end(), [](float d) { return std::isfinite(d); }), "Props render in front of the painting");
        MarcusImage preview;
        std::vector<float> previewDepth;
        check(MarcusRenderStoryboard(player.get(), board, 1, preview, &previewDepth) && preview.projection.valid &&
            previewDepth.size() == MarcusBoardWidth * MarcusBoardHeight &&
            std::any_of(previewDepth.begin(), previewDepth.end(), [](float d) { return std::isfinite(d); }),
            "Draft camera/light preview returns scene depth for ground-guide occlusion");
        std::vector<unsigned char> originalPreview(preview.rgba, preview.rgba + preview.pitch * preview.height);
        auto dark = board; for (auto &l : dark.lights) l.intensity = 0;
        check(MarcusRenderStoryboard(player.get(), dark, 1, preview), "Preview disabled lights");
        const std::vector<unsigned char> darkPreview(preview.rgba, preview.rgba + preview.pitch * preview.height);
        auto bright = board;
        bright.lights = {{0, 0, 0, -25, 12, 1, 0}, {0, 0, 0, 155, -12, 1, 0}};
        check(MarcusRenderStoryboard(player.get(), bright, 1, preview) &&
            !std::equal(darkPreview.begin(), darkPreview.end(), preview.rgba), "Draft lights change actor shading");
        check(MarcusRenderStoryboard(player.get(), board, 1, preview) &&
            std::equal(originalPreview.begin(), originalPreview.end(), preview.rgba), "Temporary lights restore exactly");
        const auto beforeInspection = marcus_document(player.get());
        auto inspection = board.angles[1]; inspection.camera.x += 10; inspection.camera.yaw += 15;
        check(MarcusRenderStoryboard(player.get(), board, 1, preview, &previewDepth, &inspection) &&
            std::abs(preview.projection.transform[9] - inspection.camera.x) < .01f &&
            marcus_document(player.get()) == beforeInspection && marcus_info(player.get(), &info) && info.camera == 1,
            "Light inspection can move its camera without authoring the movie shot or an undo entry");
        auto movedLights = board; movedLights.lights[0].x += 5; movedLights.lights[0].yaw += 20;
        check(MarcusApplyStoryboard(player.get(), movedLights, false, 1, argv[1], argv[2]) && marcus_undo(player.get()) &&
            MarcusGetStoryboard(player.get(), restored) && MarcusEncodeBoard(restored) == bytes,
            "A completed light position/direction edit is one undo step");
        const auto path = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        std::filesystem::create_directories(path.parent_path());
        auto unsafePath = path; unsafePath.replace_extension(".3mm");
        check(marcus_requires_vmm(player.get()) && !marcus_save_copy(player.get(), unsafePath.u8string().c_str()),
            "Storyboard exports require a VMM, protecting retail movies");
        check(marcus_save_copy(player.get(), path.u8string().c_str()), "Save native movie copy");
        check(marcus_save_copy(player.get(), path.u8string().c_str()), "Atomically replace saved copy");
        const auto bundle = MM::V3dmm::ReadBundle(path);
        check(bundle.dependencies.size() == 2 && bundle.dependencies[0].name == "3dmm" && bundle.dependencies[1].archive.length > 0,
            "VMM carries the base dependency and a complete portable background package");
        const auto package = MM::V3dmm::ReadPackage(bundle.dependencies[1].archive);
        check(package.metadata.at("marcusstoryboard") == "1", "Identify generated background-only packages for replacement");
        const auto packageDirectory = path.parent_path() / ("storyboard-package-check-" + package.shortName);
        std::filesystem::create_directories(packageDirectory);
        const auto members = MM::V3dmm::Materialize(bundle.dependencies[1].archive, packageDirectory);
        auto payload = [&](MM::Archive &archive, MM::Id id) {
            const auto &chunks = archive.Chunks();
            const auto at = std::find_if(chunks.begin(), chunks.end(), [&](const auto &c) { return c.id == id; });
            MM::PayloadPage page; MM::Error readError;
            check(at != chunks.end() && archive.ReadPayload(at - chunks.begin(), 0, 4096, page, readError), "Read packaged palette bytes");
            return page.bytes;
        };
        MM::Archive retail, packaged; MM::Error readError;
        check(retail.Open((std::filesystem::u8path(argv[2]) / "bkgds.3cn").u8string(), readError) &&
            packaged.Open(members.at(package.shortName + ".3cn").u8string(), readError), "Open reference and packaged content");
        const auto blackPalette = payload(retail, {0x474c4352, 0x1000c});
        MarcusBoardPalette colours = {};
        for (unsigned i = 10; i < 246; ++i)
        {
            const auto *p = blackPalette.data() + 12 + (i - 10) * 4;
            colours[i] = (unsigned(p[2]) << 16) | (unsigned(p[1]) << 8) | p[0];
        }
        const auto inks = MarcusBoardColourMap(colours);
        check(inks[1].rgb == 0 && inks[1].amount == 0 && inks[2].rgb == 0xffffff && inks[2].amount == 0,
            "Black and white remain solid palette colours");
        check(((inks[6].rgb >> 8) & 255) > ((inks[6].rgb >> 16) & 255) &&
            ((inks[6].rgb >> 8) & 255) > (inks[6].rgb & 255), "Dither preserves the soft green hue instead of choosing grey");
        for (const auto &ink : inks)
        {
            int second = 0;
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            {
                const auto index = MarcusBoardPaintIndex(ink, x, y);
                check(index >= 10 && index < 246, "Paint does not use reserved palette indices");
                if (ink.first != ink.second && index == ink.second) ++second;
            }
            check(second == ink.amount, "Dither pattern uses the chosen mixture exactly");
        }
        int palettes = 0, cameras = 0;
        for (const auto &chunk : packaged.Chunks())
            if (chunk.id.type == 0x43414d20)
            { check(payload(packaged, chunk.id).size() == 100, "Exported CAM preserves the default and two extra insertion anchors"); ++cameras; }
            else if (chunk.id.type == 0x474c4352)
            { check(payload(packaged, chunk.id) == blackPalette, "Preserve every retail scene palette entry, including prop colours"); ++palettes; }
        check(palettes > 0, "Packaged background has the standard palette");
        check(cameras == int(board.angles.size()), "All packaged angles retain insertion points");
        packaged.Close(); retail.Close();
        for (const auto &member : members) std::filesystem::remove(member.second);
        std::filesystem::remove(packageDirectory);
        check(marcus_open(player.get(), path.u8string().c_str(), argv[1], argv[2]), "Reopen native storyboard movie");
        MarcusStoryboard loaded;
        check(MarcusGetStoryboard(player.get(), loaded) && MarcusEncodeBoard(loaded) == bytes, "Reopen editable drawings and lights");
        check(marcus_seek(player.get(), 2) && marcus_info(player.get(), &info) && info.camera == 1 && info.object_count == 1, "Reopen camera cuts and props");
        check(marcus_object(player.get(), 0, &object), "Reopen object");
        edited = loaded; edited.angles[1].camera.x += 2;
        check(MarcusApplyStoryboard(player.get(), edited, false, 1, argv[1], argv[2]) && marcus_object(player.get(), 0, &object), "Edit reopened storyboard without losing actors");
        check(marcus_undo(player.get()) && MarcusGetStoryboard(player.get(), loaded) && MarcusEncodeBoard(loaded) == bytes, "Undo reopened storyboard edit");
        check(!marcus_save_copy(player.get(), path.u8string().c_str()), "Save copy protects its open source");
        auto resavedPath = path; resavedPath.replace_filename(path.stem().u8string() + "-resaved.vmm");
        auto resaveStem = resavedPath; resaveStem.replace_extension();
        check(marcus_save_copy(player.get(), resaveStem.u8string().c_str()) &&
            MM::V3dmm::ReadBundle(resavedPath).dependencies.size() == 2, "Resaving infers VMM and replaces generated packages");
        for (int renderer = 0; renderer < MARCUS_RENDERER_COUNT; ++renderer)
        {
            if (!marcus_set_renderer(player.get(), renderer)) { std::printf("Renderer %d unavailable: %s\n", renderer, marcus_error(player.get())); continue; }
            MarcusImage frame;
            check(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &frame), "Render native storyboard backend");
            check(frame.rgba && frame.width > 0 && frame.height > 0, "Native backend returns pixels");
        }
        check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER), "Restore CPU renderer");
        check(MarcusApplyStoryboard(player.get(), dark, false, 1, argv[1], argv[2]) &&
            MarcusRenderLocalWorld(player.get(), view, image, error), "Disabled native lights render safely");
        check(marcus_undo(player.get()), "Undo disabled lights");
        // Exercise more than one archive payload page, plus mixed-scene snapshots.
        auto many = board; many.angles.resize(8);
        check(MarcusApplyStoryboard(player.get(), many, true, 7, argv[1], argv[2]), "Create multi-page storyboard metadata");
        auto manyPath = path; manyPath.replace_filename(path.stem().u8string() + "-many.vmm");
        check(marcus_save_copy(player.get(), manyPath.u8string().c_str()) && marcus_open(player.get(), manyPath.u8string().c_str(), argv[1], argv[2]) &&
            marcus_select_scene(player.get(), 1) && MarcusGetStoryboard(player.get(), loaded) && loaded.angles.size() == 8, "Reopen paged multi-scene editor data");
        player.reset(); player.reset(marcus_create());
        auto ordinaryPath = path; ordinaryPath.replace_filename("ordinary-black.3mm");
        auto ordinaryStem = ordinaryPath; ordinaryStem.replace_extension();
        check(marcus_add_scene(player.get(), 11, argv[1], argv[2]) && !marcus_requires_vmm(player.get()) &&
            marcus_save_copy(player.get(), ordinaryStem.u8string().c_str()), "Ordinary movies default to native 3MM saving");
        auto ordinaryBundle = ordinaryPath; ordinaryBundle.replace_extension(".vmm");
        auto copiedBundle = ordinaryPath; copiedBundle.replace_filename("ordinary-copy.vmm");
        check(marcus_save_copy(player.get(), ordinaryBundle.u8string().c_str()) &&
            marcus_open(player.get(), ordinaryBundle.u8string().c_str(), argv[1], argv[2]) &&
            marcus_save_copy(player.get(), copiedBundle.u8string().c_str()), "Save a VMM without storyboard edits");
        auto extracted = ordinaryPath; extracted.replace_filename("ordinary-copy-inner.3mm");
        MM::V3dmm::Copy(MM::V3dmm::ReadBundle(copiedBundle).movie, extracted);
        MM::Archive ordinary;
        check(ordinary.Open(extracted.u8string(), readError), "Copied VMM contains a native movie rather than another VMM");
        std::puts("Storyboard strokes, validation, no-depth rendering, cameras, props, undo, reload and native save/reopen passed.");
        return 0;
    }
    catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
