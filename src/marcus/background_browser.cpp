#include "dialog_host.h"
#include "background_browser.h"
#include "preview_worker.h"
#include "preview_cache.h"
#include "frame_previews.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

struct MarcusBackgroundBrowser
{
    bool open = false, chosen = false, objectMode = false;
    int selected = 0;
    std::vector<MarcusBackground> backgrounds;
    struct Model { MarcusBackground entry; bool actor; };
    std::vector<Model> catalogue;
    std::vector<int> filtered;
    int filter = 0;
    std::string resources, content, error;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::future<BrowserPreview> pending;
    struct BackgroundTile
    {
        unsigned long chunk;
        SDL_Texture *texture = nullptr;
        std::string error;
        bool attempted = false;
    };
    std::vector<BackgroundTile> backgroundTiles;
    MarcusPreviewCache backgroundCache;
    std::string backgroundResources, backgroundContent;
    int loadingBackground = -1;
    struct Tile { SDL_Texture *texture = nullptr; std::string error; Uint64 used = 0; };
    std::vector<Tile> tiles;
    std::unique_ptr<MarcusFramePreviews> models;
    int cachedModels = 0;
    Uint64 cacheClock = 0;
    ~MarcusBackgroundBrowser() { Stop(); ClearGallery(); ClearBackgrounds(); }
    void ClearBackgrounds()
    {
        if (cancel) *cancel = true;
        if (pending.valid()) { pending.wait(); pending = {}; }
        for (auto &tile : backgroundTiles) dialog_retire_texture(tile.texture);
        backgroundTiles.clear(); loadingBackground = -1;
    }
    BackgroundTile *Background(unsigned long chunk)
    {
        for (auto &tile : backgroundTiles) if (tile.chunk == chunk) return &tile;
        return nullptr;
    }
    // Runs even while the chooser is closed. All SDL textures stay on this thread;
    // only disk I/O and the isolated decoder run in the worker.
    void WarmBackgrounds(SDL_Renderer *renderer)
    {
        if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto &tile = backgroundTiles[loadingBackground];
            tile.attempted = true;
            try
            {
                const auto result = pending.get();
                tile.error = result.error;
                if (!result.rgba.empty())
                {
                    auto surface = SDL_CreateSurfaceFrom(result.width, result.height, SDL_PIXELFORMAT_RGBA32,
                        const_cast<unsigned char *>(result.rgba.data()), result.width * 4);
                    tile.texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
                    SDL_DestroySurface(surface);
                    if (!tile.texture) tile.error = SDL_GetError();
                }
            }
            catch (const std::exception &e) { tile.error = e.what(); }
        }
        if (pending.valid()) return;
        int next = -1;
        // An uncached selection takes priority over the remaining warm-up work.
        for (int i = 0; i < int(backgroundTiles.size()); ++i)
            if (!backgroundTiles[i].attempted)
            {
                if (next < 0) next = i;
                if (open && !objectMode && selected >= 0 && backgroundTiles[i].chunk == backgrounds[selected].chunk) { next = i; break; }
            }
        if (next < 0) return;
        loadingBackground = next;
        cancel = std::make_shared<std::atomic<bool>>(false);
        const auto cancelled = cancel;
        const auto chunk = backgroundTiles[next].chunk;
        const auto cache = backgroundCache;
        const auto resources = backgroundResources, content = backgroundContent;
        try
        {
            pending = std::async(std::launch::async, [=] {
                BrowserPreview result;
                if (!cache.Load(chunk, result))
                {
                    result = RunPreviewWorker(std::to_string(chunk), resources,
                        [cancelled] { return cancelled->load(); }, content, false, {}, true);
                    if (!*cancelled) cache.Store(chunk, result);
                }
                return result;
            });
        }
        catch (const std::exception &e)
        { backgroundTiles[next].attempted = true; backgroundTiles[next].error = e.what(); }
    }
    void ClearGallery()
    {
        for (auto &tile : tiles) dialog_retire_texture(tile.texture);
        tiles.clear(); cachedModels = 0; cacheClock = 0;
    }
    void Stop() { models.reset(); }
    int Count() const { return int(objectMode ? filtered.size() : backgrounds.size()); }
    // Display indices change with the filter; worker/cache identities do not.
    int ModelIndex(int index) const
    { return index >= 0 && index < int(filtered.size()) ? filtered[index] : -1; }
    const MarcusBackground *Entry(int index) const
    {
        if (index < 0 || index >= Count()) return nullptr;
        return objectMode ? &catalogue[filtered[index]].entry : &backgrounds[index];
    }
    bool SelectedActor() const
    { return objectMode && ModelIndex(selected) >= 0 && catalogue[filtered[selected]].actor; }
    void Filter(int next)
    {
        const int previous = ModelIndex(selected);
        filter = std::clamp(next, 0, 2); filtered.clear();
        for (int i = 0; i < int(catalogue.size()); ++i)
            if (!filter || catalogue[i].actor == (filter == 1)) filtered.push_back(i);
        const auto found = std::find(filtered.begin(), filtered.end(), previous);
        selected = found != filtered.end() ? int(found - filtered.begin()) : filtered.empty() ? -1 : 0;
    }
    void Select(int index)
    {
        selected = Count() ? std::clamp(index, 0, Count() - 1) : -1;
    }
    void Close(bool confirm) { chosen = confirm && Entry(selected); open = false; Stop(); }

    void GalleryPreviews(SDL_Renderer *renderer, int first, int last)
    {
        if (!models && error.empty())
        {
            try
            {
                auto archive = std::make_shared<MarcusMovieFile>(std::filesystem::u8path(content) / "tmpls.3cn");
                models = std::make_unique<MarcusFramePreviews>(archive, resources, content, 0, false, true);
            }
            catch (const std::exception &e) { error = e.what(); }
        }
        if (!models) return;
        MarcusImage image = {}; char message[1024] = {};
        int status;
        while ((status = marcus_frame_previews_take(models.get(), &image, message, sizeof(message))) > 0)
        {
            if (image.scene < 0 || image.scene >= int(tiles.size())) continue;
            auto &tile = tiles[image.scene];
            tile.error = message;
            if (image.width && image.height && image.rgba)
            {
                auto surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
                    const_cast<unsigned char *>(image.rgba), image.pitch);
                auto loaded = surface ? SDL_CreateTextureFromSurface(renderer, surface) : nullptr;
                SDL_DestroySurface(surface);
                if (loaded)
                {
                    if (!tile.texture) ++cachedModels;
                    dialog_retire_texture(tile.texture); tile.texture = loaded; tile.used = ++cacheClock;
                    SDL_SetTextureScaleMode(loaded, SDL_SCALEMODE_LINEAR);
                }
                else tile.error = SDL_GetError();
            }
        }
        if (status < 0) error = message;
        first = std::clamp(first, 0, Count()); last = std::clamp(last, first, std::min(first + 32, Count()));
        std::array<int, 32> visible;
        for (int i = first; i < last; ++i) {
            const int index = filtered[i]; visible[i - first] = index;
            if (tiles[index].texture) tiles[index].used = ++cacheClock;
        }
        while (cachedModels > 64)
        {
            int oldest = -1;
            for (int i = 0; i < int(tiles.size()); ++i)
                if (std::find(visible.begin(), visible.begin() + last - first, i) == visible.begin() + last - first &&
                    tiles[i].texture && (oldest < 0 || tiles[i].used < tiles[oldest].used)) oldest = i;
            if (oldest < 0) break;
            dialog_retire_texture(tiles[oldest].texture); tiles[oldest].texture = nullptr; --cachedModels;
        }
        if (!error.empty()) return;
        std::array<MarcusFramePosition, 32> requests;
        int count = 0;
        auto request = [&](int index) {
            if (index < first || index >= last) return;
            const int model = filtered[index];
            if (!tiles[model].texture && tiles[model].error.empty())
                requests[count++] = {model, static_cast<long>(catalogue[model].entry.chunk)};
        };
        request(selected);
        for (int i = first; i < last; ++i) if (i != selected) request(i);
        marcus_frame_previews_request_positions(models.get(), requests.data(), count);
    }

};
extern "C" {
MarcusBackgroundBrowser *background_browser_create(void)
{
    try { return new MarcusBackgroundBrowser; } catch (...) { return nullptr; }
}
void background_browser_destroy(MarcusBackgroundBrowser *browser) { delete browser; }
int background_browser_visible(const MarcusBackgroundBrowser *browser) { return browser && browser->open; }
void background_browser_prepare(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content)
{
    if (!browser || !player || !resources || !content) return;
    try
    {
        std::filesystem::path directory;
        if (auto pref = SDL_GetPrefPath("3DMMForever", "Marcus"))
        { directory = std::filesystem::u8path(pref) / "background-previews"; SDL_free(pref); }
        const auto base = SDL_GetBasePath();
        MarcusPreviewCache cache(directory, {std::filesystem::u8path(resources),
            std::filesystem::u8path(content) / "bkgds.3cn",
            std::filesystem::u8path(base ? base : "") / "marcus-preview"
#ifdef _WIN32
                ".exe"
#endif
        });
        if (cache.Key() == browser->backgroundCache.Key()) return;
        browser->ClearBackgrounds();
        browser->backgroundCache = std::move(cache);
        browser->backgroundResources = resources; browser->backgroundContent = content;
        const int count = marcus_background_count(player, content);
        for (int index = 0; index < count; ++index)
        {
            MarcusBackground background = {};
            if (marcus_background(player, index, &background)) browser->backgroundTiles.push_back({background.chunk});
        }
    }
    catch (const std::exception &) { /* Cache warm-up must not prevent opening Marcus. */ }
}
static int ShowAssets(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content, bool objects)
{
    if (!browser) return 0;
    try
    {
        if (!objects)
        {
            background_browser_prepare(browser, player, resources, content);
            for (auto &tile : browser->backgroundTiles)
                if (!tile.texture && tile.attempted) { tile.attempted = false; tile.error.clear(); }
        }
        browser->Stop();
        browser->ClearGallery();
        browser->error.clear();
        browser->open = browser->chosen = false;
        browser->objectMode = objects;
        browser->backgrounds.clear();
        browser->catalogue.clear(); browser->filtered.clear();
        if (!objects)
        {
            MarcusBackground storyboard = {};
            storyboard.chunk = MARCUS_BACKGROUND_STORYBOARD;
            SDL_strlcpy(storyboard.name, "Storyboard - draw a background", sizeof(storyboard.name));
            browser->backgrounds.push_back(storyboard);
        }
        if (objects)
        {
            for (int actor = 1; actor >= 0; --actor)
            {
                const int count = actor ? marcus_actor_count(player, content) : marcus_prop_count(player, content);
                if (count < 0) return 0;
                for (int index = 0; index < count; ++index)
                {
                    MarcusProp prop = {};
                    if (actor ? marcus_actor(player, index, &prop) : marcus_prop(player, index, &prop))
                    {
                        MarcusBackgroundBrowser::Model model = {};
                        model.actor = actor != 0;
                        SDL_strlcpy(model.entry.name, prop.name, sizeof(model.entry.name));
                        model.entry.chunk = prop.chunk;
                        browser->catalogue.push_back(model);
                    }
                }
            }
            std::stable_sort(browser->catalogue.begin(), browser->catalogue.end(), [](const auto &a, const auto &b) {
                return SDL_strcasecmp(a.entry.name, b.entry.name) < 0;
            });
            if (browser->catalogue.empty()) return 0;
            browser->tiles.resize(browser->catalogue.size());
            browser->Filter(browser->filter);
        }
        else
        {
            const int count = marcus_background_count(player, content);
            for (int index = 0; index < count; ++index)
            {
                MarcusBackground background = {};
                if (marcus_background(player, index, &background))
                {
                    // Retail's black background belongs directly below Storyboard.
                    if (background.chunk == 11) browser->backgrounds.insert(browser->backgrounds.begin() + 1, background);
                    else browser->backgrounds.push_back(background);
                }
            }
            browser->selected = 0;
        }
        browser->resources = resources;
        browser->content = content;
        browser->chosen = false;
        browser->open = true;
        return 1;
    } catch (const std::exception &e) { browser->error = e.what(); return 0; }
}
int background_browser_show(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content)
{ return ShowAssets(browser, player, resources, content, false); }
int background_browser_show_objects(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content)
{ return ShowAssets(browser, player, resources, content, true); }

int background_browser_take(MarcusBackgroundBrowser *browser, unsigned long *chunk)
{
    if (!browser || browser->objectMode || !browser->chosen || !chunk) return 0;
    browser->chosen = false;
    *chunk = browser->backgrounds[browser->selected].chunk;
    return 1;
}
int background_browser_take_object(MarcusBackgroundBrowser *browser, unsigned long *chunk, int *actor)
{
    if (!browser || !browser->objectMode || !browser->chosen || !chunk || !actor) return 0;
    browser->chosen = false;
    const auto entry = browser->Entry(browser->selected);
    if (!entry) return 0;
    *chunk = entry->chunk; *actor = browser->SelectedActor();
    return 1;
}

}

