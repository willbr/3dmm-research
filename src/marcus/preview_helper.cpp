#include "preview_worker.h"
#include "player.h"
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
    if (argc != 3 && !duration) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    BrowserPreview preview;
    try
    {
        if (duration)
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
    return WritePreview(std::cout, preview) ? 0 : 1;
}
