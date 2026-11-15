// Headless prop-edit performance/cache and lazy-materialization checks.
#include "player.h"
#include "asm_renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    using Clock = std::chrono::steady_clock;
    std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
    auto check = [&](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(std::string(message) + ": " + marcus_error(player.get()));
    };
    auto open = [&] { check(marcus_open(player.get(), argv[1], argv[2], argv[3]) && marcus_seek(player.get(), 10), "Open fixture"); };
    MarcusObject before = {}, object = {};
    MarcusInfo info = {};
    MarcusMovieTime timing = {}, after = {};
    MarcusImage image = {};
    try
    {
        open();
        check(marcus_object(player.get(), 1, &before), "Read prop");
        check(marcus_movie_time(player.get(), &timing), "Prime duration cache");
        std::vector<double> releases;
        for (int i = 1; i <= 100; ++i)
        {
            check(marcus_preview_prop_position(player.get(), 1, before.x + i * .01f, before.y, before.z), "Drag preview");
            const auto start = Clock::now();
            check(marcus_move_prop(player.get(), 1, before.x + i * .01f, before.y, before.z), "Commit drag");
            releases.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            check(marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds == timing.duration_seconds &&
                  after.position_seconds == timing.position_seconds, "Release must retain ready duration cache");
            check(marcus_info(player.get(), &info) && info.frame == 10 && info.scene == 0, "Release must not seek");
        }
        for (int i = 0; i < 64; ++i)
        {
            check(marcus_undo(player.get()), "Undo placement");
            check(marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds == timing.duration_seconds,
                  "Placement undo must retain duration cache");
        }
        check(!marcus_can_undo(player.get()) && marcus_modified(player.get()), "Bounded history retains modified baseline");
        check(marcus_object(player.get(), 1, &object) && std::abs(object.x - before.x - .36f) < .001f, "Restore retained baseline");
        std::sort(releases.begin(), releases.end());
        std::printf("100 prop releases: median %.3f ms, max %.3f ms; ready duration retained\n", releases[50], releases.back());

        // The same ACTR is shared by two scenes. Baking a snapshot for a helper
        // or a structural edit must retain both independent scene placements.
        open();
        check(marcus_move_prop(player.get(), 1, before.x + 5, before.y, before.z), "Move in scene one");
        check(marcus_select_scene(player.get(), 1) && marcus_seek(player.get(), 10), "Select scene two");
        MarcusObject other;
        check(marcus_object(player.get(), 1, &other), "Read shared prop");
        check(marcus_move_prop(player.get(), 1, other.x + 11, other.y, other.z), "Move in scene two");
        check(marcus_select_scene(player.get(), 0) && marcus_seek(player.get(), 10), "Return to scene one");
        check(marcus_object(player.get(), 1, &object) && std::abs(object.x - before.x - 5) < .001f, "Scene-local placement retained");
        check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "Live moved image");
        const std::vector<unsigned char> expected(image.rgba, image.rgba + image.pitch * image.height);
        std::unique_ptr<MarcusFramePreviews, decltype(&marcus_frame_previews_destroy)> previews(
            marcus_frame_previews_create(player.get()), marcus_frame_previews_destroy);
        check(bool(previews), "Materialize moved frame helper input");
        const long frame = 10;
        marcus_frame_previews_request(previews.get(), &frame, 1);
        char error[1024] = {};
        int ready = 0;
        const auto deadline = Clock::now() + std::chrono::seconds(15);
        while (!(ready = marcus_frame_previews_take(previews.get(), &image, error, sizeof(error))) && Clock::now() < deadline)
            SDL_Delay(5);
        check(ready == 1 && image.width == 160 && image.height == 90 &&
              std::equal(expected.begin(), expected.end(), image.rgba), "Helper must render committed placements");
        previews.reset();
        MarcusBackground background;
        check(marcus_background_count(player.get(), argv[3]) > 0 && marcus_background(player.get(), 0, &background), "Background catalogue");
        check(marcus_add_scene(player.get(), background.chunk, argv[2], argv[3]), "Bake placements before structural edit");
        check(marcus_select_scene(player.get(), 0) && marcus_seek(player.get(), 10) && marcus_object(player.get(), 1, &object) &&
              std::abs(object.x - before.x - 5) < .001f, "Baked scene one placement");
        check(marcus_select_scene(player.get(), 1) && marcus_seek(player.get(), 10) && marcus_object(player.get(), 1, &object) &&
              std::abs(object.x - other.x - 11) < .001f, "Baked scene two placement");
        check(marcus_undo(player.get()) && marcus_info(player.get(), &info) && info.scene_count == 2, "Undo structural edit to placement snapshot");
        check(marcus_reload(player.get(), argv[2], argv[3]) && marcus_seek(player.get(), 10), "Reload journal");
        check(marcus_object(player.get(), 1, &object) && std::abs(object.x - before.x - 5) < .001f, "Reload retains placement");
        check(marcus_undo(player.get()) && marcus_undo(player.get()) && !marcus_modified(player.get()), "Undo all placements");

        // A late-entering prop must keep its placement when selecting its scene
        // before its entrance, then seeking forward into its visible lifetime.
        open();
        const int added = marcus_add_prop(player.get(), 0x1020);
        check(added >= 0 && marcus_object(player.get(), added, &object), "Insert late prop");
        const float target = object.x + 7;
        check(marcus_move_prop(player.get(), added, target, object.y, object.z), "Place late prop");
        check(marcus_select_scene(player.get(), 1) && marcus_select_scene(player.get(), 0) &&
              marcus_object(player.get(), added, &object) && !object.visible, "Restore placement before entrance");
        check(marcus_seek(player.get(), 10) && marcus_object(player.get(), added, &object) && object.visible &&
              std::abs(object.x - target) < .001f, "Late entrance retains placement");

        MM::Error asmError;
        if (MarcusASMRenderer::Available(asmError))
        {
            open();
            check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER_ASM), "Select ASM renderer");
            check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "Original ASM image");
            const std::vector<unsigned char> original(image.rgba, image.rgba + image.pitch * image.height);
            check(marcus_move_prop(player.get(), 1, before.x + 5, before.y, before.z), "Commit ASM placement");
            check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image) &&
                  !std::equal(original.begin(), original.end(), image.rgba), "ASM worker receives live placement journal");
            const std::vector<unsigned char> moved(image.rgba, image.rgba + image.pitch * image.height);
            check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image) &&
                  std::equal(moved.begin(), moved.end(), image.rgba), "Repeated IPC placement must not accumulate");
            check(marcus_undo(player.get()) && marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image) &&
                  std::equal(original.begin(), original.end(), image.rgba), "ASM placement undo restores exact image");
        }
        std::puts("Prop moves/undo retain timing; scene isolation, helper materialization, structural edits, reload and ASM passed");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
