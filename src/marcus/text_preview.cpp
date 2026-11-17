#include "text_preview.h"
#include "storyboard.h"
#include "preview_worker.h"
#include "colour_picker.h"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

/* Only native template, process and IPC services live here. Camera/UI policy
 * remains in text_dialog.c; the renderer stays isolated from the live player. */
struct MarcusTextPreview
{
    struct Request
    {
        std::shared_ptr<MarcusMovieFile> file;
        std::string resources;
        float yaw = 20, pitch = -10, zoom = 1;
        unsigned long generation = 0;
    };
    std::atomic<bool> exiting{false};
    std::atomic<unsigned long> generation{0};
    std::mutex mutex;
    std::condition_variable wake;
    Request desired;
    bool requested = false, available = false;
    BrowserPreview ready, output;
    std::string error;
    std::thread thread;

    MarcusTextPreview() : thread([this] { Run(); }) {}
    ~MarcusTextPreview()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            exiting = true;
        }
        wake.notify_one();
        thread.join();
    }
    void Stop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++generation;
        desired = {};
        requested = available = false;
        ready = {}; output = {}; error.clear();
        wake.notify_one();
    }
    void Transfer(SDL_IOStream *stream, void *data, std::size_t size, bool writing,
                  unsigned long revision, Uint64 deadline)
    {
        auto bytes = static_cast<unsigned char *>(data);
        while (size)
        {
            if (exiting || revision != generation || SDL_GetTicks() >= deadline)
                throw std::runtime_error("Text preview cancelled or timed out.");
            const auto count = writing ? SDL_WriteIO(stream, bytes, size) : SDL_ReadIO(stream, bytes, size);
            if (!count)
            {
                if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_NOT_READY)
                    throw std::runtime_error("The text preview helper stopped unexpectedly.");
                SDL_Delay(1);
            }
            bytes += count;
            size -= count;
        }
    }
    void Run()
    {
        SDL_Process *process = nullptr;
        unsigned long loaded = 0;
        std::shared_ptr<MarcusMovieFile> activeFile;
        const auto close = [&] {
            if (process)
            {
                int status;
                if (!SDL_WaitProcess(process, false, &status))
                { SDL_KillProcess(process, true); SDL_WaitProcess(process, true, &status); }
                SDL_DestroyProcess(process);
                process = nullptr;
            }
            activeFile.reset();
        };
        for (;;)
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                // Generation changes also wake an idle process for cleanup.
                wake.wait(lock, [&] { return exiting || requested || (process && loaded != generation); });
                if (exiting) break;
                if (!requested) { lock.unlock(); close(); continue; }
                request = desired;
                requested = false;
            }
            BrowserPreview result;
            try
            {
                if (!process || loaded != request.generation)
                {
                    close();
                    const auto base = SDL_GetBasePath();
                    if (!base) throw std::runtime_error("Cannot locate the text preview helper.");
                    const auto executable = std::string(base) + "marcus-preview"
#ifdef _WIN32
                        ".exe"
#endif
                        ;
                    const auto path = request.file->path.u8string();
                    const char *args[] = {executable.c_str(), path.c_str(), request.resources.c_str(), "--text-live", nullptr};
                    const auto properties = SDL_CreateProperties();
                    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
                    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
                    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
                    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
                    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
                    process = SDL_CreateProcessWithProperties(properties);
                    SDL_DestroyProperties(properties);
                    if (!process) throw std::runtime_error("Cannot launch the text preview helper.");
                    loaded = request.generation;
                    activeFile = request.file;
                }
                const auto input = SDL_GetProcessInput(process), outputStream = SDL_GetProcessOutput(process);
                if (!input || !outputStream) throw std::runtime_error("Cannot communicate with the text preview helper.");
                std::ostringstream camera;
                camera << request.yaw << ' ' << request.pitch << ' ' << request.zoom << '\n';
                auto command = camera.str();
                const auto deadline = SDL_GetTicks() + 30000;
                Transfer(input, command.data(), command.size(), true, request.generation, deadline);
                std::array<unsigned char, 4> header;
                Transfer(outputStream, header.data(), header.size(), false, request.generation, deadline);
                std::uint32_t size = 0;
                for (int i = 0; i < 4; ++i) size |= std::uint32_t(header[i]) << (i * 8);
                if (size < 24 || size > 1024 * 1024) throw std::runtime_error("Text preview response exceeds its limit.");
                std::vector<std::uint8_t> bytes(size);
                Transfer(outputStream, bytes.data(), bytes.size(), false, request.generation, deadline);
                result = ReadPreview(bytes);
                if (result.error.empty() && (result.width != 480 || result.height != 360))
                    throw std::runtime_error("Invalid text preview dimensions.");
            }
            catch (const std::exception &e) { result.error = e.what(); close(); }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!exiting && request.generation == generation)
                { ready = std::move(result); available = true; }
            }
        }
        close();
    }
};

