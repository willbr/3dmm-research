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

        // Repeated frame additions and Undo must stay in the live evaluator.
        open();
        check(marcus_seek(player.get(), 30) && marcus_movie_time(player.get(), &timing), "Prime frame-add timing cache");
        std::vector<double> additions;
        for (int step = 1; step <= 100; ++step)
        {
            const auto start = Clock::now();
            check(marcus_add_frame(player.get(), 1), "Add live frame");
            additions.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            check(marcus_info(player.get(), &info) && info.frame == 30 + step && info.last_frame == 30 + step,
                  "Live frame range advances once per addition");
            check(marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds > timing.duration_seconds,
                  "Frame additions keep duration immediately available");
        }
        for (int step = 0; step < 64; ++step)
        {
            check(marcus_undo(player.get()), "Undo live frame addition");
            check(marcus_movie_time_async(player.get(), &after) == 1, "Frame undo retains timing analysis");
        }
        check(marcus_info(player.get(), &info) && info.last_frame == 66 && info.frame == 66 && !marcus_can_undo(player.get()),
              "Frame additions use bounded undo without losing the live range");
        std::sort(additions.begin(), additions.end());
        std::printf("100 live frame additions: median %.3f ms, max %.3f ms; ready duration retained\n",
                    additions[50], additions.back());
        check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "Render extended live frame");
        const std::vector<unsigned char> rangeImage(image.rgba, image.rgba + image.pitch * image.height);
        {
            std::unique_ptr<MarcusFramePreviews, decltype(&marcus_frame_previews_destroy)> helper(
                marcus_frame_previews_create(player.get()), marcus_frame_previews_destroy);
            check(bool(helper), "Materialize the complete live range once for a helper");
            const long frame = 66;
            marcus_frame_previews_request(helper.get(), &frame, 1);
            char error[1024] = {};
            int ready = 0;
            const auto deadline = Clock::now() + std::chrono::seconds(15);
            while (!(ready = marcus_frame_previews_take(helper.get(), &image, error, sizeof(error))) && Clock::now() < deadline)
                SDL_Delay(5);
            check(ready == 1 && image.frame == 66 && image.width == 160 && image.height == 90 &&
                  std::equal(rangeImage.begin(), rangeImage.end(), image.rgba), "Deferred helper snapshot retains the final range");
        }
        check(marcus_set_scene_camera(player.get(), 1) && marcus_info(player.get(), &info) && info.last_frame == 66,
              "Structural edits bake the live frame range");
        check(marcus_undo(player.get()) && marcus_info(player.get(), &info) && info.frame == 66 && info.last_frame == 66,
              "Undo structural edit restores the range journal");

        // An already-running timing helper analyzes the immutable base; import
        // must apply edits made while it was running rather than restart it.
        open();
        check(marcus_seek(player.get(), 30), "Seek before asynchronous range edits");
        check(marcus_movie_time_async(player.get(), &after) == 0, "Start base timing helper");
        for (int step = 0; step < 10; ++step) check(marcus_add_frame(player.get(), 1), "Edit during timing analysis");
        const auto timingDeadline = Clock::now() + std::chrono::seconds(20);
        int timingReady;
        while (!(timingReady = marcus_movie_time_async(player.get(), &after)) && Clock::now() < timingDeadline) SDL_Delay(5);
        check(timingReady == 1 && after.duration_seconds > timing.duration_seconds &&
              marcus_info(player.get(), &info) && info.frame == 40 && info.last_frame == 40,
              "Timing import applies the latest live range");

        // Structural editor actions must keep using the same live scene and
        // timing cache. Report warmed operations separately from asset loading.
        auto report = [&](const char *label, std::vector<double> times) {
            std::sort(times.begin(), times.end());
            std::printf("%s: median %.3f ms, max %.3f ms\n", label, times[times.size()/2], times.back());
        };
        open();
        check(marcus_movie_time(player.get(), &timing) && marcus_set_scene_camera(player.get(), 1) &&
              marcus_set_scene_camera(player.get(), 0), "Warm camera assets");
        std::vector<double> editTimes;
        for (int i = 0; i < 100; ++i)
        {
            const auto start = Clock::now();
            check(marcus_set_scene_camera(player.get(), i % 2), "Change live camera cue");
            editTimes.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            check(marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds == timing.duration_seconds,
                  "Camera edit preserves ready timing");
        }
        report("100 live camera edits", editTimes);
        for (int i = 0; i < 64; ++i) check(marcus_undo(player.get()), "Undo live camera edit");
        open();
        check(marcus_movie_time(player.get(), &timing) && marcus_info(player.get(), &info), "Prime insertion timing");
        const auto insertionSoundCount = info.sound_count;
        const auto insertionTrackCount = info.sound_track_count;
        const int warmProp = marcus_add_prop(player.get(), 0x1020);
        check(warmProp >= 0 && marcus_undo(player.get()), "Warm prop template");
        editTimes.clear();
        for (int i = 0; i < 20; ++i)
        {
            const auto start = Clock::now();
            check(marcus_add_prop(player.get(), 0x1020) >= 0, "Insert live prop instance");
            editTimes.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            check(marcus_info(player.get(), &info) && info.object_count == 5 + i && info.frame == 10 &&
                  marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds == timing.duration_seconds,
                  "Prop insertion retains frame and ready timing");
        }
        report("20 live prop insertions", editTimes);
        for (int i = 0; i < 20; ++i) check(marcus_undo(player.get()), "Undo live prop insertion");
        check(marcus_info(player.get(), &info) && info.object_count == 4 && !marcus_modified(player.get()) &&
              info.sound_count == insertionSoundCount && info.sound_track_count == insertionTrackCount,
              "Insertion undo restores base state without duplicating sound lanes");
        open();
        check(marcus_movie_time(player.get(), &timing), "Prime new-scene timing");
        MarcusBackground sceneBackground;
        check(marcus_background_count(player.get(), argv[3]) > 0 && marcus_background(player.get(), 0, &sceneBackground), "Read scene background");
        editTimes.clear();
        for (int i = 0; i < 10; ++i)
        {
            const auto start = Clock::now();
            check(marcus_add_scene(player.get(), sceneBackground.chunk, argv[2], argv[3]), "Create live scene");
            editTimes.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            check(marcus_info(player.get(), &info) && info.scene_count == 3 + i && info.scene == 2 + i &&
                  info.first_frame == 1 && info.last_frame == 1 &&
                  marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds > timing.duration_seconds,
                  "New scenes extend ready duration without reanalysis");
        }
        report("10 live scene creations", editTimes);
        for (int i = 0; i < 10; ++i) check(marcus_undo(player.get()), "Undo live scene creation");
        check(marcus_info(player.get(), &info) && info.scene_count == 2 && info.scene == 0 && info.frame == 10 &&
              !marcus_modified(player.get()), "Scene undo restores original selection and frame");

        auto checkNativeSnapshot = [&] {
            check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "Render live structural edits");
            const long frame = image.frame;
            const std::vector<unsigned char> expected(image.rgba, image.rgba + image.pitch * image.height);
            std::unique_ptr<MarcusFramePreviews, decltype(&marcus_frame_previews_destroy)> helper(
                marcus_frame_previews_create(player.get()), marcus_frame_previews_destroy);
            check(bool(helper), "Materialize live scene, prop and camera together");
            marcus_frame_previews_request(helper.get(), &frame, 1);
            char error[1024] = {};
            int ready = 0;
            const auto deadline = Clock::now() + std::chrono::seconds(20);
            while (!(ready = marcus_frame_previews_take(helper.get(), &image, error, sizeof(error))) && Clock::now() < deadline)
                SDL_Delay(5);
            check(ready == 1 && image.width == 160 && image.height == 90 &&
                  std::equal(expected.begin(), expected.end(), image.rgba), "Native snapshot matches the live structural edits");
        };
        // Undo leaves holes in object/scene IDs; the native snapshot must keep
        // those IDs and combine placement, range and camera journals correctly.
        check(marcus_add_scene(player.get(), sceneBackground.chunk, argv[2], argv[3]), "Create scene after undo");
        const int snapshotProp = marcus_add_prop(player.get(), 0x1020);
        check(snapshotProp >= 0 && marcus_object(player.get(), snapshotProp, &object) &&
              marcus_move_prop(player.get(), snapshotProp, object.x + 3, object.y, object.z) &&
              marcus_set_scene_camera(player.get(), 1) && marcus_add_frame(player.get(), -1) &&
              marcus_seek(player.get(), 1), "Compose live structural edits");
        checkNativeSnapshot();

        open();
        check(marcus_movie_time_async(player.get(), &after) == 0 &&
              marcus_add_scene(player.get(), sceneBackground.chunk, argv[2], argv[3]) &&
              marcus_add_prop(player.get(), 0x1020) >= 0 && marcus_undo(player.get()) &&
              marcus_add_prop(player.get(), 0x1020) >= 0 && marcus_set_scene_camera(player.get(), 1),
              "Create scene while base timing analysis runs");
        const auto sceneTimingDeadline = Clock::now() + std::chrono::seconds(20);
        while (!(timingReady = marcus_movie_time_async(player.get(), &after)) && Clock::now() < sceneTimingDeadline) SDL_Delay(5);
        check(timingReady == 1 && after.duration_seconds > timing.duration_seconds &&
              marcus_info(player.get(), &info) && info.scene == 2, "Base timing import includes a newly authored scene");

        // Headless camera regression: selection tracking must not chase edits.
        // Check scene/selection pivots, pan, perspective and all axis views.
        for (int axisView = 0; axisView <= 3; ++axisView)
            for (int selection = 0; selection <= 1; ++selection)
            {
                open();
                MarcusRenderOptions options = {};
                options.orthographic = axisView != 0;
                options.axis_view = axisView;
                check(marcus_set_render_options(player.get(), &options) &&
                      marcus_set_orbit_target(player.get(), selection ? 1 : -1), "Configure edit orbit");
                auto render = [&] { check(marcus_render(player.get(), 160, 90, 0, 25, -10, 1.2f, &image), "Render edit orbit"); };
                render();
                check(marcus_pan_orbit(player.get(), .08f, -.04f, 25, -10, 1.2f), "Pan edit orbit");
                render();
                const auto held = image.projection;
                const std::vector<unsigned char> original(image.rgba, image.rgba + image.pitch * image.height);
                auto stationary = [&] {
                    for (int i = 0; i < 12; ++i)
                        check(std::abs(image.projection.transform[i] - held.transform[i]) < .0001f, "Placement must retain orbit camera");
                    check(image.projection.vertical_fov == held.vertical_fov &&
                          image.projection.orthographic == held.orthographic && image.projection.ortho_height == held.ortho_height,
                          "Placement must retain orbit lens and scale");
                };
                for (int step = 1; step <= 3; ++step)
                {
                    check(marcus_preview_prop_position(player.get(), 1, before.x + step * 2.5f,
                          before.y - step, before.z + step * .75f), "Preview in held orbit");
                    render(); stationary();
                }
                check(marcus_preview_prop_position(player.get(), 1, before.x, before.y, before.z), "Cancel in held orbit");
                render(); stationary();
                check(std::equal(original.begin(), original.end(), image.rgba), "Cancel restores held-orbit image");
                check(marcus_preview_prop_position(player.get(), 1, before.x + 4, before.y - 2, before.z + 1), "Preview final orbit placement");
                render(); stationary();
                check(marcus_move_prop(player.get(), 1, before.x + 4, before.y - 2, before.z + 1), "Commit in held orbit");
                render(); stationary();
                check(marcus_undo(player.get()), "Undo in held orbit");
                render(); stationary();
                check(std::equal(original.begin(), original.end(), image.rgba), "Undo restores held-orbit image");
            }

        // Exercise the same placement journal for animated actors and 3D text.
        // These checks use only the headless player API, not application input.
        auto exerciseObject = [&](int objectIndex, int renderer) {
            open();
            check(marcus_set_renderer(player.get(), renderer), "Select object movement renderer");
            const MarcusRenderOptions options = {};
            check(marcus_set_render_options(player.get(), &options), "Reset object render options");
            const long frames[] = {1, 10, 20};
            MarcusObject poses[3] = {};
            for (int i = 0; i < 3; ++i)
                check(marcus_seek(player.get(), frames[i]) && marcus_object(player.get(), objectIndex, &poses[i]), "Read original object path");
            check(marcus_select_scene(player.get(), 1) && marcus_seek(player.get(), 10), "Read other object scene");
            MarcusObject otherScene;
            check(marcus_object(player.get(), objectIndex, &otherScene), "Read shared object placement");
            check(marcus_select_scene(player.get(), 0) && marcus_seek(player.get(), 10) &&
                  marcus_movie_time(player.get(), &timing), "Restore object edit frame");
            const auto start = poses[1];
            check(start.visible && (objectIndex == 0 ? !start.prop && !start.text : start.text != 0), "Actor/text fixture types");
            check(marcus_set_orbit_target(player.get(), objectIndex), "Orbit object before placement");
            auto render = [&] { check(marcus_render(player.get(), 160, 90, 0, 25, -10, 1.2f, &image), "Render moved object"); };
            render();
            const auto camera = image.projection;
            const std::vector<unsigned char> original(image.rgba, image.rgba + image.pitch * image.height);
            auto cameraHeld = [&] {
                for (int i = 0; i < 12; ++i)
                    check(std::abs(image.projection.transform[i] - camera.transform[i]) < .0001f, "Actor/text placement retains camera");
            };
            const float delta[3] = {3.5f, -1.25f, 2.75f};
            const float target[3] = {start.x + delta[0], start.y + delta[1], start.z + delta[2]};
            MarcusBounds initialBounds, movedBounds;
            check(marcus_object_bounds(player.get(), objectIndex, &initialBounds) && initialBounds.valid, "Original actor/text bounds");
            check(marcus_preview_object_position(player.get(), objectIndex, target[0], target[1], target[2]), "Preview actor/text placement");
            check(marcus_object_bounds(player.get(), objectIndex, &movedBounds) && movedBounds.valid, "Moved actor/text bounds");
            for (int i = 0; i < 24; ++i)
                check(std::abs(movedBounds.corners[i] - initialBounds.corners[i] - delta[i % 3]) < .001f,
                      "Actor/text bounds translate without changing pose");
            render(); cameraHeld();
            // Scenery can occlude this object from the inspection angle;
            // posed bounds above verify translation even when pixels match.
            check(marcus_preview_object_position(player.get(), objectIndex, start.x, start.y, start.z), "Cancel actor/text placement");
            render(); cameraHeld();
            check(std::equal(original.begin(), original.end(), image.rgba), "Cancelled actor/text image restored");
            check(!marcus_preview_object_position(player.get(), -1, 0, 0, 0) &&
                  !marcus_preview_object_position(player.get(), objectIndex, 1e30f, 0, 0), "Reject invalid object placement");
            check(marcus_preview_object_position(player.get(), objectIndex, target[0], target[1], target[2]) &&
                  marcus_move_object(player.get(), objectIndex, target[0], target[1], target[2]), "Commit actor/text placement");
            render(); cameraHeld();
            check(marcus_movie_time_async(player.get(), &after) == 1 && after.duration_seconds == timing.duration_seconds,
                  "Actor/text placement retains duration cache");
            for (int i = 0; i < 3; ++i)
            {
                MarcusObject moved;
                check(marcus_seek(player.get(), frames[i]) && marcus_object(player.get(), objectIndex, &moved), "Read moved object path");
                check(moved.visible == poses[i].visible && moved.action == poses[i].action && moved.cel == poses[i].cel &&
                      moved.first_frame == poses[i].first_frame && moved.last_frame == poses[i].last_frame,
                      "Actor/text placement retains animation and lifetime");
                if (moved.visible)
                    check(std::abs(moved.x - poses[i].x - delta[0]) < .001f &&
                          std::abs(moved.y - poses[i].y - delta[1]) < .001f &&
                          std::abs(moved.z - poses[i].z - delta[2]) < .001f, "Whole actor/text path remains translated");
            }
            check(marcus_select_scene(player.get(), 1) && marcus_seek(player.get(), 10) &&
                  marcus_object(player.get(), objectIndex, &object) && std::abs(object.x - otherScene.x) < .001f &&
                  std::abs(object.y - otherScene.y) < .001f && std::abs(object.z - otherScene.z) < .001f, "Actor/text edit stays scene-local");
            check(marcus_select_scene(player.get(), 0) && marcus_seek(player.get(), 10) &&
                  marcus_reload(player.get(), argv[2], argv[3]) && marcus_seek(player.get(), 10) &&
                  marcus_object(player.get(), objectIndex, &object) && std::abs(object.x - target[0]) < .001f &&
                  std::abs(object.y - target[1]) < .001f && std::abs(object.z - target[2]) < .001f, "Reload retains actor/text journal");
            if (renderer == MARCUS_RENDERER_BRENDER)
            {
                check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "Authored moved actor/text image");
                const std::vector<unsigned char> expected(image.rgba, image.rgba + image.pitch * image.height);
                std::unique_ptr<MarcusFramePreviews, decltype(&marcus_frame_previews_destroy)> helper(
                    marcus_frame_previews_create(player.get()), marcus_frame_previews_destroy);
                check(bool(helper), "Materialize actor/text placement");
                const long frame = 10;
                marcus_frame_previews_request(helper.get(), &frame, 1);
                char error[1024] = {};
                int ready = 0;
                const auto deadline = Clock::now() + std::chrono::seconds(15);
                while (!(ready = marcus_frame_previews_take(helper.get(), &image, error, sizeof(error))) && Clock::now() < deadline)
                    SDL_Delay(5);
                check(ready == 1 && image.width == 160 && image.height == 90 &&
                      std::equal(expected.begin(), expected.end(), image.rgba), "Native helper retains actor/text placement and template");
            }
            check(marcus_undo(player.get()) && marcus_object(player.get(), objectIndex, &object) &&
                  std::abs(object.x - start.x) < .001f && std::abs(object.y - start.y) < .001f &&
                  std::abs(object.z - start.z) < .001f && !marcus_modified(player.get()), "Undo restores actor/text placement");
        };
        exerciseObject(0, MARCUS_RENDERER_BRENDER);
        exerciseObject(3, MARCUS_RENDERER_BRENDER);

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
            const MarcusRenderOptions asmOptions = {};
            check(marcus_set_render_options(player.get(), &asmOptions), "Reset ASM render options");
            check(marcus_set_orbit_target(player.get(), 1) &&
                  marcus_render(player.get(), 160, 90, 0, 25, -10, 1.2f, &image), "Original ASM selection orbit");
            const auto asmOrbit = image.projection;
            check(marcus_preview_prop_position(player.get(), 1, before.x + 4, before.y - 2, before.z + 1) &&
                  marcus_render(player.get(), 160, 90, 0, 25, -10, 1.2f, &image), "Preview ASM selection placement");
            for (int i = 0; i < 12; ++i)
                check(std::abs(image.projection.transform[i] - asmOrbit.transform[i]) < .0001f,
                      "ASM preview must retain selection orbit camera");
            check(marcus_move_prop(player.get(), 1, before.x + 4, before.y - 2, before.z + 1) &&
                  marcus_render(player.get(), 160, 90, 0, 25, -10, 1.2f, &image), "Commit ASM selection placement");
            for (int i = 0; i < 12; ++i)
                check(std::abs(image.projection.transform[i] - asmOrbit.transform[i]) < .0001f,
                      "ASM commit must retain selection orbit camera");
            check(marcus_undo(player.get()) &&
                  marcus_render(player.get(), 160, 90, 0, 25, -10, 1.2f, &image), "Undo ASM selection placement");
            for (int i = 0; i < 12; ++i)
                check(std::abs(image.projection.transform[i] - asmOrbit.transform[i]) < .0001f,
                      "ASM undo must retain selection orbit camera");
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
            exerciseObject(0, MARCUS_RENDERER_BRENDER_ASM);
            exerciseObject(3, MARCUS_RENDERER_BRENDER_ASM);
            open();
            check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER_ASM) &&
                  marcus_select_scene(player.get(), 1) && marcus_seek(player.get(), 1), "Open ASM live-range scene");
            check(marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "ASM opening camera before extension");
            const auto opening = image.projection;
            check(marcus_add_frame(player.get(), -1) && marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image) &&
                  image.frame == 0, "ASM worker accepts prepended live frame");
            for (int i = 0; i < 12; ++i)
                check(image.projection.transform[i] == opening.transform[i], "ASM opening camera follows live range");
            check(marcus_seek(player.get(), 30) && marcus_add_frame(player.get(), 1) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image) && image.frame == 31,
                  "ASM worker accepts appended live frame");
            check(marcus_undo(player.get()) && marcus_undo(player.get()) && marcus_info(player.get(), &info) &&
                  info.first_frame == 1 && info.last_frame == 30 &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "ASM worker accepts range undo");
            check(marcus_seek(player.get(), 10), "Select ASM insertion frame");
            const int asmProp = marcus_add_prop(player.get(), 0x1020);
            check(asmProp >= 0 && marcus_set_scene_camera(player.get(), 1) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "ASM receives live props and camera edits");
            check(marcus_add_scene(player.get(), sceneBackground.chunk, argv[2], argv[3]) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image) &&
                  marcus_add_prop(player.get(), 0x1020) >= 0 && marcus_set_scene_camera(player.get(), 1) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "ASM receives a live scene and its prop");
            for (int i = 0; i < 5; ++i) check(marcus_undo(player.get()), "Undo ASM live structure");
            check(marcus_info(player.get(), &info) && info.scene_count == 2 && info.object_count == 4 &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "ASM accepts structural undo without reopening");
            player.reset(marcus_create());
            check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER_ASM) &&
                  marcus_add_scene(player.get(), sceneBackground.chunk, argv[2], argv[3]) &&
                  marcus_add_prop(player.get(), 0x1020) >= 0 && marcus_set_scene_camera(player.get(), 1) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "ASM supports a new movie without a base file");
            checkNativeSnapshot();
        }
        std::puts("Actor/prop/text moves and undo retain timing; scene isolation, helper materialization, structural edits, reload and ASM passed");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
