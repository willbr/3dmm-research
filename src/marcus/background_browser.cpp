#include "background_browser.h"
#include "preview_worker.h"
#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

struct MarcusBackgroundBrowser
{
    bool open = false, chosen = false, scrollDragging = false, props = false, actors = false;
    int selected = 0, scroll = 0, previewIndex = -1;
    std::vector<MarcusBackground> backgrounds;
    std::string resources, content, error;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::future<BrowserPreview> pending;
    SDL_Texture *texture = nullptr;
    ~MarcusBackgroundBrowser() { Stop(); SDL_DestroyTexture(texture); }
    void Stop() { if (cancel) *cancel = true; if (pending.valid()) pending.wait(); }
    void Select(int index)
    {
        selected = std::clamp(index, 0, static_cast<int>(backgrounds.size()) - 1);
        scroll = std::clamp(scroll, std::max(0, selected - 11), selected);
    }
    void Close(bool confirm) { chosen = confirm && !backgrounds.empty(); open = false; Stop(); }
    void ScrollTo(float py, float top)
    {
        if (backgrounds.size() <= 12) return;
        const float thumb = 360.0f * 12 / backgrounds.size();
        const float fraction = std::clamp((py - top - thumb / 2) / (360 - thumb), 0.0f, 1.0f);
        scroll = static_cast<int>(fraction * (backgrounds.size() - 12) + 0.5f);
    }
};
extern "C" {
MarcusBackgroundBrowser *background_browser_create(void)
{
    try { return new MarcusBackgroundBrowser; } catch (...) { return nullptr; }
}
void background_browser_destroy(MarcusBackgroundBrowser *browser) { delete browser; }
int background_browser_visible(const MarcusBackgroundBrowser *browser) { return browser && browser->open; }
static int ShowAssets(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content, bool props, bool actors = false)
{
    if (!browser) return 0;
    browser->Stop();
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
        else if (marcus_background(player, index, &background)) browser->backgrounds.push_back(background);
    }
    if (browser->backgrounds.empty()) return 0;
    browser->resources = resources;
    browser->content = content;
    browser->selected = browser->scroll = 0;
    browser->previewIndex = -1;
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
    if (!background_browser_visible(browser)) return;
    if (browser->previewIndex != browser->selected)
    {
        browser->Stop();
        browser->previewIndex = browser->selected;
        browser->error.clear();
        SDL_DestroyTexture(browser->texture); browser->texture = nullptr;
        browser->cancel = std::make_shared<std::atomic<bool>>(false);
        const auto cancel = browser->cancel;
        const auto chunk = std::to_string(browser->backgrounds[browser->selected].chunk);
        const auto resources = browser->resources, content = browser->content;
        const auto props = browser->props;
        if (!props && browser->backgrounds[browser->selected].chunk == MARCUS_BACKGROUND_STORYBOARD) {}
        else try
        {
            browser->pending = std::async(std::launch::async, [=] {
                return RunPreviewWorker(chunk, resources, [cancel] { return cancel->load(); }, content, false, {}, !props, props);
            });
        }
        catch (const std::exception &e) { browser->error = e.what(); }
    }
    if (browser->pending.valid() && browser->pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            const auto result = browser->pending.get();
            browser->error = result.error;
            if (!result.rgba.empty())
            {
                auto surface = SDL_CreateSurfaceFrom(result.width, result.height, SDL_PIXELFORMAT_RGBA32,
                    const_cast<unsigned char *>(result.rgba.data()), result.width * 4);
                if (surface) { browser->texture = SDL_CreateTextureFromSurface(ui->renderer, surface); SDL_DestroySurface(surface); }
            }
        }
        catch (const std::exception &e) { browser->error = e.what(); }
    }
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
    else if (browser->texture)
    {
        SDL_FRect destination = {x + 388, y + 106, 528, 297};
        SDL_RenderTexture(ui->renderer, browser->texture, nullptr, &destination);
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