namespace {
void BackgroundState(void *p, MarcusDialogState *out)
{
    auto &s = *static_cast<MarcusBackgroundBrowser *>(p); *out = {};
    const char *confirm = "Add scene";
    const char *status = "Starts at frame 1 with no actors or sounds.";
    out->open = s.open; out->kind = s.objectMode ? DIALOG_MODELS : DIALOG_BACKGROUNDS;
    out->count = s.Count(); out->selected = s.selected; out->filter = s.filter;
    if (s.objectMode) {
        confirm = s.selected < 0 ? "Add object" : s.SelectedActor() ? "Add actor" : "Add prop";
        status = out->count ? "Appears at the camera's next insertion point." : "No objects match this filter.";
    }
    SDL_strlcpy(out->title, s.objectMode ? "Add actor or prop" : "New scene", sizeof(out->title));
    SDL_strlcpy(out->confirm, confirm, sizeof(out->confirm));
    SDL_snprintf(out->status, sizeof(out->status), "%d of %d / %s", s.selected + 1, out->count, status);
    SDL_strlcpy(out->error, s.error.c_str(), sizeof(out->error));
}
void BackgroundPrepare(void *p, SDL_Renderer *renderer, int first, int last)
{
    auto &s = *static_cast<MarcusBackgroundBrowser *>(p);
    try {
        if (first < 0) { s.WarmBackgrounds(renderer); return; }
        if (s.open && s.objectMode) s.GalleryPreviews(renderer, first, last);
    } catch (const std::exception &e) { s.error = e.what(); }
}
int BackgroundItem(void *p, SDL_Renderer *, int index, MarcusDialogItem *out)
{
    auto &s = *static_cast<MarcusBackgroundBrowser *>(p); *out = {};
    const auto entry = s.Entry(index); if (!entry) return 0;
    const auto &value = *entry; SDL_strlcpy(out->label, value.name, sizeof(out->label));
    if (s.objectMode) {
        const int model = s.ModelIndex(index); const auto &tile = s.tiles[model];
        out->texture = tile.texture;
        SDL_snprintf(out->detail, sizeof(out->detail), "%s%s%s", s.catalogue[model].actor ? "Actor" : "Prop",
            tile.error.empty() ? "" : "\n", tile.error.c_str());
    }
    else if (value.chunk == MARCUS_BACKGROUND_STORYBOARD) SDL_strlcpy(out->detail, "Draw your own background, camera angles and lighting.", sizeof(out->detail));
    else if (const auto tile = s.Background(value.chunk)) { out->texture = tile->texture; SDL_strlcpy(out->detail, tile->error.c_str(), sizeof(out->detail)); }
    return 1;
}
void BackgroundAction(void *p, int command, int index, float, const char *)
{
    auto &s = *static_cast<MarcusBackgroundBrowser *>(p);
    if (command == DIALOG_SELECT) s.Select(index);
    else if (command == DIALOG_FILTER && s.objectMode) {
        try { s.Filter(index); } catch (const std::exception &e) { s.error = e.what(); s.selected = -1; }
    }
    else if (command == DIALOG_ACCEPT) s.Close(true);
    else if (command == DIALOG_CANCEL) s.Close(false);
}
}
extern "C" const MarcusDialogHost marcus_background_dialog_host = {BackgroundState, BackgroundPrepare, BackgroundItem, BackgroundAction};
