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
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
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
int FramePreviews(char **argv)
{
    try
    {
        std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
        if (!player) throw std::runtime_error("Cannot initialize frame thumbnails.");
        marcus_set_muted(player.get(), 1);
        std::size_t consumed;
        const auto scene = std::stoi(argv[5], &consumed);
        if (argv[5][consumed] || scene < 0) throw std::runtime_error("Invalid frame thumbnail scene.");
        if (!marcus_open(player.get(), argv[1], argv[2], argv[4]) ||
            !marcus_select_scene(player.get(), scene)) throw std::runtime_error(marcus_error(player.get()));
        long long frame;
        while (std::cin >> frame)
        {
            BrowserPreview preview;
            MarcusImage image = {};
            if (frame < (std::numeric_limits<long>::min)() || frame > (std::numeric_limits<long>::max)() ||
                !marcus_seek(player.get(), static_cast<long>(frame)) ||
                !marcus_render(player.get(), 160, 120, 1, 0, 0, 1, &image))
                preview.error = marcus_error(player.get())[0] ? marcus_error(player.get()) : "Invalid preview frame.";
            else
            {
                preview.width = image.width;
                preview.height = image.height;
                preview.rgba.assign(image.rgba, image.rgba + image.pitch * image.height);
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
    const bool duration = argc == 5 && !std::strcmp(argv[3], "--duration");
    const bool timeline = argc == 6 && !std::strcmp(argv[3], "--timing");
    const bool frames = argc == 6 && !std::strcmp(argv[3], "--frames");
    if (argc != 3 && !duration && !timeline && !frames) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    if (frames) _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (frames) return FramePreviews(argv);
    BrowserPreview preview;
    try
    {
        if (duration || timeline)
        {
            const auto started = std::chrono::steady_clock::now();
            auto player = marcus_create();
            if (!player) preview.error = "Cannot initialize duration analysis.";
            else
            {
                marcus_set_muted(player, 1);
                MarcusMovieTime timing = {};
                if (!marcus_open(player, argv[1], argv[2], argv[4]) || !marcus_movie_time(player, &timing))
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
