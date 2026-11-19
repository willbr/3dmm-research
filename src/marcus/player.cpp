#include "player.h"
#include "mm/movie_data.h"
#include "mm/movie_publish.h"
#include "mm/movie_scene.h"
#include "mm/movie_source_ids.h"
#include "mm/movie_undo.h"
#include "mm/scene_edit.h"
#include "mm/playback.h"
#include "mm/vmm_dependencies.h"
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
#include "camera_guide.h"
#include "sound_edit.h"
#include "caption_edit.h"
#include "live_edits.h"
#include "mm/preview.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <map>
#include <set>
#include <cctype>
#include <tuple>
#include <exception>
#include <stdexcept>
#include <cmath>

namespace
{
const std::uint32_t sceneType = 0x5343454e, gstType = 0x47535420;
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
struct SoundSheet
{
    std::vector<MarcusSound> sounds;
    std::vector<MarcusSoundTrack> soundTracks;
    std::vector<MM::PreviewSoundCue> soundCues;
};
struct MarcusSourceRegistry : MMMovieSourceIds
{
    using Archive = std::shared_ptr<MarcusMovieFile>;
    MarcusSourceRegistry() { mm_movie_source_ids_init(this, nullptr); }
    MarcusSourceRegistry(const MarcusSourceRegistry &) = delete;
    MarcusSourceRegistry &operator=(const MarcusSourceRegistry &) = delete;
    ~MarcusSourceRegistry() { Clear(); }
    void Clear() { mm_movie_source_ids_clear(this, [](void *archive) { delete static_cast<Archive *>(archive); }); }
};
struct MarcusPlayer
{
    // Destroy the renderer before deleting any private VMM working files.
    std::unique_ptr<MarcusContent> content;
    MarcusSourceRegistry sourceIds;
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
    MM::PreviewImage costumeImage;
    MM::PreviewBounds actionPreviewBounds;
    std::tuple<unsigned long, int, std::int32_t, int, int> actionPreviewKey{};
    MM::PreviewImage captionFontSample;
    int movieImageScene = -1;
    MarcusPlayback playback;
    std::vector<MM::Id> scenes;
    std::vector<MarcusScene> sceneInfo, baseSceneInfo;
    std::vector<MarcusBackground> backgrounds;
    std::vector<MarcusProp> props, actors, fonts, materials;
    std::vector<MarcusSoundAsset> soundAssets;
    std::string soundAssetDirectory;
    std::string propCatalogDirectory, actorCatalogDirectory, fontCatalogDirectory, materialCatalogDirectory, backgroundCatalogDirectory;
    std::vector<MM::PreviewObject> objects;
    std::vector<MM::PreviewAction> objectActions;
    std::map<int, MM::PreviewObject> propPreviews;
    std::map<int, MM::PreviewObjectShape> shapePreviews;
    struct AnimationCache
    {
        MM::PreviewAnimationTimeline timeline;
        MM::Error error;
        bool available = false;
    };
    std::map<int, AnimationCache> animations;
    SoundSheet soundSheet;
    MarcusWaveformCache waveforms;
    MarcusSoundTimeline soundTimeline;
    int playbackRate = 0, playbackModifier = 0;
    MMPolledMovie polledMovie{1, 0};
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
    struct UndoState
    {
        std::shared_ptr<const MarcusLiveEdits> edits;
        std::shared_ptr<const Placements> placements;
        std::shared_ptr<const FrameRanges> frameRanges;
        int scene;
        long frame;
        bool modified;
        std::shared_ptr<MarcusMovieFile> file;
    };
    struct UndoHistory
    {
        MMMovieSnapshotStack stack{};
        static void Release(void *record) { delete static_cast<UndoState *>(record); }
        ~UndoHistory() { mm_movie_snapshot_clear(&stack, Release); }
    } undo;
    float inspectionRoll = 0, inspectionFov = 45;
    std::map<std::int32_t, std::string> names, baseNames;
    char error[1024] = {};
    MMI32 selected = -1;
    unsigned long document = 0;
    struct CaptionPreview { unsigned long document; int scene; std::optional<std::uint32_t> chunk; };
    std::optional<CaptionPreview> captionPreview;
    std::shared_ptr<MarcusMovieFile> timingInput;
    std::string timingResources, timingContent;
    bool timingStarted = false, timingFailed = false;
    MarcusBoardPalette paintPalette = {};
    MarcusBoardInks paintInks = {};
    MarcusBoardPaintColours paintTargets;
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
MarcusPlayer::UndoState CurrentSnapshot(const MarcusPlayer &movie)
{
    return {movie.edits, movie.placements, movie.frameRanges, movie.selected,
            movie.selected < 0 ? 0 : movie.preview.Info().currentFrame, movie.modified, movie.timingInput};
}
struct EditHistory : KeepHistory
{
    std::unique_ptr<MarcusPlayer::UndoState> record;
    const MarcusPlayer::UndoState &before;
    explicit EditHistory(MarcusPlayer &movie) : KeepHistory(movie),
        record(std::make_unique<MarcusPlayer::UndoState>(CurrentSnapshot(movie))), before(*record)
    {
    }
    void Commit()
    {
        mm_movie_snapshot_put(&player.undo.stack, record.release(), MarcusPlayer::UndoHistory::Release);
        player.modified = true;
    }
};
// Native archives are needed for structural edits and isolated thumbnail helpers,
// never for placement or frame-edge edits. Timing helpers analyze the base
// movie once; cached timing inputs are rescheduled for live frame ranges.
std::shared_ptr<MarcusMovieFile> MaterializedMovie(MarcusPlayer &player)
{
    if (player.placements->empty() && player.frameRanges->empty() && !player.edits->scenes->count &&
        player.edits->props->empty() && !player.edits->cameras->count && player.edits->shapes->empty() && player.edits->texts->empty() && player.edits->costumes->empty() && player.edits->storyboards->empty()) return player.timingInput;
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
    for (const auto &entry : *player.edits->texts)
    {
        auto &change = combined[entry.first];
        change.scene = entry.first.first; change.actor = entry.first.second;
    }
    for (const auto &entry : *player.edits->costumes)
    {
        auto &change = combined[entry.first];
        change.scene = entry.first.first; change.actor = entry.first.second;
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
bool MountGeneratedSource(MarcusPlayer &player, MMI32 id, const MarcusSourceRegistry::Archive &file, MM::Error &error)
{
    void *previous;
    if (mm_movie_source_ids_find(&player.sourceIds, id, &previous))
    {
        if (!previous || (*static_cast<const MarcusSourceRegistry::Archive *>(previous))->path != file->path)
        {
            error.message = "The generated content source ID already belongs to another archive.";
            return false;
        }
    }
    else
    {
        auto lease = std::make_unique<MarcusSourceRegistry::Archive>(file);
        if (mm_movie_source_ids_add(&player.sourceIds, id, lease.get()) != MM_MOVIE_SOURCE_ID_ADDED)
            throw std::bad_alloc();
        lease.release();
    }
    return player.preview.AddContent(id, file->path.u8string(), error);
}
bool ApplyDefinitions(MarcusPlayer &player, MM::Error &error)
{
    if (!EnsureResources(player, error)) return false;
    const bool retailScene = mm_movie_scene_definitions_uses_source(player.edits->scenes.get(), player.retailSource);
    if ((retailScene || !player.edits->props->empty()) && !EnsureRetailContent(player, error)) return false;
    for (const auto &entry : *player.edits->storyboards)
        if (entry.second.source && !MountGeneratedSource(player, entry.second.source, entry.second.file, error))
            return false;
    for (const auto &prop : *player.edits->props)
        if (prop.file && !MountGeneratedSource(player, prop.definition.source, prop.file, error))
            return false;
    for (const auto &entry : *player.edits->texts)
    {
        const auto &edit = entry.second;
        if (!MountGeneratedSource(player, edit.source, edit.file, error)) return false;
    }
    for (const auto &entry : *player.edits->costumes)
    {
        const auto &edit = entry.second;
        if (!MountGeneratedSource(player, edit.source, edit.file, error)) return false;
    }
    const MMPreviewLiveDefinitions definitions{
        &player, player.edits->scenes->count, static_cast<MMU32>(player.edits->props->size()),
        [](void *p, MMU32 index, MMPreviewLiveScene *out) {
            *out = static_cast<MarcusPlayer *>(p)->edits->scenes->begin()[index];
        },
        [](void *p, MMU32 index, MMPreviewLiveProp *out) {
            const auto &prop = (*static_cast<MarcusPlayer *>(p)->edits->props)[index].definition;
            *out = {prop.scene, prop.number, prop.templateNumber, prop.source, prop.actorId, prop.first, prop.last,
                    {prop.point[0], prop.point[1], prop.point[2]}};
        }};
    if (!player.preview.SetLiveDefinitions(definitions, error)) return false;
    player.names = player.baseNames;
    for (const auto &prop : *player.edits->props) player.names[prop.definition.actorId] = prop.name;
    for (const auto &object : player.preview.Objects())
    {
        const auto edit = player.edits->texts->find({player.preview.Info().asset.number, object.asset.number});
        if (edit != player.edits->texts->end()) player.names[object.actorId] = edit->second.value.text;
    }
    UpdateSceneRanges(player);
    return true;
}
bool ApplyCameras(MarcusPlayer &player, int index, const MarcusLiveEdits::Cameras &cameras, MM::Error &error)
{
    return player.preview.SetLiveCameraEdits(mm_movie_camera_journal_preview(&cameras, player.scenes[index].number), error);
}
bool ApplyCameras(MarcusPlayer &player, int index, MM::Error &error)
{
    return ApplyCameras(player, index, *player.edits->cameras, error);
}
bool ApplyPlacements(MarcusPlayer &player, int scene, const MarcusPlayer::Placements &placements, MM::Error &error)
{
    const auto objects = player.preview.Objects();
    const float zero[3] = {};
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        const auto &object = objects[index];
        const auto costume = player.edits->costumes->find({player.scenes[scene].number, object.asset.number});
        if (costume != player.edits->costumes->end() && !player.preview.SetObjectEventTrack(int(index), costume->second.source, error)) return false;
        const auto text = player.edits->texts->find({player.scenes[scene].number, object.asset.number});
        if (text != player.edits->texts->end())
        {
            if (!player.preview.SetObjectTextTemplate(int(index), text->second.source, 0, error)) return false;
            player.names[object.actorId] = text->second.value.text;
            if (text->second.hasMaterial && !player.preview.SetObjectTextMaterial(int(index), text->second.source, 0, error)) return false;
        }
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
    auto definitions = std::make_shared<MarcusLiveEdits::Scenes>();
    for (const auto &scene : scenes) definitions->Append({scene.number, scene.background, scene.source});
    edits->scenes = definitions;
    auto named = std::make_shared<MarcusLiveEdits::Props>();
    for (const auto &prop : props) named->push_back({prop, {}});
    edits->props = named;
    auto changes = std::make_shared<MarcusLiveEdits::Cameras>();
    for (const auto &camera : cameras) changes->Set({camera.scene, camera.frame, camera.camera});
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
bool ReadSoundTimeline(MarcusPlayer *player, int index, const std::vector<MarcusScene> &scenes,
                       MarcusSoundTimeline &timeline, MM::Error &error)
{
    while (timeline.Size() <= static_cast<std::size_t>(index))
    {
        const auto next = timeline.Size();
        MM::PreviewPresentation presentation;
        MarcusSoundTiming timing;
        if (!player->preview.ReadTimingPresentation(player->scenes[next], presentation, error) ||
            !MarcusPlayback::ReadTiming(presentation, timing, error, player->timingInput ? player->timingInput->fonts : nullptr))
            return false;
        timeline.Append(scenes[next], presentation.sounds, [&](const MM::PreviewSoundCue &cue) {
            return player->waveforms.Length(player->preview, cue);
        }, timing);
    }
    return true;
}
bool ReadSoundTimeline(MarcusPlayer *player, int index, MM::Error &error)
{
    return ReadSoundTimeline(player, index, player->sceneInfo, player->soundTimeline, error);
}
bool ReadSoundSheet(MarcusPlayer *player, int index, const std::vector<MarcusScene> &scenes,
                    MarcusSoundTimeline &timeline, SoundSheet &sheet, MM::Error &error)
{
    sheet.sounds.clear();
    sheet.soundTracks.clear();
    sheet.soundCues.clear();
    // A new empty scene has no authored cues. Let the pending base analysis
    // finish before deriving any sounds carried from earlier scenes.
    if (static_cast<std::size_t>(index) >= player->baseSceneInfo.size() &&
        timeline.Size() < player->baseSceneInfo.size()) return true;
    if (!ReadSoundTimeline(player, index, scenes, timeline, error))
        return false;
    const auto &scene = timeline.Scene(index);
    struct SheetModel
    {
        MMMovieSoundSheet data{};
        ~SheetModel() { mm_movie_sound_sheet_dispose(&data); }
    } model;
    if (!scene.BuildSheet(model.data))
        throw std::bad_alloc();
    sheet.soundTracks.reserve(model.data.track_count);
    sheet.sounds.reserve(model.data.cue_count);
    sheet.soundCues.reserve(model.data.cue_count);
    for (std::size_t i = 0; i < model.data.track_count; ++i)
    {
        const auto &lane = model.data.tracks[i];
        const auto actor = lane.actor, style = lane.style;
        MarcusSoundTrack track = {};
        track.actor_sound = lane.actor_sound;
        track.actor_id = actor;
        track.style = style;
        track.cue_count = static_cast<int>(lane.cue_count);
        const char *kind = style == MARCUS_SOUND_MUSIC ? "Music" : style == MARCUS_SOUND_SPEECH ? "Speech"
                           : style == MARCUS_SOUND_EFFECT ? "Effects" : "Sound";
        std::string owner = "Scene";
        if (lane.actor_sound)
        {
            const auto name = player->names.find(actor);
            owner = name != player->names.end() ? name->second : "Actor " + std::to_string(actor);
        }
        Copy(track.name, owner + " / " + kind);
        sheet.soundTracks.push_back(track);
    }
    for (std::size_t row = 0; row < model.data.cue_count; ++row)
    {
        const auto &extent = model.data.cues[row];
        const auto cue = scene.Sound(row).cue;
        MarcusSound sound = {};
        sound.track = static_cast<int>(extent.track);
        Copy(sound.name, cue.silent ? "Stop / silence" : "Sound " + std::to_string(cue.sound.number));
        std::string soundName;
        if (!cue.silent && player->preview.SoundName(cue, soundName)) Copy(sound.name, soundName);
        const auto name = player->names.find(cue.actor);
        Copy(sound.owner, !cue.actorSound ? std::string("Scene")
                          : name != player->names.end() ? name->second : "Actor " + std::to_string(cue.actor));
        sound.carried = extent.carried;
        sound.origin_scene = static_cast<int>(extent.origin_scene);
        sound.origin_frame = extent.origin_frame;
        sound.frame = extent.frame;
        sound.timeline_frame = extent.timeline_frame;
        sound.end_frame = extent.end_frame;
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
        sheet.sounds.push_back(sound);
        sheet.soundCues.push_back(cue);
    }
    return true;
}
bool ReadSoundSheet(MarcusPlayer *player, int index, MM::Error &error)
{
    return ReadSoundSheet(player, index, player->sceneInfo, player->soundTimeline, player->soundSheet, error);
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
    options.includeDepth = !view.authoredCamera || view.freeCamera || settings.ground_grid;
    options.includePaletteIndices = view.authoredCamera && !view.freeCamera && player->playback.PaletteAnimating();
    if (!image || player->selected < 0)
        return false;
    player->image.paletteIndices.clear();
    // Send live placements to the same worker as committed ones. Switching to
    // a different evaluator during a drag loses its cached orbit framing.
    const bool nativeBoards = !player->edits->storyboards->empty() || !player->edits->costumes->empty();
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
    const auto &caption = player->captionPreview;
    const bool editing = caption && caption->document == player->document && caption->scene == player->selected;
    const auto hide = editing && caption->chunk ? &*caption->chunk : nullptr;
    if (!player->playback.Composite(player->preview, view.authoredCamera && !view.freeCamera, player->image, error,
                                    hide, editing))
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
        player->playback.CompositeTransition(player->image);
        player->movieImage = player->image;
        player->movieImageScene = player->selected;
    }
    image->width = static_cast<int>(player->image.width);
    image->height = static_cast<int>(player->image.height);
    image->pitch = image->width * 4;
    image->rgba = player->image.rgba.data();
    std::fill(image->content_rect, image->content_rect + 4, 0);
    const auto &camera = player->image.camera;
    image->camera = {camera.x, camera.y, camera.z, camera.yaw, camera.pitch, camera.moveSpeed};
    image->projection = CopyProjection(player->image.projection);
    image->scene_camera = CopyProjection(player->image.sceneCamera);
    image->scene = player->selected;
    image->frame = player->preview.Info().currentFrame;
    image->authored_camera = view.authoredCamera && !view.freeCamera;
    image->document = player->document;
    if (settings.ground_grid && !(image->authored_camera && player->playback.Transitioning()) &&
        player->image.depth.size() == std::size_t(image->width) * image->height)
        camera_blend_ground_grid(image, player->image.rgba.data(), player->image.depth.data());
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
struct RangeEditHost
{
    MarcusPlayer &movie;
    MM::Error &error;
    std::optional<EditHistory> history;
    std::shared_ptr<const MarcusPlayer::FrameRanges> ranges;
    std::vector<MarcusScene> scenes;
    std::optional<MarcusSoundTimeline> timeline;
    SoundSheet sheet;
    std::vector<MM::PreviewObject> objects;
    std::exception_ptr failure;

    bool Prepare(const MMMovieRangeEdit &edit)
    {
        auto next = std::make_shared<MarcusPlayer::FrameRanges>(*movie.frameRanges);
        (*next)[movie.scenes[movie.selected].number] = {edit.first, edit.last};
        ranges = std::move(next);
        history.emplace(movie);
        scenes = movie.sceneInfo;
        scenes[movie.selected].first_frame = edit.first;
        scenes[movie.selected].last_frame = edit.last;
        timeline = movie.soundTimeline.Reframed(scenes);
        return timeline && ReadSoundSheet(&movie, movie.selected, scenes, *timeline, sheet, error);
    }

    static int Effect(void *context, MMMovieRangeEffect effect, const MMMovieRangeEdit *edit, int restoring)
    {
        auto &h = *static_cast<RangeEditHost *>(context);
        auto &movie = h.movie;
        MM::Error ignored;
        auto &error = restoring ? ignored : h.error;
        try
        {
            switch (effect)
            {
            case MM_MOVIE_RANGE_CANCEL_PREVIEWS: return CancelPropPreviews(&movie, error);
            case MM_MOVIE_RANGE_PREPARE: return h.Prepare(*edit);
            case MM_MOVIE_RANGE_SET_BOUNDS: return movie.preview.SetSceneRange(edit->first, edit->last, error);
            case MM_MOVIE_RANGE_SET_FRAME: return movie.preview.SetFrame(edit->frame, error);
            case MM_MOVIE_RANGE_PREPARE_FRAME:
                h.objects = movie.preview.Objects();
                movie.playback.Seek(&movie.preview);
                break;
            case MM_MOVIE_RANGE_ABANDON:
                movie.objects.clear();
                movie.animations.clear();
                movie.soundSheet = {};
                movie.movieImageScene = -1;
                movie.preview.ClearScene();
                movie.playback.Stop();
                break;
            }
            return 1;
        }
        catch (...)
        {
            if (!restoring)
                h.failure = std::current_exception();
            return 0;
        }
    }

    MMMovieRangeServices Services()
    {
        return {this, &movie.selected, &movie.polledMovie,
            [](void *p, MMMovieRangeEdit *out) {
                const auto &info = static_cast<RangeEditHost *>(p)->movie.preview.Info();
                *out = {info.firstFrame, info.lastFrame, info.currentFrame};
            },
            Effect,
            [](void *p) {
                auto &h = *static_cast<RangeEditHost *>(p);
                auto &movie = h.movie;
                movie.frameRanges = std::move(h.ranges);
                movie.sceneInfo.swap(h.scenes);
                movie.soundTimeline = std::move(*h.timeline);
                movie.soundSheet = std::move(h.sheet);
                movie.objects.swap(h.objects);
                movie.animations.clear();
                movie.movieImageScene = -1;
                ++movie.document;
                h.history->Commit();
            }};
    }
};
struct SceneCursorHost
{
    MarcusPlayer &movie;
    MM::Error &error;
    std::exception_ptr failure;

    static int Effect(void *context, MMMovieSceneCursorEffect effect, MMMovieSceneCursorRequest *request)
    {
        auto &host = *static_cast<SceneCursorHost *>(context);
        auto &movie = host.movie;
        try
        {
            switch (effect)
            {
            case MM_MOVIE_SCENE_CLEAR_VIEW:
                movie.objects.clear();
                movie.propPreviews.clear();
                movie.shapePreviews.clear();
                if (!movie.preserveEditFraming) movie.orbitObject = -1;
                movie.animations.clear();
                movie.soundSheet.sounds.clear();
                movie.soundSheet.soundTracks.clear();
                break;
            case MM_MOVIE_SCENE_STOP_PLAYBACK:
                movie.playback.Stop();
                break;
            case MM_MOVIE_SCENE_LOAD:
                return movie.preview.Select(movie.scenes[request->scene], host.error, request->keep_framing != 0);
            case MM_MOVIE_SCENE_BACKGROUND:
            {
                const auto board = movie.edits->storyboards->find(movie.scenes[request->scene].number);
                return board == movie.edits->storyboards->end() ||
                       movie.preview.SetBackground(board->second.source, board->second.background, host.error);
            }
            case MM_MOVIE_SCENE_READ_RANGE:
            {
                const auto &range = movie.sceneInfo[request->scene];
                request->first = static_cast<MMI32>(range.first_frame);
                request->last = static_cast<MMI32>(range.last_frame);
                break;
            }
            case MM_MOVIE_SCENE_SET_RANGE:
                return movie.preview.SetSceneRange(request->first, request->last, host.error);
            case MM_MOVIE_SCENE_READ_FRAME:
                request->frame = movie.preview.Info().currentFrame;
                break;
            case MM_MOVIE_SCENE_SET_FRAME:
                return movie.preview.SetFrame(request->frame, host.error);
            case MM_MOVIE_SCENE_APPLY_CAMERAS:
                return ApplyCameras(movie, request->scene, host.error);
            case MM_MOVIE_SCENE_APPLY_PLACEMENTS:
                return ApplyPlacements(movie, request->scene, *movie.placements, host.error);
            case MM_MOVIE_SCENE_RESET_FRAMING:
                movie.inspectionMatched = false;
                break;
            case MM_MOVIE_SCENE_SET_PLAYBACK:
                return movie.playback.Select(movie.preview, host.error, request->keep_audio != 0);
            case MM_MOVIE_SCENE_SYNC_OBJECTS:
                movie.objects = movie.preview.Objects();
                break;
            case MM_MOVIE_SCENE_READ_SOUNDS:
                return ReadSoundSheet(&movie, request->scene, host.error);
            case MM_MOVIE_SCENE_CANCEL_PREVIEWS:
                return CancelPropPreviews(&movie, host.error);
            case MM_MOVIE_SCENE_SEEK_PLAYBACK:
                movie.playback.Seek(&movie.preview);
                break;
            }
            return 1;
        }
        catch (...) { host.failure = std::current_exception(); return -1; }
    }

    MMMovieSceneCursorServices Services()
    {
        return {this, &movie.selected, &movie.polledMovie, Effect};
    }

    bool Select(MMI32 scene, unsigned flags)
    {
        const auto services = Services();
        return Result(mm_movie_scene_cursor_select(scene, movie.scenes.size(), flags, &services));
    }

    bool Seek(long frame)
    {
        const auto services = Services();
        return Result(mm_movie_scene_cursor_seek(frame, &services));
    }

    bool Result(int status)
    {
        if (failure)
            std::rethrow_exception(failure);
        return status > 0;
    }
};
int SelectScene(MarcusPlayer *player, int index, bool preserveFraming, bool preserveAudio = false)
{
    return Guard(player, [&](MM::Error &error) {
        unsigned flags = 0;
        if (preserveFraming) flags |= MM_MOVIE_SCENE_KEEP_FRAMING;
        if (preserveAudio) flags |= MM_MOVIE_SCENE_KEEP_AUDIO;
        return SceneCursorHost{*player, error, {}}.Select(index, flags);
    });
}
bool OpenEditedBase(MarcusPlayer *player, const std::shared_ptr<MarcusMovieFile> &file, MM::Error &error)
{
    KeepHistory history(*player);
    const auto resources = player->timingResources, directory = player->timingContent;
    const auto nextScene = player->nextScene, nextObject = player->nextObject;
    const auto nextActor = player->nextActorId;
    struct FramingGuard {
        bool &value; bool before;
        explicit FramingGuard(bool &v) : value(v), before(v) { value = true; }
        ~FramingGuard() { value = before; }
    } framing(player->preserveEditFraming);
    bool ok;
    if (file)
    {
        MarcusMovieInput input; PrepareInput(input, file);
        ok = marcus_open_input(player, &input, 0, resources.c_str(), directory.c_str()) != 0;
        if (!ok) error.message = player->error;
    }
    else
    {
        ok = player->preview.OpenEmpty(resources, error);
        if (ok)
        {
            auto empty = std::make_shared<MarcusMovieFile>(std::filesystem::path{});
            player->content = std::make_unique<MarcusContent>(empty, directory);
            player->timingInput.reset(); player->timingJob.reset();
            player->timingStarted = player->timingFailed = false;
            player->baseSceneInfo.clear(); player->baseNames.clear(); player->names.clear();
            player->scenes.clear(); player->sceneInfo.clear(); player->objects.clear();
            player->soundTimeline.Clear(); player->waveforms.Clear(); player->playback.Stop();
            player->soundSheet.sounds.clear(); player->soundSheet.soundCues.clear(); player->soundSheet.soundTracks.clear();
            player->resourcesReady = true; player->retailPrepared = false;
            player->asmRenderer.reset(); player->asmBoardFile.reset(); player->selected = -1;
            player->sourceIds.Clear();
        }
    }
    player->nextScene = std::max(nextScene, player->nextScene); player->nextObject = std::max(nextObject, player->nextObject);
    player->nextActorId = std::max(nextActor, player->nextActorId);
    player->materializedFile.reset(); player->materializedEdits.reset();
    player->materializedPlacements.reset(); player->materializedFrameRanges.reset(); player->movieImageScene = -1;
    return ok;
}
struct SnapshotHost
{
    MarcusPlayer &movie;
    MM::Error &error;
    std::exception_ptr failure;
    MMI32 pastedActor = -1;
    int *outputObject = nullptr;
    int matchedObject = -1;

    static int Apply(void *p, MMMovieSnapshotEffect effect, const void *record, int recovering)
    {
        auto &h = *static_cast<SnapshotHost *>(p);
        auto &movie = h.movie;
        const auto *snapshot = static_cast<const MarcusPlayer::UndoState *>(record);
        MM::Error ignored;
        auto &error = recovering ? ignored : h.error;
        try
        {
            switch (effect)
            {
            case MM_MOVIE_SNAPSHOT_CANCEL: return CancelPropPreviews(&movie, error);
            case MM_MOVIE_SNAPSHOT_OPEN: return OpenEditedBase(&movie, snapshot->file, error);
            case MM_MOVIE_SNAPSHOT_INSTALL:
                movie.edits = snapshot->edits;
                movie.placements = snapshot->placements;
                movie.frameRanges = snapshot->frameRanges;
                break;
            case MM_MOVIE_SNAPSHOT_DEFINITIONS: return ApplyDefinitions(movie, error);
            case MM_MOVIE_SNAPSHOT_RANGES: UpdateSceneRanges(movie); break;
            case MM_MOVIE_SNAPSHOT_TIMING:
                if (!movie.soundTimeline.Reframe(movie.sceneInfo))
                    return 0;
                movie.animations.clear();
                break;
            case MM_MOVIE_SNAPSHOT_ABANDON:
            case MM_MOVIE_SNAPSHOT_CLEAR_SCENE:
                movie.selected = -1;
                movie.movieImageScene = -1;
                movie.objects.clear();
                movie.soundSheet.sounds.clear();
                movie.soundSheet.soundTracks.clear();
                movie.soundSheet.soundCues.clear();
                movie.preview.ClearScene();
                movie.playback.Stop();
                break;
            case MM_MOVIE_SNAPSHOT_SELECT:
            case MM_MOVIE_SNAPSHOT_SELECT_RESET_FRAMING:
                if (!SelectScene(&movie, snapshot->scene, effect == MM_MOVIE_SNAPSHOT_SELECT))
                {
                    error.message = movie.error;
                    return 0;
                }
                break;
            case MM_MOVIE_SNAPSHOT_APPLY_VIEW:
            {
                const auto &range = movie.sceneInfo[snapshot->scene];
                return movie.preview.SetSceneRange(range.first_frame, range.last_frame, error) &&
                       ApplyCameras(movie, snapshot->scene, error) &&
                       ApplyPlacements(movie, snapshot->scene, *snapshot->placements, error);
            }
            case MM_MOVIE_SNAPSHOT_SOUNDS: return ReadSoundSheet(&movie, snapshot->scene, error);
            case MM_MOVIE_SNAPSHOT_SEEK:
                if (!marcus_seek(&movie, snapshot->frame))
                {
                    error.message = movie.error;
                    return 0;
                }
                break;
            case MM_MOVIE_SNAPSHOT_VERIFY:
                if (h.outputObject)
                {
                    const auto found = std::find_if(movie.objects.begin(), movie.objects.end(),
                        [&](const auto &object) { return object.actorId == h.pastedActor; });
                    if (found == movie.objects.end())
                    {
                        error.message = "Cannot load the pasted object and its content.";
                        return 0;
                    }
                    h.matchedObject = static_cast<int>(found - movie.objects.begin());
                }
                break;
            }
            return 1;
        }
        catch (...)
        {
            if (!recovering)
                h.failure = std::current_exception();
            return 0;
        }
    }

    MMMovieSnapshotServices Services()
    {
        return {this,
            [](void *p, const void *record, MMMovieSnapshotView *out) {
                const auto &movie = static_cast<SnapshotHost *>(p)->movie;
                const auto *snapshot = static_cast<const MarcusPlayer::UndoState *>(record);
                const auto &edits = *(snapshot ? snapshot->edits : movie.edits);
                *out = {snapshot ? snapshot->file.get() : movie.timingInput.get(),
                        edits.scenes.get(), edits.props.get(), edits.storyboards.get(), edits.texts.get(),
                        edits.costumes.get(), snapshot ? snapshot->frameRanges.get() : movie.frameRanges.get(),
                        snapshot ? snapshot->scene : movie.selected};
            },
            [](const void *left, const void *right) {
                return int(static_cast<const MarcusMovieFile *>(left)->path ==
                           static_cast<const MarcusMovieFile *>(right)->path);
            },
            [](void *p, MMI32 scene) {
                const auto &movie = static_cast<SnapshotHost *>(p)->movie;
                return int(movie.selected == scene && static_cast<std::size_t>(scene) < movie.scenes.size() &&
                           movie.preview.Info().scene && movie.preview.Info().asset.number == movie.scenes[scene].number);
            },
            Apply,
            [](void *p, const void *record) {
                auto &movie = static_cast<SnapshotHost *>(p)->movie;
                ++movie.document;
                movie.movieImageScene = -1;
                movie.materializedFile.reset();
                movie.materializedPlacements.reset();
                movie.materializedFrameRanges.reset();
                movie.materializedEdits.reset();
                movie.modified = !record || static_cast<const MarcusPlayer::UndoState *>(record)->modified;
            },
            [](void *p) {
                auto &host = *static_cast<SnapshotHost *>(p);
                host.movie.modified = true;
                if (host.outputObject)
                    *host.outputObject = host.matchedObject;
            }};
    }
};
bool ReplaceEditedMovie(MarcusPlayer *player, const std::shared_ptr<MarcusMovieFile> &file, MM::Error &error,
                        std::int32_t actorId = -1, int *newObject = nullptr)
{
    EditHistory history(*player);
    auto edits = std::make_shared<MarcusLiveEdits>();
    edits->storyboards = player->edits->storyboards;
    edits->revision = player->nextEditRevision++;
    const MarcusPlayer::UndoState replacement{std::move(edits), std::make_shared<MarcusPlayer::Placements>(),
        std::make_shared<MarcusPlayer::FrameRanges>(), history.before.scene, history.before.frame, true, file};
    SnapshotHost host{*player, error, {}, actorId, newObject};
    const auto services = host.Services();
    const int installed = mm_movie_snapshot_replace(&player->undo.stack, history.record.release(), &replacement,
                                                     MarcusPlayer::UndoHistory::Release, &services);
    if (host.failure)
        std::rethrow_exception(host.failure);
    return installed != 0;
}
bool ApplySceneEdit(MarcusPlayer &movie, std::shared_ptr<const MarcusLiveEdits> edits, MMI32 scene, MMI32 frame,
                    bool keepFraming, bool seekFrame, MM::Error &error)
{
    EditHistory history(movie);
    auto replacement = history.before;
    replacement.edits = std::move(edits);
    replacement.scene = scene;
    replacement.frame = frame;
    replacement.modified = true;
    SnapshotHost host{movie, error, {}};
    const auto services = host.Services();
    const auto applied = mm_movie_snapshot_edit_scene(&movie.undo.stack, history.record.release(), &replacement,
        keepFraming, seekFrame, MarcusPlayer::UndoHistory::Release, &services);
    if (host.failure)
        std::rethrow_exception(host.failure);
    return applied != 0;
}
void ReadSoundAssets(MarcusPlayer *player)
{
    if (player->soundAssetDirectory == player->timingContent && !player->soundAssets.empty()) return;
    auto assets = MarcusReadSoundAssets(std::filesystem::u8path(player->timingContent) / "snds.3cn");
    player->soundAssets = std::move(assets); player->soundAssetDirectory = player->timingContent;
}
bool SoundAssetCue(MarcusPlayer *player, int index, MM::PreviewSoundCue &cue, MM::Error &error)
{
    ReadSoundAssets(player);
    if (index < 0 || std::size_t(index) >= player->soundAssets.size())
    { error.message = "Choose a built-in sound from the library."; return false; }
    if (!EnsureRetailContent(*player, error)) return false;
    const auto &asset = player->soundAssets[index];
    cue.source = player->retailSource; cue.sound = {0x4d534e44, static_cast<std::uint32_t>(asset.chunk)};
    cue.style = asset.style; return true;
}
bool SoundChange(MarcusPlayer *player, int index, const char *path, const MarcusSoundOptions &options, bool remove, MM::Error &error,
    const MarcusSoundAsset *asset = nullptr)
{
    if (player->selected < 0 || options.volume < 0 || options.volume > 65536 ||
        options.style < MARCUS_SOUND_EFFECT || options.style > MARCUS_SOUND_MUSIC ||
        (options.loop != 0 && options.loop != 1) || (options.queue != 0 && options.queue != 1))
    { error.message = "Choose a scene and valid sound settings."; return false; }
    MM::PreviewSoundCue cue;
    const bool adding = path || asset;
    if (asset && (options.style == MARCUS_SOUND_MUSIC) != (asset->style == MARCUS_SOUND_MUSIC))
    { error.message = "Music uses MIDI; sampled sounds use Effects or Speech."; return false; }
    if (!adding)
    {
        if (index < 0 || std::size_t(index) >= player->soundSheet.soundCues.size()) return false;
        cue = player->soundSheet.soundCues[index];
        if (!remove && (options.style == MARCUS_SOUND_MUSIC) != (cue.style == MARCUS_SOUND_MUSIC))
        { error.message = "Music uses MIDI; sampled sounds can switch between Effects and Speech."; return false; }
        if (cue.actorSound && (options.frame != cue.frame || options.style != cue.style))
        { error.message = "Actor sounds retain their animation binding and category."; return false; }
        if (!remove && cue.frame == options.frame && cue.volume == options.volume && cue.style == options.style &&
            cue.loop == bool(options.loop) && (cue.actorSound ? cue.queue == bool(options.queue) : true)) return true;
    }
    const int sceneIndex = adding ? player->selected : player->soundSheet.sounds[index].origin_scene;
    if (sceneIndex < 0 || std::size_t(sceneIndex) >= player->sceneInfo.size() ||
        options.frame < player->sceneInfo[sceneIndex].first_frame || options.frame > player->sceneInfo[sceneIndex].last_frame)
    { error.message = "The cue frame must be inside its scene."; return false; }
    if (!CancelPropPreviews(player, error)) return false;
    const auto library = (std::filesystem::u8path(player->timingContent) / "snds.3cn").u8string();
    auto file = MarcusEditSound(MaterializedMovie(*player), player->scenes[sceneIndex].number, adding ? nullptr : &cue, options, path, remove,
        asset ? library.c_str() : nullptr, asset ? asset->chunk : 0);
    return ReplaceEditedMovie(player, file, error);
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
        MMI32 source;
        if (!mm_movie_source_ids_take(&player->sourceIds, player->retailSource, &source))
        { error.message = "No background source IDs are available."; return false; }
        const auto resource = MarcusBoardContent(board, source, player->timingResources, player->timingContent);
        const auto scene = newScene ? static_cast<unsigned long>(player->nextScene++) : player->scenes[player->selected].number;
        const auto selected = newScene ? static_cast<int>(player->scenes.size()) : player->selected;
        const auto frame = newScene ? 1 : player->preview.Info().currentFrame;
        auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
        auto boards = std::make_shared<MarcusStoryboards>(*edits->storyboards);
        (*boards)[scene] = resource; edits->storyboards = boards;
        auto scenes = std::make_shared<MarcusLiveEdits::Scenes>(*edits->scenes);
        const MMPreviewLiveScene definition{static_cast<MMU32>(scene), static_cast<MMU32>(resource.background),
                                            static_cast<MMI32>(resource.source)};
        if (newScene) scenes->Append(definition);
        else mm_movie_scene_definitions_retarget(scenes.get(), &definition);
        edits->scenes = scenes;
        auto cameras = std::make_shared<MarcusLiveEdits::Cameras>(*edits->cameras);
        const auto first = newScene ? 1 : player->preview.Info().firstFrame;
        cameras->Set({static_cast<MMU32>(scene), mm_scene_camera_key(first, static_cast<MMI32>(frame)),
                      static_cast<MMU32>(camera)});
        edits->cameras = cameras; edits->revision = player->nextEditRevision++;
        return ApplySceneEdit(*player, std::move(edits), selected, static_cast<MMI32>(frame), true, true, error);
    });
}
namespace
{
bool PrepareBoardPaint(MarcusPlayer *player, const unsigned *targets, size_t count)
{
    MarcusBoardPalette palette;
    if (!MarcusGetStoryboardPalette(player, palette)) return false;
    const bool reuse = player->paintReady && palette == player->paintPalette;
    player->paintInks.resize(count);
    for (size_t i = 0; i < count; ++i)
        if (!reuse || i >= player->paintTargets.size() || targets[i] != player->paintTargets[i])
            player->paintInks[i] = MarcusBoardMatchColour(palette, targets[i]);
    player->paintPalette = palette; player->paintTargets.assign(targets, targets + count); player->paintReady = true;
    return true;
}
}
bool MarcusRenderStoryboard(MarcusPlayer *player, const MarcusStoryboard &board, unsigned long camera, MarcusImage &image,
    std::vector<float> *depth, const MarcusBoardAngle *inspection)
{
    MarcusBoardWire wire{};
    if (!MarcusBoardView(board, wire)) return false;
    const auto pose = inspection ? MarcusBoardPoseOf(*inspection) : MarcusBoardPose{};
    return MarcusRenderStoryboardView(player, wire, camera, image, depth, inspection ? &pose : nullptr);
}
bool MarcusRenderStoryboardView(MarcusPlayer *player, const MarcusBoardWire &board, unsigned long camera, MarcusImage &image,
    std::vector<float> *depth, const MarcusBoardPose *inspection)
{
    return Guard(player, [&](MM::Error &error) {
        const auto &values = board.values;
        if (player->selected < 0 || !marcus_board_wire_size(&board) || camera >= static_cast<unsigned>(values.angle_count)) return false;
        const auto &a = inspection ? *inspection : values.angles[camera];
        if (!PrepareBoardPaint(player, values.colours, values.colour_count)) return false;
        MM::PreviewView view;
        view.width = MarcusBoardWidth; view.height = MarcusBoardHeight;
        view.authoredCamera = false; view.freeCamera = true; view.includeDepth = true;
        view.cameraX = a.camera.x; view.cameraY = a.camera.y; view.cameraZ = a.camera.z;
        view.yaw = a.camera.yaw; view.pitch = a.camera.pitch; view.roll = a.roll; view.verticalFov = a.fov; view.nearClip = a.near_clip;
        std::vector<MM::PreviewLight> lights;
        for (int i = 0; i < values.light_count; ++i)
        {
            const auto &l = values.lights[i];
            lights.push_back({l.x, l.y, l.z, l.yaw, l.pitch, l.intensity, l.point != 0});
        }
        if (!player->preview.RenderWithLights(view, lights, player->thumbnail, error)) return false;
        auto &rendered = player->thumbnail;
        const auto pixels = board.pixels[camera];
        for (size_t i = 0; i < MARCUS_BOARD_PIXELS; ++i)
            if (rendered.depth.empty() || !std::isfinite(rendered.depth[i]) || rendered.depth[i] > a.far_clip)
            {
                const auto rgb = player->paintPalette[MarcusBoardPaintIndex(player->paintInks[pixels[i]], int(i % MarcusBoardWidth), int(i / MarcusBoardWidth))];
                rendered.rgba[i * 4] = static_cast<unsigned char>(rgb >> 16);
                rendered.rgba[i * 4 + 1] = static_cast<unsigned char>(rgb >> 8);
                rendered.rgba[i * 4 + 2] = static_cast<unsigned char>(rgb); rendered.rgba[i * 4 + 3] = 255;
            }
        image = {}; image.width = rendered.width; image.height = rendered.height;
        image.pitch = image.width * 4; image.rgba = rendered.rgba.data();
        image.projection = CopyProjection(rendered.projection);
        if (depth) *depth = rendered.depth;
        return true;
    });
}
bool MarcusGetStoryboardColours(MarcusPlayer *player, MarcusBoardPaintColours &colours)
{
    MarcusStoryboard board;
    if (!MarcusGetStoryboard(player, board) || !PrepareBoardPaint(player, board.colours.data(), board.colours.size())) return false;
    colours.resize(board.colours.size());
    for (unsigned i = 0; i < colours.size(); ++i) colours[i] = player->paintInks[i].rgb;
    return true;
}
bool MarcusGetStoryboardPalette(MarcusPlayer *player, MarcusBoardPalette &palette)
{
    if (!player || player->selected < 0) return false;
    for (unsigned i = 0; i < palette.size(); ++i) palette[i] = player->preview.PresentationColour(0xfe000000 | i) & 0xffffff;
    return true;
}
bool MarcusRenderLocalWorld(MarcusPlayer *player, const MM::PreviewView &view,
                            MM::PreviewImage &image, MM::Error &error)
{
    return player && player->preview.Render(view, image, error);
}
static MM::PreviewTextMaterial NativeMaterial(const MarcusTextMaterial &m)
{
    MM::PreviewTextMaterial out;
    out.textured = m.textured != 0; out.colour = m.colour; out.indexBase = m.index_base; out.indexRange = m.index_range;
    out.ambient = m.ambient; out.diffuse = m.diffuse; out.specular = m.specular; out.shininess = m.shininess;
    std::copy(m.mapping, m.mapping + 6, out.mapping); return out;
}
static MarcusTextMaterial PublicMaterial(const MM::PreviewTextMaterial &m)
{
    MarcusTextMaterial out = {};
    out.enabled = 1; out.textured = m.textured; out.colour = m.colour; out.index_base = m.indexBase; out.index_range = m.indexRange;
    out.ambient = m.ambient; out.diffuse = m.diffuse; out.specular = m.specular; out.shininess = m.shininess;
    std::copy(m.mapping, m.mapping + 6, out.mapping); return out;
}
static bool ValidMaterial(const MarcusTextMaterial &m, MM::Error &error)
{
    if (!m.enabled) return true;
    auto range = [](float n, float low, float high) { return std::isfinite(n) && n >= low && n <= high; };
    if (!range(m.ambient, 0, 1) || !range(m.diffuse, 0, 1) || !range(m.specular, 0, 1) || !range(m.shininess, 0, 32700) ||
        m.colour > 0xffffff || m.index_base > 255 || m.index_range > 255 || m.index_base + m.index_range > 255 ||
        std::any_of(m.mapping, m.mapping + 6, [&](float n) { return !range(n, -32700, 32700); }))
    { error.message = "Material lighting or texture placement is out of range."; return false; }
    return true;
}
static bool SameMaterial(const MarcusTextMaterial &a, const MM::PreviewTextMaterial &b)
{
    if (!a.enabled) return true;
    const auto close = [](float x, float y) { return std::abs(x - y) < .00002f; };
    return !a.preset && a.colour == b.colour && a.index_base == b.indexBase && a.index_range == b.indexRange &&
        close(a.ambient, b.ambient) && close(a.diffuse, b.diffuse) && close(a.specular, b.specular) && close(a.shininess, b.shininess) &&
        std::equal(a.mapping, a.mapping + 6, b.mapping, close);
}
static bool WriteMaterial(MarcusPlayer *p, int object, const MarcusTextMaterial &m, const MarcusMovieFile &file, MM::Error &error)
{
    if (!ValidMaterial(m, error)) return false;
    return p->preview.WriteTextMaterial(object, m.preset != 0, m.source == -1 ? p->retailSource : m.source,
        static_cast<std::uint32_t>(m.chunk), NativeMaterial(m), file.path.u8string(), error);
}
std::shared_ptr<MarcusMovieFile> MarcusPrepareTextPreview(MarcusPlayer *player, int object, const MarcusText &text)
{
    std::shared_ptr<MarcusMovieFile> result;
    Guard(player, [&](MM::Error &error) {
        if (!EnsureRetailContent(*player, error)) return false;
        const auto font = text.font_source == -1 ? player->retailSource : text.font_source;
        auto file = MarcusCreateTextTemplate(text.text, text.font, font, text.shape);
        auto material = text.appearance;
        if (!material.enabled)
        {
            MM::PreviewTextMaterial current;
            if (!player->preview.TextMaterial(object, false, 0, 0, current, error)) return false;
            material = PublicMaterial(current);
        }
        if (!WriteMaterial(player, object, material, *file, error) ||
            !player->preview.PrepareTextPreview(file->path.u8string(), font, text.font, error)) return false;
        result = file; return true;
    });
    return result;
}
static bool InsertObject(MarcusPlayer *player, MarcusNamedProp prop, int &result, MM::Error &error, const MarcusTextEdit *initial = nullptr)
{
    if (player->selected < 0) { error.message = "Create or open a scene before inserting an object."; return false; }
    if (!EnsureRetailContent(*player, error)) return false;
    if (player->nextObject > 0xffffffffULL || player->nextActorId > std::numeric_limits<std::int32_t>::max())
    { error.message = "No object IDs are available."; return false; }
    auto &definition = prop.definition;
    if (!player->preview.NextPlacementPoint(definition.point)) { error.message = "This scene has no background placement point."; return false; }
    struct PlacementGuard
    {
        MM::Preview &preview;
        bool committed = false;
        ~PlacementGuard() { if (!committed) preview.ReusePlacementPoint(); }
    } placement{player->preview};
    for (const auto value : definition.point)
        if (!std::isfinite(value) || std::abs(value) > 32700) { error.message = "The placement point is out of range."; return false; }
    definition.scene = player->scenes[player->selected].number;
    definition.number = static_cast<std::uint32_t>(player->nextObject++);
    definition.actorId = static_cast<std::int32_t>(player->nextActorId++);
    definition.first = player->preview.Info().currentFrame;
    definition.last = player->preview.Info().lastFrame;
    auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
    auto props = std::make_shared<MarcusLiveEdits::Props>(*edits->props);
    props->push_back(prop); edits->props = props; edits->revision = player->nextEditRevision++;
    if (initial)
    {
        auto texts = std::make_shared<MarcusLiveEdits::Texts>(*edits->texts);
        (*texts)[{definition.scene, definition.number}] = *initial; edits->texts = texts;
    }
    EditHistory history(*player);
    const auto previous = player->edits;
    player->edits = edits;
    if (!ApplyDefinitions(*player, error) || (initial && !ApplyPlacements(*player, player->selected, *player->placements, error))) { player->edits = previous; return false; }
    player->objects = player->preview.Objects();
    player->animations.clear();
    for (std::size_t index = 0; index < player->objects.size(); ++index)
        if (player->objects[index].actorId == definition.actorId) result = static_cast<int>(index);
    if (result < 0) { error.message = "The new object could not be selected."; return false; }
    ++player->document; player->movieImageScene = -1;
    history.Commit();
    placement.committed = true;
    return true;
}
static int ReadCatalogue(MarcusPlayer *player, const char *directory, int kind)
{
    const bool ok = Guard(player, [&](MM::Error &error) {
        if (!directory) return false;
        MM::Archive archive;
        if (!archive.Open((std::filesystem::u8path(directory) / (kind == 3 ? "mtrls.3cn" : kind == 2 ? "tdfs.3cn" : "tmpls.3cn")).u8string(), error)) return false;
        std::vector<MarcusProp> entries;
        for (std::size_t index = 0; index < archive.Chunks().size(); ++index)
        {
            const auto &chunk = archive.Chunks()[index];
            if (chunk.id.type != (kind == 3 ? 0x4d54524cU : kind == 2 ? 0x54444620U : 0x544d504cU)) continue;
            if (kind < 2)
            {
                MM::PayloadPage header;
                if (!archive.ReadPayload(index, 0, 16, header, error)) return false;
                if (header.bytes.size() != 16 || !((header.bytes[0] == 1 && header.bytes[1] == 0) ||
                    (header.bytes[0] == 0 && header.bytes[1] == 1))) continue;
                const auto flags = Word(std::vector<std::uint8_t>(header.bytes.begin() + 12, header.bytes.end()), header.bytes[0] == 0);
                if ((flags & 2) || bool(flags & 4) != (kind == 0)) continue;
            }
            MarcusProp entry = {};
            entry.chunk = chunk.id.number;
            Copy(entry.name, chunk.name.empty() ? std::string(kind == 0 ? "Prop " : kind == 1 ? "Actor " : kind == 2 ? "3D font " : "Material ") + std::to_string(entry.chunk) : chunk.name);
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return std::strcmp(a.name, b.name) < 0; });
        (kind == 0 ? player->props : kind == 1 ? player->actors : kind == 2 ? player->fonts : player->materials).swap(entries);
        (kind == 0 ? player->propCatalogDirectory : kind == 1 ? player->actorCatalogDirectory : kind == 2 ? player->fontCatalogDirectory : player->materialCatalogDirectory) = directory;
        return true;
    });
    return ok ? static_cast<int>((kind == 0 ? player->props : kind == 1 ? player->actors : kind == 2 ? player->fonts : player->materials).size()) : -1;
}
static int InsertTemplate(MarcusPlayer *player, unsigned long chunk, bool actor)
{
    int result = -1;
    const bool ok = Guard(player, [&](MM::Error &error) {
        auto &entries = actor ? player->actors : player->props;
        const auto &directory = actor ? player->actorCatalogDirectory : player->propCatalogDirectory;
        if ((entries.empty() || directory != player->timingContent) && ReadCatalogue(player, player->timingContent.c_str(), actor ? 1 : 0) < 0)
        { error.message = player->error; return false; }
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.chunk == chunk; });
        if (found == entries.end()) { error.message = "The selected object is unavailable."; return false; }
        MarcusNamedProp prop;
        prop.definition.source = player->retailSource;
        prop.definition.templateNumber = static_cast<std::uint32_t>(chunk);
        prop.name = found->name;
        return InsertObject(player, std::move(prop), result, error);
    });
    return ok ? result : -1;
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
    int marcus_board_copy_camera(MarcusPlayer *player, const MarcusImage *work)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!marcus_is_storyboard(player))
            { error.message = "Copy to camera requires a storyboard scene."; return false; }
            const auto &info = player->preview.Info();
            MarcusBoardPose pose;
            if (!work || work->scene != player->selected || work->frame != info.currentFrame ||
                work->document != player->document || !marcus_board_pose_from_view(work, &pose))
            { error.message = "Use a current perspective work view."; return false; }
            MarcusStoryboard board;
            if (!MarcusGetStoryboard(player, board) || info.currentCamera >= board.angles.size())
            { error.message = "The active storyboard angle is unavailable."; return false; }
            const auto camera = info.currentCamera;
            auto &angle = board.angles[camera];
            pose.camera.move_speed = angle.camera.move_speed;
            angle.camera = pose.camera; angle.roll = pose.roll; angle.fov = pose.fov;
            angle.nearClip = pose.near_clip; angle.farClip = pose.far_clip;
            if (MarcusApplyStoryboard(player, board, false, camera, player->timingResources.c_str(),
                                      player->timingContent.c_str())) return true;
            error.message = player->error;
            return false;
        });
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
            const auto extension = destination.extension().u8string();
            MMMovieCopyFormat format;
            const char *appendSuffix;
            auto decision = mm_movie_copy_format(extension.c_str(), 0, &format, &appendSuffix);
            if (decision == MM_MOVIE_COPY_BAD_EXTENSION)
            { error.message = "Save the movie copy with a .3mm or .vmm extension."; return false; }
            if (format == MM_MOVIE_COPY_NATIVE && marcus_requires_vmm(player))
                decision = mm_movie_copy_format(extension.c_str(), 1, &format, &appendSuffix);
            if (decision == MM_MOVIE_COPY_BUNDLE_REQUIRED)
            { error.message = "This movie includes storyboard or V3DMM content. Save it with a .vmm filename."; return false; }
            if (appendSuffix) destination += appendSuffix;
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
            MM::V3dmm::SaveAtomically(destination, [&](const std::filesystem::path &temporary) {
                if (format == MM_MOVIE_COPY_NATIVE)
                {
                    std::filesystem::copy_file(file->path, temporary);
                    return;
                }
                const auto dependencies = player->content ? player->content->Dependencies()
                    : std::vector<MM::V3dmm::Dependency>{{mm_vmm_library_name(MM_VMM_BASE_LIBRARY), {}}};
                if (player->edits->storyboards->empty()) MM::V3dmm::WriteBundle(MM::V3dmm::WholeFile(
                    player->content && player->content->bundled && file == player->timingInput ? player->content->movie : file->path), dependencies, temporary);
                else MarcusWriteStoryboardBundle(*file, *player->edits->storyboards, player->timingResources, player->timingContent,
                    dependencies, player->content ? player->content->files.Files("v3dmm", ".3cn") : std::vector<std::filesystem::path>{}, temporary);
            });
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
    int marcus_can_undo(const MarcusPlayer *player) { return player && player->undo.stack.count != 0; }
    int marcus_modified(const MarcusPlayer *player) { return player && player->modified; }
    int marcus_undo(MarcusPlayer *player)
    {
        return Guard(player, [&](MM::Error &error) {
            const auto prior = CurrentSnapshot(*player);
            SnapshotHost host{*player, error, {}};
            const auto services = host.Services();
            const int restored = mm_movie_snapshot_undo(&player->undo.stack, &prior,
                                                        MarcusPlayer::UndoHistory::Release, &services);
            if (host.failure)
                std::rethrow_exception(host.failure);
            return restored != 0;
        });
    }
    int marcus_material_count(MarcusPlayer *player, const char *directory) { return ReadCatalogue(player, directory, 3); }
    int marcus_material(const MarcusPlayer *player, int index, MarcusProp *material)
    {
        if (!player || !material || index < 0 || std::size_t(index) >= player->materials.size()) return 0;
        *material = player->materials[index]; return 1;
    }
    int marcus_text_material_preset(MarcusPlayer *player, unsigned long chunk, MarcusTextMaterial *material)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewTextMaterial value;
            if (!material || !EnsureRetailContent(*player, error) || !player->preview.TextMaterial(-1, true, player->retailSource, chunk, value, error)) return false;
            *material = PublicMaterial(value); material->preset = 1; material->source = -1; material->chunk = chunk; return true;
        });
    }
    int marcus_prop_count(MarcusPlayer *player, const char *directory) { return ReadCatalogue(player, directory, 0); }
    int marcus_actor_count(MarcusPlayer *player, const char *directory) { return ReadCatalogue(player, directory, 1); }
    int marcus_font_count(MarcusPlayer *player, const char *directory) { return ReadCatalogue(player, directory, 2); }
    int marcus_prop(const MarcusPlayer *player, int index, MarcusProp *prop)
    {
        if (!player || !prop || index < 0 || static_cast<std::size_t>(index) >= player->props.size()) return 0;
        *prop = player->props[index]; return 1;
    }
    int marcus_actor(const MarcusPlayer *player, int index, MarcusProp *actor)
    {
        if (!player || !actor || index < 0 || static_cast<std::size_t>(index) >= player->actors.size()) return 0;
        *actor = player->actors[index]; return 1;
    }
    int marcus_font(const MarcusPlayer *player, int index, MarcusProp *font)
    {
        if (!player || !font || index < 0 || static_cast<std::size_t>(index) >= player->fonts.size()) return 0;
        *font = player->fonts[index]; return 1;
    }
    int marcus_add_prop(MarcusPlayer *player, unsigned long chunk) { return InsertTemplate(player, chunk, false); }
    int marcus_add_actor(MarcusPlayer *player, unsigned long chunk) { return InsertTemplate(player, chunk, true); }
    MarcusObjectClipboard *marcus_copy_object(MarcusPlayer *player, int object)
    {
        std::unique_ptr<MarcusObjectClipboard> copy;
        Guard(player, [&](MM::Error &error) {
            MarcusObject selected;
            if (player->selected < 0 || !marcus_object(player, object, &selected))
            { error.message = "Select an object to copy."; return false; }
            const auto file = MaterializedMovie(*player);
            const auto &path = file == player->timingInput ? player->content->movie : file->path;
            copy = MarcusCopyObject(path, player->scenes[player->selected].number, player->preview.Info().currentFrame, selected);
            return bool(copy);
        });
        return copy.release();
    }
    void marcus_object_clipboard_destroy(MarcusObjectClipboard *clipboard) { delete clipboard; }
    int marcus_paste_object(MarcusPlayer *player, const MarcusObjectClipboard *clipboard)
    {
        int object = -1;
        Guard(player, [&](MM::Error &error) {
            if (!clipboard || player->selected < 0)
            { error.message = "Copy an object and open a destination scene first."; return false; }
            if (player->nextActorId > std::numeric_limits<std::int32_t>::max())
            { error.message = "No object IDs are available."; return false; }
            if (!CancelPropPreviews(player, error)) return false;
            const auto movie = MaterializedMovie(*player);
            const auto &path = movie == player->timingInput ? player->content->movie : movie->path;
            const auto id = static_cast<std::int32_t>(player->nextActorId);
            const auto file = MarcusPasteObject(path, movie, *clipboard, player->scenes[player->selected].number,
                player->preview.Info().currentFrame, id);
            return ReplaceEditedMovie(player, file, error, id, &object);
        });
        return object;
    }
    int marcus_copy_to_camera(MarcusPlayer *player, const MarcusImage *work)
    {
        return Guard(player, [&](MM::Error &error) {
            const auto &info = player->preview.Info();
            if (player->selected < 0 || player->objects.empty())
            { error.message = "This scene has no objects to transform."; return false; }
            if (!work || work->scene != player->selected || work->frame != info.currentFrame ||
                work->document != player->document || work->authored_camera || !work->projection.valid || work->projection.orthographic)
            { error.message = "Use the current perspective work view."; return false; }
            MarcusProjection camera;
            if (!marcus_camera_projection(player, info.currentCamera, &camera) || !camera.valid || camera.orthographic)
            { error.message = "This scene does not have a perspective movie camera."; return false; }
            if (!CancelPropPreviews(player, error)) return false;
            std::map<std::int32_t, MM::PreviewTrackPose> poses;
            for (std::size_t i = 0; i < player->objects.size(); ++i)
            {
                MM::PreviewTrackPose pose;
                if (!player->objects[i].visible) continue;
                if (!player->preview.ObjectTrackPose(static_cast<int>(i), pose))
                { error.message = "Cannot read a visible object's track at the current frame."; return false; }
                poses.emplace(player->objects[i].actorId, pose);
            }
            const auto movie = MaterializedMovie(*player);
            const auto &path = movie == player->timingInput ? player->content->movie : movie->path;
            const auto file = MarcusReframeScene(path, movie, player->scenes[player->selected].number, info.currentFrame,
                work->projection, camera, poses);
            return ReplaceEditedMovie(player, file, error);
        });
    }
    MarcusCaptionList *marcus_captions(MarcusPlayer *player)
    {
        std::unique_ptr<MarcusCaptionList> result;
        Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || std::size_t(player->selected) >= player->scenes.size())
            { error.message = "Open a scene before editing 2D text."; return false; }
            const auto &info = player->preview.Info();
            if (std::int64_t(info.lastFrame) - info.firstFrame >= std::numeric_limits<int>::max())
            { error.message = "The scene has too many frames for the 2D text editor."; return false; }
            if (!CancelPropPreviews(player, error)) return false;
            const auto movie = MaterializedMovie(*player);
            const auto &path = movie == player->timingInput ? player->content->movie : movie->path;
            auto list = std::make_unique<MarcusCaptionList>();
            list->document = player->document;
            list->scene = player->scenes[player->selected].number;
            list->firstFrame = info.firstFrame;
            list->lastFrame = info.lastFrame;
            auto &value = list->defaults;
            value.x = 32;
            value.y = MM::PreviewCaptionHeight - 88;
            value.width = MM::PreviewCaptionWidth - 2 * value.x;
            value.height = 56;
            value.first_frame = info.currentFrame - info.firstFrame + 1;
            value.last_frame = info.lastFrame - info.firstFrame + 1;
            value.through_end = value.transparent = 1;
            value.font_size = 20;
            value.alignment = 2;
            value.ink = 0xffffff;
            Copy(value.font, "Arial");
            MarcusReadCaptions(path, player->preview, *list);
            result = std::move(list);
            return true;
        });
        return result.release();
    }
    void marcus_captions_destroy(MarcusCaptionList *list) { delete list; }
    int marcus_caption_count(const MarcusCaptionList *list) { return list ? static_cast<int>(list->entries.size()) : 0; }
    const char *marcus_caption(const MarcusCaptionList *list, int index, MarcusCaptionOptions *options)
    {
        if (!list || index < -1 || index >= marcus_caption_count(list)) return nullptr;
        if (options) *options = index < 0 ? list->defaults : list->entries[index].options;
        return index < 0 ? "" : list->entries[index].text.c_str();
    }
    static bool CurrentCaptions(const MarcusPlayer *player, const MarcusCaptionList *list)
    {
        return player && list && list->document == player->document && player->selected >= 0 &&
            std::size_t(player->selected) < player->scenes.size() && list->scene == player->scenes[player->selected].number;
    }
    int marcus_caption_font_count(MarcusPlayer *player, MarcusCaptionList *list)
    {
        if (!Guard(player, [&](MM::Error &) {
            if (!CurrentCaptions(player, list)) return false;
            list->fontNames = MarcusCaptionFontNames(player->timingInput->fonts.get());
            return true;
        })) return 0;
        return static_cast<int>(list->fontNames.size());
    }
    const char *marcus_caption_font_name(const MarcusCaptionList *list, int index)
    {
        return list && index >= 0 && std::size_t(index) < list->fontNames.size() ? list->fontNames[index].c_str() : nullptr;
    }
    int marcus_caption_font_sample(MarcusPlayer *player, const MarcusCaptionList *list, int index, MarcusImage *image)
    {
        if (!image) return 0;
        *image = {};
        return Guard(player, [&](MM::Error &) {
            if (!CurrentCaptions(player, list) || !marcus_caption_font_name(list, index))
                return false;
            auto &sample = player->captionFontSample;
            if (!player->playback.CaptionFontSample(list->fontNames[index], sample))
                return false;
            image->width = sample.width;
            image->height = sample.height;
            image->pitch = sample.width * 4;
            image->rgba = sample.rgba.data();
            return true;
        });
    }
    void marcus_caption_preview(MarcusPlayer *player, const MarcusCaptionList *list, int index)
    {
        if (!player) return;
        player->playback.ClearCaptionDraft();
        player->captionPreview.reset();
        if (CurrentCaptions(player, list) && index >= 0 && index < marcus_caption_count(list))
            player->captionPreview = MarcusPlayer::CaptionPreview{player->document, player->selected, list->entries[index].chunk};
    }
    int marcus_caption_draft(MarcusPlayer *player, const MarcusCaptionList *list, int index,
                             const MarcusCaptionOptions *options, const char *text)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!CurrentCaptions(player, list) || index < -1 || index >= marcus_caption_count(list) ||
                !options || !std::memchr(options->font, 0, sizeof(options->font)) || !text)
            {
                error.message = "Choose a valid 2D text box.";
                return false;
            }
            const auto movie = MaterializedMovie(*player);
            const auto &path = movie == player->timingInput ? player->content->movie : movie->path;
            if (!player->playback.CaptionDraft(MarcusDraftCaption(path, *list, index, *options, text), error))
                return false;
            MarcusPlayer::CaptionPreview preview{player->document, player->selected, {}};
            if (index >= 0)
                preview.chunk = list->entries[index].chunk;
            player->captionPreview = preview;
            return true;
        });
    }
    const MarcusCaptionStop *marcus_caption_positions(const MarcusPlayer *player, int *count, int *scroll)
    {
        *count = *scroll = 0;
        if (!player)
            return nullptr;
        const auto &stops = player->playback.CaptionPositions(*scroll);
        *count = static_cast<int>(stops.size());
        return stops.data();
    }
    int marcus_caption_caret(MarcusPlayer *player, int offset)
    {
        return player && player->playback.CaptionCaret(offset);
    }
    int marcus_caption_at(const MarcusPlayer *player, const MarcusCaptionList *list, int x, int y)
    {
        if (!CurrentCaptions(player, list)) return -1;
        std::uint32_t chunk;
        const auto &caption = player->captionPreview;
        const auto hide = caption && caption->document == player->document && caption->scene == player->selected &&
            caption->chunk ? &*caption->chunk : nullptr;
        if (player->playback.CaptionAt(x, y, chunk, hide))
            for (std::size_t i = 0; i < list->entries.size(); ++i)
                if (list->entries[i].chunk == chunk) return static_cast<int>(i);
        return -1;
    }
    int marcus_edit_caption(MarcusPlayer *player, const MarcusCaptionList *list, int index,
                            const MarcusCaptionOptions *options, const char *text)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!CurrentCaptions(player, list))
            { error.message = "Reopen the 2D text editor for the current scene."; return false; }
            if (index < -1 || index >= marcus_caption_count(list) ||
                (text ? !options || !std::memchr(options->font, 0, sizeof(options->font)) : index < 0))
            { error.message = "Choose a valid 2D text box."; return false; }
            if (!CancelPropPreviews(player, error)) return false;
            const auto movie = MaterializedMovie(*player);
            const auto &path = movie == player->timingInput ? player->content->movie : movie->path;
            const auto file = MarcusWriteCaption(path, movie, *list, index, options, text);
            return !file || ReplaceEditedMovie(player, file, error);
        });
    }
    int marcus_costume_count(MarcusPlayer *player, int object)
    {
        int count = -1;
        Guard(player, [&](MM::Error &error) {
            std::vector<MM::PreviewCostume> values;
            if (!player->preview.ObjectCostumes(object, values, error)) return false;
            count = int(values.size()); return true;
        });
        return count;
    }
    int marcus_costume(MarcusPlayer *player, int object, int index, MarcusCostume *costume)
    {
        return Guard(player, [&](MM::Error &error) {
            std::vector<MM::PreviewCostume> values;
            if (!costume || !player->preview.ObjectCostumes(object, values, error) || index < 0 || std::size_t(index) >= values.size()) return false;
            const auto &c = values[index]; *costume = {c.bodySet, c.costume, c.selected, c.accessory}; return true;
        });
    }
    static int CostumePreview(MarcusPlayer *player, int object, int bodySet, const MarcusCostume *choices, int count,
        float yaw, float pitch, float zoom, int width, int height, MarcusImage *image)
    {
        if (!image) return 0;
        *image = {};
        return Guard(player, [&](MM::Error &error) {
            if (count < 0 || count > 512 || (count && !choices) || width < 64 || height < 64 || width > 1024 || height > 1024) return false;
            std::vector<MM::PreviewCostume> values;
            for (int i = 0; i < count; ++i) values.push_back({choices[i].body_set, choices[i].costume});
            MM::PreviewView view;
            view.width = width; view.height = height; view.yaw = yaw; view.pitch = pitch; view.zoom = zoom;
            if (!player->preview.RenderObjectCostumes(object, values, view, player->costumeImage, error, bodySet)) return false;
            image->width = int(player->costumeImage.width); image->height = int(player->costumeImage.height);
            image->pitch = image->width * 4; image->rgba = player->costumeImage.rgba.data();
            image->projection = CopyProjection(player->costumeImage.projection);
            std::copy(player->costumeImage.contentRect, player->costumeImage.contentRect + 4, image->content_rect);
            image->scene = player->selected; image->frame = player->preview.Info().currentFrame; image->document = player->document;
            return true;
        });
    }
    int marcus_costume_preview(MarcusPlayer *player, int object, const MarcusCostume *choices, int count,
        float yaw, float pitch, float zoom, int width, int height, MarcusImage *image)
    { return CostumePreview(player, object, -1, choices, count, yaw, pitch, zoom, width, height, image); }
    int marcus_costume_part_preview(MarcusPlayer *player, int object, int bodySet, const MarcusCostume *choices, int count,
        float yaw, float pitch, float zoom, int width, int height, MarcusImage *image)
    { return CostumePreview(player, object, bodySet, choices, count, yaw, pitch, zoom, width, height, image); }
    int marcus_set_costumes(MarcusPlayer *player, int object, const MarcusCostume *choices, int count)
    {
        return Guard(player, [&](MM::Error &error) {
            if (count < 0 || count > 512 || (count && !choices) || player->selected < 0) return false;
            MarcusObject selected;
            if (!marcus_object(player, object, &selected) || selected.text || !selected.visible)
            { error.message = "Select a visible actor or prop."; return false; }
            std::vector<MM::PreviewCostume> available, changes;
            if (!player->preview.ObjectCostumes(object, available, error)) return false;
            std::set<int> sets;
            for (int i = 0; i < count; ++i)
            {
                const auto &c = choices[i];
                const auto found = std::find_if(available.begin(), available.end(), [&](const auto &v) { return v.bodySet == c.body_set && v.costume == c.costume; });
                if (found == available.end() || !sets.insert(c.body_set).second)
                { error.message = "Choose one available costume per body part."; return false; }
                if (!found->selected) changes.push_back(*found);
            }
            if (changes.empty()) return true;
            if (!CancelPropPreviews(player, error)) return false;
            auto file = MarcusCreateEventFile();
            if (!player->preview.WriteObjectCostumes(object, changes, file->path.u8string(), error)) return false;
            MMI32 source;
            if (!mm_movie_source_ids_take(&player->sourceIds, player->retailSource, &source))
            { error.message = "No costume source IDs are available."; return false; }
            auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
            auto costumes = std::make_shared<MarcusLiveEdits::Costumes>(*edits->costumes);
            (*costumes)[{player->scenes[player->selected].number, selected.chunk}] = {file, source};
            edits->costumes = costumes; edits->revision = player->nextEditRevision++;
            EditHistory history(*player); const auto previous = player->edits;
            const auto frame = player->preview.Info().currentFrame;
            player->edits = edits;
            if (!ApplyDefinitions(*player, error) || !ApplyPlacements(*player, player->selected, *player->placements, error))
            {
                player->edits = previous;
                MM::Error ignored; ApplyDefinitions(*player, ignored);
                if (marcus_select_scene_keep_view(player, player->selected)) marcus_seek(player, frame);
                return false;
            }
            player->animations.clear(); ++player->document; player->movieImageScene = -1;
            history.Commit(); return true;
        });
    }
    int marcus_object_action_count(MarcusPlayer *player, int object)
    {
        return Guard(player, [&](MM::Error &error) {
            return player->preview.ObjectActions(object, player->objectActions, error);
        }) ? static_cast<int>(player->objectActions.size()) : 0;
    }
    int marcus_object_action(const MarcusPlayer *player, int index, MarcusObjectAction *action)
    {
        if (!player || !action || index < 0 || std::size_t(index) >= player->objectActions.size()) return 0;
        const auto &entry = player->objectActions[index];
        Copy(action->name, entry.name);
        action->cels = static_cast<int>(entry.celCount);
        return 1;
    }
    int marcus_action_preview(MarcusPlayer *player, int object, int action, int cel, int clip,
        float yaw, float pitch, float zoom, int width, int height, MarcusImage *image)
    {
        if (!image) return 0;
        *image = {};
        return Guard(player, [&](MM::Error &error) {
            if (width < 64 || height < 64 || width > 1024 || height > 1024) return false;
            const auto frame = player->preview.Info().currentFrame;
            if (clip)
            {
                const auto key = std::make_tuple(player->document, player->selected, frame, object, action);
                if (player->actionPreviewKey != key) player->actionPreviewBounds = {};
                player->actionPreviewKey = key;
            }
            MM::PreviewView view;
            view.width = width; view.height = height; view.yaw = yaw; view.pitch = pitch; view.zoom = zoom;
            auto &rendered = player->costumeImage;
            if (!player->preview.RenderObjectAction(object, action, cel, view, rendered, error,
                clip ? &player->actionPreviewBounds : nullptr)) return false;
            image->width = int(rendered.width); image->height = int(rendered.height);
            image->pitch = image->width * 4; image->rgba = rendered.rgba.data();
            image->projection = CopyProjection(rendered.projection);
            image->scene = player->selected; image->frame = frame; image->document = player->document;
            return true;
        });
    }
    static bool EditAnimation(MarcusPlayer *player, int object, const MM::PreviewAnimationEdit &edit, MM::Error &error)
    {
        MarcusObject selected;
        if (player->selected < 0 || !marcus_object(player, object, &selected) || !selected.visible)
        {
            error.message = "Select an on-stage object.";
            return false;
        }
        if (!CancelPropPreviews(player, error)) return false;
        const auto track = MarcusCreateEventFile();
        int lastActorFrame, lastEditedFrame;
        if (!player->preview.WriteObjectAnimation(object, edit, track->path.u8string(),
                                                 lastActorFrame, lastEditedFrame, error)) return false;
        const auto movie = MaterializedMovie(*player);
        const auto &path = movie == player->timingInput ? player->content->movie : movie->path;
        const auto scene = player->scenes[player->selected].number;
        const auto key = std::make_pair(static_cast<unsigned long>(scene), selected.chunk);
        const auto shape = player->edits->shapes->find(key);
        const auto text = player->edits->texts->find(key);
        // The native draft contains live events. Replay appearance/shape
        // overlays that are only written as events when materializing a movie.
        const auto changed = MarcusApplyAnimation(path, movie, *track, scene, selected.actor_id, selected.last_frame, lastActorFrame,
            lastEditedFrame, shape == player->edits->shapes->end() ? MM::PreviewObjectShape{} : shape->second,
            text == player->edits->texts->end() ? nullptr : &text->second);
        return ReplaceEditedMovie(player, changed, error);
    }
    int marcus_set_object_action(MarcusPlayer *player, int object, int action, int cel, int freeze)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewAnimationEdit edit;
            edit.action = action;
            edit.cel = cel;
            edit.freeze = freeze != 0;
            return EditAnimation(player, object, edit, error);
        });
    }
    int marcus_tweak_object_path(MarcusPlayer *player, int object, float x, float y, float z)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewAnimationEdit edit;
            edit.kind = MM::PreviewAnimationEdit::Kind::tweak;
            edit.points.push_back({x, y, z});
            return EditAnimation(player, object, edit, error);
        });
    }
    int marcus_record_object_path(MarcusPlayer *player, int object, const MarcusPathPoint *points, int count, int replace)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!points || count <= 0 || count > MARCUS_PATH_MAX_POINTS) return false;
            MM::PreviewAnimationEdit edit;
            edit.kind = MM::PreviewAnimationEdit::Kind::record;
            edit.replace = replace != 0;
            edit.points.reserve(count);
            for (int i = 0; i < count; ++i)
                edit.points.push_back({points[i].x, points[i].y, points[i].z});
            return EditAnimation(player, object, edit, error);
        });
    }
    int marcus_add_text(MarcusPlayer *player, const char *text, unsigned long font, int shape)
    {
        if (!text || std::strlen(text) >= sizeof(MarcusText::text)) return -1;
        MarcusText options = {}; Copy(options.text, text); options.font = font; options.font_source = -1; options.shape = shape;
        return marcus_add_text_options(player, &options);
    }
    int marcus_add_text_options(MarcusPlayer *player, const MarcusText *options)
    {
        if (!options) return -1;
        const auto text = options->text; const auto font = options->font; const auto shape = options->shape;
        int result = -1;
        const bool ok = Guard(player, [&](MM::Error &error) {
            if (player->selected < 0) { error.message = "Create or open a scene before adding 3D text."; return false; }
            if (!EnsureRetailContent(*player, error)) return false;
            if ((player->fonts.empty() || player->fontCatalogDirectory != player->timingContent) &&
                ReadCatalogue(player, player->timingContent.c_str(), 2) < 0) { error.message = player->error; return false; }
            if (std::none_of(player->fonts.begin(), player->fonts.end(), [&](const auto &entry) { return entry.chunk == font; }))
            { error.message = "The selected 3D font is unavailable."; return false; }
            MarcusNamedProp prop;
            if (!mm_movie_source_ids_take(&player->sourceIds, player->retailSource, &prop.definition.source))
            { error.message = "No text source IDs are available."; return false; }
            prop.file = MarcusCreateTextTemplate(text, font, player->retailSource, shape);
            prop.name = text;
            prop.definition.templateNumber = 0;
            MarcusTextEdit initial;
            if (options->appearance.enabled)
            {
                if (!WriteMaterial(player, -1, options->appearance, *prop.file, error)) return false;
                initial.file = prop.file; initial.source = prop.definition.source; initial.hasMaterial = true; initial.retailFont = true;
                initial.value = {text, player->retailSource, shape, static_cast<std::uint32_t>(font)};
            }
            return InsertObject(player, std::move(prop), result, error, options->appearance.enabled ? &initial : nullptr);
        });
        return ok ? result : -1;
    }
    int marcus_text(MarcusPlayer *player, int object, MarcusText *text)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewObjectText value;
            if (!text || !player->preview.ObjectText(object, value, error)) return false;
            *text = {}; Copy(text->text, value.text);
            text->font = value.font; text->font_source = value.fontSource == player->retailSource ? -1 : value.fontSource;
            text->shape = value.shape;
            MM::PreviewTextMaterial material;
            if (!player->preview.TextMaterial(object, false, 0, 0, material, error)) return false;
            text->appearance = PublicMaterial(material); return true;
        });
    }
    int marcus_set_text(MarcusPlayer *player, int index, const MarcusText *text)
    {
        return Guard(player, [&](MM::Error &error) {
            MarcusObject object = {}; MM::PreviewObjectText before;
            if (!text || !std::memchr(text->text, 0, sizeof(text->text)) || !marcus_object(player, index, &object) ||
                !player->preview.ObjectText(index, before, error)) return false;
            MarcusTextAnsi(text->text);
            if (text->shape < 0 || text->shape >= 10) { error.message = "Choose a valid 3D text shape."; return false; }
            const auto fontSource = text->font_source == -1 ? player->retailSource : text->font_source;
            const bool sameFont = fontSource == before.fontSource && text->font == before.font;
            if (!sameFont)
            {
                if (fontSource != player->retailSource) { error.message = "Choose an original 3D font or keep this text's current font."; return false; }
                if (!EnsureRetailContent(*player, error)) return false;
                if ((player->fonts.empty() || player->fontCatalogDirectory != player->timingContent) &&
                    ReadCatalogue(player, player->timingContent.c_str(), 2) < 0) { error.message = player->error; return false; }
                if (std::none_of(player->fonts.begin(), player->fonts.end(), [&](const auto &entry) { return entry.chunk == text->font; }))
                { error.message = "The selected 3D font is unavailable."; return false; }
            }
            if (!ValidMaterial(text->appearance, error)) return false;
            MM::PreviewTextMaterial current;
            if (!player->preview.TextMaterial(index, false, 0, 0, current, error)) return false;
            const bool materialChanged = !SameMaterial(text->appearance, current);
            if (sameFont && before.text == text->text && before.shape == text->shape && !materialChanged) return true;
            if (!CancelPropPreviews(player, error)) return false;
            MarcusTextEdit change;
            if (!mm_movie_source_ids_take(&player->sourceIds, player->retailSource, &change.source))
            { error.message = "No text source IDs are available."; return false; }
            change.value = {text->text, fontSource, text->shape, static_cast<std::uint32_t>(text->font)};
            change.retailFont = fontSource == player->retailSource;
            change.file = MarcusCreateTextTemplate(text->text, text->font, fontSource, text->shape);
            const auto oldText = player->edits->texts->find({player->scenes[player->selected].number, object.chunk});
            change.hasMaterial = materialChanged || (oldText != player->edits->texts->end() && oldText->second.hasMaterial);
            if (change.hasMaterial)
            {
                const auto appearance = text->appearance.enabled ? text->appearance : PublicMaterial(current);
                if (!WriteMaterial(player, index, appearance, *change.file, error)) return false;
            }
            auto edits = std::make_shared<MarcusLiveEdits>(*player->edits);
            auto texts = std::make_shared<MarcusLiveEdits::Texts>(*edits->texts);
            (*texts)[{player->scenes[player->selected].number, object.chunk}] = change;
            edits->texts = texts; edits->revision = player->nextEditRevision++;
            EditHistory history(*player);
            const auto previous = player->edits;
            player->edits = edits;
            if (!ApplyDefinitions(*player, error) || !ApplyPlacements(*player, player->selected, *player->placements, error))
            {
                player->edits = previous;
                MM::Error ignored; ApplyDefinitions(*player, ignored);
                return false;
            }
            player->animations.clear(); ++player->document; player->movieImageScene = -1;
            history.Commit(); return true;
        });
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
            player->preview.ReusePlacementPoint();
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
            struct Host
            {
                MarcusPlayer &movie;
                MM::Error &error;
                std::shared_ptr<MarcusLiveEdits> edits;
                std::optional<EditHistory> history;
                std::exception_ptr failure;
            } host{*player, error, {}, {}, {}};
            const MMSceneCameraJournalServices services{
                &host,
                [](void *p, MMI32 *first, MMI32 *frame, MMU32 *count) {
                    const auto &info = static_cast<Host *>(p)->movie.preview.Info();
                    *first = info.firstFrame;
                    *frame = info.currentFrame;
                    *count = info.cameraCount;
                },
                [](void *p, MMI32 key, MMU32 camera) -> int {
                    auto &h = *static_cast<Host *>(p);
                    try
                    {
                        auto &movie = h.movie;
                        h.edits = std::make_shared<MarcusLiveEdits>(*movie.edits);
                        auto cameras = std::make_shared<MarcusLiveEdits::Cameras>(*h.edits->cameras);
                        cameras->Set({movie.scenes[movie.selected].number, key, camera});
                        h.edits->cameras = std::move(cameras);
                        h.history.emplace(movie);
                        return 1;
                    }
                    catch (...) { h.failure = std::current_exception(); return 0; }
                },
                [](void *p) -> int {
                    auto &h = *static_cast<Host *>(p);
                    try { return ApplyCameras(h.movie, h.movie.selected, *h.edits->cameras, h.error); }
                    catch (...) { h.failure = std::current_exception(); return 0; }
                },
                [](void *p) {
                    auto &h = *static_cast<Host *>(p);
                    auto &movie = h.movie;
                    h.edits->revision = movie.nextEditRevision++;
                    movie.edits = std::move(h.edits);
                    movie.movieImageScene = -1;
                    ++movie.document;
                    h.history->Commit();
                }};
            const int status = mm_scene_camera_journal(player->selected, camera, &services);
            if (host.failure)
                std::rethrow_exception(host.failure);
            if (status == MM_SCENE_CAMERA_INVALID)
                error.message = "Camera angle is out of range.";
            return status == 1;
        });
    }
    int marcus_add_frame(MarcusPlayer *player, int direction)
    {
        return Guard(player, [&](MM::Error &error) {
            RangeEditHost host{*player, error};
            auto services = host.Services();
            const int result = mm_movie_scene_extend(direction, &services);
            if (host.failure)
                std::rethrow_exception(host.failure);
            if (result == MM_MOVIE_RANGE_DIRECTION)
                error.message = "Choose the beginning or end of a scene to add a frame.";
            else if (result == MM_MOVIE_RANGE_BOUNDARY)
                error.message = "A frame can only be added at a valid scene boundary.";
            return result == 1;
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
                ~Restore() { MM::Error ignored; preview.SetCamera(camera, ignored, true); }
            } restore{player->preview, previous};
            if (!player->preview.SetCamera(camera, error, true)) return false;
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
            scenes->Append({static_cast<MMU32>(player->nextScene++), static_cast<MMU32>(background), player->retailSource});
            edits->scenes = scenes; edits->revision = player->nextEditRevision++;
            return ApplySceneEdit(*player, std::move(edits), static_cast<MMI32>(player->scenes.size()), 1, false, false,
                                  error);
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
            MM::MovieDirectory movie;
            if (!archive.ReadMovieDirectory(movie, error))
                return false;
            std::vector<MM::Id> scenes;
            std::vector<MarcusScene> sceneInfo;
            std::map<std::int32_t, std::string> names;
            auto sources = std::move(movie.sources);
            const auto &chunks = archive.Chunks();
            const auto addScene = [&](const MM::Chunk &chunk, const MM::SceneHeader &header) {
                MarcusScene scene = {};
                scene.chunk = chunk.id.number;
                scene.first_frame = header.firstFrame;
                scene.last_frame = header.lastFrame;
                scene.transition = header.transition;
                Copy(scene.name, chunk.name.empty() ? "Scene " + std::to_string(sceneInfo.size() + 1) : chunk.name);
                scenes.push_back(chunk.id);
                sceneInfo.push_back(scene);
            };
            if (movie.hasRoot)
            {
                for (const auto &scene : movie.scenes)
                {
                    const auto sceneIndex = Find(archive, scene.id);
                    if (sceneIndex == chunks.size())
                        return false;
                    addScene(chunks[sceneIndex], scene.header);
                }
                const auto rootIndex = Find(archive, movie.root);
                if (rootIndex == chunks.size())
                    return false;
                for (const auto &child : chunks[rootIndex].children)
                {
                    if (child.target.type != gstType || child.slot != 0)
                        continue;
                    std::vector<MM::StringTableEntry> entries;
                    bool big = false;
                    if (!Strings(archive, child.target, entries, big, error))
                        return false;
                    for (const auto &entry : entries)
                        if (entry.extra.size() == MM_MOVIE_ACTOR_BYTES)
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
                }
            }
            else
            {
                // Also accept headless scene fixtures, which use source 1.
                for (std::size_t i = 0; i < chunks.size(); ++i)
                    if (chunks[i].id.type == sceneType)
                    {
                        MM::RecordHeader record;
                        if (!archive.ReadRecord(i, record, error))
                            return false;
                        addScene(chunks[i], record.scene);
                    }
                sources.push_back({1, "3dmovie"});
            }
            if (scenes.empty())
            {
                error.message = "This document contains no movie scenes.";
                return false;
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
            MarcusSourceRegistry sourceIds;
            if (player->internalOpenDepth)
                mm_movie_source_ids_limit(&sourceIds, player->sourceIds.next);
            for (const auto &source : sources)
                if (mm_movie_source_ids_add(&sourceIds, source.id, nullptr) == MM_MOVIE_SOURCE_ID_MEMORY)
                    throw std::bad_alloc();
            MMI32 retailSource = 2;
            bool retailPrepared = false;
            for (const auto &source : sources)
                if (MM::Content::SourceKey(source.name) == "3dmovie") { retailSource = source.id; retailPrepared = true; break; }
            if (!retailPrepared && !mm_movie_source_ids_retail_alias(&sourceIds, &retailSource))
            { error.message = "No retail source ID is available."; return false; }
            for (const auto &source : sources)
                content->PrepareSource(source.name);
            auto loadedBoards = MarcusReadBoards(archive, materialized);
            for (auto &entry : loadedBoards)
            {
                MMI32 source;
                if (!mm_movie_source_ids_take(&sourceIds, retailSource, &source))
                { error.message = "No background source IDs are available."; return false; }
                // Rebuild from editable pixels, repairing old palettes. Never
                // treat an outer VMM file as a native chunky resource file.
                entry.second = MarcusBoardContent(*entry.second.board, source, resources, directory);
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
            mm_movie_source_ids_swap(&player->sourceIds, &sourceIds);
            player->asmBoardFile.reset();
            player->materializedEdits.reset();
            player->nextScene = nextScene; player->nextObject = nextObject; player->nextActorId = nextActor;
            player->resourcesReady = true; player->retailPrepared = retailPrepared; player->retailSource = retailSource;
            player->materializedFrameRanges.reset();
            player->materializedPlacements.reset();
            player->materializedFile.reset();
            player->animations.clear();
            player->names.clear();
            player->soundSheet.sounds.clear();
            player->soundSheet.soundTracks.clear();
            player->soundTimeline.Clear();
            player->waveforms.Clear();
            player->image = {};
            player->movieImage = {};
            player->movieImageScene = -1;
            if (!player->preserveEditFraming) player->inspectionMatched = false;
            for (const auto &source : sources)
                for (const auto &path : player->content->files.Files(source.name, ".3cn"))
                {
                    if (!player->preview.AddContent(source.id, path.u8string(), error))
                        return false;
                }
            player->scenes.swap(scenes);
            player->sceneInfo.swap(sceneInfo);
            player->baseSceneInfo = player->sceneInfo;
            player->names.swap(names);
            player->baseNames = player->names;
            for (const auto &entry : *player->edits->storyboards)
            {
                const auto &resource = entry.second;
                if (!MountGeneratedSource(*player, resource.source, resource.file, error)) return false;
            }
            unsigned flags = MM_MOVIE_SCENE_INITIAL;
            if (player->preserveEditFraming) flags |= MM_MOVIE_SCENE_KEEP_FRAMING;
            if (!SceneCursorHost{*player, error, {}}.Select(0, flags))
                return false;
            if (!player->internalOpenDepth)
            {
                mm_movie_snapshot_clear(&player->undo.stack, MarcusPlayer::UndoHistory::Release);
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
            const auto costumed = player->edits->costumes->lower_bound({player->scenes[index].number, 0});
            if (costumed != player->edits->costumes->end() && costumed->first.first == player->scenes[index].number) return true;
            if (static_cast<std::size_t>(index) >= player->baseSceneInfo.size()) return true;
            for (const auto &prop : *player->edits->props)
                if (prop.definition.scene == player->scenes[index].number && prop.definition.first <= player->sceneInfo[index].first_frame)
                    return true;
            MMU32 count;
            const auto cameras = mm_movie_camera_journal_find(player->edits->cameras.get(), player->scenes[index].number, &count);
            for (MMU32 i = 0; i < count; ++i)
                if (mm_scene_camera_frame(static_cast<MMI32>(player->sceneInfo[index].first_frame),
                                          cameras[i].frame) == player->sceneInfo[index].first_frame)
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
        info->sound_count = static_cast<int>(player->soundSheet.sounds.size());
        info->sound_track_count = static_cast<int>(player->soundSheet.soundTracks.size());
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
            player->soundSheet.sounds.clear();
            player->soundSheet.soundTracks.clear();
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
            const auto &scene = player->soundTimeline.Scene(player->selected);
            scene.MovieTime(player->preview.Info().currentFrame, player->playback.FrameSeconds(),
                            player->polledMovie.finished, *time);
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
            player->soundTimeline.Scene(index).ChapterTime(*time);
            return true;
        });
    }
    int marcus_seek_time(MarcusPlayer *player, double seconds)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || !std::isfinite(seconds) ||
                !ReadSoundTimeline(player, static_cast<int>(player->scenes.size()) - 1, error))
                return false;
            MMMovieSoundPosition position;
            if (!player->soundTimeline.Locate(seconds, position))
                return false;
            const auto index = static_cast<int>(position.scene);
            if ((index != player->selected && !SelectScene(player, index, true)) || !marcus_seek(player, position.frame))
            {
                error.message = player->error;
                return false;
            }
            player->polledMovie.finished = position.finished;
            return true;
        });
    }
    int marcus_sound_track(const MarcusPlayer *player, int index, MarcusSoundTrack *track)
    {
        if (!player || player->selected < 0 || !track || index < 0 ||
            static_cast<std::size_t>(index) >= player->soundSheet.soundTracks.size())
            return 0;
        *track = player->soundSheet.soundTracks[index];
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
            static_cast<std::size_t>(index) >= player->soundSheet.sounds.size())
            return 0;
        *sound = player->soundSheet.sounds[index];
        return 1;
    }
    int marcus_add_sound(MarcusPlayer *player, const char *path, const MarcusSoundOptions *options)
    {
        int result = -1;
        Guard(player, [&](MM::Error &error) {
            if (!path || !*path || !options || !SoundChange(player, -1, path, *options, false, error)) return false;
            for (int i = int(player->soundSheet.sounds.size()) - 1; i >= 0; --i)
                if (!player->soundSheet.sounds[i].actor_sound && player->soundSheet.sounds[i].frame == options->frame && player->soundSheet.sounds[i].style == options->style)
                { result = i; break; }
            return true;
        });
        return result;
    }
    int marcus_set_sound(MarcusPlayer *player, int index, const MarcusSoundOptions *options)
    {
        return Guard(player, [&](MM::Error &error) { return options && SoundChange(player, index, nullptr, *options, false, error); });
    }
    int marcus_sound_asset_count(MarcusPlayer *player)
    {
        return Guard(player, [&](MM::Error &) { ReadSoundAssets(player); return true; }) ? static_cast<int>(player->soundAssets.size()) : -1;
    }
    int marcus_sound_asset(const MarcusPlayer *player, int index, MarcusSoundAsset *out)
    {
        if (!player || !out || index < 0 || std::size_t(index) >= player->soundAssets.size()) return 0;
        *out = player->soundAssets[index]; return 1;
    }
    int marcus_add_sound_asset(MarcusPlayer *player, int index, const MarcusSoundOptions *options)
    {
        int result = -1;
        Guard(player, [&](MM::Error &error) {
            ReadSoundAssets(player);
            if (!options || index < 0 || std::size_t(index) >= player->soundAssets.size()) return false;
            if (!SoundChange(player, -1, nullptr, *options, false, error, &player->soundAssets[index])) return false;
            for (int i = int(player->soundSheet.sounds.size()) - 1; i >= 0; --i)
                if (!player->soundSheet.sounds[i].actor_sound && player->soundSheet.sounds[i].frame == options->frame && player->soundSheet.sounds[i].style == options->style)
                { result = i; break; }
            return true;
        });
        return result;
    }
    int marcus_audition_sound_asset(MarcusPlayer *player, int index, long volume)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewSoundCue cue;
            if (volume < 0 || volume > 65536 || !SoundAssetCue(player, index, cue, error)) return false;
            cue.volume = volume; return player->playback.Audition(player->preview, cue, error);
        });
    }
    int marcus_sound_asset_waveform(MarcusPlayer *player, int index, int count, MarcusWaveformColumn *columns, MarcusWaveformInfo *info)
    {
        return Guard(player, [&](MM::Error &error) {
            MM::PreviewSoundCue cue;
            if (!columns || !info || count < 1 || count > 4096 || !SoundAssetCue(player, index, cue, error)) return false;
            auto length = player->waveforms.Length(player->preview, cue);
            player->waveforms.Read(player->preview, cue, 0, length.seconds > 0 ? length.seconds / count : 1, count, columns, *info);
            return true;
        });
    }
    int marcus_remove_sound(MarcusPlayer *player, int index)
    {
        return Guard(player, [&](MM::Error &error) {
            if (index < 0 || std::size_t(index) >= player->soundSheet.soundCues.size()) return false;
            const auto &cue = player->soundSheet.soundCues[index];
            MarcusSoundOptions options = {cue.frame, cue.volume, cue.style, int(cue.loop), int(cue.queue)};
            return SoundChange(player, index, nullptr, options, true, error);
        });
    }
    int marcus_audition_sound(MarcusPlayer *player, int index, long volume)
    {
        return Guard(player, [&](MM::Error &error) {
            if (index < 0 || std::size_t(index) >= player->soundSheet.soundCues.size() || volume < 0 || volume > 65536) return false;
            auto cue = player->soundSheet.soundCues[index]; cue.volume = volume;
            return player->playback.Audition(player->preview, cue, error);
        });
    }
    int marcus_audition_sound_file(MarcusPlayer *player, const char *path, int style, long volume)
    {
        return Guard(player, [&](MM::Error &error) {
            if (!path || volume < 0 || volume > 65536) return false;
            auto file = MarcusPrepareSound(path, style);
            MMI32 source;
            if (!mm_movie_source_ids_take(&player->sourceIds, player->retailSource, &source))
            { error.message = "No sound source IDs are available."; return false; }
            if (!MountGeneratedSource(*player, source, file, error)) return false;
            MM::PreviewSoundCue cue; cue.source = source; cue.sound = {0x4d534e44, 0}; cue.volume = volume; cue.style = style;
            return player->playback.Audition(player->preview, cue, error);
        });
    }
    void marcus_stop_sound_preview(MarcusPlayer *player) { if (player) player->playback.Stop(); }
    int marcus_sound_preview_playing(const MarcusPlayer *player) { return player && player->playback.Auditioning(); }
    int marcus_sound_waveform(MarcusPlayer *player, int index, double first, double step, int count,
                              MarcusWaveformColumn *columns, MarcusWaveformInfo *info)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || index < 0 || static_cast<std::size_t>(index) >= player->soundSheet.soundCues.size() ||
                !columns || !info || count < 1 || count > 4096 || !std::isfinite(first) || first < 0 ||
                !std::isfinite(step) || step <= 0)
            {
                error.code = MM::ErrorCode::invalid_argument;
                error.message = "Waveforms require a sound cue, output columns and a finite clip-relative range.";
                return false;
            }
            player->waveforms.Read(player->preview, player->soundSheet.soundCues[index], first, step, count, columns, *info);
            return true;
        });
    }
    int marcus_sound_waveform_frames(MarcusPlayer *player, int index, double first, double step, int count,
                                     MarcusWaveformColumn *columns, MarcusWaveformInfo *info)
    {
        return Guard(player, [&](MM::Error &error) {
            if (player->selected < 0 || index < 0 || static_cast<std::size_t>(index) >= player->soundSheet.soundCues.size() ||
                !columns || !info || count < 1 || count > 4096 || !std::isfinite(first) ||
                !std::isfinite(step) || step <= 0)
            {
                error.message = "Timeline waveforms require a sound cue, output columns and a finite frame range.";
                return false;
            }
            const auto &scene = player->soundTimeline.Scene(player->selected);
            const auto span = scene.Sound(index);
            player->waveforms.ReadFrames(player->preview, scene, span, first, step, count, columns, *info);
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
            return SceneCursorHost{*player, error, {}}.Seek(frame);
        });
    }
    void marcus_stop(MarcusPlayer *player)
    {
        if (player)
        {
            player->playback.Stop();
            player->polledMovie.restore_audio = 1;
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
            player->soundSheet.sounds.clear();
            player->soundSheet.soundTracks.clear();
            return player->selected < 0 || ReadSoundSheet(player, player->selected, error);
        });
    }
    int marcus_update(MarcusPlayer *player, int playing, int loop, double elapsed, int click,
                      MarcusPlaybackState *state)
    {
        const int success = Guard(player, [&](MM::Error &error) {
            struct Host
            {
                MarcusPlayer &movie;
                MM::Error &error;
                MM::PreviewImage previous;
                unsigned char outgoingPalette[1024];
                bool hasPalette;
                std::exception_ptr failure;
            } context{*player, error, {}, {}, false, {}};
            const MMPolledMovieServices services{
                &context,
                [](void *p, int playing, double elapsed, int click, int *advance) -> int {
                    auto &h = *static_cast<Host *>(p);
                    try
                    {
                        bool next = false;
                        const bool ok = h.movie.playback.Update(h.movie.preview, playing != 0, elapsed, click != 0,
                                                                next, h.error);
                        *advance = next;
                        return ok;
                    }
                    catch (...) { h.failure = std::current_exception(); return 0; }
                },
                [](void *p, MMPlaybackPosition *out, MMI32 *firstFrame) {
                    auto &movie = static_cast<Host *>(p)->movie;
                    const auto &info = movie.preview.Info();
                    *out = {movie.selected, static_cast<MMI32>(movie.scenes.size()), info.currentFrame, info.lastFrame,
                            movie.playback.Transition()};
                    *firstFrame = info.firstFrame;
                },
                [](void *p, MMPolledMovieEffect effect, MMI32 value) -> int {
                    auto &h = *static_cast<Host *>(p);
                    auto &movie = h.movie;
                    try
                    {
                        switch (effect)
                        {
                        case MM_POLLED_MOVIE_CANCEL_PREVIEWS:
                            return CancelPropPreviews(&movie, h.error);
                        case MM_POLLED_MOVIE_RESTORE_AUDIO:
                            return movie.playback.RestoreAudio(movie.preview,
                                movie.soundTimeline.Resume(movie.selected, movie.preview.Info().currentFrame), h.error);
                        case MM_POLLED_MOVIE_SET_FRAME:
                            if (!movie.preview.SetFrame(value, h.error))
                                return 0;
                            movie.objects = movie.preview.Objects();
                            break;
                        case MM_POLLED_MOVIE_REWIND:
                            if (!marcus_seek(&movie, value))
                            {
                                h.error.message = movie.error;
                                return 0;
                            }
                            break;
                        case MM_POLLED_MOVIE_CAPTURE_OUTGOING:
                            // Transition frames come from the movie camera.
                            if (movie.movieImageScene == value)
                                h.previous = std::move(movie.movieImage);
                            movie.movieImageScene = -1;
                            h.hasPalette = !h.previous.rgba.empty() && movie.preview.CopyPresentationPalette(h.outgoingPalette);
                            break;
                        case MM_POLLED_MOVIE_SELECT_SCENE:
                            if (!SelectScene(&movie, value, true, true))
                            {
                                h.error.message = movie.error;
                                return 0;
                            }
                            break;
                        case MM_POLLED_MOVIE_START_TRANSITION:
                            movie.playback.BeginTransition(value, std::move(h.previous),
                                                           h.hasPalette ? h.outgoingPalette : nullptr, movie.preview);
                            break;
                        case MM_POLLED_MOVIE_STOP:
                            movie.playback.Stop();
                            break;
                        }
                        return 1;
                    }
                    catch (...)
                    {
                        if (!h.failure)
                            h.failure = std::current_exception();
                        return 0;
                    }
                }};
            MMPolledMovieResult result;
            const int status = mm_polled_movie_update(&player->polledMovie, &player->playback.Clock(), player->selected,
                                                      playing, loop, elapsed, click, &services, state ? &result : nullptr);
            if (context.failure)
                std::rethrow_exception(context.failure);
            if (status == MM_POLLED_MOVIE_INVALID)
                error.message = "Playback requires a loaded movie and a finite, nonnegative elapsed time.";
            if (status != 1)
                return false;
            *state = {result.playing, result.changed, result.scene_changed, result.wait, result.scrolling,
                      result.transition};
            return true;
        });
        if (!success && state)
            *state = {};
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
