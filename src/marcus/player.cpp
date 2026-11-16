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
#include "live_edits.h"
#include "mm/preview.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <chrono>
#include <cctype>
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
    std::vector<std::shared_ptr<MarcusMovieFile>> boardFiles;
    std::shared_ptr<MarcusMovieFile> asmBoardFile;
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
    std::vector<MarcusScene> sceneInfo, baseSceneInfo;
    std::vector<MarcusBackground> backgrounds;
    std::vector<MarcusProp> props;
    std::string propCatalogDirectory, backgroundCatalogDirectory;
    std::vector<MM::PreviewObject> objects;
    std::map<int, MM::PreviewObject> propPreviews;
    std::map<int, MM::PreviewObjectShape> shapePreviews;
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
    using PlacementKey = std::pair<unsigned long, unsigned long>; // Scene and original ACTR chunk.
    using Placements = std::map<PlacementKey, std::array<float, 3>>;
    std::shared_ptr<const Placements> placements = std::make_shared<Placements>();
    struct FrameRange { long first, last; };
    using FrameRanges = std::map<unsigned long, FrameRange>;
    std::shared_ptr<const FrameRanges> frameRanges = std::make_shared<FrameRanges>();
    std::shared_ptr<const FrameRanges> materializedFrameRanges;
    std::shared_ptr<const MarcusLiveEdits> edits = std::make_shared<MarcusLiveEdits>();
    std::shared_ptr<const MarcusLiveEdits> materializedEdits;
    std::uint64_t nextScene = 0, nextObject = 0, nextEditRevision = 1;
    std::int64_t nextActorId = 0;
    std::int32_t retailSource = 2;
    bool resourcesReady = false, retailPrepared = false;
    std::shared_ptr<const Placements> materializedPlacements;
    std::shared_ptr<MarcusMovieFile> materializedFile;
    std::set<long> contentSourceIds;
    long nextBoardSource = 0x7ffffffeL;
    struct UndoState
    {
        std::shared_ptr<const MarcusLiveEdits> edits;
        std::shared_ptr<const Placements> placements;
        std::shared_ptr<const FrameRanges> frameRanges;
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
    MarcusBoardPalette paintPalette = {};
    MarcusBoardInks paintInks = {};
    bool paintReady = false;
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
        before{value.edits, value.placements, value.frameRanges, value.selected, value.selected < 0 ? 0 : value.preview.Info().currentFrame, value.modified}
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
// Native archives are needed for structural edits and isolated thumbnail helpers,
// never for placement or frame-edge edits. Timing helpers analyze the base
// movie once; cached timing inputs are rescheduled for live frame ranges.
std::shared_ptr<MarcusMovieFile> MaterializedMovie(MarcusPlayer &player)
{
    if (player.placements->empty() && player.frameRanges->empty() && player.edits->scenes->empty() &&
        player.edits->props->empty() && player.edits->cameras->empty() && player.edits->shapes->empty() && player.edits->storyboards->empty()) return player.timingInput;
    if (player.materializedPlacements == player.placements && player.materializedFrameRanges == player.frameRanges && player.materializedEdits == player.edits &&
        player.materializedFile) return player.materializedFile;
    std::vector<MarcusPropPlacement> changes;
    std::map<MarcusPlayer::PlacementKey, MarcusPropPlacement> combined;
    for (const auto &entry : *player.placements)
        combined[entry.first] = {entry.first.first, entry.first.second, {entry.second[0], entry.second[1], entry.second[2]}};
    for (const auto &entry : *player.edits->shapes)
    {
        auto &change = combined[entry.first];
        change.scene = entry.first.first; change.actor = entry.first.second; change.shape = entry.second;
    }
    for (const auto &entry : combined) changes.push_back(entry.second);
    std::vector<MarcusSceneRange> ranges;
    for (const auto &entry : *player.frameRanges) ranges.push_back({entry.first, entry.second.first, entry.second.last});
    auto file = MarcusMaterializeEdits(player.content ? player.content->movie : std::filesystem::path{}, player.timingInput,
        changes, ranges, *player.edits, std::filesystem::u8path(player.timingContent) / "tmpls.3cn");
    player.materializedFile = file;
    player.materializedPlacements = player.placements;
    player.materializedFrameRanges = player.frameRanges;
    player.materializedEdits = player.edits;
    return file;
}
void UpdateSceneRanges(MarcusPlayer &player)
{
    player.sceneInfo = player.baseSceneInfo;
    player.scenes.clear();
    for (const auto &scene : player.baseSceneInfo) player.scenes.push_back({sceneType, static_cast<std::uint32_t>(scene.chunk)});
    for (const auto &definition : *player.edits->scenes)
    {
        MarcusScene scene = {};
        scene.chunk = definition.number;
        scene.first_frame = scene.last_frame = 1;
        Copy(scene.name, "Scene " + std::to_string(player.sceneInfo.size() + 1));
        player.sceneInfo.push_back(scene);
        player.scenes.push_back({sceneType, definition.number});
    }
    for (auto &scene : player.sceneInfo)
    {
        const auto range = player.frameRanges->find(scene.chunk);
        if (range == player.frameRanges->end()) continue;
        scene.first_frame = range->second.first;
        scene.last_frame = range->second.last;
    }
}
bool EnsureResources(MarcusPlayer &player, MM::Error &error)
{
    if (!player.resourcesReady)
    {
        auto lease = std::make_shared<MarcusMovieFile>(std::filesystem::path{});
        auto content = std::make_unique<MarcusContent>(lease, player.timingContent);
        if (!player.preview.OpenEmpty(player.timingResources, error)) return false;
        player.content = std::move(content);
        player.resourcesReady = true;
    }
    return true;
}
bool EnsureRetailContent(MarcusPlayer &player, MM::Error &error)
{
    if (!EnsureResources(player, error)) return false;
    if (player.retailPrepared) return true;
    player.content->PrepareSource("3dmovie");
    const auto paths = player.content->files.Files("3dmovie", ".3cn");
    if (paths.empty()) { error.message = "The original 3DMM content files are unavailable."; return false; }
    for (const auto &path : paths)
        if (!player.preview.AddContent(player.retailSource, path.u8string(), error)) return false;
    player.retailPrepared = true;
    return true;
}
std::vector<MM::PreviewPropDefinition> PropDefinitions(const MarcusLiveEdits &edits)
{
    std::vector<MM::PreviewPropDefinition> result;
    result.reserve(edits.props->size());
    for (const auto &prop : *edits.props) result.push_back(prop.definition);
    return result;
}
bool ApplyDefinitions(MarcusPlayer &player, MM::Error &error)
{
    if (!EnsureResources(player, error)) return false;
    const bool retailScene = std::any_of(player.edits->scenes->begin(), player.edits->scenes->end(),
        [&](const auto &scene) { return scene.source == player.retailSource; });
    if ((retailScene || !player.edits->props->empty()) && !EnsureRetailContent(player, error)) return false;
    for (const auto &entry : *player.edits->storyboards)
        if (entry.second.source)
        {
            player.contentSourceIds.insert(entry.second.source);
            if (!player.preview.AddContent(entry.second.source, entry.second.file->path.u8string(), error)) return false;
        }
    if (!player.preview.SetDefinitions(*player.edits->scenes, PropDefinitions(*player.edits), error)) return false;
    for (const auto &prop : *player.edits->props) player.names[prop.definition.actorId] = prop.name;
    UpdateSceneRanges(player);
    return true;
}
bool ApplyCameras(MarcusPlayer &player, int index, MM::Error &error)
{
    std::vector<MM::PreviewCameraEdit> changes;
    const auto scene = player.scenes[index].number;
    for (const auto &entry : *player.edits->cameras)
        if (entry.first.scene == scene) changes.push_back({static_cast<std::int32_t>(entry.first.frame), static_cast<std::uint32_t>(entry.second)});
    return player.preview.SetCameraEdits(changes, error);
}
bool ApplyPlacements(MarcusPlayer &player, int scene, const MarcusPlayer::Placements &placements, MM::Error &error)
{
    const auto objects = player.preview.Objects();
    const float zero[3] = {};
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        const auto &object = objects[index];
        if (!object.hasLifetime) continue;
        const auto found = placements.find({player.scenes[scene].number, object.asset.number});
        if (!player.preview.SetObjectOffset(static_cast<int>(index), found == placements.end() ? zero : found->second.data(), error))
            return false;
        const auto shape = player.edits->shapes->find({player.scenes[scene].number, object.asset.number});
        if (!player.preview.SetObjectShape(static_cast<int>(index), shape == player.edits->shapes->end() ? MM::PreviewObjectShape{} : shape->second, error))
            return false;
    }
    player.objects = player.preview.Objects();
    player.movieImageScene = -1;
    return true;
}
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
// Worker-side IPC adapter: absolute placements avoid both archive reloads and
// accumulating an offset each time the host renders another frame/camera.
bool MarcusPrepareEmpty(MarcusPlayer *player, const char *resources, const char *content, MM::Error &error)
{
    player->timingResources = resources; player->timingContent = content;
    return EnsureRetailContent(*player, error);
}
bool MarcusApplyLiveEdits(MarcusPlayer *player, const std::vector<MM::PreviewSceneDefinition> &scenes,
                         const std::vector<MM::PreviewPropDefinition> &props,
                         const std::vector<MarcusASM::CameraEdit> &cameras, std::uint64_t revision, MM::Error &error)
{
    auto edits = std::make_shared<MarcusLiveEdits>();
    edits->scenes = std::make_shared<MarcusLiveEdits::Scenes>(scenes);
    auto named = std::make_shared<MarcusLiveEdits::Props>();
    for (const auto &prop : props) named->push_back({prop, {}});
    edits->props = named;
    auto changes = std::make_shared<MarcusLiveEdits::Cameras>();
    for (const auto &camera : cameras) (*changes)[{camera.scene, camera.frame}] = camera.camera;
    edits->cameras = changes; edits->revision = revision;
    player->edits = edits;
    if (!ApplyDefinitions(*player, error)) return false;
    if (!player->preview.Info().scene) player->selected = -1;
    player->objects = player->preview.Objects();
    return true;
}
bool MarcusApplySceneRange(MarcusPlayer *player, std::int32_t first, std::int32_t last, MM::Error &error)
{
    if (!player || player->selected < 0 || !player->preview.SetSceneRange(first, last, error)) return false;
    auto &scene = player->sceneInfo[player->selected];
    scene.first_frame = first;
    scene.last_frame = last;
    return ApplyCameras(*player, player->selected, error);
}
bool MarcusApplyPropOffsets(MarcusPlayer *player, const std::vector<MarcusASM::PropOffset> &offsets, MM::Error &error)
{
    int previous = -1;
    for (const auto &offset : offsets)
    {
        if (offset.object <= previous || offset.object < 0 || static_cast<std::size_t>(offset.object) >= player->objects.size() ||
            !player->objects[offset.object].hasLifetime)
        { error.message = "Invalid ASM object placement."; return false; }
        previous = offset.object;
    }
    const float zero[3] = {};
    std::size_t next = 0;
    for (std::size_t index = 0; index < player->objects.size(); ++index)
    {
        const auto &object = player->objects[index];
        if (!object.hasLifetime) continue;
        const bool placed = next < offsets.size() && offsets[next].object == static_cast<int>(index);
        if (!player->preview.SetObjectOffset(static_cast<int>(index), placed ? offsets[next].offset : zero, error)) return false;
        if (!player->preview.SetObjectShape(static_cast<int>(index), placed ? offsets[next].shape : MM::PreviewObjectShape{}, error)) return false;
        if (placed) ++next;
    }
    player->objects = player->preview.Objects();
    return true;
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
            !MarcusPlayback::ReadTiming(presentation, timing, error, player->timingInput ? player->timingInput->fonts : nullptr))
            return false;
        player->soundTimeline.Append(player->sceneInfo[next], presentation.sounds, [&](const MM::PreviewSoundCue &cue) {
            return player->waveforms.Length(player->preview, cue);
        }, timing);
    }
    return true;
}
bool ReadSoundSheet(MarcusPlayer *player, int index, MM::Error &error)
{
    player->sounds.clear();
    player->soundTracks.clear();
    player->soundCues.clear();
    // A new empty scene has no authored cues. Let the pending base analysis
    // finish before deriving any sounds carried from earlier scenes.
    if (static_cast<std::size_t>(index) >= player->baseSceneInfo.size() &&
        player->soundTimeline.Size() < player->baseSceneInfo.size()) return true;
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
    // Send live placements to the same worker as committed ones. Switching to
    // a different evaluator during a drag loses its cached orbit framing.
    const bool nativeBoards = !player->edits->storyboards->empty();
    if (player->renderer == MARCUS_RENDERER_BRENDER_ASM &&
        !(nativeBoards && (!player->propPreviews.empty() || !player->shapePreviews.empty())))
    {
        if (!nativeBoards && player->asmBoardFile) { player->asmRenderer.reset(); player->asmBoardFile.reset(); }
        if (nativeBoards)
        {
            const auto file = MaterializedMovie(*player);
            if (player->asmBoardFile != file) { player->asmRenderer.reset(); player->asmBoardFile = file; }
        }
        if (!player->asmRenderer)
            player->asmRenderer = std::make_unique<MarcusASMRenderer>((nativeBoards ? player->asmBoardFile->path : player->content->movie).u8string(),
                player->timingResources, player->timingContent, player->content && player->content->bundled && player->timingInput
                    ? (player->timingInput->contentOrigin.empty() ? player->timingInput->path : player->timingInput->contentOrigin).u8string() : std::string{});
        std::vector<MarcusASM::PropOffset> offsets;
        for (std::size_t index = 0; !nativeBoards && index < player->objects.size(); ++index)
        {
            const auto &object = player->objects[index];
            const auto placement = player->placements->find({player->scenes[player->selected].number, object.asset.number});
            std::array<float, 3> offset = {};
            if (placement != player->placements->end()) offset = placement->second;
            const auto preview = player->propPreviews.find(static_cast<int>(index));
            if (preview != player->propPreviews.end())
            {
                offset[0] += object.x - preview->second.x;
                offset[1] += object.y - preview->second.y;
                offset[2] += object.z - preview->second.z;
            }
            const bool shaped = player->edits->shapes->count({player->scenes[player->selected].number, object.asset.number}) || player->shapePreviews.count(static_cast<int>(index));
            if (offset[0] != 0 || offset[1] != 0 || offset[2] != 0 || shaped)
            {
                MarcusASM::PropOffset change{static_cast<std::int32_t>(index), {offset[0], offset[1], offset[2]}};
                if (shaped && !player->preview.ObjectShape(static_cast<int>(index), change.shape, nullptr, nullptr, error)) return false;
                offsets.push_back(change);
            }
        }
        const auto &range = player->sceneInfo[player->selected];
        if (!player->asmRenderer->Render(player->selected, player->preview.Info().currentFrame, options, player->image, error,
                                         offsets, range.first_frame, range.last_frame, nativeBoards ? nullptr : player->edits.get()))
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
    for (const auto &entry : player->shapePreviews)
        if (!player->preview.SetObjectShape(entry.first, entry.second, error)) return false;
    if (!player->shapePreviews.empty()) player->movieImageScene = -1;
    player->shapePreviews.clear();
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
        player->shapePreviews.clear();
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
        const auto board = player->edits->storyboards->find(player->scenes[index].number);
        if (board != player->edits->storyboards->end() &&
            !player->preview.SetBackground(board->second.source, board->second.background, error)) return false;
        const auto &range = player->sceneInfo[index];
        if (!player->preview.SetSceneRange(range.first_frame, range.last_frame, error) ||
            (player->preview.Info().currentFrame != range.first_frame && !player->preview.SetFrame(range.first_frame, error)))
            return false;
        if (!ApplyCameras(*player, index, error)) return false;
        if (!ApplyPlacements(*player, index, *player->placements, error)) return false;
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
bool MarcusGetStoryboard(MarcusPlayer *player, MarcusStoryboard &board)
{
    if (!player || player->selected < 0) return false;
    const auto found = player->edits->storyboards->find(player->scenes[player->selected].number);
    if (found == player->edits->storyboards->end()) return false;
    board = *found->second.board;
    return true;
}
bool MarcusApplyStoryboard(MarcusPlayer *player, const MarcusStoryboard &board, bool newScene,
    unsigned long camera, const char *resources, const char *content)
{
    return Guard(player, [&](MM::Error &error) {
        if (!resources || !content || !MarcusBoardValid(board) || camera >= board.angles.size() ||
            (!newScene && !marcus_is_storyboard(player)))
        { error.message = "Select a storyboard scene and valid camera settings."; return false; }
        if (newScene && player->nextScene > 0xffffffffULL)
        { error.message = "No scene IDs are available."; return false; }
        if (!player->resourcesReady) { player->timingResources = resources; player->timingContent = content; }
        if (!EnsureResources(*player, error)) return false;
        while (player->nextBoardSource > 2 && (player->contentSourceIds.count(player->nextBoardSource) || player->nextBoardSource == player->retailSource)) --player->nextBoardSource;
        if (player->nextBoardSource <= 2) { error.message = "No background source IDs are available."; return false; }
        const auto resource = MarcusBoardContent(board, player->nextBoardSource--, player->timingResources, player->timingContent);
        player->boardFiles.push_back(resource.file);
        const auto scene = newScene ? static_cast<unsigned long>(player->nextScene++) : player->scenes[player->selected].number;
        const auto selected = newScene ? static_cast<int>(player->scenes.size()) : player->selected;
        const auto frame = newScene ? 1 : player->preview.Info().currentFrame;
        auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
        auto boards = std::make_shared<MarcusStoryboards>(*edits->storyboards);
        (*boards)[scene] = resource; edits->storyboards = boards;
        auto scenes = std::make_shared<MarcusLiveEdits::Scenes>(*edits->scenes);
        if (newScene) scenes->push_back({static_cast<std::uint32_t>(scene), static_cast<std::uint32_t>(resource.background), static_cast<std::int32_t>(resource.source)});
        else for (auto &definition : *scenes) if (definition.number == scene)
        { definition.background = resource.background; definition.source = resource.source; }
        edits->scenes = scenes;
        auto cameras = std::make_shared<MarcusLiveEdits::Cameras>(*edits->cameras);
        const auto first = newScene ? 1 : player->preview.Info().firstFrame;
        (*cameras)[{scene, frame == first ? std::numeric_limits<std::int32_t>::min() : frame}] = camera;
        edits->cameras = cameras; edits->revision = player->nextEditRevision++;
        EditHistory history(*player);
        const auto previous = player->edits;
        player->edits = edits;
        if (!ApplyDefinitions(*player, error) || !SelectScene(player, selected, true) || !marcus_seek(player, frame))
        {
            const auto failure = error.message.empty() ? std::string(player->error) : error.message;
            player->edits = previous;
            MM::Error ignored; ApplyDefinitions(*player, ignored);
            player->soundTimeline.Reframe(player->sceneInfo);
            if (history.before.scene >= 0) { SelectScene(player, history.before.scene, true); marcus_seek(player, history.before.frame); }
            else { player->preview.ClearScene(); player->selected = -1; }
            error.message = failure;
            return false;
        }
        player->movieImageScene = -1; ++player->document;
        history.Commit();
        return true;
    });
}
bool MarcusRenderStoryboard(MarcusPlayer *player, const MarcusStoryboard &board, unsigned long camera, MarcusImage &image)
{
    return Guard(player, [&](MM::Error &error) {
        if (player->selected < 0 || camera >= board.angles.size()) return false;
        const auto &a = board.angles[camera];
        MarcusBoardPaintColours colours;
        if (!MarcusGetStoryboardColours(player, colours)) return false;
        MM::PreviewView view;
        view.width = MarcusBoardWidth; view.height = MarcusBoardHeight;
        view.authoredCamera = false; view.freeCamera = true; view.includeDepth = true;
        view.cameraX = a.camera.x; view.cameraY = a.camera.y; view.cameraZ = a.camera.z;
        view.yaw = a.camera.yaw; view.pitch = a.camera.pitch; view.roll = a.roll; view.verticalFov = a.fov; view.nearClip = a.nearClip;
        std::vector<MM::PreviewLight> lights;
        for (const auto &l : board.lights) lights.push_back({l.x, l.y, l.z, l.yaw, l.pitch, l.intensity, l.point != 0});
        if (!player->preview.RenderWithLights(view, lights, player->thumbnail, error)) return false;
        auto &rendered = player->thumbnail;
        for (size_t i = 0; i < a.pixels.size(); ++i)
            if (rendered.depth.empty() || !std::isfinite(rendered.depth[i]) || rendered.depth[i] > a.farClip)
            {
                const auto rgb = player->paintPalette[MarcusBoardPaintIndex(player->paintInks[a.pixels[i]], int(i % MarcusBoardWidth), int(i / MarcusBoardWidth))];
                rendered.rgba[i * 4] = static_cast<unsigned char>(rgb >> 16);
                rendered.rgba[i * 4 + 1] = static_cast<unsigned char>(rgb >> 8);
                rendered.rgba[i * 4 + 2] = static_cast<unsigned char>(rgb); rendered.rgba[i * 4 + 3] = 255;
            }
        image = {}; image.width = rendered.width; image.height = rendered.height;
        image.pitch = image.width * 4; image.rgba = rendered.rgba.data();
        return true;
    });
}
bool MarcusGetStoryboardColours(MarcusPlayer *player, MarcusBoardPaintColours &colours)
{
    if (!player || player->selected < 0) return false;
    MarcusBoardPalette palette;
    for (unsigned i = 0; i < palette.size(); ++i) palette[i] = player->preview.PresentationColour(0xfe000000 | i) & 0xffffff;
    if (!player->paintReady || palette != player->paintPalette)
    {
        player->paintPalette = palette; player->paintInks = MarcusBoardColourMap(palette); player->paintReady = true;
    }
    for (unsigned i = 0; i < colours.size(); ++i) colours[i] = player->paintInks[i].rgb;
    return true;
}
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
    int marcus_is_storyboard(const MarcusPlayer *player)
    {
        return player && player->selected >= 0 && player->edits->storyboards->count(player->scenes[player->selected].number) != 0;
    }
    int marcus_requires_vmm(const MarcusPlayer *player)
    {
        if (!player) return 0;
        if (!player->edits->storyboards->empty()) return 1;
        if (!player->content) return 0;
        if (player->content->bundled) return 1;
        const auto dependencies = player->content->Dependencies();
        return std::any_of(dependencies.begin(), dependencies.end(), [](const auto &d) { return d.archive.length != 0; });
    }
    int marcus_save_copy(MarcusPlayer *player, const char *path)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!path || player->scenes.empty()) { error.message = "Add a scene before saving a movie copy."; return false; }
            auto destination = std::filesystem::absolute(std::filesystem::u8path(path));
            auto extension = destination.extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
            if (extension.empty()) { extension = marcus_requires_vmm(player) ? ".vmm" : ".3mm"; destination += extension; }
            if (extension != ".3mm" && extension != ".vmm") { error.message = "Save the movie copy with a .3mm or .vmm extension."; return false; }
            if (extension == ".3mm" && marcus_requires_vmm(player))
            { error.message = "This movie includes storyboard or V3DMM content. Save it with a .vmm filename."; return false; }
            std::error_code sameSourceError;
            if (player->timingInput && std::filesystem::equivalent(player->timingInput->path, destination, sameSourceError))
            { error.message = "Choose a different filename for Save copy; the current movie is open."; return false; }
            if (player->timingInput && !player->timingInput->contentOrigin.empty() &&
                std::filesystem::equivalent(player->timingInput->contentOrigin, destination, sameSourceError))
            { error.message = "Choose a different filename for Save copy; the current bundle is open."; return false; }
            const auto file = MaterializedMovie(*player);
            if (!file) return false;
            std::error_code equivalentError;
            if (std::filesystem::equivalent(file->path, destination, equivalentError)) return true;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            auto temporary = destination; temporary += ".marcus-" + std::to_string(stamp) + ".tmp";
            struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove(path, e); } } cleanup{temporary};
            if (extension == ".vmm")
            {
                const auto dependencies = player->content ? player->content->Dependencies() : std::vector<MM::V3dmm::Dependency>{{"3dmm", {}}};
                if (player->edits->storyboards->empty()) MM::V3dmm::WriteBundle(MM::V3dmm::WholeFile(
                    player->content && player->content->bundled && file == player->timingInput ? player->content->movie : file->path), dependencies, temporary);
                else MarcusWriteStoryboardBundle(*file, *player->edits->storyboards, player->timingResources, player->timingContent,
                    dependencies, player->content ? player->content->files.Files("v3dmm", ".3cn") : std::vector<std::filesystem::path>{}, temporary);
            }
            else std::filesystem::copy_file(file->path, temporary);
            MM::V3dmm::Publish(temporary, destination);
            return true;
        });
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
            if (!resources || !directory) return false;
            KeepHistory history(*player);
            const auto scene = player->selected;
            const auto placements = player->placements;
            const auto ranges = player->frameRanges;
            const auto edits = player->edits;
            const auto nextScene = player->nextScene, nextObject = player->nextObject;
            const auto nextActor = player->nextActorId;
            if (player->timingInput)
            {
                MarcusMovieInput input;
                PrepareInput(input, player->timingInput);
                if (!marcus_open_input(player, &input, 0, resources, directory))
                { error.message = player->error; return false; }
            }
            else
            {
                if (!player->preview.OpenEmpty(resources, error)) return false;
                auto lease = std::make_shared<MarcusMovieFile>(std::filesystem::path{});
                player->content = std::make_unique<MarcusContent>(lease, directory);
                player->resourcesReady = true; player->retailPrepared = false;
                player->timingResources = resources; player->timingContent = directory;
                player->selected = -1; player->asmRenderer.reset();
            }
            player->placements = placements; player->frameRanges = ranges; player->edits = edits;
            player->nextScene = std::max(nextScene, player->nextScene);
            player->nextObject = std::max(nextObject, player->nextObject);
            player->nextActorId = std::max(nextActor, player->nextActorId);
            if (!ApplyDefinitions(*player, error) || !player->soundTimeline.Reframe(player->sceneInfo)) return false;
            if (scene >= 0 && !marcus_select_scene(player, scene)) { error.message = player->error; return false; }
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
            if (!CancelPropPreviews(player, error)) return false;
            const auto previousEdits = player->edits;
            const bool boardsChanged = previousEdits->storyboards != before.edits->storyboards;
            const bool definitionsChanged = previousEdits->scenes != before.edits->scenes || previousEdits->props != before.edits->props || boardsChanged;
            const bool rangesChanged = player->frameRanges != before.frameRanges;
            player->edits = before.edits;
            player->placements = before.placements;
            player->frameRanges = before.frameRanges;
            if (definitionsChanged) { if (!ApplyDefinitions(*player, error)) return false; }
            else UpdateSceneRanges(*player);
            if (rangesChanged || definitionsChanged)
            {
                if (!player->soundTimeline.Reframe(player->sceneInfo)) return false;
                player->animations.clear();
            }
            if (before.scene < 0)
            {
                player->preview.ClearScene();
                player->selected = -1;
                player->objects.clear();
                player->sounds.clear(); player->soundTracks.clear(); player->soundCues.clear();
                player->playback.Stop();
            }
            else
            {
                if (boardsChanged || player->selected != before.scene || !player->preview.Info().scene ||
                    player->preview.Info().asset.number != player->scenes[before.scene].number)
                {
                    if (!marcus_select_scene_keep_view(player, before.scene)) { error.message = player->error; return false; }
                }
                else
                {
                    const auto &range = player->sceneInfo[before.scene];
                    if (!player->preview.SetSceneRange(range.first_frame, range.last_frame, error) ||
                        !ApplyCameras(*player, before.scene, error) ||
                        !ApplyPlacements(*player, before.scene, *before.placements, error)) return false;
                    if ((rangesChanged || definitionsChanged) && !ReadSoundSheet(player, before.scene, error)) return false;
                }
                if (!marcus_seek(player, before.frame)) { error.message = player->error; return false; }
            }
            ++player->document;
            player->movieImageScene = -1;
            player->materializedFile.reset(); player->materializedPlacements.reset();
            player->materializedFrameRanges.reset(); player->materializedEdits.reset();
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
            player->propCatalogDirectory = directory;
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
            if ((player->props.empty() || player->propCatalogDirectory != player->timingContent) &&
                marcus_prop_count(player, player->timingContent.c_str()) < 0)
            { error.message = player->error; return false; }
            const auto found = std::find_if(player->props.begin(), player->props.end(), [&](const auto &prop) { return prop.chunk == chunk; });
            if (found == player->props.end()) { error.message = "The selected prop is unavailable."; return false; }
            if (!EnsureRetailContent(*player, error)) return false;
            if (player->nextObject > 0xffffffffULL || player->nextActorId > std::numeric_limits<std::int32_t>::max())
            { error.message = "No object IDs are available."; return false; }
            MarcusNamedProp prop;
            auto &definition = prop.definition;
            if (!player->preview.PlacementPoint(definition.point)) { error.message = "This scene has no background placement point."; return false; }
            for (const auto value : definition.point)
                if (!std::isfinite(value) || std::abs(value) > 32700) { error.message = "The placement point is out of range."; return false; }
            definition.scene = player->scenes[player->selected].number;
            definition.number = static_cast<std::uint32_t>(player->nextObject++);
            definition.actorId = static_cast<std::int32_t>(player->nextActorId++);
            definition.source = player->retailSource;
            definition.templateNumber = static_cast<std::uint32_t>(chunk);
            definition.first = player->preview.Info().currentFrame;
            definition.last = player->preview.Info().lastFrame;
            prop.name = found->name;
            auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
            auto props = std::make_shared<MarcusLiveEdits::Props>(*edits->props);
            props->push_back(prop); edits->props = props; edits->revision = player->nextEditRevision++;
            EditHistory history(*player);
            const auto previous = player->edits;
            player->edits = edits;
            if (!ApplyDefinitions(*player, error)) { player->edits = previous; return false; }
            player->objects = player->preview.Objects();
            player->animations.clear();
            for (std::size_t index = 0; index < player->objects.size(); ++index)
                if (player->objects[index].actorId == definition.actorId) result = static_cast<int>(index);
            if (result < 0) { error.message = "The prop could not be selected."; return false; }
            ++player->document; player->movieImageScene = -1;
            history.Commit();
            return true;
        });
        return ok ? result : -1;
    }
    unsigned long marcus_document(const MarcusPlayer *player) { return player ? player->document : 0; }
    int marcus_object_shape(MarcusPlayer *player, int object, MarcusObjectShape *shape, float left[9], float right[9])
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewObjectShape value;
            if (!shape || !player->preview.ObjectShape(object, value, left, right, error)) return false;
            std::copy(value.rotation, value.rotation+9, shape->rotation);
            std::copy(value.scale, value.scale+3, shape->scale);
            return true;
        });
    }
    int marcus_preview_object_shape(MarcusPlayer *player, int object, const MarcusObjectShape *shape)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject current;
            if (!shape || !marcus_object(player, object, &current) || !current.visible) return false;
            MM::PreviewObjectShape before, value;
            if (!player->preview.ObjectShape(object, before, nullptr, nullptr, error)) return false;
            std::copy(shape->rotation, shape->rotation+9, value.rotation);
            std::copy(shape->scale, shape->scale+3, value.scale);
            if (!player->preview.SetObjectShape(object, value, error)) return false;
            const auto saved=player->shapePreviews.emplace(object,before);
            if (std::equal(value.rotation,value.rotation+9,saved.first->second.rotation) &&
                std::equal(value.scale,value.scale+3,saved.first->second.scale)) player->shapePreviews.erase(saved.first);
            player->movieImageScene = -1;
            return true;
        });
    }
    int marcus_set_object_shape(MarcusPlayer *player, int object, const MarcusObjectShape *shape)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject current;
            if (!shape || !marcus_object(player, object, &current) || !current.visible) return false;
            MM::PreviewObjectShape value, before;
            if (!player->preview.ObjectShape(object, before, nullptr, nullptr, error)) return false;
            const auto preview = player->shapePreviews.find(object);
            if (preview != player->shapePreviews.end()) before = preview->second;
            std::copy(shape->rotation, shape->rotation+9, value.rotation);
            std::copy(shape->scale, shape->scale+3, value.scale);
            if (std::equal(value.rotation,value.rotation+9,before.rotation) && std::equal(value.scale,value.scale+3,before.scale))
            {
                if (!player->preview.SetObjectShape(object, before, error)) return false;
                player->shapePreviews.erase(object);
                player->movieImageScene = -1;
                return true;
            }
            auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
            auto shapes = std::make_shared<MarcusLiveEdits::Shapes>(*edits->shapes);
            (*shapes)[{player->scenes[player->selected].number, current.chunk}] = value;
            edits->shapes = shapes;
            EditHistory history(*player);
            if (!player->preview.SetObjectShape(object, value, error)) return false;
            player->edits = edits;
            player->shapePreviews.erase(object);
            player->movieImageScene = -1;
            ++player->document;
            history.Commit();
            return true;
        });
    }
    int marcus_preview_object_position(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject before;
            if (!marcus_object(player, object, &before) || !before.visible ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::abs(x) > 32700 || std::abs(y) > 32700 || std::abs(z) > 32700)
            { error.message = "Select a visible object and a position within +/-32700 world units."; return false; }
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
    int marcus_move_object(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject current;
            if (!marcus_object(player, object, &current) || !current.visible ||
                !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::abs(x) > 32700 || std::abs(y) > 32700 || std::abs(z) > 32700)
            { error.message = "Select a visible object and a position within +/-32700 world units."; return false; }
            const auto preview = player->propPreviews.find(object);
            const auto &before = preview == player->propPreviews.end() ? player->objects[object] : preview->second;
            const float delta[3] = {x - before.x, y - before.y, z - before.z};
            if (std::abs(delta[0]) < 1e-6f && std::abs(delta[1]) < 1e-6f && std::abs(delta[2]) < 1e-6f)
            {
                if (preview != player->propPreviews.end())
                {
                    const float restore[3] = {before.x - current.x, before.y - current.y, before.z - current.z};
                    if (!player->preview.TranslateObject(object, restore, error)) return false;
                    player->objects = player->preview.Objects();
                    player->propPreviews.erase(preview);
                }
                return true;
            }
            EditHistory history(*player);
            auto placements = std::make_shared<MarcusPlayer::Placements>(*player->placements);
            const MarcusPlayer::PlacementKey key{player->scenes[player->selected].number, current.chunk};
            auto &offset = (*placements)[key];
            for (int axis = 0; axis < 3; ++axis) offset[axis] += delta[axis];
            if (!player->preview.SetObjectOffset(object, offset.data(), error)) return false;
            if (offset[0] == 0 && offset[1] == 0 && offset[2] == 0) placements->erase(key);
            player->placements = std::move(placements);
            player->objects = player->preview.Objects();
            player->propPreviews.erase(object);
            player->movieImageScene = -1;
            ++player->document;
            history.Commit();
            return true;
        });
    }
    // Retain the narrower prop-only C API for existing callers.
    int marcus_preview_prop_position(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject prop;
            if (!marcus_object(player, object, &prop) || !prop.prop || prop.text)
            { error.message = "Select a visible prop to move."; return false; }
            if (!marcus_preview_object_position(player, object, x, y, z))
            { error.message = player->error; return false; }
            return true;
        });
    }
    int marcus_move_prop(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject prop;
            if (!marcus_object(player, object, &prop) || !prop.prop || prop.text)
            { error.message = "Select a visible prop to move."; return false; }
            if (!marcus_move_object(player, object, x, y, z))
            { error.message = player->error; return false; }
            return true;
        });
    }
    int marcus_set_scene_camera(MarcusPlayer *player, unsigned long camera)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || camera >= player->preview.Info().cameraCount)
            { error.message = "Camera angle is out of range."; return false; }
            auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
            auto cameras = std::make_shared<MarcusLiveEdits::Cameras>(*edits->cameras);
            const auto &info = player->preview.Info();
            const auto frame = info.currentFrame == info.firstFrame ? std::numeric_limits<std::int32_t>::min() : info.currentFrame;
            (*cameras)[{player->scenes[player->selected].number, frame}] = camera;
            edits->cameras = cameras; edits->revision = player->nextEditRevision++;
            EditHistory history(*player);
            const auto previous = player->edits;
            player->edits = edits;
            if (!ApplyCameras(*player, player->selected, error)) { player->edits = previous; return false; }
            player->movieImageScene = -1;
            ++player->document;
            history.Commit();
            return true;
        });
    }
    int marcus_add_frame(MarcusPlayer *player, int direction)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || (direction != -1 && direction != 1))
            { error.message = "Choose the beginning or end of a scene to add a frame."; return false; }
            if (!CancelPropPreviews(player, error)) return false;
            const auto info = player->preview.Info();
            const auto edge = direction < 0 ? info.firstFrame : info.lastFrame;
            const auto frame = std::int64_t(edge) + direction;
            if (info.currentFrame != edge || frame <= std::numeric_limits<std::int32_t>::min() ||
                frame > std::numeric_limits<std::int32_t>::max())
            { error.message = "A frame can only be added at a valid scene boundary."; return false; }
            auto ranges = std::make_shared<MarcusPlayer::FrameRanges>(*player->frameRanges);
            const MarcusPlayer::FrameRange range{direction < 0 ? long(frame) : info.firstFrame,
                                                 direction > 0 ? long(frame) : info.lastFrame};
            (*ranges)[player->scenes[player->selected].number] = range;
            EditHistory history(*player);
            if (!player->preview.SetSceneRange(range.first, range.last, error)) return false;
            if (!player->preview.SetFrame(static_cast<std::int32_t>(frame), error))
            {
                MM::Error ignored;
                player->preview.SetSceneRange(info.firstFrame, info.lastFrame, ignored);
                player->preview.SetFrame(info.currentFrame, ignored);
                return false;
            }
            player->frameRanges = std::move(ranges);
            UpdateSceneRanges(*player);
            if (!player->soundTimeline.Reframe(player->sceneInfo) || !ReadSoundSheet(player, player->selected, error)) return false;
            player->playback.Seek(&player->preview);
            player->objects = player->preview.Objects();
            player->animations.clear();
            player->movieImageScene = -1;
            player->finished = false;
            player->restoreAudio = true;
            ++player->document;
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
            player->backgroundCatalogDirectory = directory;
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
            if ((player->backgrounds.empty() || player->backgroundCatalogDirectory != directory) &&
                marcus_background_count(player, directory) < 0) { error.message = player->error; return false; }
            if (std::none_of(player->backgrounds.begin(), player->backgrounds.end(), [&](const auto &value) { return value.chunk == background; }))
            { error.message = "The selected background is unavailable."; return false; }
            if (!player->resourcesReady) { player->timingResources = resources; player->timingContent = directory; }
            if (!EnsureRetailContent(*player, error)) return false;
            if (player->nextScene > 0xffffffffULL) { error.message = "No scene IDs are available."; return false; }
            auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
            auto scenes = std::make_shared<MarcusLiveEdits::Scenes>(*edits->scenes);
            scenes->push_back({static_cast<std::uint32_t>(player->nextScene++), static_cast<std::uint32_t>(background), player->retailSource});
            edits->scenes = scenes; edits->revision = player->nextEditRevision++;
            EditHistory history(*player);
            const auto previous = player->edits;
            player->edits = edits;
            if (!ApplyDefinitions(*player, error)) { player->edits = previous; return false; }
            if (!marcus_select_scene(player, static_cast<int>(player->scenes.size()) - 1))
            {
                const auto failure = std::string(player->error);
                player->edits = previous;
                MM::Error ignored;
                ApplyDefinitions(*player, ignored);
                player->soundTimeline.Reframe(player->sceneInfo);
                if (history.before.scene >= 0)
                {
                    marcus_select_scene_keep_view(player, history.before.scene);
                    marcus_seek(player, history.before.frame);
                }
                else { player->preview.ClearScene(); player->selected = -1; }
                error.message = failure;
                return false;
            }
            ++player->document; player->movieImageScene = -1;
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
            std::uint64_t nextScene = 0, nextObject = 0;
            std::int64_t nextActor = 0;
            for (std::size_t index = 0; index < archive.Chunks().size(); ++index)
            {
                const auto &chunk = archive.Chunks()[index];
                if (chunk.id.type == sceneType) nextScene = std::max(nextScene, std::uint64_t(chunk.id.number) + 1);
                if (chunk.id.type == 0x41435452)
                {
                    nextObject = std::max(nextObject, std::uint64_t(chunk.id.number) + 1);
                    MM::RecordHeader record;
                    if (!archive.ReadRecord(index, record, error)) return false;
                    nextActor = std::max(nextActor, std::int64_t(record.actor.actorId) + 1);
                }
            }
            for (const auto &name : names) nextActor = std::max(nextActor, std::int64_t(name.first) + 1);
            std::int32_t retailSource = 2;
            bool retailPrepared = false;
            for (const auto &source : sources) retailSource = std::max(retailSource, source.first + 1);
            for (const auto &source : sources)
                if (MM::Content::SourceKey(source.second) == "3dmovie") { retailSource = source.first; retailPrepared = true; break; }
            for (const auto &source : sources)
                content->PrepareSource(source.second);
            auto loadedBoards = MarcusReadBoards(archive, materialized);
            long nextBoardSource = 0x7ffffffeL;
            for (auto &entry : loadedBoards)
            {
                while (std::any_of(sources.begin(), sources.end(), [&](const auto &source) { return source.first == nextBoardSource; })) --nextBoardSource;
                if (nextBoardSource <= 2) { error.message = "No background source IDs are available."; return false; }
                // Rebuild from editable pixels, repairing old palettes. Never
                // treat an outer VMM file as a native chunky resource file.
                entry.second = MarcusBoardContent(*entry.second.board, nextBoardSource--, resources, directory);
            }
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
            player->shapePreviews.clear();
            player->placements = std::make_shared<MarcusPlayer::Placements>();
            player->frameRanges = std::make_shared<MarcusPlayer::FrameRanges>();
            auto initialEdits = std::make_shared<MarcusLiveEdits>();
            initialEdits->storyboards = std::make_shared<MarcusStoryboards>(std::move(loadedBoards));
            player->edits = initialEdits;
            player->contentSourceIds.clear();
            for (const auto &source : sources) player->contentSourceIds.insert(source.first);
            player->nextBoardSource = nextBoardSource;
            player->boardFiles.clear(); player->asmBoardFile.reset();
            player->materializedEdits.reset();
            player->nextScene = nextScene; player->nextObject = nextObject; player->nextActorId = nextActor;
            player->resourcesReady = true; player->retailPrepared = retailPrepared; player->retailSource = retailSource;
            player->materializedFrameRanges.reset();
            player->materializedPlacements.reset();
            player->materializedFile.reset();
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
            player->baseSceneInfo = player->sceneInfo;
            player->names.swap(names);
            for (const auto &entry : *player->edits->storyboards)
            {
                const auto &resource = entry.second;
                if (!player->preview.AddContent(resource.source, resource.file->path.u8string(), error)) return false;
                player->contentSourceIds.insert(resource.source);
            }
            if (!player->preview.Select(player->scenes[0], error, player->preserveEditFraming))
                return false;
            const auto firstBoard = player->edits->storyboards->find(player->scenes[0].number);
            if (firstBoard != player->edits->storyboards->end() &&
                !player->preview.SetBackground(firstBoard->second.source, firstBoard->second.background, error)) return false;
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
            if (player->selected < 0) return false;
            result = new MarcusFramePreviews(MaterializedMovie(*player), player->timingResources,
                                             player->timingContent, player->selected);
            return true;
        });
        return result;
    }
    MarcusFramePreviews *marcus_camera_previews_create(MarcusPlayer *player)
    {
        MarcusFramePreviews *result = nullptr;
        Guard(player, [&](MM::Error &) {
            if (player->selected < 0) return false;
            result = new MarcusFramePreviews(MaterializedMovie(*player), player->timingResources,
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
            const auto first = player->placements->lower_bound({player->scenes[index].number, 0});
            if (first != player->placements->end() && first->first.first == player->scenes[index].number) return true;
            const auto shaped = player->edits->shapes->lower_bound({player->scenes[index].number, 0});
            if (shaped != player->edits->shapes->end() && shaped->first.first == player->scenes[index].number) return true;
            if (static_cast<std::size_t>(index) >= player->baseSceneInfo.size()) return true;
            for (const auto &prop : *player->edits->props)
                if (prop.definition.scene == player->scenes[index].number && prop.definition.first <= player->sceneInfo[index].first_frame)
                    return true;
            for (const auto &camera : *player->edits->cameras)
                if (camera.first.scene == player->scenes[index].number &&
                    (camera.first.frame == std::numeric_limits<std::int32_t>::min() || camera.first.frame == player->sceneInfo[index].first_frame))
                    return true;
            if (player->sceneInfo[index].first_frame != player->baseSceneInfo[index].first_frame) return true;
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
                                                                     player->timingContent, player->baseSceneInfo,
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
            if (!timeline->Reframe(player->sceneInfo)) throw std::runtime_error("Invalid live frame ranges.");
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
