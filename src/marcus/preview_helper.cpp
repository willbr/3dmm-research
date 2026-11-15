#include "preview_worker.h"
#include "player.h"
#include "timing_job.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <memory>
#include <limits>
#include <algorithm>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
bool OpenPlayer(MarcusPlayer *player, char **argv, const char *fonts, const char *origin = nullptr)
{
    MarcusMovieInput input;
    input.Open(argv[1]);
    auto file = input.Materialize(0);
    if (origin && *origin) file->contentOrigin = std::filesystem::u8path(origin);
    if (fonts && *fonts) file->LoadFonts(std::filesystem::u8path(fonts));
    return marcus_open_input(player, &input, 0, argv[2], argv[4]) != 0;
}
bool FrameResponse(const BrowserPreview &preview)
{
    std::ostringstream payload(std::ios::binary);
    if (!WritePreview(payload, preview)) return false;
    const auto bytes = payload.str();
    for (int i = 0; i < 4; ++i) std::cout.put(static_cast<char>(bytes.size() >> (i * 8)));
    std::cout.write(bytes.data(), bytes.size());
    std::cout.flush();
    return bool(std::cout);
}
int FramePreviews(char **argv, const char *fonts, const char *origin)
{
    try
    {
        std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
        if (!player) throw std::runtime_error("Cannot initialize frame thumbnails.");
        marcus_set_muted(player.get(), 1);
        std::size_t consumed;
        const auto scene = std::stoi(argv[5], &consumed);
        if (argv[5][consumed] || scene < 0) throw std::runtime_error("Invalid frame thumbnail scene.");
        if (!OpenPlayer(player.get(), argv, fonts, origin) ||
            !marcus_select_scene(player.get(), scene)) throw std::runtime_error(marcus_error(player.get()));
        int requestedScene, selectedScene = scene;
        long long frame;
        while (std::cin >> requestedScene >> frame)
        {
            BrowserPreview preview;
            MarcusImage image = {};
            bool selected = true;
            if (requestedScene != selectedScene)
            {
                selected = marcus_select_scene(player.get(), requestedScene) != 0;
                if (selected) selectedScene = requestedScene;
            }
            if (!selected || frame < (std::numeric_limits<long>::min)() || frame > (std::numeric_limits<long>::max)() ||
                !marcus_seek(player.get(), static_cast<long>(frame)) ||
                !marcus_render(player.get(), 160, 120, 1, 0, 0, 1, &image))
                preview.error = marcus_error(player.get())[0] ? marcus_error(player.get()) : "Invalid preview frame.";
            else
            {
                preview.width = image.width;
                preview.height = image.height;
                // Remove only the renderer's fitted-camera canvas, never black
                // pixels authored inside the shot (or empty space around actors).
                const auto aspect = image.scene_camera.aspect;
                if (image.scene_camera.valid && std::isfinite(aspect) && aspect > 0)
                {
                    if (aspect >= float(image.width) / image.height)
                        preview.height = std::clamp(int(std::floor(image.width / aspect + 0.001f)), 1, image.height);
                    else
                        preview.width = std::clamp(int(std::floor(image.height * aspect + 0.001f)), 1, image.width);
                }
                const int left = (image.width - preview.width) / 2, top = (image.height - preview.height) / 2;
                preview.rgba.reserve(preview.width * preview.height * 4);
                for (int y = 0; y < preview.height; ++y)
                {
                    const auto row = image.rgba + (y + top) * image.pitch + left * 4;
                    preview.rgba.insert(preview.rgba.end(), row, row + preview.width * 4);
                }
            }
            if (!FrameResponse(preview)) return 1;
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        BrowserPreview preview;
        preview.error = e.what();
        FrameResponse(preview);
        return 1;
    }
}
}
int main(int argc, char **argv)
{
    const bool duration = (argc == 5 || argc == 6) && !std::strcmp(argv[3], "--duration");
    const bool timeline = (argc == 6 || argc == 8 || argc == 9 || argc == 10) && !std::strcmp(argv[3], "--timing");
    const bool frames = (argc == 6 || argc == 7 || argc == 8) && !std::strcmp(argv[3], "--frames");
    const bool background = argc == 5 && !std::strcmp(argv[3], "--background");
    if (argc != 3 && !duration && !timeline && !frames && !background) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    if (frames) _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (frames) return FramePreviews(argv, argc >= 7 ? argv[6] : nullptr, argc == 8 ? argv[7] : nullptr);
    BrowserPreview preview;
    try
    {
        if (background)
        {
            std::size_t end;
            const auto chunk = std::stoul(argv[1], &end);
            std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
            MarcusImage image = {};
            if (!player || argv[1][end] || !marcus_add_scene(player.get(), chunk, argv[2], argv[4]) ||
                !marcus_render(player.get(), 480, 270, 1, 0, 0, 1, &image))
                preview.error = marcus_error(player.get());
            else
            {
                preview.width = image.width;
                preview.height = image.height;
                preview.rgba.assign(image.rgba, image.rgba + image.pitch * image.height);
            }
        }
        else if (duration || timeline)
        {
            const auto started = std::chrono::steady_clock::now();
            auto player = marcus_create();
            if (!player) preview.error = "Cannot initialize duration analysis.";
            else
            {
                marcus_set_muted(player, 1);
                MarcusMovieTime timing = {};
                if (timeline && argc >= 8)
                {
                    std::size_t rateEnd, modifierEnd;
                    const int rate = std::stoi(argv[6], &rateEnd), modifier = std::stoi(argv[7], &modifierEnd);
                    if (argv[6][rateEnd] || argv[7][modifierEnd] || !marcus_set_playback_rate(player, rate, modifier))
                    {
                        marcus_destroy(player);
                        return 2;
                    }
                }
                const char *fonts = timeline && argc >= 9 ? argv[8] : duration && argc == 6 ? argv[5] : nullptr;
                if (!OpenPlayer(player, argv, fonts, timeline && argc == 10 ? argv[9] : nullptr) || !marcus_movie_time(player, &timing))
                    preview.error = marcus_error(player);
                else
                {
                    if (timeline)
                    {
                        std::ofstream output(std::filesystem::u8path(argv[5]), std::ios::binary);
                        const bool written = MarcusWriteTiming(player, output);
                        output.close();
                        marcus_destroy(player);
                        return written && !output.fail() ? 0 : 1;
                    }
                    const auto seconds = static_cast<long long>(std::llround(timing.duration_seconds));
                    char value[192];
                    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                    if (seconds >= 3600)
                        std::snprintf(value, sizeof(value), "Calculated %lld:%02lld:%02lld\nAnalysis %.1fs; click waits excluded", seconds / 3600, seconds / 60 % 60, seconds % 60, elapsed);
                    else
                        std::snprintf(value, sizeof(value), "Calculated %lld:%02lld\nAnalysis %.1fs; click waits excluded", seconds / 60, seconds % 60, elapsed);
                    preview.details = value;
                }
                marcus_destroy(player);
            }
        }
        else preview = ReadBrowserPreview(argv[1], argv[2]);
    }
    catch (const std::exception &e) { preview.error = e.what(); }
    if (timeline) return 1;
    return WritePreview(std::cout, preview) ? 0 : 1;
}
