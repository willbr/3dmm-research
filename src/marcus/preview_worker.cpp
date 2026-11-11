#include "preview_worker.h"
#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <stdexcept>

BrowserPreview RunPreviewWorker(const std::string &movie, const std::string &resources,
                               const std::function<bool()> &cancelled)
{
    const auto base = SDL_GetBasePath();
    if (!base) throw std::runtime_error("Cannot locate the preview helper.");
    const auto executable = std::string(base) + "marcus-preview"
#ifdef _WIN32
        ".exe"
#endif
        ;
    const char *args[] = {executable.c_str(), movie.c_str(), resources.c_str(), nullptr};
    const auto properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    auto process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process) throw std::runtime_error("Cannot launch marcus-preview. Keep it beside Marcus.");
    const auto destroy = [](SDL_Process *value) {
        int status;
        if (!SDL_WaitProcess(value, false, &status))
        { SDL_KillProcess(value, true); SDL_WaitProcess(value, true, &status); }
        SDL_DestroyProcess(value);
    };
    std::unique_ptr<SDL_Process, decltype(destroy)> owner(process, destroy);
    const auto stream = SDL_GetProcessOutput(process);
    if (!stream) throw std::runtime_error("Cannot read the preview helper output.");
    const auto deadline = SDL_GetTicks() + 30000;
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 65536> buffer;
    for (;;)
    {
        if (cancelled() || SDL_GetTicks() >= deadline) throw std::runtime_error("Preview cancelled or timed out.");
        const auto count = SDL_ReadIO(stream, buffer.data(), buffer.size());
        if (!count)
        {
            if (SDL_GetIOStatus(stream) == SDL_IO_STATUS_NOT_READY) { SDL_Delay(5); continue; }
            break;
        }
        if (bytes.size() + count > 5 * 1024 * 1024) throw std::runtime_error("Preview helper output exceeds its limit.");
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
    }
    int status = -1;
    // A helper can close stdout before exiting; never wait indefinitely here.
    while (!SDL_WaitProcess(process, false, &status))
    {
        if (cancelled() || SDL_GetTicks() >= deadline) throw std::runtime_error("Preview cancelled or timed out.");
        SDL_Delay(5);
    }
    if (status) throw std::runtime_error("The movie preview could not be decoded.");
    return ReadPreview(bytes);
}
