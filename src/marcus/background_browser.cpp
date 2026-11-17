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

namespace
{
constexpr int galleryColumns = 4, galleryRows = 3, galleryPage = galleryColumns * galleryRows;
constexpr float galleryWidth = 1000, galleryHeight = 720, galleryTop = 94, galleryTrack = 510;
SDL_FRect GalleryCard(float x, float y, int cell)
{ return {x + 24 + (cell % galleryColumns) * 236.f, y + galleryTop + (cell / galleryColumns) * 170.f, 224, 158}; }
}
struct MarcusBackgroundBrowser
{
    bool open = false, chosen = false, scrollDragging = false, props = false, actors = false;
    int selected = 0, scroll = 0;
    std::vector<MarcusBackground> backgrounds;
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
        for (auto &tile : backgroundTiles) SDL_DestroyTexture(tile.texture);
        backgroundTiles.clear(); loadingBackground = -1;
    }
    BackgroundTile *Background(unsigned long chunk)
    {
        for (auto &tile : backgroundTiles) if (tile.chunk == chunk) return &tile;
        return nullptr;
    }
    // Runs even while the chooser is closed. All SDL textures stay on this thread;
    // only disk I/O and the isolated decoder run in the worker.
    void WarmBackgrounds(MarcusUI *ui)
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
                    tile.texture = surface ? SDL_CreateTextureFromSurface(ui->renderer, surface) : nullptr;
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
                if (open && !props && backgroundTiles[i].chunk == backgrounds[selected].chunk) { next = i; break; }
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
        for (auto &tile : tiles) SDL_DestroyTexture(tile.texture);
        tiles.clear(); cachedModels = 0; cacheClock = 0;
    }
    void Stop() { models.reset(); }
    void Select(int index)
    {
        selected = std::clamp(index, 0, static_cast<int>(backgrounds.size()) - 1);
        if (props) scroll = std::clamp(scroll, std::max(0, selected / galleryColumns - galleryRows + 1), selected / galleryColumns);
        else scroll = std::clamp(scroll, std::max(0, selected - 11), selected);
    }
    void Close(bool confirm) { chosen = confirm && !backgrounds.empty(); open = false; Stop(); }
    void ScrollTo(float py, float top)
    {
        if (props)
        {
            const int rows = (int(backgrounds.size()) + galleryColumns - 1) / galleryColumns;
            if (rows <= galleryRows) return;
            const float thumb = std::max(28.f, galleryTrack * galleryRows / rows);
            const float fraction = std::clamp((py - top - thumb / 2) / (galleryTrack - thumb), 0.f, 1.f);
            scroll = int(fraction * (rows - galleryRows) + .5f); return;
        }
        if (backgrounds.size() <= 12) return;
        const float thumb = 360.0f * 12 / backgrounds.size();
        const float fraction = std::clamp((py - top - thumb / 2) / (360 - thumb), 0.0f, 1.0f);
        scroll = static_cast<int>(fraction * (backgrounds.size() - 12) + 0.5f);
    }
    void GalleryPreviews(MarcusUI *ui)
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
                auto loaded = surface ? SDL_CreateTextureFromSurface(ui->renderer, surface) : nullptr;
                SDL_DestroySurface(surface);
                if (loaded)
                {
                    if (!tile.texture) ++cachedModels;
                    SDL_DestroyTexture(tile.texture); tile.texture = loaded; tile.used = ++cacheClock;
                    SDL_SetTextureScaleMode(loaded, SDL_SCALEMODE_LINEAR);
                }
                else tile.error = SDL_GetError();
            }
        }
        if (status < 0) error = message;
        const int first = scroll * galleryColumns, last = std::min(first + galleryPage, int(tiles.size()));
        for (int i = first; i < last; ++i) if (tiles[i].texture) tiles[i].used = ++cacheClock;
        while (cachedModels > 64)
        {
            int oldest = -1;
            for (int i = 0; i < int(tiles.size()); ++i)
                if ((i < first || i >= last) && tiles[i].texture && (oldest < 0 || tiles[i].used < tiles[oldest].used)) oldest = i;
            if (oldest < 0) break;
            SDL_DestroyTexture(tiles[oldest].texture); tiles[oldest].texture = nullptr; --cachedModels;
        }
        if (!error.empty()) return;
        std::array<MarcusFramePosition, galleryPage> requests;
        int count = 0;
        auto request = [&](int index) {
            if (index >= first && index < last && !tiles[index].texture && tiles[index].error.empty())
                requests[count++] = {index, static_cast<long>(backgrounds[index].chunk)};
        };
        request(selected);
        for (int i = first; i < last; ++i) if (i != selected) request(i);
        marcus_frame_previews_request_positions(models.get(), requests.data(), count);
    }
    void DrawGallery(MarcusUI *ui, int width, int height)
    {
        GalleryPreviews(ui);
        const float x = (width - galleryWidth) / 2, y = (height - galleryHeight) / 2;
        ui_fill(ui, 0, 0, float(width), float(height), 0x121519);
        ui_fill(ui, x - 1, y - 1, galleryWidth + 2, galleryHeight + 2, 0x5b6570);
        ui_fill(ui, x, y, galleryWidth, galleryHeight, 0x25282d);
        ui_heading(ui, x + 24, y + 22, actors ? "NEW ACTOR" : "NEW PROP", 0x78cbc4);
        ui_text(ui, x + 24, y + 58, actors ? "Choose an actor to add at the current frame." : "Choose a prop to add at the current frame.", 0xdde0e5);
        ui_button(ui, x + 944, y + 18, 32, "x", 0, 1);
        const int first = scroll * galleryColumns;
        for (int cell = 0; cell < galleryPage && first + cell < int(backgrounds.size()); ++cell)
        {
            const int index = first + cell;
            const auto r = GalleryCard(x, y, cell);
            const auto &tile = tiles[index];
            const bool hovered = ui_inside(ui->mouse_x, ui->mouse_y, r.x, r.y, r.w, r.h);
            ui_fill(ui, r.x - 2, r.y - 2, r.w + 4, r.h + 4, index == selected ? 0x78cbc4 : hovered ? 0x5b6570 : 0x363a40);
            ui_fill(ui, r.x, r.y, r.w, r.h, 0x13161a);
            if (tile.texture)
            {
                const SDL_FRect destination = {r.x + 28, r.y + 4, 168, 126};
                SDL_RenderTexture(ui->renderer, tile.texture, nullptr, &destination);
            }
            else ui_label(ui, r.x + 18, r.y + 62, r.w - 36,
                tile.error.empty() && error.empty() ? "Loading model..." : "Preview unavailable", 0x969da8);
            ui_fill(ui, r.x, r.y + 134, r.w, 24, index == selected ? 0x375e64 : hovered ? 0x343c45 : 0x202328);
            ui_label(ui, r.x + 8, r.y + 140, r.w - 16, backgrounds[index].name, 0xdde0e5);
            if (hovered) ui->hint = !tile.error.empty() ? tile.error.c_str() : !error.empty() ? error.c_str() : backgrounds[index].name;
        }
        const int rows = (int(backgrounds.size()) + galleryColumns - 1) / galleryColumns;
        if (rows > galleryRows)
        {
            const float thumb = std::max(28.f, galleryTrack * galleryRows / rows);
            const float top = (galleryTrack - thumb) * scroll / (rows - galleryRows);
            ui_fill(ui, x + 967, y + galleryTop, 6, galleryTrack, 0x1c1e22);
            ui_fill(ui, x + 967, y + galleryTop + top, 6, thumb, 0x78cbc4);
        }
        char label[256];
        SDL_snprintf(label, sizeof(label), "%s / %d of %d %s", backgrounds[selected].name, selected + 1,
            int(backgrounds.size()), actors ? "actors" : "props");
        ui_heading_label(ui, x + 24, y + 620, 952, label, 0xdde0e5);
        ui_label(ui, x + 24, y + 644, 952, "Appears at the current camera's next insertion point.", 0x969da8);
        ui_text(ui, x + 24, y + 676, "Arrows to browse / Wheel to scroll / Enter to add / Escape to cancel", 0x969da8);
        ui_button_sized(ui, x + 750, y + 666, 92, 30, "Cancel", 0, 1);
        ui_button_sized(ui, x + 854, y + 666, 122, 30, actors ? "Add actor" : "Add prop", 1, 1);
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
static int ShowAssets(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content, bool props, bool actors = false)
{
    if (!browser) return 0;
    if (!props)
    {
        background_browser_prepare(browser, player, resources, content);
        for (auto &tile : browser->backgroundTiles)
            if (!tile.texture && tile.attempted) { tile.attempted = false; tile.error.clear(); }
    }
    browser->Stop();
    browser->ClearGallery();
    browser->error.clear();
    browser->open = browser->chosen = false;
    browser->props = props; browser->actors = actors;
    browser->backgrounds.clear();
    if (!props)
    {
        MarcusBackground storyboard = {};
        storyboard.chunk = MARCUS_BACKGROUND_STORYBOARD;
        SDL_strlcpy(storyboard.name, "Storyboard - draw a background", sizeof(storyboard.name));
        browser->backgrounds.push_back(storyboard);
    }
    const int count = actors ? marcus_actor_count(player, content) : props ? marcus_prop_count(player, content) : marcus_background_count(player, content);
    for (int index = 0; index < count; ++index)
    {
        MarcusBackground background = {};
        if (props)
        {
            MarcusProp prop = {};
            if (actors ? marcus_actor(player, index, &prop) : marcus_prop(player, index, &prop))
            {
                SDL_strlcpy(background.name, prop.name, sizeof(background.name));
                background.chunk = prop.chunk;
                browser->backgrounds.push_back(background);
            }
        }
        else if (marcus_background(player, index, &background))
        {
            // Retail's black background belongs directly below Storyboard.
            if (background.chunk == 11) browser->backgrounds.insert(browser->backgrounds.begin() + 1, background);
            else browser->backgrounds.push_back(background);
        }
    }
    if (browser->backgrounds.empty()) return 0;
    if (props) browser->tiles.resize(browser->backgrounds.size());
    browser->resources = resources;
    browser->content = content;
    browser->selected = browser->scroll = 0;
    browser->chosen = false;
    browser->scrollDragging = false;
    browser->open = true;
    return 1;
}
int background_browser_show(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content)
{ return ShowAssets(browser, player, resources, content, false); }
int background_browser_show_props(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content)
{ return ShowAssets(browser, player, resources, content, true); }
int background_browser_show_actors(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content)
{ return ShowAssets(browser, player, resources, content, true, true); }
int background_browser_event(MarcusBackgroundBrowser *browser, const SDL_Event *event, int width, int height)
{
    if (!background_browser_visible(browser)) return 0;
    if (browser->props)
    {
        const float x = (width - galleryWidth) / 2, y = (height - galleryHeight) / 2;
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
        {
            switch (event->key.key)
            {
            case SDLK_ESCAPE: browser->Close(false); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: browser->Close(true); break;
            case SDLK_LEFT: browser->Select(browser->selected - 1); break;
            case SDLK_RIGHT: browser->Select(browser->selected + 1); break;
            case SDLK_UP: browser->Select(browser->selected - galleryColumns); break;
            case SDLK_DOWN: browser->Select(browser->selected + galleryColumns); break;
            case SDLK_PAGEUP: browser->Select(browser->selected - galleryPage); break;
            case SDLK_PAGEDOWN: browser->Select(browser->selected + galleryPage); break;
            case SDLK_HOME: browser->Select(0); break;
            case SDLK_END: browser->Select(int(browser->backgrounds.size()) - 1); break;
            }
        }
        else if (event->type == SDL_EVENT_MOUSE_MOTION && browser->scrollDragging)
            browser->ScrollTo(event->motion.y, y + galleryTop);
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
            browser->scrollDragging = false;
        else if (event->type == SDL_EVENT_MOUSE_WHEEL)
        {
            const int rows = (int(browser->backgrounds.size()) + galleryColumns - 1) / galleryColumns;
            const float delta = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
            browser->scroll = std::clamp(browser->scroll - int(delta), 0, std::max(0, rows - galleryRows));
        }
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
        {
            const float px = event->button.x, py = event->button.y;
            if (ui_inside(px, py, x + 750, y + 666, 92, 30) || ui_inside(px, py, x + 944, y + 18, 32, 28)) browser->Close(false);
            else if (ui_inside(px, py, x + 854, y + 666, 122, 30)) browser->Close(true);
            else if (ui_inside(px, py, x + 964, y + galleryTop, 12, galleryTrack))
            { browser->scrollDragging = true; browser->ScrollTo(py, y + galleryTop); }
            else for (int cell = 0; cell < galleryPage; ++cell)
            {
                const auto r = GalleryCard(x, y, cell);
                const int index = browser->scroll * galleryColumns + cell;
                if (index < int(browser->backgrounds.size()) && ui_inside(px, py, r.x, r.y, r.w, r.h))
                { browser->Select(index); if (event->button.clicks == 2) browser->Close(true); break; }
            }
        }
        return event->type != SDL_EVENT_QUIT && event->type != SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    }
    const float x = (width - 940) / 2.0f, y = (height - 560) / 2.0f;
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.key)
        {
        case SDLK_ESCAPE: browser->Close(false); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: browser->Close(true); break;
        case SDLK_UP: browser->Select(browser->selected - 1); break;
        case SDLK_DOWN: browser->Select(browser->selected + 1); break;
        case SDLK_PAGEUP: browser->Select(browser->selected - 12); break;
        case SDLK_PAGEDOWN: browser->Select(browser->selected + 12); break;
        case SDLK_HOME: browser->Select(0); break;
        case SDLK_END: browser->Select(static_cast<int>(browser->backgrounds.size()) - 1); break;
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_MOTION && browser->scrollDragging)
        browser->ScrollTo(event->motion.y, y + 106);
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
        browser->scrollDragging = false;
    else if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        const int limit = std::max(0, static_cast<int>(browser->backgrounds.size()) - 12);
        const float delta = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        browser->scroll = std::clamp(browser->scroll - static_cast<int>(delta * 3), 0, limit);
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        const float px = event->button.x, py = event->button.y;
        if (browser->backgrounds.size() > 12 && ui_inside(px, py, x + 352, y + 106, 12, 360))
        { browser->scrollDragging = true; browser->ScrollTo(py, y + 106); }
        else if (ui_inside(px, py, x + 24, y + 106, 328, 360))
        {
            const int index = browser->scroll + static_cast<int>((py - y - 106) / 30);
            if (index < static_cast<int>(browser->backgrounds.size()))
            { browser->Select(index); if (event->button.clicks == 2) browser->Close(true); }
        }
        else if (ui_inside(px, py, x + 690, y + 506, 92, 30)) browser->Close(false);
        else if (ui_inside(px, py, x + 794, y + 506, 122, 30)) browser->Close(true);
        else if (ui_inside(px, py, x + 884, y + 18, 32, 28)) browser->Close(false);
    }
    return event->type != SDL_EVENT_QUIT && event->type != SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}
