#include "timing_job.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <stdexcept>

MarcusTimingJob::MarcusTimingJob(std::shared_ptr<MarcusMovieFile> movie, std::string resources, std::string content,
                                 std::vector<MarcusScene> scenes)
{
    // Resolve the sibling executable on the owner thread.
    const auto base = SDL_GetBasePath();
    const auto executable = base ? std::string(base) + "marcus-preview"
#ifdef _WIN32
                                                       ".exe"
#endif
                                 : std::string();
    thread = std::thread(
        [this, movie = std::move(movie), resources = std::move(resources), content = std::move(content),
         scenes = std::move(scenes), executable] {
            std::filesystem::path staging;
            SDL_Process *process = nullptr;
            try
            {
                if (executable.empty())
                    throw std::runtime_error("Cannot locate the timing helper.");
                // Atomically reserve a private directory; never reuse an existing file.
                const auto root = std::filesystem::temp_directory_path();
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                for (unsigned attempt = 0; attempt < 100; ++attempt)
                {
                    const auto candidate =
                        root / ("marcus-timing-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                    if (std::filesystem::create_directory(candidate))
                    {
                        staging = candidate;
                        break;
                    }
                }
                if (staging.empty())
                    throw std::runtime_error("Cannot reserve timing output.");
                const auto output = (staging / "timing.bin").u8string();
                const auto input = movie->path.u8string();
                const char *args[] = {executable.c_str(), input.c_str(), resources.c_str(), "--timing", content.c_str(),
                                      output.c_str(),     nullptr};
                const auto properties = SDL_CreateProperties();
                SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
                SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
                SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
                SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
                SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
                process = SDL_CreateProcessWithProperties(properties);
                SDL_DestroyProperties(properties);
                if (!process)
                    throw std::runtime_error("Cannot launch marcus-preview for timing.");
                const auto deadline = SDL_GetTicks() + 300000;
                int status = -1;
                while (!SDL_WaitProcess(process, false, &status))
                {
                    if (cancel || SDL_GetTicks() >= deadline)
                        throw std::runtime_error("Movie timing cancelled or timed out.");
                    SDL_Delay(5);
                }
                if (status)
                    throw std::runtime_error("Movie timing analysis failed.");
                const auto size = std::filesystem::file_size(std::filesystem::u8path(output));
                if (!size || size > 64u * 1024 * 1024)
                    throw std::runtime_error("Movie timing output exceeds its limit.");
                std::ifstream stream(std::filesystem::u8path(output), std::ios::binary);
                result = std::make_unique<MarcusSoundTimeline>();
                if (!result->Read(stream, scenes))
                    throw std::runtime_error("Invalid movie timing output.");
            }
            catch (const std::exception &error)
            {
                failure = error.what();
                result.reset();
            }
            if (process)
            {
                int status;
                if (!SDL_WaitProcess(process, false, &status))
                {
                    SDL_KillProcess(process, true);
                    SDL_WaitProcess(process, true, &status);
                }
                SDL_DestroyProcess(process);
            }
            if (!staging.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(staging / "timing.bin", ignored);
                std::filesystem::remove(staging, ignored);
            }
            done.store(true, std::memory_order_release);
        });
}
MarcusTimingJob::~MarcusTimingJob()
{
    cancel = true;
    if (thread.joinable())
        thread.join();
}
bool MarcusTimingJob::Take(std::unique_ptr<MarcusSoundTimeline> &timeline, std::string &error)
{
    if (!done.load(std::memory_order_acquire))
        return false;
    timeline = std::move(result);
    error = std::move(failure);
    return true;
}
