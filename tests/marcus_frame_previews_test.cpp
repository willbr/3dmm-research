#include "player.h"
#include <SDL3/SDL.h>
#include <array>
#include <vector>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <algorithm>

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    using Player = std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)>;
    using Previews = std::unique_ptr<MarcusFramePreviews, decltype(&marcus_frame_previews_destroy)>;
    Player player(marcus_create(), marcus_destroy);
    auto check = [&](bool ok, const char *message) {
        if (!ok) throw std::runtime_error(std::string(message) + ": " + marcus_error(player.get()));
    };
    try
    {
        check(!marcus_frame_previews_create(player.get()), "Unopened player must reject frame previews");
        check(marcus_open(player.get(), argv[1], argv[2], argv[3]), "Open fixture");
        const std::array<long, 3> frames = {1, 15, 30};
        struct Expected { int width, height; std::vector<unsigned char> pixels; };
        std::array<Expected, 3> expected;
        MarcusImage image = {};
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            check(marcus_seek(player.get(), frames[i]) && marcus_render(player.get(), 160, 120, 1, 0, 0, 1, &image),
                  "Reference frame");
            // This fixture has an opaque background. Its nontransparent bounds
            // independently identify the fitted shot, including authored black.
            int left = image.width, top = image.height, right = 0, bottom = 0;
            for (int y = 0; y < image.height; ++y)
                for (int x = 0; x < image.width; ++x)
                    if (image.rgba[y * image.pitch + x * 4 + 3])
                    {
                        left = std::min(left, x); top = std::min(top, y);
                        right = std::max(right, x + 1); bottom = std::max(bottom, y + 1);
                    }
            expected[i].width = right - left;
            expected[i].height = bottom - top;
            check(expected[i].width == 160 && expected[i].height < 120, "Fixture must exercise letterbox removal");
            for (int y = top; y < bottom; ++y)
                expected[i].pixels.insert(expected[i].pixels.end(), image.rgba + y * image.pitch + left * 4,
                                          image.rgba + y * image.pitch + right * 4);
        }
        check(expected[0].pixels != expected[2].pixels, "Fixture must contain visibly different frames");
        Previews previews(marcus_frame_previews_create(player.get()), marcus_frame_previews_destroy);
        check(bool(previews), "Start previews");
        marcus_frame_previews_request(previews.get(), frames.data(), int(frames.size()));
        // The helper must retain its original scene while the live player moves.
        check(marcus_select_scene(player.get(), 1) && marcus_seek(player.get(), 10) &&
              marcus_render(player.get(), 160, 120, 1, 0, 0, 1, &image), "Live scene");
        const auto livePixels = image.rgba;
        const std::vector<unsigned char> live(livePixels, livePixels + image.pitch * image.height);
        std::array<bool, 3> received = {};
        int count = 0;
        char error[256] = {};
        const auto deadline = SDL_GetTicks() + 30000;
        while (count < 3 && SDL_GetTicks() < deadline)
        {
            const auto status = marcus_frame_previews_take(previews.get(), &image, error, sizeof(error));
            if (status < 0) throw std::runtime_error(error);
            if (!status) { SDL_Delay(5); continue; }
            std::size_t i = 0;
            while (i < frames.size() && frames[i] != image.frame) ++i;
            check(i < frames.size() && !received[i], "Requested frame identity");
            check(image.width == expected[i].width && image.height == expected[i].height && image.pitch == image.width * 4,
                  "Thumbnail dimensions exclude letterboxing");
            check(expected[i].pixels == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height),
                  "Helper pixels must match authored-camera rendering");
            received[i] = true;
            ++count;
            MarcusInfo info = {};
            check(marcus_info(player.get(), &info) && info.scene == 1 && info.frame == 10, "Live playhead unchanged");
            check(live == std::vector<unsigned char>(livePixels, livePixels + live.size()), "Live image unchanged");
        }
        check(count == 3, "Thumbnail completion");
        check(marcus_seek(player.get(), 1) && marcus_render(player.get(), 160, 120, 1, 0, 0, 1, &image),
              "Second-scene reference");
        std::vector<unsigned char> other;
        const int cropLeft = (image.width - expected[0].width) / 2;
        const int cropTop = (image.height - expected[0].height) / 2;
        for (int y = 0; y < expected[0].height; ++y)
        {
            const auto row = image.rgba + (y + cropTop) * image.pitch + cropLeft * 4;
            other.insert(other.end(), row, row + expected[0].width * 4);
        }
        check(other != expected[0].pixels, "Scenes must have different pixels at frame 1");
        check(marcus_seek(player.get(), 10), "Restore live playhead");
        const MarcusFramePosition acrossScenes[] = {{1, 1}, {0, 1}};
        marcus_frame_previews_request_positions(previews.get(), acrossScenes, 2);
        std::array<bool, 2> scenesReceived = {};
        const auto movieDeadline = SDL_GetTicks() + 30000;
        int movieCount = 0;
        while (movieCount < 2 && SDL_GetTicks() < movieDeadline)
        {
            const auto status = marcus_frame_previews_take(previews.get(), &image, error, sizeof(error));
            if (status < 0) throw std::runtime_error(error);
            if (!status) { SDL_Delay(5); continue; }
            check(image.scene >= 0 && image.scene < 2 && image.frame == 1 && !scenesReceived[image.scene],
                  "Cross-scene thumbnail identity");
            const auto &pixels = image.scene == 0 ? expected[0].pixels : other;
            check(image.width == expected[0].width && image.height == expected[0].height &&
                  pixels == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height),
                  "Cross-scene pixels must match the requested scene");
            scenesReceived[image.scene] = true;
            ++movieCount;
            MarcusInfo info = {};
            check(marcus_info(player.get(), &info) && info.scene == 1 && info.frame == 10,
                  "Whole-movie previews leave the live scene and frame unchanged");
        }
        check(movieCount == 2, "Cross-scene preview completion");
        const long invalid = 31;
        marcus_frame_previews_request(previews.get(), &invalid, 1);
        bool failed = false;
        const auto errorDeadline = SDL_GetTicks() + 10000;
        while (SDL_GetTicks() < errorDeadline)
        {
            if (marcus_frame_previews_take(previews.get(), &image, error, sizeof(error)) < 0)
            { failed = true; break; }
            SDL_Delay(5);
        }
        check(failed && error[0], "Invalid frame reports a helper error");
        previews.reset();
        check(marcus_open(player.get(), argv[4], argv[2], argv[3]), "Open ZIP fixture");
        previews.reset(marcus_frame_previews_create(player.get()));
        check(bool(previews), "Start ZIP preview");
        marcus_frame_previews_request(previews.get(), frames.data(), 1);
        // Reopening releases the player's extraction lease, while the job keeps its own.
        check(marcus_open(player.get(), argv[1], argv[2], argv[3]), "Reopen during ZIP preview");
        const auto leaseDeadline = SDL_GetTicks() + 30000;
        bool leaseReady = false;
        while (SDL_GetTicks() < leaseDeadline)
        {
            const auto status = marcus_frame_previews_take(previews.get(), &image, error, sizeof(error));
            if (status < 0) throw std::runtime_error(error);
            if (status > 0)
            {
                check(image.frame == 1 && image.width == expected[0].width && image.height == expected[0].height &&
                      expected[0].pixels == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height),
                      "ZIP lease retains original movie pixels");
                leaseReady = true;
                break;
            }
            SDL_Delay(5);
        }
        check(leaseReady, "ZIP preview completes after reopening");
        previews.reset();
        // Closing during startup/rendering must cancel rather than await a full movie load.
        const auto cancelStarted = SDL_GetTicks();
        previews.reset(marcus_frame_previews_create(player.get()));
        check(bool(previews), "Start cancellation preview");
        marcus_frame_previews_request(previews.get(), frames.data(), int(frames.size()));
        previews.reset();
        check(SDL_GetTicks() - cancelStarted < 5000, "Prompt cancellation");
        puts("Frame previews: authored pixels, scene isolation, live image and ZIP leases, errors and cancellation passed.");
        return 0;
    }
    catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
