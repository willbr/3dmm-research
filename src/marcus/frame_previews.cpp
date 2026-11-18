#include "frame_previews.h"
#include "preview_worker.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace
{
bool SamePosition(const MarcusFramePosition &a, const MarcusFramePosition &b)
{
    return a.scene == b.scene && a.frame == b.frame;
}
void ReadExact(SDL_IOStream *stream, void *data, std::size_t size, const std::atomic<bool> &cancel)
{
    const auto deadline = SDL_GetTicks() + 60000;
    auto bytes = static_cast<unsigned char *>(data);
    while (size)
    {
        if (cancel || SDL_GetTicks() >= deadline) throw std::runtime_error("Frame preview cancelled or timed out.");
        const auto count = SDL_ReadIO(stream, bytes, size);
        if (!count)
        {
            if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_NOT_READY)
                throw std::runtime_error("The frame preview helper stopped unexpectedly.");
            SDL_Delay(5);
            continue;
        }
        bytes += count;
        size -= count;
    }
}

BrowserPreview RequestPreview(SDL_Process *process, const std::string &request, const std::atomic<bool> &cancel)
{
    const auto input = SDL_GetProcessInput(process), stream = SDL_GetProcessOutput(process);
    if (!input || !stream) throw std::runtime_error("Cannot communicate with the frame preview helper.");
    std::size_t sent = 0;
    const auto deadline = SDL_GetTicks() + 60000;
    while (sent < request.size())
    {
        if (cancel || SDL_GetTicks() >= deadline) throw std::runtime_error("Frame preview cancelled or timed out.");
        const auto count = SDL_WriteIO(input, request.data() + sent, request.size() - sent);
        if (!count)
        {
            if (SDL_GetIOStatus(input) != SDL_IO_STATUS_NOT_READY)
                throw std::runtime_error("Cannot request a frame thumbnail.");
            SDL_Delay(5);
        }
        sent += count;
    }
    std::array<unsigned char, 4> header;
    ReadExact(stream, header.data(), header.size(), cancel);
    std::uint32_t size = 0;
    for (int i = 0; i < 4; ++i) size |= std::uint32_t(header[i]) << (i * 8);
    if (size < 24 || size > 256 * 1024) throw std::runtime_error("Frame preview response exceeds its limit.");
    std::vector<std::uint8_t> bytes(size);
    ReadExact(stream, bytes.data(), bytes.size(), cancel);
    return ReadPreview(bytes);
}