void background_browser_draw(MarcusBackgroundBrowser *browser, MarcusUI *ui, int width, int height)
{
    if (!browser) return;
    browser->WarmBackgrounds(ui);
    if (!background_browser_visible(browser)) return;
    if (browser->props) { browser->DrawGallery(ui, width, height); return; }
    const auto preview = browser->Background(browser->backgrounds[browser->selected].chunk);
    browser->error = preview ? preview->error : "";
    const float x = (width - 940) / 2.0f, y = (height - 560) / 2.0f;
    ui_fill(ui, 0, 0, static_cast<float>(width), static_cast<float>(height), 0x121519);
    ui_fill(ui, x - 1, y - 1, 942, 562, 0x5b6570);
    ui_fill(ui, x, y, 940, 560, 0x25282d);
    ui_heading(ui, x + 24, y + 22, browser->actors ? "NEW ACTOR" : browser->props ? "NEW PROP" : "NEW SCENE", 0x78cbc4);
    ui_text(ui, x + 24, y + 58, browser->actors ? "Choose an actor to add at the current frame." : browser->props ? "Choose a prop to add at the current frame." : "Choose a background for your scene.", 0xdde0e5);
    ui_button(ui, x + 884, y + 18, 32, "x", 0, 1);
    ui_heading(ui, x + 24, y + 84, browser->actors ? "ACTORS" : browser->props ? "PROPS" : "BACKGROUNDS", 0x969da8);
    ui_fill(ui, x + 24, y + 106, 340, 360, 0x1c1e22);
    for (int row = 0; row < 12 && browser->scroll + row < static_cast<int>(browser->backgrounds.size()); ++row)
    {
        const int index = browser->scroll + row;
        if (index == browser->selected) ui_fill(ui, x + 24, y + 106 + row * 30, 332, 30, 0x375e64);
        ui_label(ui, x + 36, y + 113 + row * 30, 306, browser->backgrounds[index].name, 0xdde0e5);
    }
    if (browser->backgrounds.size() > 12)
    {
        const float thumb = 360.0f * 12 / browser->backgrounds.size();
        const float top = (360 - thumb) * browser->scroll / (browser->backgrounds.size() - 12);
        ui_fill(ui, x + 358, y + 106 + top, 6, thumb, 0x78cbc4);
    }
    ui_fill(ui, x + 388, y + 106, 528, 297, 0x13161a);
    const bool storyboard = !browser->props && browser->backgrounds[browser->selected].chunk == MARCUS_BACKGROUND_STORYBOARD;
    if (storyboard)
    {
        ui_fill(ui, x + 388, y + 106, 528, 297, 0xd3d4d6);
        ui_heading(ui, x + 538, y + 230, "DRAW YOUR SCENE", 0x363a40);
        ui_text(ui, x + 488, y + 264, "Six colours / crunchy brush / no depth", 0x555b63);
    }
    else if (preview && preview->texture)
    {
        SDL_FRect destination = {x + 388, y + 106, 528, 297};
        SDL_RenderTexture(ui->renderer, preview->texture, nullptr, &destination);
    }
    else ui_text(ui, x + 552, y + 244, browser->error.empty() ? "Loading preview..." : "Preview unavailable", 0x969da8);
    ui_heading_label(ui, x + 388, y + 422, 528, browser->backgrounds[browser->selected].name, 0xdde0e5);
    ui_label(ui, x + 388, y + 450, 528, storyboard ? "Paint backgrounds, create camera angles and light your blockout." : browser->error.empty() ?
        (browser->props ? "Appears at the current camera's next insertion point." : "A new scene starts at frame 1, with no actors or sounds.") : browser->error.c_str(), 0x969da8);
    char count[96];
    SDL_snprintf(count, sizeof(count), "%d of %d %s", browser->selected + 1, static_cast<int>(browser->backgrounds.size()), browser->actors ? "actors" : browser->props ? "props" : "backgrounds");
    ui_text(ui, x + 24, y + 480, count, 0x969da8);
    ui_text(ui, x + 24, y + 516, "Arrows to browse / Enter to add / Escape to cancel", 0x969da8);
    ui_button_sized(ui, x + 690, y + 506, 92, 30, "Cancel", 0, 1);
    ui_button_sized(ui, x + 794, y + 506, 122, 30, browser->actors ? "Add actor" : browser->props ? "Add prop" : "Add scene", 1, 1);
}
int background_browser_take(MarcusBackgroundBrowser *browser, unsigned long *chunk)
{
    if (!browser || browser->props || !browser->chosen || !chunk) return 0;
    browser->chosen = false;
    *chunk = browser->backgrounds[browser->selected].chunk;
    return 1;
}
int background_browser_take_prop(MarcusBackgroundBrowser *browser, unsigned long *chunk)
{
    if (!browser || !browser->props || browser->actors || !browser->chosen || !chunk) return 0;
    browser->chosen = false;
    *chunk = browser->backgrounds[browser->selected].chunk;
    return 1;
}
int background_browser_take_actor(MarcusBackgroundBrowser *browser, unsigned long *chunk)
{
    if (!browser || !browser->actors || !browser->chosen || !chunk) return 0;
    browser->chosen = false;
    *chunk = browser->backgrounds[browser->selected].chunk;
    return 1;
}

}