extern "C" {
MarcusTextPreview *text_preview_create(void)
{
    try { return new MarcusTextPreview; } catch (...) { return nullptr; }
}
void text_preview_destroy(MarcusTextPreview *preview) { delete preview; }
void text_preview_stop(MarcusTextPreview *preview) { if (preview) preview->Stop(); }
const char *text_preview_error(const MarcusTextPreview *preview)
{ return preview ? preview->error.c_str() : "Cannot allocate the preview worker."; }
int text_preview_start(MarcusTextPreview *preview, MarcusPlayer *player, int object,
                       const MarcusText *text, const char *resources, float yaw, float pitch, float zoom)
{
    if (!preview || !text || !resources) return 0;
    preview->Stop();
    try
    {
        auto file = MarcusPrepareTextPreview(player, object, *text);
        if (!file) { preview->error = marcus_error(player); return 0; }
        std::lock_guard<std::mutex> lock(preview->mutex);
        preview->desired = {std::move(file), resources, yaw, pitch, zoom, preview->generation.load()};
        preview->requested = true;
        preview->wake.notify_one();
        return 1;
    }
    catch (const std::exception &e) { preview->error = e.what(); }
    catch (...) { preview->error = "Cannot start the text preview."; }
    return 0;
}
int text_preview_view(MarcusTextPreview *preview, float yaw, float pitch, float zoom)
{
    if (!preview) return 0;
    std::lock_guard<std::mutex> lock(preview->mutex);
    auto &request = preview->desired;
    if (!request.file) return 0;
    if (request.yaw != yaw || request.pitch != pitch || request.zoom != zoom)
    {
        request.yaw = yaw; request.pitch = pitch; request.zoom = zoom;
        preview->requested = true;
        preview->wake.notify_one();
    }
    return 1;
}
int text_preview_poll(MarcusTextPreview *preview, MarcusImage *image)
{
    if (!preview || !image) return 0;
    std::lock_guard<std::mutex> lock(preview->mutex);
    if (!preview->available) return 0;
    preview->available = false;
    preview->output = std::move(preview->ready);
    preview->error = preview->output.error;
    if (!preview->error.empty()) return -1;
    *image = {};
    image->width = preview->output.width; image->height = preview->output.height;
    image->pitch = image->width * 4; image->rgba = preview->output.rgba.data();
    return 1;
}
int text_preview_palette(MarcusPlayer *player, unsigned rgb[256], float hue[256])
{
    if (!rgb || !hue) return 0;
    try
    {
        MarcusBoardPalette palette = {};
        if (!MarcusGetStoryboardPalette(player, palette)) return 0;
        for (unsigned i = 0; i < palette.size(); ++i)
        { rgb[i] = palette[i]; hue[i] = MarcusToOkHSL(palette[i])[0]; }
        return 1;
    }
    catch (...) { return 0; }
}
}
