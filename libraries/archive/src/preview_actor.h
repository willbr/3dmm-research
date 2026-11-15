/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_PREVIEW_ACTOR_H
#define MM_PREVIEW_ACTOR_H
#include <functional>
#include "mm/preview.h"
#include "soc_core.h"
#include "actor_sound.h"

namespace MM
{
namespace Internal
{
// Engine adapter for the pure frame/pose evaluators. No Studio, World or audio.
class PreviewActor
{
    ActorChunkOnFile header = {};
    ActorTrack track;
    ActorFrameState state;
    bool inactive = false;
    PTemplate tmpl = nullptr;
    std::function<PChunkyResourceFile(const TAGOnFile &)> resolve;
    bool Apply(LegacyLong index);
    bool ApplyEvent(ActorFrameState &value, const ActorTrack &events, LegacyLong index, bool metadataOnly,
                    ActorSoundList *motion = nullptr,
                    std::vector<PreviewSoundCue> *cues = nullptr) const;
    bool Position(bool selectCel);
    bool Evaluate(bool display);
    ActorStepSource Steps() const;
    ActorSoundList motionSounds;
    bool soundDisplay = false;
    std::uint64_t *visibilityClock = nullptr;
    mutable std::uint64_t lastShown = 0;
    PreviewSoundCue SoundCue(const ActorEvent::Sound &sound) const;

  public:
    ChunkNumber number = 0;
    PBody body = nullptr;
    std::vector<PreviewSoundCue> sounds;
    ~PreviewActor();
    bool Load(PChunkyResourceFile file, ChunkNumber number,
              std::function<PChunkyResourceFile(const TAGOnFile &)> resolver,
              std::function<void(PTemplate)> bindTemplate, std::uint64_t &visibilityClock);
    bool Seek(LegacyLong frame);
    PreviewObject Snapshot() const;
    bool ReadTimeline(LegacyLong first, LegacyLong last, PreviewAnimationTimeline &timeline) const;
    bool Visible() const
    {
        return state.onStage;
    }
    std::uint64_t DrawOrder() const { return lastShown; }
    LegacyLong FirstFrame() const
    {
        return header.nfrmFirst;
    }
    LegacyLong LastFrame() const
    {
        return header.nfrmLast;
    }
};
} // namespace Internal
} // namespace MM
#endif
