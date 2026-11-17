#include "dialog_host.h"
#include "scene_organiser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <limits>
#include <vector>
#include <iterator>
#include <unordered_map>

namespace
{
constexpr int frameThumbnailLimit = 4096;
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

struct Thumbnail
{
    int scene = -1, width = 0, height = 0;
    unsigned long used = 0;
    SDL_Texture *texture = nullptr;
};
}
struct MarcusSceneOrganiser
{
    bool loadedThisFrame = false;
    MarcusPlayer *player = nullptr;
    bool open = false;
    int count = 0, current = 0, selected = 0, result = -1;
    unsigned long clock = 0;
    std::array<Thumbnail, frameThumbnailLimit> thumbnails;
    std::unordered_map<int, Thumbnail *> thumbnailByIndex;
    Thumbnail empty;
    bool frames = false;
    int sourceScene = 0;
    long firstFrame = 0;
    MarcusFramePreviews *framePreviews = nullptr;
    char frameError[256] = {};
    float frameZoom = 1.0f;
    int previewWidth = 160, previewHeight = 120;
    bool previewSizeKnown = false;
    bool fitFrames = false;
    struct SceneFrames { long first; int count, offset; };
    std::vector<SceneFrames> scenes;
    int movieFrameCount = 0;
    bool wholeMovie = false;
};
namespace
{
float MinimumZoom(const MarcusSceneOrganiser *s)
{
    return std::min(1.0f, std::max({0.10f, 16.0f / s->previewWidth, 9.0f / s->previewHeight}));
}

void Confirm(MarcusSceneOrganiser *s)
{
    s->result = s->selected;
    s->open = false;
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
    s->thumbnailByIndex.clear();
    for (auto &t : s->thumbnails)
    {
        dialog_retire_texture(t.texture);
        t = {};
    }
    // A request already being rendered may finish; its scene/frame identity is
    // remapped into the new scope when consumed, rather than reusing a tile ID.
    std::array<MarcusFramePosition, 1> none;
    marcus_frame_previews_request_positions(s->framePreviews, none.data(), 0);
}
Thumbnail &OldestThumbnail(MarcusSceneOrganiser *s)
{
    return *std::min_element(s->thumbnails.begin(), s->thumbnails.begin() + (s->frames ? frameThumbnailLimit : 64),
                            [](const Thumbnail &a, const Thumbnail &b) { return a.used < b.used; });
}
void StoreThumbnail(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int index, const MarcusImage &image)
{
    const auto existing = s->thumbnailByIndex.find(index);
    auto &t = existing == s->thumbnailByIndex.end() ? OldestThumbnail(s) : *existing->second;
    if (t.scene >= 0) s->thumbnailByIndex.erase(t.scene);
    dialog_retire_texture(t.texture);
    t = {};
    t.scene = index;
    t.used = ++s->clock;
    s->thumbnailByIndex[index] = &t;
    if (!image.rgba || !image.width || !image.height) return;
    t.width = image.width;
    t.height = image.height;
    t.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, t.width, t.height);
    if (t.texture && !SDL_UpdateTexture(t.texture, nullptr, image.rgba, image.pitch))
    {
        dialog_retire_texture(t.texture);
        t.texture = nullptr;
    }
    if (t.texture) SDL_SetTextureScaleMode(t.texture, SDL_SCALEMODE_LINEAR);
}
Thumbnail &GetThumbnail(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int scene, bool &loaded)
{
    const auto found = s->thumbnailByIndex.find(scene);
    if (found != s->thumbnailByIndex.end())
    {
        found->second->used = ++s->clock;
        return *found->second;
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
void UpdateFrameThumbnails(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int first, int end)
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
    auto request = [&](int index) {
        if (count == int(requests.size())) return;
        if (s->thumbnailByIndex.find(index) == s->thumbnailByIndex.end()) requests[count++] = PositionAt(s, index);
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
    s->open = false;
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
    s->thumbnailByIndex.clear();
    for (auto &t : s->thumbnails)
    {
        dialog_retire_texture(t.texture);
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
    s->open = true;
    return 1;
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

}

int scene_organiser_visible(const MarcusSceneOrganiser *value) { return value && value->open; }

namespace {
void OrganiserState(void *p, MarcusDialogState *out)
{
    auto &s = *static_cast<MarcusSceneOrganiser *>(p); *out = {};
    out->open = s.open; out->kind = s.frames ? DIALOG_FRAMES : DIALOG_SCENES; out->count = s.count;
    out->selected = s.selected; out->current = s.current; out->whole_movie = s.wholeMovie; out->fit = s.fitFrames;
    out->zoom = s.frameZoom; out->minimum_zoom = MinimumZoom(&s); out->preview_width = float(s.previewWidth); out->preview_height = float(s.previewHeight);
    SDL_strlcpy(out->title, s.frames ? "Frame organiser" : "Scene organiser", sizeof(out->title));
    SDL_strlcpy(out->confirm, s.frames ? "Open frame" : "Open scene", sizeof(out->confirm));
    SDL_snprintf(out->status, sizeof(out->status), "%d %s / Select a thumbnail, then Open or double-click.", s.count, s.frames ? "frames" : "scenes");
    SDL_strlcpy(out->error, s.frameError, sizeof(out->error));
}
void OrganiserPrepare(void *p, SDL_Renderer *renderer, int first, int last)
{
    auto &s = *static_cast<MarcusSceneOrganiser *>(p); if (!s.open) return;
    if (first < 0) { s.loadedThisFrame = false; return; }
    try { if (s.frames) UpdateFrameThumbnails(&s, renderer, first, last); }
    catch (const std::exception &e) { SDL_strlcpy(s.frameError, e.what(), sizeof(s.frameError)); }
}
int OrganiserItem(void *p, SDL_Renderer *renderer, int index, MarcusDialogItem *out)
{
    auto &s = *static_cast<MarcusSceneOrganiser *>(p); *out = {};
    if (index < 0 || index >= s.count) return 0;
    try {
    auto &thumbnail = GetThumbnail(&s, renderer, index, s.loadedThisFrame); out->texture = thumbnail.texture;
    if (s.frames) { const auto position = PositionAt(&s, index); out->scene = position.scene; out->frame = position.frame;
        const auto first = s.scenes[position.scene].first;
        SDL_snprintf(out->label, sizeof(out->label), "Scene %d / frame %ld", position.scene + 1, position.frame - first + 1); }
    else { MarcusScene scene = {}; marcus_scene(s.player, index, &scene); out->scene = index; out->transition = scene.transition;
        SDL_snprintf(out->label, sizeof(out->label), "Scene %d / %s", index + 1, scene.name);
        SDL_snprintf(out->detail, sizeof(out->detail), "%ld frames / After scene: %s", scene.last_frame - scene.first_frame + 1, TransitionName(scene.transition)); }
    return 1;
    } catch (const std::exception &e) { SDL_strlcpy(s.frameError, e.what(), sizeof(s.frameError)); return 0; }
}
void OrganiserAction(void *p, int command, int index, float value, const char *)
{
    auto &s = *static_cast<MarcusSceneOrganiser *>(p);
    if (command == DIALOG_SELECT) s.selected = std::clamp(index, 0, s.count - 1);
    else if (command == DIALOG_ACCEPT) Confirm(&s);
    else if (command == DIALOG_CANCEL) scene_organiser_close(&s);
    else if (command == DIALOG_SCOPE) ToggleWholeMovie(&s);
    else if (command == DIALOG_ZOOM) { s.frameZoom = std::clamp(value, MinimumZoom(&s), 1.0f); if (!index) s.fitFrames = false; }
    else if (command == DIALOG_FIT) s.fitFrames = true;
}
}
extern "C" const MarcusDialogHost marcus_scene_dialog_host = {OrganiserState, OrganiserPrepare, OrganiserItem, OrganiserAction};
