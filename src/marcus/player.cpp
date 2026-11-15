#include "player.h"
#include "mm/movie_data.h"
#include "mm/playback.h"
#include "content.h"
#include "movie_input.h"
#include "playback.h"
#include "gpu_renderer.h"
#include "asm_renderer.h"
#include "waveform.h"
#include "sound_timeline.h"
#include "timing_job.h"
#include "frame_previews.h"
#include "scene_edit.h"
#include "mm/preview.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <tuple>
#include <exception>
#include <stdexcept>
#include <cmath>

namespace
{
const std::uint32_t sceneType = 0x5343454e, movieType = 0x4d564945, gstType = 0x47535420;
template <std::size_t N> void Copy(char (&output)[N], const char *input)
{
    const auto count = std::min(N - 1, std::strlen(input));
    std::memcpy(output, input, count);
    output[count] = 0;
}
template <std::size_t N> void Copy(char (&output)[N], const std::string &input)
{
    // Do not cut a UTF-8 sequence in half at the fixed C record boundary.
    auto count = std::min(N - 1, input.size());
    while (count && count < input.size() && (static_cast<unsigned char>(input[count]) & 0xc0) == 0x80)
        --count;
    std::memcpy(output, input.data(), count);
    output[count] = 0;
}
std::int32_t Word(const std::vector<std::uint8_t> &bytes, bool big)
{
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= std::uint32_t(bytes[i]) << ((big ? 3 - i : i) * 8);
    std::int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
} // namespace
struct MarcusPlayer
{
    // Destroy the renderer before deleting any private VMM working files.
    std::unique_ptr<MarcusContent> content;
    std::unique_ptr<MarcusASMRenderer> asmRenderer;
    MM::Preview preview;
    // Keep devices alive when movie and work cameras use different backends.
    std::unique_ptr<MM::PreviewRasterizer> rasterizers[MARCUS_RENDERER_COUNT];
    int renderer = MARCUS_RENDERER_BRENDER;
    MarcusRenderOptions renderOptions = {};
    int orbitObject = -1;
    MM::PreviewImage image;
    MM::PreviewImage thumbnail;
    MM::PreviewImage movieImage;
    int movieImageScene = -1;
    std::int32_t movieImageFrame = 0;
    MarcusPlayback playback;
    std::vector<MM::Id> scenes;
    std::vector<MarcusScene> sceneInfo;
    std::vector<MarcusBackground> backgrounds;
    std::vector<MarcusProp> props;
    std::vector<MM::PreviewObject> objects;
    std::map<int, MM::PreviewObject> propPreviews;
    struct AnimationCache
    {
        MM::PreviewAnimationTimeline timeline;
        MM::Error error;
        bool available = false;
    };
    std::map<int, AnimationCache> animations;
    std::vector<MarcusSound> sounds;
    std::vector<MarcusSoundTrack> soundTracks;
    std::vector<MM::PreviewSoundCue> soundCues;
    MarcusWaveformCache waveforms;
    MarcusSoundTimeline soundTimeline;
    int playbackRate = 0, playbackModifier = 0;
    bool restoreAudio = true;
    bool finished = false;
    bool inspectionMatched = false;
    bool preserveEditFraming = false;
    unsigned internalOpenDepth = 0;
    bool modified = false;
    struct UndoState
    {
        std::shared_ptr<MarcusMovieFile> file;
        int scene;
        long frame;
        bool modified;
    };
    std::vector<UndoState> undo;
    float inspectionRoll = 0, inspectionFov = 45;
    std::map<std::int32_t, std::string> names;
    char error[1024] = {};
    int selected = -1;
    unsigned long document = 0;
    std::shared_ptr<MarcusMovieFile> timingInput;
    std::string timingResources, timingContent;
    bool timingStarted = false, timingFailed = false;
    // Cancel/join before releasing movie/content leases and the live preview.
    std::unique_ptr<MarcusTimingJob> timingJob;
};
namespace
{
// Internal reopens retain history; ordinary opens start a new document session.
struct KeepHistory
{
    MarcusPlayer &player;
    explicit KeepHistory(MarcusPlayer &value) : player(value) { ++player.internalOpenDepth; }
    ~KeepHistory() { --player.internalOpenDepth; }
};
struct EditHistory : KeepHistory
{
    MarcusPlayer::UndoState before;
    explicit EditHistory(MarcusPlayer &value) : KeepHistory(value),
        before{value.timingInput, value.selected, value.selected < 0 ? 0 : value.preview.Info().currentFrame, value.modified}
    {
        // Reserve before replacing the live document, so recording success cannot allocate.
        player.undo.reserve(player.undo.size() + 1);
    }
    void Commit()
    {
        if (player.undo.size() == 64) player.undo.erase(player.undo.begin());
        player.undo.push_back(std::move(before));
        player.modified = true;
    }
};
MarcusMovieInput &PrepareInput(MarcusMovieInput &input, const std::shared_ptr<MarcusMovieFile> &file)
{
    input.Open(file->path.u8string());
    // Materialize a new wrapper: never make a retained history lease point to itself.
    auto prepared = input.Materialize(0);
    prepared->origin = file;
    prepared->contentOrigin = file->contentOrigin;
    prepared->fonts = file->fonts;
    prepared->fontFiles = file->fontFiles;
    prepared->fontDirectory = file->fontDirectory;
    input.prepared = std::move(prepared);
    return input;
}
}
bool MarcusWriteTiming(MarcusPlayer *player, std::ostream &output)
{
    return player && player->soundTimeline.Size() == player->scenes.size() && player->soundTimeline.Write(output);
}
namespace
{
MarcusPlayer::AnimationCache *Animation(MarcusPlayer *player, int object, MM::Error &error)
{
    if (object < 0 || static_cast<std::size_t>(object) >= player->objects.size())
        return nullptr;
    auto found = player->animations.find(object);
    if (found == player->animations.end())
    {
        MarcusPlayer::AnimationCache cache;
        cache.available = player->preview.ReadAnimationTimeline(object, cache.timeline, cache.error);
        found = player->animations.emplace(object, std::move(cache)).first;
    }
    if (!found->second.available)
    {
        error = found->second.error;
        return nullptr;
    }
    return &found->second;
}
bool ReadSoundTimeline(MarcusPlayer *player, int index, MM::Error &error)
{
    while (player->soundTimeline.Size() <= static_cast<std::size_t>(index))
    {
        const auto next = player->soundTimeline.Size();
        MM::PreviewPresentation presentation;
        MarcusSoundTiming timing;
        if (!player->preview.ReadTimingPresentation(player->scenes[next], presentation, error) ||
            !MarcusPlayback::ReadTiming(presentation, timing, error, player->timingInput->fonts))
            return false;
        player->soundTimeline.Append(player->sceneInfo[next], presentation.sounds, [&](const MM::PreviewSoundCue &cue) {
            return player->waveforms.Length(player->preview, cue);
        }, timing);
    }
    return true;
}
bool ReadSoundSheet(MarcusPlayer *player, int index, MM::Error &error)
{
    player->soundCues.clear();
    if (!ReadSoundTimeline(player, index, error))
        return false;
    const auto &scene = player->soundTimeline.Scene(index);
    // Separate scene and actor lanes, including actor ID zero. Keep styles in
    // separate lanes so simultaneous speech and effects remain distinguishable.
    std::map<std::tuple<bool, std::int32_t, std::int32_t>, int> lanes;
    for (const auto &span : scene.sounds)
    {
        const auto &cue = span->cue;
        lanes.emplace(std::make_tuple(cue.actorSound, cue.actorSound ? cue.actor : 0, cue.style), 0);
    }
    for (auto &lane : lanes)
    {
        MarcusSoundTrack track = {};
        const auto actor = std::get<1>(lane.first), style = std::get<2>(lane.first);
        track.actor_sound = std::get<0>(lane.first);
        track.actor_id = actor;
        track.style = style;
        const char *kind = style == MARCUS_SOUND_MUSIC ? "Music" : style == MARCUS_SOUND_SPEECH ? "Speech"
                           : style == MARCUS_SOUND_EFFECT ? "Effects" : "Sound";
        std::string owner = "Scene";
        if (std::get<0>(lane.first))
        {
            const auto name = player->names.find(actor);
            owner = name != player->names.end() ? name->second : "Actor " + std::to_string(actor);
        }
        Copy(track.name, owner + " / " + kind);
        lane.second = static_cast<int>(player->soundTracks.size());
        player->soundTracks.push_back(track);
    }
    for (const auto &span : scene.sounds)
    {
        const auto &cue = span->cue;
        MarcusSound sound = {};
        sound.track = lanes.at(std::make_tuple(cue.actorSound, cue.actorSound ? cue.actor : 0, cue.style));
        Copy(sound.name, cue.silent ? "Stop / silence" : "Sound " + std::to_string(cue.sound.number));
        const auto name = player->names.find(cue.actor);
        Copy(sound.owner, !cue.actorSound ? std::string("Scene")
                          : name != player->names.end() ? name->second : "Actor " + std::to_string(cue.actor));
        sound.carried = span->scene != index;
        sound.origin_scene = span->scene;
        sound.origin_frame = double(cue.frame) - player->sceneInfo[span->scene].first_frame + 1;
        sound.frame = sound.carried ? scene.info.first_frame : cue.frame;
        sound.timeline_frame = scene.FrameAt(span->start);
        sound.end_frame = std::min(double(scene.info.last_frame) + 1, scene.FrameAt(span->end));
        sound.actor_id = cue.actor;
        sound.source_id = cue.source;
        sound.style = cue.style;
        sound.volume = cue.volume;
        sound.motion_cel = cue.motionCel;
        sound.chunk = cue.sound.number;
        sound.child = cue.child;
        sound.actor_sound = cue.actorSound;
        sound.loop = cue.loop;
        sound.queue = cue.queue;
        sound.silent = cue.silent;
        sound.motion = cue.motion;
        ++player->soundTracks[sound.track].cue_count;
        player->sounds.push_back(sound);
        player->soundCues.push_back(cue);
    }
    return true;
}
MarcusProjection CopyProjection(const MM::PreviewProjection &source)
{
    MarcusProjection result = {};
    result.valid = source.valid;
    std::memcpy(result.transform, source.transform, sizeof(result.transform));
    result.vertical_fov = source.verticalFov;
    result.aspect = source.aspect;
    result.near_clip = source.nearClip;
    result.far_clip = source.farClip;
    result.display_depth = source.displayDepth;
    result.orthographic = source.orthographic;
    result.ortho_height = source.orthoHeight;
    return result;
}
MM::PreviewProjection ReadProjection(const MarcusProjection &source)
{
    MM::PreviewProjection result;
    result.valid = source.valid != 0;
    std::memcpy(result.transform, source.transform, sizeof(result.transform));
    result.verticalFov = source.vertical_fov;
    result.aspect = source.aspect;
    result.nearClip = source.near_clip;
    result.farClip = source.far_clip;
    result.displayDepth = source.display_depth;
    result.orthographic = source.orthographic != 0;
    result.orthoHeight = source.ortho_height;
    return result;
}
bool Render(MarcusPlayer *player, const MM::PreviewView &view, MarcusImage *image, MM::Error &error)
{
    auto options = view;
    options.renderAtOutputResolution = true;
    options.fitSceneDepth = true;
    const auto &settings = player->renderOptions;
    const int scale = marcus_renderer_is_gpu(player->renderer) ? 1 << settings.antialias : 1;
    if (view.width > MM::PreviewMaxDimension / scale || view.height > MM::PreviewMaxDimension / scale)
    {
        error.message = "Supersampling exceeds the 4096-pixel render limit. Choose a smaller resolution or turn AA off.";
        return false;
    }
    options.width *= scale;
    options.height *= scale;
    options.displayMode = settings.display_mode;
    options.orthographic = !view.authoredCamera && settings.orthographic;
    options.axisView = !view.authoredCamera ? settings.axis_view : 0;
    if (options.axisView) { options.roll = 0; options.verticalFov = 45; }
    options.rasterizer = player->rasterizers[player->renderer].get();
    if (options.rasterizer) MarcusConfigureGPURenderer(options.rasterizer, settings);
    options.includeDepth = !view.authoredCamera || view.freeCamera;
    if (!image || player->selected < 0)
        return false;
    // The out-of-process ASM worker reads committed archives. Live edits use
    // the local evaluator until release/cancel, then return to the chosen backend.
    if (player->renderer == MARCUS_RENDERER_BRENDER_ASM && player->propPreviews.empty())
    {
        if (!player->asmRenderer)
            player->asmRenderer = std::make_unique<MarcusASMRenderer>(player->content->movie.u8string(),
                player->timingResources, player->timingContent);
        if (!player->asmRenderer->Render(player->selected, player->preview.Info().currentFrame, options, player->image, error))
        { player->asmRenderer.reset(); return false; }
    }
    else if (!player->preview.Render(options, player->image, error)) return false;
    if (!player->playback.Composite(player->preview, view.authoredCamera && !view.freeCamera, player->image, error))
        return false;
    if (scale > 1)
    {
        MM::PreviewImage reduced;
        reduced.camera = player->image.camera;
        reduced.projection = player->image.projection;
        reduced.sceneCamera = player->image.sceneCamera;
        reduced.width = view.width; reduced.height = view.height;
        reduced.rgba.assign(std::size_t(view.width) * view.height * 4, 0);
        reduced.depth.clear();
        const bool depth = !player->image.depth.empty();
        if (depth) reduced.depth.assign(std::size_t(view.width) * view.height, std::numeric_limits<float>::infinity());
        reduced.coveredPixels = 0;
        for (unsigned y = 0; y < view.height; ++y)
            for (unsigned x = 0; x < view.width; ++x)
            {
                unsigned alpha = 0, colour[3] = {};
                const auto target = std::size_t(y) * view.width + x;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx)
                    {
                        const auto source = std::size_t(y * scale + sy) * player->image.width + x * scale + sx;
                        const auto pixel = &player->image.rgba[source * 4];
                        alpha += pixel[3];
                        for (int channel = 0; channel < 3; ++channel) colour[channel] += pixel[channel] * pixel[3];
                        if (depth) reduced.depth[target] = std::min(reduced.depth[target], player->image.depth[source]);
                    }
                for (int channel = 0; channel < 3; ++channel)
                    reduced.rgba[target * 4 + channel] = alpha ? static_cast<unsigned char>(colour[channel] / alpha) : 0;
                reduced.rgba[target * 4 + 3] = static_cast<unsigned char>(alpha / (scale * scale));
                reduced.coveredPixels += reduced.rgba[target * 4 + 3] != 0;
            }
        player->image = std::move(reduced);
    }
    if (view.authoredCamera && !view.freeCamera)
    {
        player->movieImage = player->image;
        player->movieImageScene = player->selected;
        player->movieImageFrame = player->preview.Info().currentFrame;
    }
    image->width = static_cast<int>(player->image.width);
    image->height = static_cast<int>(player->image.height);
    image->pitch = image->width * 4;
    image->rgba = player->image.rgba.data();
    const auto &camera = player->image.camera;
    image->camera = {camera.x, camera.y, camera.z, camera.yaw, camera.pitch, camera.moveSpeed};
    image->projection = CopyProjection(player->image.projection);
    image->scene_camera = CopyProjection(player->image.sceneCamera);
    image->scene = player->selected;
    image->frame = player->preview.Info().currentFrame;
    image->authored_camera = view.authoredCamera && !view.freeCamera;
    image->document = player->document;
    return true;
}
template <typename F> int Guard(MarcusPlayer *player, F operation)
{
    if (!player)
        return 0;
    player->error[0] = 0;
    try
    {
        MM::Error error;
        if (operation(error))
            return 1;
        Copy(player->error, error.message.empty() ? "The player could not complete this operation." : error.message);
    }
    catch (const std::exception &error)
    {
        Copy(player->error, error.what());
    }
    catch (...)
    {
        Copy(player->error, "Unexpected error in the movie backend.");
    }
    return 0;
}
bool CancelPropPreviews(MarcusPlayer *player, MM::Error &error)
{
    for (const auto &entry : player->propPreviews)
    {
        const auto &before = entry.second;
        const auto &current = player->objects[entry.first];
        const float delta[3] = {before.x - current.x, before.y - current.y, before.z - current.z};
        if (!player->preview.TranslateObject(entry.first, delta, error)) return false;
    }
    if (!player->propPreviews.empty())
    {
        player->objects = player->preview.Objects();
        player->movieImageScene = -1;
    }
    player->propPreviews.clear();
    return true;
}
int SelectScene(MarcusPlayer *player, int index, bool preserveFraming, bool preserveAudio = false)
{
    return Guard(player, [&](MM::Error &error) {
        if (index < 0 || static_cast<std::size_t>(index) >= player->scenes.size())
            return false;
        player->selected = -1;
        player->objects.clear();
        player->propPreviews.clear();
        if (!player->preserveEditFraming) player->orbitObject = -1;
        player->animations.clear();
        player->sounds.clear();
        player->soundTracks.clear();
        if (!preserveAudio)
            player->playback.Stop();
        if (!player->preview.Select(player->scenes[index], error, preserveFraming))
        {
            player->playback.Stop();
            return false;
        }
        if (!preserveFraming)
            player->inspectionMatched = false;
        if (!player->playback.Select(player->preview, error, preserveAudio))
        {
            player->playback.Stop();
            return false;
        }
        player->objects = player->preview.Objects();
        if (!ReadSoundSheet(player, index, error))
        {
            player->playback.Stop();
            return false;
        }
        player->selected = index;
        player->finished = false;
        player->restoreAudio = !preserveAudio;
        return true;
    });
}
std::size_t Find(const MM::Archive &archive, MM::Id id)
{
    const auto &chunks = archive.Chunks();
    const auto found = std::lower_bound(chunks.begin(), chunks.end(), id, [](const auto &chunk, MM::Id key) {
        return chunk.id.type < key.type || (chunk.id.type == key.type && chunk.id.number < key.number);
    });
    return found != chunks.end() && found->id == id ? static_cast<std::size_t>(found - chunks.begin()) : chunks.size();
}
bool Strings(MM::Archive &archive, MM::Id id, std::vector<MM::StringTableEntry> &entries, bool &big,
             MM::Error &error)
{
    const auto index = Find(archive, id);
    MM::PayloadPage header;
    if (index == archive.Chunks().size() || !archive.ReadPayload(index, 0, 2, header, error) ||
        header.bytes.size() != 2)
        return false;
    big = header.bytes[0] == 0;
    MM::StringTablePage page;
    for (std::uint32_t first = 0;; first += static_cast<std::uint32_t>(page.entries.size()))
    {
        if (!archive.ReadStrings(index, first, 128, page, error))
            return false;
        entries.insert(entries.end(), page.entries.begin(), page.entries.end());
        if (first + page.entries.size() >= page.totalEntries)
            return true;
        if (entries.size() > 65536 || page.entries.empty())
            return false;
    }
}
} // namespace
bool MarcusRenderLocalWorld(MarcusPlayer *player, const MM::PreviewView &view,
                            MM::PreviewImage &image, MM::Error &error)
{
    return player && player->preview.Render(view, image, error);
}
extern "C"
{
    MarcusPlayer *marcus_create(void)
    {
        try
        {
            return new MarcusPlayer;
        }
        catch (...)
        {
            return nullptr;
        }
    }
    void marcus_destroy(MarcusPlayer *player)
    {
        delete player;
    }

    int marcus_get_renderer(const MarcusPlayer *player)
    {
        return player ? player->renderer : -1;
    }

    int marcus_set_renderer(MarcusPlayer *player, int renderer)
    {
        return Guard(player, [&](MM::Error &error) {
            if (renderer < 0 || renderer >= MARCUS_RENDERER_COUNT)
            {
                error.code = MM::ErrorCode::invalid_argument;
                error.message = "Unknown movie renderer.";
                return false;
            }
            if (renderer == MARCUS_RENDERER_BRENDER_ASM && !MarcusASMRenderer::Available(error)) return false;
            if (marcus_renderer_is_gpu(renderer) && !player->rasterizers[renderer])
            {
                auto replacement = MarcusCreateGPURenderer(renderer == MARCUS_RENDERER_BRENDER_COMPUTE, error);
                if (!replacement) return false;
                player->rasterizers[renderer] = std::move(replacement);
            }
            player->renderer = renderer;
            return true;
        });
    }
    int marcus_set_render_options(MarcusPlayer *player, const MarcusRenderOptions *options)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!marcus_render_options_valid(options))
            {
                error.message = "Camera render settings are invalid.";
                return false;
            }
            if (!marcus_renderer_is_gpu(player->renderer) &&
                (options->display_mode >= MARCUS_DISPLAY_DEPTH || options->wire_overlay || options->antialias || options->texture_filter))
            {
                error.message = "Depth, normals, wire overlay, AA and filtering require a GPU renderer.";
                return false;
            }
            player->renderOptions = *options;
            return true;
        });
    }
    const char *marcus_error(const MarcusPlayer *player)
    {
        return player ? player->error : "Player is unavailable.";
    }
    int marcus_open(MarcusPlayer *player, const char *movie, const char *resources, const char *directory)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!movie)
                return false;
            MarcusMovieInput input;
            input.Open(movie);
            if (input.Count() != 1)
            {
                error.message = "This ZIP contains several movies. Select one from the movie list.";
                return false;
            }
            if (!marcus_open_input(player, &input, 0, resources, directory))
            {
                error.message = player->error;
                return false;
            }
            return true;
        });
    }
    int marcus_reload(MarcusPlayer *player, const char *resources, const char *directory)
    {
        return Guard(player, [&](MM::Error &error) {
            auto original = player->timingInput;
            if (!original) return false;
            KeepHistory history(*player);
            const auto scene = player->selected;
            MarcusMovieInput input;
            input.Open(original->path.u8string());
            auto file = input.Materialize(0);
            file->origin = original;
            file->contentOrigin = original->contentOrigin;
            file->fonts = original->fonts;
            file->fontFiles = original->fontFiles;
            file->fontDirectory = original->fontDirectory;
            input.prepared = file;
            if (!marcus_open_input(player, &input, 0, resources, directory) || !marcus_select_scene(player, scene))
            { error.message = player->error; return false; }
            return true;
        });
    }
    int marcus_can_undo(const MarcusPlayer *player) { return player && !player->undo.empty(); }
    int marcus_modified(const MarcusPlayer *player) { return player && player->modified; }
    int marcus_undo(MarcusPlayer *player)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->undo.empty()) return false;
            const auto before = player->undo.back();
            KeepHistory history(*player);
            if (before.file)
            {
                MarcusMovieInput input;
                PrepareInput(input, before.file);
                const auto resources = player->timingResources, directory = player->timingContent;
                struct Restore { bool &flag; bool value; ~Restore() { flag = value; } } restore{
                    player->preserveEditFraming, player->preserveEditFraming};
                player->preserveEditFraming = true;
                if (!marcus_open_input(player, &input, 0, resources.c_str(), directory.c_str()) ||
                    !marcus_select_scene_keep_view(player, before.scene) || !marcus_seek(player, before.frame))
                { error.message = player->error; return false; }
            }
            else
            {
                // Undoing the first scene returns an untitled movie to its empty state.
                player->timingJob.reset();
                player->playback.Stop();
                player->asmRenderer.reset();
                player->preview.Close();
                player->content.reset();
                player->timingInput.reset();
                player->selected = player->orbitObject = player->movieImageScene = -1;
                player->scenes.clear(); player->sceneInfo.clear(); player->objects.clear();
                player->propPreviews.clear(); player->animations.clear(); player->names.clear();
                player->sounds.clear(); player->soundTracks.clear(); player->soundCues.clear();
                player->soundTimeline.Clear(); player->waveforms.Clear();
                player->image = {}; player->thumbnail = {}; player->movieImage = {};
                player->inspectionMatched = player->timingStarted = player->timingFailed = player->finished = false;
                player->restoreAudio = true;
                ++player->document;
            }
            player->modified = before.modified;
            player->undo.pop_back();
            return true;
        });
    }
    int marcus_prop_count(MarcusPlayer *player, const char *directory)
    {
        const bool ok = Guard(player, [&](MM::Error &error) {
            if (!directory) return false;
            MM::Archive archive;
            if (!archive.Open((std::filesystem::u8path(directory) / "tmpls.3cn").u8string(), error)) return false;
            std::vector<MarcusProp> props;
            for (std::size_t index = 0; index < archive.Chunks().size(); ++index)
            {
                const auto &chunk = archive.Chunks()[index];
                if (chunk.id.type != 0x544d504c) continue;
                MM::PayloadPage header;
                if (!archive.ReadPayload(index, 0, 16, header, error)) return false;
                if (header.bytes.size() != 16 || !((header.bytes[0] == 1 && header.bytes[1] == 0) ||
                                                  (header.bytes[0] == 0 && header.bytes[1] == 1))) continue;
                const auto flags = Word(std::vector<std::uint8_t>(header.bytes.begin() + 12, header.bytes.end()), header.bytes[0] == 0);
                if (!(flags & 4) || (flags & 2)) continue; // Native ftmplProp / ftmplTdt.
                MarcusProp prop = {};
                prop.chunk = chunk.id.number;
                Copy(prop.name, chunk.name.empty() ? "Prop " + std::to_string(prop.chunk) : chunk.name);
                props.push_back(prop);
            }
            std::sort(props.begin(), props.end(), [](const auto &a, const auto &b) { return std::strcmp(a.name, b.name) < 0; });
            player->props.swap(props);
            return true;
        });
        return ok ? static_cast<int>(player->props.size()) : -1;
    }
    int marcus_prop(const MarcusPlayer *player, int index, MarcusProp *prop)
    {
        if (!player || !prop || index < 0 || static_cast<std::size_t>(index) >= player->props.size()) return 0;
        *prop = player->props[index];
        return 1;
    }
    int marcus_add_prop(MarcusPlayer *player, unsigned long chunk)
    {
        int result = -1;
        const bool ok = Guard(player, [&](MM::Error &error) {
            if (player->selected < 0) { error.message = "Create or open a scene before adding a prop."; return false; }
            if (marcus_prop_count(player, player->timingContent.c_str()) < 0) { error.message = player->error; return false; }
            const auto found = std::find_if(player->props.begin(), player->props.end(), [&](const auto &prop) { return prop.chunk == chunk; });
            if (found == player->props.end()) { error.message = "The selected prop is unavailable."; return false; }
            float point[3];
            if (!player->preview.PlacementPoint(point)) { error.message = "This scene has no background placement point."; return false; }
            for (const auto value : point)
                if (!std::isfinite(value) || std::abs(value) > 32700) { error.message = "The background placement point is out of range."; return false; }
            const auto scene = player->selected;
            const auto frame = player->preview.Info().currentFrame;
            long actorId;
            auto file = MarcusAppendProp(player->content->movie, player->timingInput,
                std::filesystem::u8path(player->timingContent) / "tmpls.3cn", player->scenes[scene].number,
                frame, chunk, found->name, point, actorId);
            EditHistory history(*player);
            MarcusMovieInput input;
            input.Open(file->path.u8string());
            auto materialized = input.Materialize(0);
            materialized->origin = file;
            materialized->contentOrigin = file->contentOrigin;
            materialized->fonts = file->fonts;
            materialized->fontFiles = file->fontFiles;
            materialized->fontDirectory = file->fontDirectory;
            input.prepared = materialized;
            const auto resources = player->timingResources, directory = player->timingContent;
            if (!marcus_open_input(player, &input, 0, resources.c_str(), directory.c_str()) ||
                !marcus_select_scene(player, scene) || !marcus_seek(player, frame))
            { error.message = player->error; return false; }
            for (std::size_t index = 0; index < player->objects.size(); ++index)
                if (player->objects[index].actorId == actorId) { result = static_cast<int>(index); history.Commit(); return true; }
            error.message = "The new prop could not be selected.";
            return false;
        });
        return ok ? result : -1;
    }
    unsigned long marcus_document(const MarcusPlayer *player) { return player ? player->document : 0; }
    int marcus_preview_prop_position(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject before;
            if (!marcus_object(player, object, &before) || !before.prop || before.text || !before.visible ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::abs(x) > 32700 || std::abs(y) > 32700 || std::abs(z) > 32700)
            { error.message = "Select a visible prop and a position within +/-32700 world units."; return false; }
            const float delta[3] = {x - before.x, y - before.y, z - before.z};
            if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0) return true;
            const auto inserted = player->propPreviews.emplace(object, player->objects[object]);
            if (!player->preview.TranslateObject(object, delta, error))
            {
                if (inserted.second) player->propPreviews.erase(inserted.first);
                return false;
            }
            player->objects = player->preview.Objects();
            const auto &original = inserted.first->second;
            const auto &current = player->objects[object];
            if (std::abs(current.x - original.x) < .0001f && std::abs(current.y - original.y) < .0001f &&
                std::abs(current.z - original.z) < .0001f) player->propPreviews.erase(inserted.first);
            player->movieImageScene = -1;
            return true;
        });
    }
    int marcus_move_prop(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject before;
            if (!marcus_object(player, object, &before) || !before.prop || before.text || !before.visible ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::abs(x) > 32700 || std::abs(y) > 32700 || std::abs(z) > 32700)
            { error.message = "Select a visible prop and a position within +/-32700 world units."; return false; }
            const float delta[3] = {x - before.x, y - before.y, z - before.z};
            if (std::abs(delta[0]) < 1e-6f && std::abs(delta[1]) < 1e-6f && std::abs(delta[2]) < 1e-6f) return true;
            const auto scene = player->selected;
            const auto frame = player->preview.Info().currentFrame;
            auto file = MarcusMoveProp(player->content->movie, player->timingInput, player->scenes[scene].number, before.chunk, delta);
            EditHistory history(*player);
            MarcusMovieInput input;
            input.Open(file->path.u8string());
            auto materialized = input.Materialize(0);
            materialized->origin = file;
            materialized->contentOrigin = file->contentOrigin;
            materialized->fonts = file->fonts;
            materialized->fontFiles = file->fontFiles;
            materialized->fontDirectory = file->fontDirectory;
            input.prepared = materialized;
            struct Restore { bool &flag; ~Restore() { flag = false; } } restore{player->preserveEditFraming};
            player->preserveEditFraming = true;
            const auto resources = player->timingResources, directory = player->timingContent;
            if (!marcus_open_input(player, &input, 0, resources.c_str(), directory.c_str()) ||
                !marcus_select_scene_keep_view(player, scene) || !marcus_seek(player, frame))
            { error.message = player->error; return false; }
            history.Commit();
            return true;
        });
    }
    int marcus_set_scene_camera(MarcusPlayer *player, unsigned long camera)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || camera >= player->preview.Info().cameraCount)
            { error.message = "Camera angle is out of range."; return false; }
            const auto scene = player->selected;
            const auto frame = player->preview.Info().currentFrame;
            auto file = MarcusSetSceneCamera(player->content->movie, player->timingInput,
                player->scenes[scene].number, frame, camera);
            EditHistory history(*player);
            MarcusMovieInput input;
            input.Open(file->path.u8string());
            auto materialized = input.Materialize(0);
            materialized->origin = file;
            materialized->contentOrigin = file->contentOrigin;
            materialized->fonts = file->fonts;
            materialized->fontFiles = file->fontFiles;
            materialized->fontDirectory = file->fontDirectory;
            input.prepared = materialized;
            const auto resources = player->timingResources, directory = player->timingContent;
            if (!marcus_open_input(player, &input, 0, resources.c_str(), directory.c_str()) ||
                !marcus_select_scene(player, scene) || !marcus_seek(player, frame))
            { error.message = player->error; return false; }
            history.Commit();
            return true;
        });
    }
    int marcus_camera_projection(MarcusPlayer *player, unsigned long camera, MarcusProjection *projection)
    {
        if (projection) *projection = {};
        return Guard(player, [&](MM::Error &error) {
            if (!projection || player->selected < 0) return false;
            MM::PreviewProjection value;
            if (!player->preview.CameraProjection(camera, value, error)) return false;
            *projection = CopyProjection(value);
            return true;
        });
    }
    int marcus_camera_thumbnail(MarcusPlayer *player, unsigned long camera, MarcusImage *image)
    {
        if (image) *image = {};
        return Guard(player, [&](MM::Error &error) {
            if (!image || player->selected < 0 || camera >= player->preview.Info().cameraCount) return false;
            const auto previous = player->preview.Info().currentCamera;
            struct Restore
            {
                MM::Preview &preview;
                std::uint32_t camera;
                ~Restore() { MM::Error ignored; preview.SetCamera(camera, ignored); }
            } restore{player->preview, previous};
            if (!player->preview.SetCamera(camera, error)) return false;
            MM::PreviewView view;
            view.width = 160; view.height = 120;
            view.renderAtOutputResolution = true;
            if (!player->preview.Render(view, player->thumbnail, error) ||
                !player->playback.Composite(player->preview, true, player->thumbnail, error)) return false;
            image->width = player->thumbnail.width;
            image->height = player->thumbnail.height;
            image->pitch = image->width * 4;
            image->rgba = player->thumbnail.rgba.data();
            image->scene_camera = CopyProjection(player->thumbnail.sceneCamera);
            image->scene = player->selected;
            image->frame = player->preview.Info().currentFrame;
            return true;
        });
    }
    int marcus_background_count(MarcusPlayer *player, const char *directory)
    {
        const bool ok = Guard(player, [&](MM::Error &error) {
            if (!directory) return false;
            MM::Archive archive;
            if (!archive.Open((std::filesystem::u8path(directory) / "bkgds.3cn").u8string(), error)) return false;
            std::vector<MarcusBackground> backgrounds;
            for (const auto &chunk : archive.Chunks())
                if (chunk.id.type == 0x424b4744)
                {
                    MarcusBackground background = {};
                    background.chunk = chunk.id.number;
                    Copy(background.name, chunk.name.empty() ? "Background " + std::to_string(chunk.id.number) : chunk.name);
                    backgrounds.push_back(background);
                }
            player->backgrounds.swap(backgrounds);
            return true;
        });
        return ok ? static_cast<int>(player->backgrounds.size()) : -1;
    }
    int marcus_background(const MarcusPlayer *player, int index, MarcusBackground *background)
    {
        if (!player || !background || index < 0 || static_cast<std::size_t>(index) >= player->backgrounds.size()) return 0;
        *background = player->backgrounds[index];
        return 1;
    }
    int marcus_add_scene(MarcusPlayer *player, unsigned long background, const char *resources, const char *directory)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!resources || !directory) return false;
            // Validate the chosen retail asset before preparing or replacing the live document.
            if (marcus_background_count(player, directory) < 0) { error.message = player->error; return false; }
            if (std::none_of(player->backgrounds.begin(), player->backgrounds.end(),
                             [&](const auto &value) { return value.chunk == background; }))
            { error.message = "The selected background is unavailable."; return false; }
            const auto index = static_cast<int>(player->scenes.size());
            auto file = MarcusAppendScene(player->content ? player->content->movie : std::filesystem::path{},
                                           player->timingInput, background, index + 1);
            EditHistory history(*player);
            MarcusMovieInput input;
            input.Open(file->path.u8string());
            auto materialized = input.Materialize(0);
            materialized->origin = file;
            materialized->contentOrigin = file->contentOrigin;
            materialized->fonts = file->fonts;
            materialized->fontFiles = file->fontFiles;
            materialized->fontDirectory = file->fontDirectory;
            input.prepared = materialized;
            if (!marcus_open_input(player, &input, 0, resources, directory) || !marcus_select_scene(player, index))
            { error.message = player->error; return false; }
            history.Commit();
            return true;
        });
    }
    int marcus_open_input(MarcusPlayer *player, MarcusMovieInput *input, int index, const char *resources,
                          const char *directory)
    {
        if (player)
            player->playback.Stop();
        return Guard(player, [&](MM::Error &error) {
            if (!input || !resources || !directory)
                return false;
            auto materialized = input->Materialize(index);
            auto content = std::make_unique<MarcusContent>(materialized, directory);
            const auto document = content->movie.u8string();
            // Validate the graph and ordered scene list before replacing the live renderer.
            MM::Archive archive;
            if (!archive.Open(document, error))
                return false;
            std::vector<MM::Id> scenes;
            std::vector<MarcusScene> sceneInfo;
            std::map<std::int32_t, std::string> names;
            std::vector<std::pair<std::int32_t, std::string>> sources;
            bool hasSourceTable = false;
            const MM::Chunk *root = nullptr;
            for (const auto &chunk : archive.Chunks())
                if (chunk.id.type == movieType)
                {
                    root = &chunk;
                    break;
                }
            if (root)
            {
                MM::RecordHeader metadata;
                if (!archive.ReadRecord(Find(archive, root->id), metadata, error))
                    return false;
                for (const auto &child : root->children)
                {
                    if (child.target.type == sceneType)
                    {
                        if (!child.present)
                        {
                            error.message = "The movie references a missing scene.";
                            return false;
                        }
                        scenes.push_back(child.target);
                    }
                    if (child.target.type != gstType || child.slot > 1)
                        continue;
                    hasSourceTable = hasSourceTable || child.slot == 1;
                    std::vector<MM::StringTableEntry> entries;
                    bool big = false;
                    if (!Strings(archive, child.target, entries, big, error))
                        return false;
                    for (const auto &entry : entries)
                        if (child.slot == 0 && entry.extra.size() == MM_MOVIE_ACTOR_BYTES)
                        {
                            MMMemoryStream memory;
                            MMMovieActor actor;
                            if (!mm_memory_stream_init(&memory, entry.extra.data(), entry.extra.size()) ||
                                mm_movie_read_actor(&memory.stream, big, &actor, nullptr) != MM_MOVIE_OK)
                            {
                                error.code = MM::ErrorCode::corrupt_archive;
                                error.message = "The movie roll-call record is invalid.";
                                return false;
                            }
                            names[actor.actor_id] = entry.text;
                        }
                        else if (child.slot == 1 && entry.extra.size() == 4)
                            sources.push_back({Word(entry.extra, big), entry.text});
                }
                // Source 2 is the original retail content when older movies omit a table.
                if (!hasSourceTable)
                    sources.push_back({2, "3dmovie"});
            }
            else
            {
                // Also accept headless scene fixtures, which use source 1.
                for (const auto &chunk : archive.Chunks())
                    if (chunk.id.type == sceneType)
                        scenes.push_back(chunk.id);
                sources.push_back({1, "3dmovie"});
            }
            if (scenes.empty())
            {
                error.message = "This document contains no movie scenes.";
                return false;
            }
            for (const auto &id : scenes)
            {
                const auto index = Find(archive, id);
                MM::RecordHeader record;
                if (index == archive.Chunks().size() || !archive.ReadRecord(index, record, error) ||
                    record.kind != MM::RecordKind::scene || record.scene.lastFrame < record.scene.firstFrame)
                    return false;
                MarcusScene scene = {};
                scene.chunk = id.number;
                scene.first_frame = record.scene.firstFrame;
                scene.last_frame = record.scene.lastFrame;
                scene.transition = record.scene.transition;
                const auto &name = archive.Chunks()[index].name;
                Copy(scene.name, name.empty() ? "Scene " + std::to_string(sceneInfo.size() + 1) : name);
                sceneInfo.push_back(scene);
            }
            for (const auto &source : sources)
                content->PrepareSource(source.second);
            if (!player->preview.Open(std::move(archive), resources, error, player->preserveEditFraming))
                return false;
            player->timingJob.reset();
            player->timingInput = std::move(materialized);
            player->playback.SetFonts(player->timingInput->fonts);
            player->timingResources = resources;
            player->timingContent = directory;
            player->timingStarted = player->timingFailed = false;
            ++player->document;
            if (!player->preserveEditFraming) player->playback.ResetMix();
            player->asmRenderer.reset();
            player->content = std::move(content);
            player->selected = -1;
            player->scenes.clear();
            player->sceneInfo.clear();
            player->objects.clear();
            player->propPreviews.clear();
            player->animations.clear();
            player->names.clear();
            player->sounds.clear();
            player->soundTracks.clear();
            player->soundTimeline.Clear();
            player->waveforms.Clear();
            player->image = {};
            player->movieImage = {};
            player->movieImageScene = -1;
            if (!player->preserveEditFraming) player->inspectionMatched = false;
            for (const auto &source : sources)
                for (const auto &path : player->content->files.Files(source.second, ".3cn"))
                {
                    if (!player->preview.AddContent(source.first, path.u8string(), error))
                        return false;
                }
            player->scenes.swap(scenes);
            player->sceneInfo.swap(sceneInfo);
            player->names.swap(names);
            if (!player->preview.Select(player->scenes[0], error, player->preserveEditFraming))
                return false;
            if (!player->playback.Select(player->preview, error))
                return false;
            player->objects = player->preview.Objects();
            if (!ReadSoundSheet(player, 0, error))
                return false;
            player->selected = 0;
            player->finished = false;
            player->restoreAudio = true;
            if (!player->internalOpenDepth)
            {
                player->undo.clear();
                player->modified = false;
            }
            return true;
        });
    }
    int marcus_scene(const MarcusPlayer *player, int index, MarcusScene *scene)
    {
        if (!player || !scene || index < 0 || static_cast<std::size_t>(index) >= player->sceneInfo.size())
            return 0;
        *scene = player->sceneInfo[index];
        return 1;
    }
    MarcusFramePreviews *marcus_frame_previews_create(MarcusPlayer *player)
    {
        MarcusFramePreviews *result = nullptr;
        Guard(player, [&](MM::Error &) {
            if (!player->timingInput || player->selected < 0) return false;
            result = new MarcusFramePreviews(player->timingInput, player->timingResources,
                                             player->timingContent, player->selected);
            return true;
        });
        return result;
    }
    MarcusFramePreviews *marcus_camera_previews_create(MarcusPlayer *player)
    {
        MarcusFramePreviews *result = nullptr;
        Guard(player, [&](MM::Error &) {
            if (!player->timingInput || player->selected < 0) return false;
            result = new MarcusFramePreviews(player->timingInput, player->timingResources,
                                             player->timingContent, player->selected, true);
            return true;
        });
        return result;
    }
    int marcus_scene_thumbnail(MarcusPlayer *player, int index, MarcusImage *image)
    {
        if (!image)
            return 0;
        *image = {};
        return Guard(player, [&](MM::Error &error) {
            if (index < 0 || static_cast<std::size_t>(index) >= player->scenes.size())
                return false;
            if (!player->preview.ReadSceneThumbnail(player->scenes[index], player->thumbnail, error))
                return false;
            image->width = player->thumbnail.width;
            image->height = player->thumbnail.height;
            image->pitch = image->width * 4;
            image->rgba = player->thumbnail.rgba.empty() ? nullptr : player->thumbnail.rgba.data();
            return true;
        });
    }
    int marcus_select_scene(MarcusPlayer *player, int index)
    {
        return SelectScene(player, index, false);
    }
    int marcus_select_scene_keep_view(MarcusPlayer *player, int index)
    {
        return SelectScene(player, index, true);
    }
    int marcus_info(const MarcusPlayer *player, MarcusInfo *info)
    {
        if (!player || !info || player->selected < 0)
            return 0;
        const auto &source = player->preview.Info();
        info->scene = player->selected;
        info->scene_count = static_cast<int>(player->scenes.size());
        info->object_count = static_cast<int>(player->objects.size());
        info->first_frame = source.firstFrame;
        info->last_frame = source.lastFrame;
        info->frame = source.currentFrame;
        info->camera = source.currentCamera;
        info->camera_count = source.cameraCount;
        info->sound_count = static_cast<int>(player->sounds.size());
        info->sound_track_count = static_cast<int>(player->soundTracks.size());
        return 1;
    }
    int marcus_movie_time_async(MarcusPlayer *player, MarcusMovieTime *time)
    {
        if (time)
            *time = {};
        if (!player || !time || player->selected < 0)
            return -1;
        try
        {
            if (player->soundTimeline.Size() == player->scenes.size())
                return marcus_movie_time(player, time) ? 1 : -1;
            if (player->timingFailed)
                return -1;
            if (!player->timingStarted)
            {
                player->timingJob = std::make_unique<MarcusTimingJob>(player->timingInput, player->timingResources,
                                                                     player->timingContent, player->sceneInfo,
                                                                     player->playbackRate, player->playbackModifier);
                player->timingStarted = true;
            }
            std::unique_ptr<MarcusSoundTimeline> timeline;
            std::string failure;
            if (!player->timingJob->Take(timeline, failure))
                return 0;
            player->timingJob.reset();
            if (!timeline)
            {
                player->timingFailed = true;
                Copy(player->error, failure.empty() ? "Invalid movie timing output." : failure);
                return -1;
            }
            player->soundTimeline = std::move(*timeline);
            player->sounds.clear();
            player->soundTracks.clear();
            MM::Error error;
            if (!ReadSoundSheet(player, player->selected, error))
                throw std::runtime_error(error.message);
            return marcus_movie_time(player, time) ? 1 : -1;
        }
        catch (const std::exception &error)
        {
            player->timingJob.reset();
            player->timingFailed = true;
            Copy(player->error, error.what());
            return -1;
        }
    }
    int marcus_movie_time(MarcusPlayer *player, MarcusMovieTime *time)
    {
        if (time)
            *time = {};
        return Guard(player, [&](MM::Error &error) {
            if (!time || player->selected < 0 ||
                !ReadSoundTimeline(player, static_cast<int>(player->scenes.size()) - 1, error))
                return false;
            const auto &last = player->soundTimeline.Scene(player->scenes.size() - 1);
            time->duration_seconds = last.TimeAt(double(last.info.last_frame) + 1);
            const auto &scene = player->soundTimeline.Scene(player->selected);
            const double frame = player->preview.Info().currentFrame;
            const double start = scene.TimeAt(frame), end = scene.TimeAt(frame + 1);
            time->position_seconds = player->finished ? time->duration_seconds
                : std::clamp(start + std::min(player->playback.FrameSeconds(), end - start),
                             0.0, time->duration_seconds);
            return true;
        });
    }
    int marcus_scene_time(MarcusPlayer *player, int index, MarcusSceneTime *time)
    {
        if (time)
            *time = {};
        return Guard(player, [&](MM::Error &error) {
            if (!time || player->selected < 0 || index < 0 ||
                static_cast<std::size_t>(index) >= player->scenes.size() || !ReadSoundTimeline(player, index, error))
                return false;
            if (index)
            {
                const auto &previous = player->soundTimeline.Scene(index - 1);
                time->start_seconds = previous.TimeAt(double(previous.info.last_frame) + 1);
            }
            const auto &scene = player->soundTimeline.Scene(index);
            time->end_seconds = scene.TimeAt(double(scene.info.last_frame) + 1);
            return true;
        });
    }
    int marcus_seek_time(MarcusPlayer *player, double seconds)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || !std::isfinite(seconds) ||
                !ReadSoundTimeline(player, static_cast<int>(player->scenes.size()) - 1, error))
                return false;
            int index = 0;
            while (static_cast<std::size_t>(index + 1) < player->scenes.size())
            {
                const auto &scene = player->soundTimeline.Scene(index);
                if (seconds < scene.TimeAt(double(scene.info.last_frame) + 1))
                    break;
                ++index;
            }
            const auto &scene = player->soundTimeline.Scene(index);
            const double end = scene.TimeAt(double(scene.info.last_frame) + 1);
            const auto frame = static_cast<long>(std::clamp(std::floor(scene.FrameAt(
                std::clamp(seconds, scene.start, end)) + 1e-7),
                double(scene.info.first_frame), double(scene.info.last_frame)));
            if ((index != player->selected && !SelectScene(player, index, true)) || !marcus_seek(player, frame))
            {
                error.message = player->error;
                return false;
            }
            player->finished = static_cast<std::size_t>(index + 1) == player->scenes.size() && seconds >= end;
            return true;
        });
    }
    int marcus_sound_track(const MarcusPlayer *player, int index, MarcusSoundTrack *track)
    {
        if (!player || player->selected < 0 || !track || index < 0 ||
            static_cast<std::size_t>(index) >= player->soundTracks.size())
            return 0;
        *track = player->soundTracks[index];
        player->playback.TrackMix(track->actor_sound != 0, track->actor_id, track->style,
                                  track->muted, track->solo, track->audible);
        return 1;
    }
    int marcus_set_sound_track_mix(MarcusPlayer *player, int index, int muted, int solo)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusSoundTrack track;
            if ((muted != 0 && muted != 1) || (solo != 0 && solo != 1) ||
                !marcus_sound_track(player, index, &track))
            {
                error.message = "Choose a valid sound layer and mute/solo state.";
                return false;
            }
            player->playback.SetTrackMix(track.actor_sound != 0, track.actor_id, track.style, muted != 0, solo != 0);
            return true;
        });
    }
    void marcus_reset_sound_mix(MarcusPlayer *player)
    {
        if (player)
            player->playback.ResetMix();
    }
    int marcus_sound_mix_active(const MarcusPlayer *player)
    {
        return player && player->playback.MixActive();
    }
    int marcus_sound(const MarcusPlayer *player, int index, MarcusSound *sound)
    {
        if (!player || player->selected < 0 || !sound || index < 0 ||
            static_cast<std::size_t>(index) >= player->sounds.size())
            return 0;
        *sound = player->sounds[index];
        return 1;
    }
    int marcus_sound_waveform(MarcusPlayer *player, int index, double first, double step, int count,
                              MarcusWaveformColumn *columns, MarcusWaveformInfo *info)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || index < 0 || static_cast<std::size_t>(index) >= player->soundCues.size() ||
                !columns || !info || count < 1 || count > 4096 || !std::isfinite(first) || first < 0 ||
                !std::isfinite(step) || step <= 0)
            {
                error.code = MM::ErrorCode::invalid_argument;
                error.message = "Waveforms require a sound cue, output columns and a finite clip-relative range.";
                return false;
            }
            player->waveforms.Read(player->preview, player->soundCues[index], first, step, count, columns, *info);
            return true;
        });
    }
    int marcus_sound_waveform_frames(MarcusPlayer *player, int index, double first, double step, int count,
                                     MarcusWaveformColumn *columns, MarcusWaveformInfo *info)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || index < 0 || static_cast<std::size_t>(index) >= player->soundCues.size() ||
                !columns || !info || count < 1 || count > 4096 || !std::isfinite(first) ||
                !std::isfinite(step) || step <= 0)
            {
                error.message = "Timeline waveforms require a sound cue, output columns and a finite frame range.";
                return false;
            }
            const auto &scene = player->soundTimeline.Scene(player->selected);
            const auto &span = scene.sounds[index];
            MarcusWaveformColumn ignored;
            player->waveforms.Read(player->preview, span->cue, 0, 1, 1, &ignored, *info);
            std::fill_n(columns, count, MarcusWaveformColumn{});
            for (int column = 0; column < count; ++column)
            {
                const double at = scene.TimeAt(first + column * step);
                const double end = std::min(span->end, scene.TimeAt(first + (column + 1) * step));
                if (at >= span->start && at < span->end && end > at)
                    player->waveforms.Read(player->preview, span->cue, at - span->start, end - at,
                                           1, &columns[column], *info);
            }
            return true;
        });
    }
    int marcus_object(MarcusPlayer *player, int index, MarcusObject *object)
    {
        return Guard(player, [&](MM::Error &) {
            if (!object || index < 0 || static_cast<std::size_t>(index) >= player->objects.size())
                return false;
            *object = {};
            const auto &source = player->objects[index];
            const auto name = player->names.find(source.actorId);
            Copy(object->name, name != player->names.end() ? name->second
                                                           : std::string(source.text   ? "3D text "
                                                                         : source.prop ? "Prop "
                                                                                       : "Actor ") +
                                                                 std::to_string(source.actorId));
            object->chunk = source.asset.number;
            object->template_chunk = source.templateAsset.number;
            object->actor_id = source.actorId;
            object->source_id = source.sourceId;
            object->first_frame = source.firstFrame;
            object->last_frame = source.lastFrame;
            object->action = source.action;
            object->cel = source.cel;
            object->prop = source.prop;
            object->text = source.text;
            object->visible = source.visible;
            object->has_lifetime = source.hasLifetime;
            object->x = source.x;
            object->y = source.y;
            object->z = source.z;
            return true;
        });
    }
    int marcus_animation_info(MarcusPlayer *player, int object, MarcusAnimationInfo *info)
    {
        if (info)
            *info = {};
        return Guard(player, [&](MM::Error &error) {
            const auto cache = info ? Animation(player, object, error) : nullptr;
            if (!cache)
                return false;
            info->span_count = static_cast<int>(cache->timeline.spans.size());
            info->event_count = static_cast<int>(cache->timeline.events.size());
            return true;
        });
    }
    int marcus_action_span(MarcusPlayer *player, int object, int index, MarcusActionSpan *span)
    {
        if (span)
            *span = {};
        return Guard(player, [&](MM::Error &error) {
            const auto cache = span ? Animation(player, object, error) : nullptr;
            if (!cache || index < 0 || static_cast<std::size_t>(index) >= cache->timeline.spans.size())
                return false;
            const auto &source = cache->timeline.spans[index];
            span->first_frame = static_cast<double>(source.firstFrame);
            span->end_frame = static_cast<double>(source.endFrame);
            span->action = source.action;
            span->frozen = source.frozen;
            Copy(span->name, source.name);
            return true;
        });
    }
    int marcus_animation_event(MarcusPlayer *player, int object, int index, MarcusAnimationEvent *event)
    {
        if (event)
            *event = {};
        return Guard(player, [&](MM::Error &error) {
            const auto cache = event ? Animation(player, object, error) : nullptr;
            if (!cache || index < 0 || static_cast<std::size_t>(index) >= cache->timeline.events.size())
                return false;
            const auto &source = cache->timeline.events[index];
            event->frame = source.frame;
            event->kind = static_cast<int>(source.kind);
            Copy(event->name, source.name);
            Copy(event->detail, source.detail);
            return true;
        });
    }
    int marcus_object_bounds(MarcusPlayer *player, int index, MarcusBounds *bounds)
    {
        if (bounds)
            *bounds = {};
        return Guard(player, [&](MM::Error &error) {
            if (!bounds)
                return false;
            MM::PreviewBounds source;
            if (!player->preview.ObjectBounds(index, source, error))
                return false;
            bounds->valid = source.valid;
            std::memcpy(bounds->corners, source.corners, sizeof(bounds->corners));
            bounds->colour = source.colour;
            return true;
        });
    }
    int marcus_seek(MarcusPlayer *player, long frame)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!CancelPropPreviews(player, error)) return false;
            if (player->selected < 0 || frame < (std::numeric_limits<std::int32_t>::min)() ||
                frame > (std::numeric_limits<std::int32_t>::max)())
                return false;
            if (!player->preview.SetFrame(static_cast<std::int32_t>(frame), error))
                return false;
            player->playback.Seek(&player->preview);
            player->finished = false;
            player->restoreAudio = true;
            player->objects = player->preview.Objects();
            return true;
        });
    }
    void marcus_stop(MarcusPlayer *player)
    {
        if (player)
        {
            player->playback.Stop();
            player->restoreAudio = true;
        }
    }
    int marcus_audio_stats(MarcusPlayer *player, MarcusAudioStats *stats)
    {
        return Guard(player, [&](MM::Error &) {
            if (!stats)
                return false;
            player->playback.Statistics(*stats);
            return true;
        });
    }
    void marcus_set_muted(MarcusPlayer *player, int muted)
    {
        if (player)
            player->playback.Mute(muted != 0);
    }
    int marcus_set_playback_rate(MarcusPlayer *player, int rate, int modifier)
    {
        return Guard(player, [&](MM::Error &error) {
            if (rate < 0 || rate > 4 || modifier < 0 || modifier > 2)
            {
                error.message = "Invalid playback frame rate or compatibility modifier.";
                return false;
            }
            if (player->playbackRate == rate && player->playbackModifier == modifier)
                return true;
            player->timingJob.reset(); // A pending helper describes the old rate.
            player->timingStarted = player->timingFailed = false;
            player->playbackRate = rate;
            player->playbackModifier = modifier;
            const double fps = mm_playback_fps(rate, modifier);
            player->playback.SetRate(fps);
            player->soundTimeline.SetRate(fps);
            player->sounds.clear();
            player->soundTracks.clear();
            return player->selected < 0 || ReadSoundSheet(player, player->selected, error);
        });
    }
    int marcus_update(MarcusPlayer *player, int playing, int loop, double elapsed, int click,
                      MarcusPlaybackState *state)
    {
        const int success = Guard(player, [&](MM::Error &error) {
            if (!state || !std::isfinite(elapsed) || elapsed < 0 || player->selected < 0)
            {
                error.message = "Playback requires a loaded movie and a finite, nonnegative elapsed time.";
                return false;
            }
            if (playing && !CancelPropPreviews(player, error)) return false;
            *state = {};
            state->playing = playing != 0;
            const bool wasAnimated = player->playback.Scrolling() || player->playback.Transitioning();
            bool advance = false;
            if (playing && player->restoreAudio)
            {
                if (!player->playback.RestoreAudio(player->preview,
                        player->soundTimeline.Resume(player->selected, player->preview.Info().currentFrame), error))
                    return false;
                player->restoreAudio = false;
            }
            if (!player->playback.Update(player->preview, playing != 0, elapsed, click != 0, advance, error))
            {
                player->playback.Stop();
                state->playing = 0;
                return false;
            }
            state->changed = playing && (wasAnimated || player->playback.Scrolling() ||
                                         player->playback.Transitioning());
            if (advance)
            {
                state->changed = 1;
                const auto info = player->preview.Info();
                if (info.currentFrame < info.lastFrame)
                {
                    if (!player->preview.SetFrame(info.currentFrame + 1, error))
                        return false;
                    player->objects = player->preview.Objects();
                    bool ignored = false;
                    if (!player->playback.Update(player->preview, true, 0, false, ignored, error))
                        return false;
                }
                else if (loop)
                {
                    // Rewind in place: reset audio/captions and actor poses,
                    // retaining selection, cached event tracks and work framing.
                    if (!marcus_seek(player, info.firstFrame))
                    {
                        error.message = player->error;
                        return false;
                    }
                }
                else if (player->selected + 1 < static_cast<int>(player->scenes.size()))
                {
                    const auto transition = player->playback.Transition();
                    // Inspection renders must not replace the movie camera's
                    // outgoing transition image when both views are visible.
                    const auto previous = player->movieImageScene == player->selected &&
                                          player->movieImageFrame == info.currentFrame
                                              ? player->movieImage : player->image;
                    const auto next = player->selected + 1;
                    if (!SelectScene(player, next, true, true))
                    {
                        error.message = player->error;
                        return false;
                    }
                    player->playback.BeginTransition(transition, previous);
                    state->scene_changed = 1;
                }
                else
                {
                    player->playback.Stop();
                    player->finished = true;
                    state->playing = 0;
                }
            }
            state->wait = player->playback.Wait();
            state->scrolling = player->playback.Scrolling();
            state->transition = player->playback.Transitioning();
            return true;
        });
        if (!success)
        {
            marcus_stop(player);
            if (state)
                *state = {};
        }
        return success;
    }
    void marcus_reset_framing(MarcusPlayer *player)
    {
        if (player)
        {
            player->preview.ResetOrbitFraming();
            player->inspectionMatched = false;
        }
    }
    int marcus_set_orbit_target(MarcusPlayer *player, int object)
    {
        return Guard(player, [&](MM::Error &error) {
            if (object < -1 || (object >= 0 && static_cast<std::size_t>(object) >= player->objects.size()))
            {
                error.message = "Select a scene object before orbiting the selection.";
                return false;
            }
            if (player->orbitObject != object)
            {
                player->orbitObject = object;
                marcus_reset_framing(player);
            }
            return true;
        });
    }
    int marcus_match_camera(MarcusPlayer *player, int width, int height, MarcusCamera *camera)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!camera || width < 64 || height < 64 || width > MM::PreviewMaxDimension ||
                height > MM::PreviewMaxDimension)
                return false;
            MM::PreviewCamera matched;
            if (!player->preview.MatchSceneCamera(float(width) / height, matched, error))
                return false;
            *camera = {matched.x, matched.y, matched.z, matched.yaw, matched.pitch, matched.moveSpeed};
            player->inspectionRoll = matched.roll;
            player->inspectionFov = matched.verticalFov;
            player->inspectionMatched = true;
            return true;
        });
    }
    int marcus_pan_orbit(MarcusPlayer *player, float right, float up, float yaw, float pitch, float zoom)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewView view;
            view.width = player->image.width;
            view.height = player->image.height;
            view.yaw = yaw;
            view.pitch = pitch;
            view.zoom = zoom;
            view.orthographic = player->renderOptions.orthographic != 0;
            if (player->inspectionMatched)
            {
                view.roll = player->inspectionRoll;
                view.verticalFov = player->inspectionFov;
                view.nearClip = .1f;
            }
            return player->preview.PanOrbit(view, right, up, error);
        });
    }
    int marcus_render(MarcusPlayer *player, int width, int height, int authored, float yaw, float pitch, float zoom,
                      MarcusImage *image)
    {
        if (image)
            *image = {};
        return Guard(player, [&](MM::Error &error) {
            if (!image || player->selected < 0)
                return false;
            MM::PreviewView view;
            view.width = width;
            view.height = height;
            view.authoredCamera = authored != 0;
            view.frameSceneCamera = !view.authoredCamera;
            view.stableOrbit = !view.authoredCamera;
            view.orbitObject = player->orbitObject;
            view.yaw = yaw;
            view.pitch = pitch;
            view.zoom = zoom;
            if (!view.authoredCamera && player->inspectionMatched)
            {
                view.roll = player->inspectionRoll;
                view.verticalFov = player->inspectionFov;
                view.nearClip = .1f;
            }
            return Render(player, view, image, error);
        });
    }
    int marcus_pick(MarcusPlayer *player, const MarcusImage *image, int x, int y, int *object_index)
    {
        if (object_index)
            *object_index = -1;
        return Guard(player, [&](MM::Error &error) {
            if (!image || !object_index || player->selected < 0 || image->scene != player->selected ||
                image->frame != player->preview.Info().currentFrame || image->document != player->document)
            {
                error.code = MM::ErrorCode::invalid_argument;
                error.message = "Pick requires an image of the current scene and frame.";
                return false;
            }
            // A dissolve can show two scenes. Avoid selecting incoming actors
            // through the outgoing image; the independent work view still picks.
            if (image->authored_camera && player->playback.Transitioning())
                return true;
            float distance;
            const auto projection = ReadProjection(image->projection);
            if (!player->preview.Pick(projection, image->authored_camera != 0, image->width, image->height, x, y,
                                      *object_index, distance, error, true))
                return false;
            if (*object_index < 0)
                return true;
            // Composite only captions over a transparent canvas, with this hit
            // as its depth sample. Opaque lettering must block selection too.
            MM::PreviewImage captions;
            captions.width = image->width;
            captions.height = image->height;
            captions.projection = projection;
            captions.sceneCamera = ReadProjection(image->scene_camera);
            captions.rgba.assign(std::size_t(image->width) * image->height * 4, 0);
            const auto pixel = std::size_t(y) * image->width + x;
            if (!image->authored_camera)
            {
                captions.depth.assign(std::size_t(image->width) * image->height,
                                      std::numeric_limits<float>::infinity());
                captions.depth[pixel] = distance;
            }
            if (!player->playback.Composite(player->preview, image->authored_camera != 0, captions, error))
            {
                *object_index = -1;
                return false;
            }
            if (captions.rgba[pixel * 4 + 3])
                *object_index = -1;
            return true;
        });
    }
    int marcus_render_fly(MarcusPlayer *player, int width, int height, const MarcusCamera *camera, MarcusImage *image)
    {
        // Allow a caller to reuse the camera record returned in this image.
        const MarcusCamera pose = camera ? *camera : MarcusCamera{};
        if (image)
            *image = {};
        return Guard(player, [&](MM::Error &error) {
            if (!camera)
                return false;
            MM::PreviewView view;
            view.width = width;
            view.height = height;
            view.authoredCamera = false;
            view.freeCamera = true;
            view.frameSceneCamera = true;
            view.stableOrbit = true;
            view.cameraX = pose.x;
            view.cameraY = pose.y;
            view.cameraZ = pose.z;
            view.yaw = pose.yaw;
            view.pitch = pose.pitch;
            if (player->inspectionMatched)
            {
                view.roll = player->inspectionRoll;
                view.verticalFov = player->inspectionFov;
                view.nearClip = .1f;
            }
            return Render(player, view, image, error);
        });
    }
}