SDL_Process *StartPreviewProcess(const char **args)
{
    const auto properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    auto process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!process) throw std::runtime_error("Cannot launch marcus-preview for frame thumbnails.");
    return process;
}
}
MarcusFramePreviews::MarcusFramePreviews(std::shared_ptr<MarcusMovieFile> movie, std::string resources,
                                       std::string content, int scene, bool cameras, bool models, MarcusPreviewCache cache)
    : sourceScene(scene)
{
    const auto base = SDL_GetBasePath();
    const auto executable = base ? std::string(base) + "marcus-preview"
#ifdef _WIN32
        ".exe"
#endif
        : std::string();
    thread = std::thread([this, movie = std::move(movie), resources = std::move(resources),
                          content = std::move(content), scene, cameras, models, executable, cache = std::move(cache)] {
        SDL_Process *process = nullptr;
        try
        {
            const auto path = movie->path.u8string(), sceneText = std::to_string(scene);
            const auto fonts = movie->fontDirectory.u8string();
            const auto origin = movie->contentOrigin.u8string();
            const char *args[] = {executable.c_str(), path.c_str(), resources.c_str(), models ? "--models" : cameras ? "--cameras" : "--frames", content.c_str(),
                                  sceneText.c_str(), fonts.c_str(), origin.c_str(), nullptr};
            while (!cancel)
            {
                MarcusFramePosition position;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [&] {
                        return cancel || (pendingRead < pendingCount && ready.size() < MARCUS_FRAME_PREVIEW_REQUEST_LIMIT);
                    });
                    if (cancel) break;
                    position = pending[pendingRead++];
                    busy = true;
                    active = position;
                }
                const auto chunk = static_cast<std::uint32_t>(position.frame);
                BrowserPreview preview;
                if (!models || !cache.Load(chunk, preview) || preview.width != 240 || preview.height != 180)
                {
                    if (!process)
                    {
                        if (executable.empty()) throw std::runtime_error("Cannot locate the frame preview helper.");
                        process = StartPreviewProcess(args);
                    }
                    const auto frame = models ? std::to_string(chunk) : std::to_string(position.frame);
                    preview = RequestPreview(process, std::to_string(position.scene) + " " + frame + "\n", cancel);
                    if (preview.width > (models ? 240 : 160) || preview.height > (models ? 180 : 120))
                        throw std::runtime_error("Frame thumbnail dimensions exceed their limit.");
                    if (!models && !preview.error.empty()) throw std::runtime_error(preview.error);
                    if (preview.error.empty() && (!preview.width || !preview.height))
                        throw std::runtime_error("The thumbnail is empty.");
                    if (models && !cancel) cache.Store(chunk, preview);
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ready.push_back({position, std::move(preview)});
                    busy = false;
                }
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard<std::mutex> lock(mutex);
            failure = e.what();
            busy = false;
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
    });
}
MarcusFramePreviews::~MarcusFramePreviews()
{
    {
        // Change the wait predicate under its mutex so cancellation cannot
        // slip between the worker's predicate check and going to sleep.
        std::lock_guard<std::mutex> lock(mutex);
        cancel = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
}
extern "C"
{
void marcus_frame_previews_destroy(MarcusFramePreviews *previews) { delete previews; }
void marcus_frame_previews_request(MarcusFramePreviews *previews, const long *frames, int count)
{
    if (!previews || !frames || count < 0 || count > MARCUS_FRAME_PREVIEW_REQUEST_LIMIT) return;
    std::array<MarcusFramePosition, MARCUS_FRAME_PREVIEW_REQUEST_LIMIT> positions;
    for (int i = 0; i < count; ++i) positions[i] = {previews->sourceScene, frames[i]};
    marcus_frame_previews_request_positions(previews, positions.data(), count);
}
int marcus_frame_previews_request_positions(MarcusFramePreviews *previews, const MarcusFramePosition *positions, int count)
try
{
    if (!previews || (!positions && count != 0) || count < 0 || count > MARCUS_FRAME_PREVIEW_REQUEST_LIMIT) return 0;
    std::lock_guard<std::mutex> lock(previews->mutex);
    previews->pendingRead = previews->pendingCount = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto position = positions[i];
        const auto pendingEnd = previews->pending.begin() + previews->pendingCount;
        if ((previews->busy && SamePosition(previews->active, position)) ||
            std::any_of(previews->ready.begin(), previews->ready.end(),
                        [&](const MarcusFramePreviews::Result &r) { return SamePosition(r.position, position); }) ||
            std::any_of(previews->pending.begin(), pendingEnd,
                        [&](const MarcusFramePosition &p) { return SamePosition(p, position); })) continue;
        previews->pending[previews->pendingCount++] = position;
    }
    previews->wake.notify_all();
    return 1;
}
catch (...)
{
    return 0;
}
int marcus_frame_previews_take(MarcusFramePreviews *previews, MarcusImage *image, char *error, int errorSize)
try
{
    if (!previews || !image) return -1;
    *image = {};
    std::lock_guard<std::mutex> lock(previews->mutex);
    if (!previews->ready.empty())
    {
        previews->output = std::move(previews->ready.front());
        previews->ready.pop_front();
        const auto &p = previews->output.preview;
        if (error && errorSize > 0) SDL_strlcpy(error, p.error.c_str(), errorSize);
        image->scene = previews->output.position.scene;
        image->frame = previews->output.position.frame;
        image->width = p.width; image->height = p.height; image->pitch = p.width * 4;
        image->rgba = p.rgba.data();
        previews->wake.notify_all();
        return 1;
    }
    if (previews->failure.empty()) return 0;
    if (error && errorSize > 0) SDL_strlcpy(error, previews->failure.c_str(), errorSize);
    return -1;
}
catch (const std::exception &e)
{
    if (error && errorSize > 0) SDL_strlcpy(error, e.what(), errorSize);
    return -1;
}
}
