#include "background_preview.h"
#include "frame_previews.h"
#include "preview_cache.h"
#include "preview_worker.h"
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <utility>

namespace
{
MarcusPreviewCache ContentPreviewCache(const char *folder, const std::filesystem::path &archive,
                                       const char *resources, std::size_t fileLimit = 0)
{
    std::unique_ptr<char, decltype(&SDL_free)> pref(SDL_GetPrefPath("3DMMForever", "Marcus"), SDL_free);
    std::filesystem::path directory;
    if (pref) directory = std::filesystem::u8path(pref.get()) / folder;
    const auto base = SDL_GetBasePath();
    return MarcusPreviewCache(directory, {std::filesystem::u8path(resources), archive,
        std::filesystem::u8path(base ? base : "") / "marcus-preview"
#ifdef _WIN32
        ".exe"
#endif
    }, fileLimit);
}
}

struct MarcusBackgroundPreviews
{
    MarcusPreviewCache cache;
    std::string resources, content;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::future<BrowserPreview> pending;
    BrowserPreview output;

    void Stop()
    {
        if (cancelled) *cancelled = true;
        pending = {};
        cancelled.reset();
    }
    ~MarcusBackgroundPreviews() { Stop(); }
};

extern "C"
{
MarcusBackgroundPreviews *marcus_background_previews_create(void)
try { return new MarcusBackgroundPreviews; }
catch (...) { return nullptr; }

void marcus_background_previews_destroy(MarcusBackgroundPreviews *previews) { delete previews; }

int marcus_background_previews_configure(MarcusBackgroundPreviews *previews, const char *resources, const char *content)
try
{
    auto cache = ContentPreviewCache("background-previews", std::filesystem::u8path(content) / "bkgds.3cn", resources);
    if (previews->cache.Key() == cache.Key()) return MARCUS_BACKGROUND_PREVIEWS_UNCHANGED;
    std::string nextResources(resources), nextContent(content);
    previews->Stop();
    previews->cache = std::move(cache);
    previews->resources = std::move(nextResources);
    previews->content = std::move(nextContent);
    previews->output = {};
    return MARCUS_BACKGROUND_PREVIEWS_CONFIGURED;
}
catch (...) { return MARCUS_BACKGROUND_PREVIEWS_FAILED; }

int marcus_background_previews_start(MarcusBackgroundPreviews *previews, unsigned long chunk, char *error, size_t capacity)
try
{
    if (previews->pending.valid()) return 0;
    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    previews->pending = std::async(std::launch::async,
        [cancelled, chunk, cache = previews->cache, resources = previews->resources, content = previews->content] {
            BrowserPreview result;
            if (!cache.Load(chunk, result))
            {
                result = RunPreviewWorker(std::to_string(chunk), resources, [cancelled] { return cancelled->load(); },
                                          content, false, {}, true);
                if (!*cancelled) cache.Store(chunk, result);
            }
            return result;
        });
    previews->cancelled = cancelled;
    return 1;
}
catch (const std::exception &e)
{
    SDL_strlcpy(error, e.what(), capacity);
    return 0;
}

int marcus_background_previews_take(MarcusBackgroundPreviews *previews, MarcusImage *image, char *error, size_t capacity)
try
{
    if (!previews->pending.valid() || previews->pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return 0;
    previews->output = previews->pending.get();
    const auto &output = previews->output;
    *image = {};
    image->rgba = output.rgba.empty() ? nullptr : output.rgba.data();
    image->width = output.width;
    image->height = output.height;
    image->pitch = output.width * 4;
    SDL_strlcpy(error, output.error.c_str(), capacity);
    return 1;
}
catch (const std::exception &e)
{
    previews->Stop();
    SDL_strlcpy(error, e.what(), capacity);
    return -1;
}

MarcusFramePreviews *marcus_model_previews_create(const char *resources, const char *content, char *error, size_t capacity)
try
{
    const auto path = std::filesystem::u8path(content) / "tmpls.3cn";
    auto cache = ContentPreviewCache("model-previews", path, resources, 256);
    return new MarcusFramePreviews(std::make_shared<MarcusMovieFile>(path), resources, content, 0, false, true,
                                  std::move(cache));
}
catch (const std::exception &e)
{
    SDL_strlcpy(error, e.what(), capacity);
    return nullptr;
}
}
