/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_PREVIEW_ACTOR_H
#define MM_PREVIEW_ACTOR_H
#include <functional>
#include "mm/preview.h"
#include "soc_core.h"
#include "actor_sound.h"
#include "mm/preview_actor.h"

namespace MM
{
namespace Internal
{
// Engine adapter for the pure frame/pose evaluators. No Studio, World or audio.
class PreviewActor
{
    struct Host;
    struct TimelineHost;
    struct CostumeHost;
    struct AnimationHost;
    struct TrackDraft;
    MMPreviewActor core;
    ActorTrack track;
    PTemplate tmpl = nullptr;
    PMaterial_MTRL textMaterial = nullptr;
    std::function<PChunkyResourceFile(const TAGOnFile &)> resolve;
    bool ApplyEvent(MMActorState &value, const ActorTrack &events, LegacyLong index, bool metadataOnly,
                    ActorSoundList *motion = nullptr, std::vector<PreviewSoundCue> *cues = nullptr) const;
    std::string ActionName(LegacyLong action) const;
    ActorSoundList motionSounds;
    std::uint64_t *visibilityClock = nullptr;
    mutable std::uint64_t lastShown = 0;
    std::int32_t eventSource = 0;
    PreviewSoundCue SoundCue(const ActorEvent::Sound &sound) const;

  public:
    PreviewActor()
    {
        mm_preview_actor_init(&core);
    }
    ChunkNumber number = 0;
    PBody body = nullptr;
    std::vector<PreviewSoundCue> sounds;
    ~PreviewActor();
    bool Load(PChunkyResourceFile file, ChunkNumber number,
              std::function<PChunkyResourceFile(const TAGOnFile &)> resolver,
              std::function<void(PTemplate)> bindTemplate, std::uint64_t &visibilityClock);
    bool Create(const PreviewPropDefinition &, std::function<PChunkyResourceFile(const TAGOnFile &)> resolver,
                std::function<void(PTemplate)> bindTemplate, std::uint64_t &visibilityClock);
    bool Seek(LegacyLong frame);
    bool Translate(const float delta[3]);
    bool SetOffset(const float offset[3]);
    bool SetShape(const PreviewObjectShape &);
    void Shape(PreviewObjectShape &, float left[9], float right[9]) const;
    bool TrackPose(PreviewTrackPose &) const;
    PreviewObject Snapshot() const;
    bool Text(PreviewObjectText &) const;
    bool Costumes(std::vector<PreviewCostume> &) const;
    bool Actions(std::vector<PreviewAction> &) const;
    LegacyLong ActionCelCount(LegacyLong action) const;
    bool TryActionCel(LegacyLong action, LegacyLong cel);
    bool WriteAnimation(const PreviewAnimationEdit &, const std::string &path,
                        int &lastActorFrame, int &lastEditedFrame);
    bool TryCostumes(const std::vector<PreviewCostume> &);
    void RestOrientation(BMAT34 &) const;
    bool CostumeBounds(int bodySet, PreviewBounds &) const;
    bool WriteCostumes(const std::vector<PreviewCostume> &, const std::string &path);
    bool SetEventTrack(std::int32_t source);
    PMaterial_MTRL TextMaterial() const;
    bool SetTextMaterial(std::int32_t source, std::uint32_t number);
    bool SetTextTemplate(std::int32_t source, std::uint32_t number, std::function<void(PTemplate)> bindTemplate);
    bool ReadTimeline(LegacyLong first, LegacyLong last, PreviewAnimationTimeline &timeline) const;
    bool Visible() const
    {
        return core.state.onStage != 0;
    }
    std::uint64_t DrawOrder() const
    {
        return lastShown;
    }
    LegacyLong FirstFrame() const
    {
        return core.header.first_frame;
    }
    LegacyLong LastFrame() const
    {
        return core.header.last_frame;
    }
};
} // namespace Internal
} // namespace MM
#endif
