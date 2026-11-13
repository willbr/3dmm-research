#include "scene_organiser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <limits>
#include <vector>
#include <iterator>

namespace
{
constexpr int cardHeight = 192, gap = 12;
// Retail scene sorter order (not the order of the on-disk transition IDs).
constexpr int transitions[] = {MARCUS_TRANSITION_CUT, MARCUS_TRANSITION_DISSOLVE,
                               MARCUS_TRANSITION_FADE_BLACK, MARCUS_TRANSITION_FADE_WHITE};
const char *TransitionName(int transition)
{
    switch (transition)
    {
    case MARCUS_TRANSITION_NONE: return "None";
    case MARCUS_TRANSITION_CUT: return "Cut";
    case MARCUS_TRANSITION_DISSOLVE: return "Dissolve";
    case MARCUS_TRANSITION_FADE_BLACK: return "Fade to black";
    case MARCUS_TRANSITION_FADE_WHITE: return "Fade to white";
    case MARCUS_TRANSITION_BLACK: return "Black";
    default: return "Unknown";
    }
}
void TransitionSymbol(MarcusUI *ui, float x, float y, int transition, bool active)
{
    // Scalable versions of retail's e3trn1..4 motifs: a hard cut, a ramp,
    // white through black, and black through white. Red marks the saved choice.
    ui_fill(ui, x, y, 22, 20, active ? 0xec2020 : 0x67758b);
    for (int column = 0; column < 16; ++column)
    {
        float value;
        const float position = column / 15.0f;
        if (transition == MARCUS_TRANSITION_CUT) value = column < 8 ? 1.0f : 0.0f;
        else if (transition == MARCUS_TRANSITION_DISSOLVE) value = 1.0f - position;
        else
        {
            value = std::clamp((std::abs(position - 0.5f) - 0.1f) / 0.3f, 0.0f, 1.0f);
            if (transition == MARCUS_TRANSITION_FADE_WHITE) value = 1.0f - value;
        }
        const unsigned long shade = 11 + static_cast<unsigned long>(238 * value);
        ui_fill(ui, x + 3 + column, y + 3, 1, 14, shade * 0x010101UL);
    }
}
struct Layout
{
    SDL_FRect panel, grid, track, thumb, cancel, confirm;
    SDL_FRect zoomOut, zoomIn, zoomTrack, zoomThumb;
    SDL_FRect wholeMovie;
    int columns, rows, maxScroll;
    float cellWidth, cellHeight, spacing;
};
struct Thumbnail
{
    int scene = -1, width = 0, height = 0;
    unsigned long used = 0;
    SDL_Texture *texture = nullptr;
};
}
struct MarcusSceneOrganiser
{
    MarcusPlayer *player = nullptr;
    bool open = false, dragging = false;
    int count = 0, current = 0, selected = 0, scroll = 0, result = -1;
    float dragOffset = 0;
    unsigned long clock = 0;
    std::array<Thumbnail, 512> thumbnails;
    Thumbnail empty;
    bool frames = false;
    int sourceScene = 0;
    long firstFrame = 0;
    MarcusFramePreviews *framePreviews = nullptr;
    char frameError[256] = {};
    float frameZoom = 1.0f;
    int previewWidth = 160, previewHeight = 120;
    bool previewSizeKnown = false;
    bool zoomDragging = false;
    struct SceneFrames { long first; int count, offset; };
    std::vector<SceneFrames> scenes;
    int movieFrameCount = 0;
    bool wholeMovie = false;
};
namespace
{
float MinimumZoom(const MarcusSceneOrganiser *s)
{
    return std::min(1.0f, std::max({0.25f, 40.0f / s->previewWidth, 30.0f / s->previewHeight}));
}
Layout Arrange(MarcusSceneOrganiser *s, int width, int height)
{
    Layout l = {};
    l.panel.w = std::min(1080.0f, width - 48.0f);
    l.panel.h = std::min(760.0f, height - 48.0f);
    l.panel.x = (width - l.panel.w) / 2;
    l.panel.y = (height - l.panel.h) / 2;
    l.grid = {l.panel.x + 24, l.panel.y + 74, l.panel.w - 68, l.panel.h - 156};
    l.spacing = s->frames ? 2.0f : float(gap);
    l.cellHeight = s->frames ? s->previewHeight * s->frameZoom : float(cardHeight);
    if (s->frames)
    {
        l.grid.y = l.panel.y + 66;
        l.grid.h = l.panel.h - 134;
        l.cellWidth = s->previewWidth * s->frameZoom;
        l.columns = std::max(1, int((l.grid.w + l.spacing) / (l.cellWidth + l.spacing)));
    }
    else
    {
        l.columns = std::max(1, int((l.grid.w + gap) / 188));
        l.cellWidth = (l.grid.w - (l.columns - 1) * gap) / l.columns;
    }
    l.rows = std::max(1, int((l.grid.h + l.spacing) / (l.cellHeight + l.spacing)));
    const int totalRows = std::max(l.rows, int((static_cast<long long>(s->count) + l.columns - 1) / l.columns));
    l.maxScroll = totalRows - l.rows;
    s->scroll = std::clamp(s->scroll, 0, l.maxScroll);
    l.track = {l.grid.x + l.grid.w + 12, l.grid.y, 12, l.rows * (l.cellHeight + l.spacing) - l.spacing};
    l.thumb = l.track;
    l.thumb.h = std::max(24.0f, l.track.h * l.rows / totalRows);
    if (l.maxScroll)
        l.thumb.y += (l.track.h - l.thumb.h) * s->scroll / l.maxScroll;
    l.cancel = {l.panel.x + l.panel.w - 132, l.panel.y + l.panel.h - 59,
                UI_DIALOG_BUTTON_WIDTH, UI_DIALOG_BUTTON_HEIGHT};
    l.confirm = l.cancel;
    l.confirm.x += 62;
    l.zoomOut = {l.panel.x + 24, l.panel.y + l.panel.h - 50, 28, 28};
    l.zoomTrack = {l.zoomOut.x + 40, l.zoomOut.y, 144, 28};
    l.zoomIn = {l.zoomTrack.x + l.zoomTrack.w + 12, l.zoomOut.y, 28, 28};
    const float zoomRange = std::max(0.001f, 1.0f - MinimumZoom(s));
    l.zoomThumb = {l.zoomTrack.x + (l.zoomTrack.w - 10) * (s->frameZoom - MinimumZoom(s)) / zoomRange,
                   l.zoomTrack.y + 4, 10, 20};
    l.wholeMovie = {l.panel.x + l.panel.w - 300, l.panel.y + 10, 144, 28};
    return l;
}
void SetZoom(MarcusSceneOrganiser *s, float zoom, int width, int height)
{
    const auto previous = Arrange(s, width, height);
    const int first = s->scroll * previous.columns;
    s->frameZoom = std::clamp(zoom, MinimumZoom(s), 1.0f);
    const auto next = Arrange(s, width, height);
    s->scroll = std::clamp(first / next.columns, 0, next.maxScroll);
    s->dragging = false;
}
void ZoomAt(MarcusSceneOrganiser *s, float x, int width, int height)
{
    const auto l = Arrange(s, width, height);
    const float minimum = MinimumZoom(s);
    SetZoom(s, minimum + (1.0f - minimum) * (x - l.zoomTrack.x - 5) / (l.zoomTrack.w - 10), width, height);
}
bool Inside(float x, float y, const SDL_FRect &r)
{
    return ui_inside(x, y, r.x, r.y, r.w, r.h) != 0;
}
void Reveal(MarcusSceneOrganiser *s, const Layout &l)
{
    const int row = s->selected / l.columns;
    if (row < s->scroll) s->scroll = row;
    if (row >= s->scroll + l.rows) s->scroll = row - l.rows + 1;
    s->scroll = std::clamp(s->scroll, 0, l.maxScroll);
}
void Confirm(MarcusSceneOrganiser *s)
{
    s->result = s->selected;
    s->open = s->dragging = false;
    marcus_frame_previews_destroy(s->framePreviews);
    s->framePreviews = nullptr;
}
MarcusFramePosition PositionAt(const MarcusSceneOrganiser *s, int index)
{
    if (!s->wholeMovie) return {s->sourceScene, static_cast<long>(static_cast<long long>(s->firstFrame) + index)};
    const auto next = std::upper_bound(s->scenes.begin(), s->scenes.end(), index,
                                      [](int value, const MarcusSceneOrganiser::SceneFrames &scene) {
                                          return value < scene.offset;
                                      });
    const auto scene = std::prev(next);
    return {int(scene - s->scenes.begin()), static_cast<long>(static_cast<long long>(scene->first) + index - scene->offset)};
}
int IndexAt(const MarcusSceneOrganiser *s, int scene, long frame)
{
    if (scene < 0 || scene >= int(s->scenes.size())) return -1;
    const auto &range = s->scenes[scene];
    const auto local = static_cast<long long>(frame) - range.first;
    if (local < 0 || local >= range.count || (!s->wholeMovie && scene != s->sourceScene)) return -1;
    return static_cast<int>(local) + (s->wholeMovie ? range.offset : 0);
}
void ToggleWholeMovie(MarcusSceneOrganiser *s)
{
    if (!s->movieFrameCount) return;
    const auto position = PositionAt(s, s->selected);
    s->wholeMovie = !s->wholeMovie;
    s->sourceScene = position.scene;
    s->firstFrame = s->scenes[position.scene].first;
    s->count = s->wholeMovie ? s->movieFrameCount : s->scenes[position.scene].count;
    s->selected = IndexAt(s, position.scene, position.frame);
    s->scroll = -1;
    s->dragging = s->zoomDragging = false;
    for (auto &t : s->thumbnails)
    {
        SDL_DestroyTexture(t.texture);
        t = {};
    }
    // A request already being rendered may finish; its scene/frame identity is
    // remapped into the new scope when consumed, rather than reusing a tile ID.
    std::array<MarcusFramePosition, 1> none;
    marcus_frame_previews_request_positions(s->framePreviews, none.data(), 0);
}
Thumbnail &OldestThumbnail(MarcusSceneOrganiser *s)
{
    return *std::min_element(s->thumbnails.begin(), s->thumbnails.begin() + (s->frames ? 512 : 64),
                            [](const Thumbnail &a, const Thumbnail &b) { return a.used < b.used; });
}
void StoreThumbnail(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int index, const MarcusImage &image)
{
    auto &t = OldestThumbnail(s);
    SDL_DestroyTexture(t.texture);
    t = {};
    t.scene = index;
    t.used = ++s->clock;
    if (!image.rgba || !image.width || !image.height) return;
    t.width = image.width;
    t.height = image.height;
    t.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.width, t.height);
    if (t.texture && !SDL_UpdateTexture(t.texture, nullptr, image.rgba, image.pitch))
    {
        SDL_DestroyTexture(t.texture);
        t.texture = nullptr;
    }
    if (t.texture) SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_LINEAR);
}
Thumbnail &GetThumbnail(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int scene, bool &loaded)
{
    for (auto &t : s->thumbnails)
        if (t.scene == scene)
        {
            t.used = ++s->clock;
            return t;
        }
    if (s->frames) return s->empty;
    // Decode at most one saved thumbnail per draw to keep input responsive.
    if (loaded) return s->empty;
    loaded = true;
    MarcusImage image = {};
    marcus_scene_thumbnail(s->player, scene, &image);
    StoreThumbnail(s, renderer, scene, image);
    return GetThumbnail(s, renderer, scene, loaded);
}
void UpdateFrameThumbnails(MarcusSceneOrganiser *s, SDL_Renderer *renderer, const Layout &l)
{
    for (int n = 0; n < 4; ++n)
    {
        MarcusImage image = {};
        const auto status = marcus_frame_previews_take(s->framePreviews, &image, s->frameError, sizeof(s->frameError));
        if (status <= 0) break;
        if (!s->previewSizeKnown)
        {
            s->previewWidth = image.width;
            s->previewHeight = image.height;
            s->frameZoom = std::clamp(s->frameZoom, MinimumZoom(s), 1.0f);
            s->previewSizeKnown = true;
        }
        const auto index = IndexAt(s, image.scene, image.frame);
        if (index >= 0 && index < s->count) StoreThumbnail(s, renderer, static_cast<int>(index), image);
    }
    if (s->frameError[0]) return;
    std::array<MarcusFramePosition, 32> requests;
    int count = 0;
    const int first = s->scroll * l.columns;
    const int end = int(std::min(static_cast<long long>(s->count), static_cast<long long>(first) + l.rows * l.columns));
    auto request = [&](int index) {
        if (count == int(requests.size())) return;
        if (std::none_of(s->thumbnails.begin(), s->thumbnails.end(),
                         [&](const Thumbnail &t) { return t.scene == index; })) requests[count++] = PositionAt(s, index);
    };
    if (s->selected >= first && s->selected < end) request(s->selected);
    for (int index = first; index < end; ++index)
        if (index != s->selected) request(index);
    marcus_frame_previews_request_positions(s->framePreviews, requests.data(), count);
}
}
extern "C"
{
MarcusSceneOrganiser *scene_organiser_create(void)
{
    return new (std::nothrow) MarcusSceneOrganiser;
}
void scene_organiser_close(MarcusSceneOrganiser *s)
{
    if (!s) return;
    s->open = s->dragging = false;
    s->zoomDragging = false;
    s->result = -1;
    marcus_frame_previews_destroy(s->framePreviews);
    s->framePreviews = nullptr;
    s->frames = false;
    s->frameError[0] = 0;
    s->previewSizeKnown = false;
    s->previewWidth = 160;
    s->previewHeight = 120;
    s->scenes.clear();
    s->movieFrameCount = 0;
    for (auto &t : s->thumbnails)
    {
        SDL_DestroyTexture(t.texture);
        t = {};
    }
}
void scene_organiser_destroy(MarcusSceneOrganiser *s)
{
    scene_organiser_close(s);
    delete s;
}
void scene_organiser_show(MarcusSceneOrganiser *s, MarcusPlayer *player)
{
    if (!s) return;
    scene_organiser_close(s);
    MarcusInfo info = {};
    if (!marcus_info(player, &info) || !info.scene_count) return;
    s->player = player;
    s->count = info.scene_count;
    s->current = s->selected = info.scene;
    s->scroll = -1; // First layout reveals the current scene at any window size.
    s->open = true;
}
int scene_organiser_show_frames(MarcusSceneOrganiser *s, MarcusPlayer *player)
{
    if (!s) return 0;
    scene_organiser_close(s);
    MarcusInfo info = {};
    if (!marcus_info(player, &info)) return 0;
    const auto count = static_cast<long long>(info.last_frame) - info.first_frame + 1;
    if (count <= 0 || count > (std::numeric_limits<int>::max)()) return 0;
    long long total = 0;
    for (int index = 0; index < info.scene_count; ++index)
    {
        MarcusScene scene = {};
        if (!marcus_scene(player, index, &scene)) return 0;
        const auto length = static_cast<long long>(scene.last_frame) - scene.first_frame + 1;
        if (length <= 0 || length > (std::numeric_limits<int>::max)() ||
            total + length > (std::numeric_limits<int>::max)()) return 0;
        s->scenes.push_back({scene.first_frame, int(length), int(total)});
        total += length;
    }
    s->movieFrameCount = int(total);
    s->framePreviews = marcus_frame_previews_create(player);
    if (!s->framePreviews) return 0;
    s->player = player;
    s->frames = true;
    s->sourceScene = info.scene;
    s->firstFrame = info.first_frame;
    s->count = s->wholeMovie ? s->movieFrameCount : static_cast<int>(count);
    s->current = s->selected = IndexAt(s, info.scene, info.frame);
    s->scroll = -1;
    s->open = true;
    return 1;
}
int scene_organiser_event(MarcusSceneOrganiser *s, const SDL_Event *event, int width, int height)
{
    if (!s || !s->open) return 0;
    const bool initial = s->scroll < 0;
    auto l = Arrange(s, width, height);
    if (initial) Reveal(s, l);
    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        long long selection = s->selected;
        switch (event->key.key)
        {
        case SDLK_MINUS: case SDLK_KP_MINUS:
            if (s->frames) SetZoom(s, s->frameZoom - 0.125f, width, height);
            return 1;
        case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS:
            if (s->frames) SetZoom(s, s->frameZoom + 0.125f, width, height);
            return 1;
        case SDLK_ESCAPE: scene_organiser_close(s); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: Confirm(s); break;
        case SDLK_LEFT: --selection; break;
        case SDLK_RIGHT: ++selection; break;
        case SDLK_UP: selection -= l.columns; break;
        case SDLK_DOWN: selection += l.columns; break;
        case SDLK_PAGEUP: selection -= l.rows * l.columns; break;
        case SDLK_PAGEDOWN: selection += l.rows * l.columns; break;
        case SDLK_HOME: selection = 0; break;
        case SDLK_END: selection = s->count - 1; break;
        default: return 1;
        }
        s->selected = int(std::clamp(selection, 0LL, static_cast<long long>(s->count) - 1));
        Reveal(s, l);
    }
    else if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        const auto delta = event->wheel.y * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        if (s->frames && (SDL_GetModState() & SDL_KMOD_CTRL))
            SetZoom(s, s->frameZoom + delta * 0.125f, width, height);
        else
            s->scroll = std::clamp(s->scroll - int(std::ceil(std::abs(delta))) * (delta > 0 ? 1 : -1), 0, l.maxScroll);
    }
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        const auto x = event->button.x, y = event->button.y;
        if (Inside(x, y, l.cancel)) scene_organiser_close(s);
        else if (Inside(x, y, l.confirm)) Confirm(s);
        else if (s->frames && Inside(x, y, l.wholeMovie)) ToggleWholeMovie(s);
        else if (s->frames && Inside(x, y, l.zoomOut)) SetZoom(s, s->frameZoom - 0.125f, width, height);
        else if (s->frames && Inside(x, y, l.zoomIn)) SetZoom(s, s->frameZoom + 0.125f, width, height);
        else if (s->frames && Inside(x, y, l.zoomTrack))
        {
            s->zoomDragging = true;
            ZoomAt(s, x, width, height);
        }
        else if (l.maxScroll && Inside(x, y, l.thumb))
        {
            s->dragging = true;
            s->dragOffset = y - l.thumb.y;
        }
        else if (l.maxScroll && Inside(x, y, l.track))
            s->scroll = std::clamp(s->scroll + (y < l.thumb.y ? -l.rows : l.rows), 0, l.maxScroll);
        else if (Inside(x, y, l.grid))
        {
            const int column = int((x - l.grid.x) / (l.cellWidth + l.spacing));
            const int row = int((y - l.grid.y) / (l.cellHeight + l.spacing));
            const auto index = static_cast<long long>(row + s->scroll) * l.columns + column;
            if (column < l.columns && row < l.rows && index < s->count &&
                x - l.grid.x - column * (l.cellWidth + l.spacing) < l.cellWidth &&
                y - l.grid.y - row * (l.cellHeight + l.spacing) < l.cellHeight)
            {
                s->selected = static_cast<int>(index);
                if (event->button.clicks >= 2) Confirm(s);
            }
        }
    }
    else if (event->type == SDL_EVENT_MOUSE_MOTION && s->zoomDragging)
        ZoomAt(s, event->motion.x, width, height);
    else if (event->type == SDL_EVENT_MOUSE_MOTION && s->dragging && l.maxScroll)
        s->scroll = std::clamp(int(std::lround((event->motion.y - s->dragOffset - l.track.y) *
                                             l.maxScroll / (l.track.h - l.thumb.h))), 0, l.maxScroll);
    else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_WINDOW_FOCUS_LOST)
        s->dragging = s->zoomDragging = false;
    // Window lifecycle and quit must still reach the application.
    return event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
           event->type == SDL_EVENT_TEXT_INPUT || event->type == SDL_EVENT_MOUSE_MOTION ||
           event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
           event->type == SDL_EVENT_MOUSE_WHEEL || event->type == SDL_EVENT_DROP_FILE;
}
int scene_organiser_take(MarcusSceneOrganiser *s)
{
    if (!s || s->frames) return -1;
    const int result = s->result;
    s->result = -1;
    return result;
}
int scene_organiser_take_frame(MarcusSceneOrganiser *s, int *scene, long *frame)
{
    if (!s || !s->frames || s->result < 0 || !scene || !frame) return 0;
    const auto position = PositionAt(s, s->result);
    *scene = position.scene;
    *frame = position.frame;
    s->result = -1;
    return 1;
}
void scene_organiser_draw(MarcusSceneOrganiser *s, MarcusUI *ui, int width, int height)
{
    if (!s || !s->open) return;
    const bool initial = s->scroll < 0;
    auto l = Arrange(s, width, height);
    if (initial) { Reveal(s, l); l = Arrange(s, width, height); }
    if (s->frames)
    {
        UpdateFrameThumbnails(s, ui->renderer, l);
        l = Arrange(s, width, height);
    }
    ui_opacity(ui, 0.75f);
    ui_fill(ui, 0, 0, float(width), float(height), 0x080b12);
    ui_opacity(ui, 1);
    ui_fill(ui, l.panel.x - 2, l.panel.y - 2, l.panel.w + 4, l.panel.h + 4, 0x7c8aa3);
    ui_fill(ui, l.panel.x, l.panel.y, l.panel.w, l.panel.h, 0x252d40);
    ui_fill(ui, l.panel.x, l.panel.y, l.panel.w, 50, 0x323f5a);
    ui_heading(ui, l.panel.x + 24, l.panel.y + 16, s->frames ? "FRAME ORGANISER" : "SCENE ORGANISER", 0xf0f2f7);
    char text[256];
    SDL_snprintf(text, sizeof(text), "%d %s", s->count, s->frames ? "frames" : "scenes");
    ui_text(ui, l.panel.x + l.panel.w - 126, l.panel.y + 16, text, 0xc5cedd);
    if (s->frames)
        ui_button_sized(ui, l.wholeMovie.x, l.wholeMovie.y, l.wholeMovie.w, l.wholeMovie.h,
                        "Whole movie", s->wholeMovie, s->movieFrameCount > 0);
    bool loaded = false;
    MarcusScene selectedScene = {};
    if (!s->frames) marcus_scene(s->player, s->selected, &selectedScene);
    char transitionHint[192];
    SDL_snprintf(transitionHint, sizeof(transitionHint), "After scene %d: %s", s->selected + 1,
                 TransitionName(selectedScene.transition));
    if (s->frames)
        SDL_snprintf(transitionHint, sizeof(transitionHint), "Scene %d / Frame %d%s", s->sourceScene + 1,
                     s->selected + 1, s->frameError[0] ? " / Thumbnails unavailable" : "");
    for (int n = 0; n < l.rows * l.columns; ++n)
    {
        const auto position = static_cast<long long>(s->scroll) * l.columns + n;
        if (position >= s->count) break;
        const int index = static_cast<int>(position);
        const float x = l.grid.x + (n % l.columns) * (l.cellWidth + l.spacing);
        const float y = l.grid.y + (n / l.columns) * (l.cellHeight + l.spacing);
        const bool selected = index == s->selected;
        const bool hovered = ui_inside(ui->mouse_x, ui->mouse_y, x, y, l.cellWidth, l.cellHeight) != 0;
        if (s->frames)
        {
            if (selected || hovered)
                ui_fill(ui, x - 1, y - 1, l.cellWidth + 2, l.cellHeight + 2, selected ? 0x9edbd0 : 0x7387a6);
            const SDL_FRect picture = {x, y, l.cellWidth, l.cellHeight};
            ui_fill(ui, x, y, picture.w, picture.h, 0x0b0e15);
            auto &t = GetThumbnail(s, ui->renderer, index, loaded);
            if (t.scene == index && t.texture)
            {
                // Different scenes may use different camera aspect ratios.
                const auto scale = std::min(picture.w / t.width, picture.h / t.height);
                const SDL_FRect dest = {picture.x + (picture.w - t.width * scale) / 2,
                                        picture.y + (picture.h - t.height * scale) / 2,
                                        t.width * scale, t.height * scale};
                SDL_RenderTexture(ui->renderer, t.texture, nullptr, &dest);
            }
            continue;
        }
        ui_fill(ui, x - 2, y - 2, l.cellWidth + 4, cardHeight + 4,
                selected ? 0x9edbd0 : hovered ? 0x7387a6 : 0x45516a);
        ui_fill(ui, x, y, l.cellWidth, cardHeight, selected ? 0x3c5060 : 0x1c2331);
        SDL_FRect picture = {x + 14, y + 10, l.cellWidth - 28, 100};
        ui_fill(ui, picture.x, picture.y, picture.w, picture.h, 0x0b0e15);
        auto &t = GetThumbnail(s, ui->renderer, index, loaded);
        if (t.scene == index && t.texture)
        {
            const float scale = std::min(picture.w / t.width, picture.h / t.height);
            SDL_FRect dest = {picture.x + (picture.w - t.width * scale) / 2,
                              picture.y + (picture.h - t.height * scale) / 2, t.width * scale, t.height * scale};
            SDL_RenderTexture(ui->renderer, t.texture, nullptr, &dest);
        }
        else ui_text(ui, picture.x + 12, picture.y + 40,
                     s->frames && s->frameError[0] ? "Unavailable" : t.scene == index ? "No thumbnail" : "Loading...", 0x909cb0);
        for (int hole = 0; hole < 6; ++hole)
        {
            ui_fill(ui, x + 4, y + 12 + hole * 16, 5, 8, 0x8792a4);
            ui_fill(ui, x + l.cellWidth - 9, y + 12 + hole * 16, 5, 8, 0x8792a4);
        }
        MarcusScene scene = {};
        marcus_scene(s->player, index, &scene);
        SDL_snprintf(text, sizeof(text), "%d. %s", index + 1, scene.name[0] ? scene.name : "Untitled scene");
        ui_label(ui, x + 10, y + 118, l.cellWidth - 20, text, 0xe7edf5);
        SDL_snprintf(text, sizeof(text), "%ld frames%s", scene.last_frame - scene.first_frame + 1,
                     index == s->current ? " / Current" : "");
        ui_label(ui, x + 10, y + 140, l.cellWidth - 20, text, 0xafbdce);
        for (int effect = 0; effect < 4; ++effect)
        {
            const float iconX = x + 10 + effect * 26;
            TransitionSymbol(ui, iconX, y + 164, transitions[effect], scene.transition == transitions[effect]);
            if (ui_inside(ui->mouse_x, ui->mouse_y, iconX, y + 164, 22, 20))
                SDL_snprintf(transitionHint, sizeof(transitionHint), "After scene %d: %s%s", index + 1,
                             TransitionName(transitions[effect]),
                             scene.transition == transitions[effect] ? " (saved)" : "");
        }
        const char *shortName = scene.transition == MARCUS_TRANSITION_FADE_BLACK ? "Black" :
                                scene.transition == MARCUS_TRANSITION_FADE_WHITE ? "White" :
                                TransitionName(scene.transition);
        ui_label(ui, x + 116, y + 166, l.cellWidth - 126, shortName, 0xc5cedd);
    }
    ui_fill(ui, l.track.x, l.track.y, l.track.w, l.track.h, 0x151b28);
    if (l.maxScroll) ui_fill(ui, l.thumb.x, l.thumb.y, l.thumb.w, l.thumb.h, s->dragging ? 0x9edbd0 : 0x7387a6);
    if (s->frames)
    {
        ui_button_sized(ui, l.zoomOut.x, l.zoomOut.y, l.zoomOut.w, l.zoomOut.h, "-", 0, s->frameZoom > MinimumZoom(s));
        ui_button_sized(ui, l.zoomIn.x, l.zoomIn.y, l.zoomIn.w, l.zoomIn.h, "+", 0, s->frameZoom < 1.0f);
        ui_fill(ui, l.zoomTrack.x, l.zoomTrack.y + 12, l.zoomTrack.w, 4, 0x151b28);
        ui_fill(ui, l.zoomThumb.x, l.zoomThumb.y, l.zoomThumb.w, l.zoomThumb.h,
                s->zoomDragging ? 0x9edbd0 : 0x7387a6);
        SDL_snprintf(text, sizeof(text), "%.0f%%", s->frameZoom * 100);
        ui_text(ui, l.zoomIn.x + 40, l.zoomIn.y + 5, text, 0xc5cedd);
        if (s->frameError[0])
            ui_label(ui, l.zoomIn.x + 100, l.zoomIn.y + 5, l.panel.w - 500, "Thumbnails unavailable", 0xc5cedd);
    }
    else
    {
        ui_label(ui, l.panel.x + 24, l.panel.y + l.panel.h - 66, l.panel.w - 180, transitionHint, 0xe7edf5);
        ui_label(ui, l.panel.x + 24, l.panel.y + l.panel.h - 44, l.panel.w - 180,
                 "Select a scene / Enter or double-click to open / Esc to cancel", 0xc5cedd);
    }
    ui_dialog_button(ui, l.cancel.x, l.cancel.y, 0, 1, "Cancel / Escape");
    ui_dialog_button(ui, l.confirm.x, l.confirm.y, 1, 1,
                     s->frames ? "Open selected frame / Enter" : "Open selected scene / Enter");
}
}
