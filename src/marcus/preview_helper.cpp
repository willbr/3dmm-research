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
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv)
{
    const bool duration = argc == 5 && !std::strcmp(argv[3], "--duration");
    const bool timeline = argc == 6 && !std::strcmp(argv[3], "--timing");
    if (argc != 3 && !duration && !timeline) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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
