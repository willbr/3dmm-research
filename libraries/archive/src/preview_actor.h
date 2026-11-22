/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_PREVIEW_ACTOR_H
#define MM_PREVIEW_ACTOR_H
#include <functional>
#include "mm/preview.h"
#include "soc_core.h"
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
    struct DraftHost;
    MMPreviewActor core;
    MMActorTrack Track() const { return mm_preview_actor_track(&core); }
    PTemplate Ptmpl() const { return static_cast<PTemplate>(core.tmpl); }
    std::function<MMResourceCache *(const TAGOnFile &)> resolve;
    std::string ActionName(LegacyLong action) const;

  public:
    PreviewActor()
    {
        mm_preview_actor_init(&core);
    }
    PBody Pbody() const { return static_cast<PBody>(core.body); }
    static const MMPreviewActor *View(const void *handle)
    {
        return &static_cast<const PreviewActor *>(handle)->core;
    }
    ~PreviewActor();
    bool Load(const MMMovieSessionGraph &, ChunkNumber number,
              std::function<MMResourceCache *(const TAGOnFile &)> resolver,
              std::function<void(PTemplate)> bindTemplate, MMWide &drawClock);
    bool Create(const PreviewPropDefinition &, std::function<MMResourceCache *(const TAGOnFile &)> resolver,
                std::function<void(PTemplate)> bindTemplate, MMWide &drawClock);
    bool Seek(LegacyLong frame);
    bool Translate(const float delta[3], MMPreviewOrbit &, MMI32 object);
    bool SetOffset(const float offset[3], MMPreviewOrbit &, MMI32 object);
    int SetShape(const PreviewObjectShape &, MMPreviewOrbit &, MMI32 object);
    void Shape(PreviewObjectShape &, float left[9], float right[9]) const;
    bool TrackPose(PreviewTrackPose &) const;
    bool Text(PreviewObjectText &) const;
    bool Costumes(std::vector<PreviewCostume> &) const;
    bool Actions(std::vector<PreviewAction> &) const;
    LegacyLong ActionCelCount(LegacyLong action) const;
    int RenderAction(LegacyLong action, LegacyLong cel, float *clip, int *clipValid, const MMPreviewIsolatedView &);
    int RenderCostumes(const std::vector<PreviewCostume> &, MMI32 set, const MMPreviewIsolatedView &);
    using SoundGraphResolver = std::function<bool(const TAGOnFile &, MMMovieSessionGraph &)>;
    bool WriteAnimation(const MMActorAnimation &, const std::string &path, int &lastActorFrame,
                        int &lastEditedFrame, const SoundGraphResolver &);
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
