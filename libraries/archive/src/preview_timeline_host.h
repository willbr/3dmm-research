#ifndef MM_PREVIEW_TIMELINE_HOST_H
#define MM_PREVIEW_TIMELINE_HOST_H
// Typed clone, metadata query and display formatting for C timeline traversal.
struct PreviewActor::TimelineHost
{
    const PreviewActor *actor;
    PreviewAnimationTimeline *output;
    std::exception_ptr exception;
    int failure = 0;
    std::map<LegacyLong, std::string> names;
    MMActorEffectRenderer renderer;
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return failure = -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return failure = -2;
        }
    }
    std::string Name(LegacyLong action)
    {
        auto found = names.find(action);
        if (found != names.end())
            return found->second;
        auto result = actor->ActionName(action);
        names.emplace(action, result);
        return result;
    }
    static std::int64_t Frame(MMWide v)
    {
        return std::int64_t(v.high) * 4294967296LL + v.low;
    }
    void Marker(const MMPreviewMarker &e)
    {
        PreviewAnimationEvent marker;
        marker.frame = e.frame;
        marker.kind = static_cast<PreviewAnimationKind>(e.kind);
        std::ostringstream detail;
        detail.precision(4);
        switch (e.kind)
        {
        case MM_PREVIEW_MARK_ENTRANCE:
            marker.name = "Enter stage";
            break;
        case MM_PREVIEW_MARK_EXIT:
            marker.name = "Leave stage";
            break;
        case MM_PREVIEW_MARK_ACTION:
            marker.name = Name(e.values[0]);
            detail << "Action " << e.values[0] << ", starting cel " << e.values[1];
            break;
        case MM_PREVIEW_MARK_COSTUME:
            marker.name = "Costume";
            detail << "Part set " << e.values[0];
            if (e.values[1])
                detail << ", costume " << e.values[2];
            else
                detail << ", material " << e.number << ", source " << e.values[3];
            break;
        case MM_PREVIEW_MARK_ROTATION:
            marker.name = e.values[0] ? "Rotate this frame" : "Rotate";
            break;
        case MM_PREVIEW_MARK_SCALE:
            marker.name = "Size";
            detail << BrScalarToFloat(e.values[0]) << "x";
            break;
        case MM_PREVIEW_MARK_STRETCH:
            marker.name = "Stretch";
            detail << "X " << BrScalarToFloat(e.values[0]) << ", Y " << BrScalarToFloat(e.values[1]) << ", Z "
                   << BrScalarToFloat(e.values[2]);
            break;
        case MM_PREVIEW_MARK_MOVE:
        case MM_PREVIEW_MARK_PATH:
            marker.name = e.kind == MM_PREVIEW_MARK_MOVE ? "Move path" : "Adjust path";
            detail << "X " << BrScalarToFloat(e.values[0]) << ", Y " << BrScalarToFloat(e.values[1]) << ", Z "
                   << BrScalarToFloat(e.values[2]);
            break;
        case MM_PREVIEW_MARK_STEP:
            marker.name = e.values[0] == rZero     ? "Stop movement"
                          : e.values[0] == kdwrNil ? "Action speed"
                                                   : "Movement speed";
            if (e.values[0] != kdwrNil)
                detail << BrScalarToFloat(e.values[0]) << " units per frame (before size scaling)";
            break;
        case MM_PREVIEW_MARK_FREEZE:
            marker.name = e.values[0] ? "Freeze animation" : "Unfreeze animation";
            break;
        }
        marker.detail = detail.str();
        output->events.push_back(std::move(marker));
    }
    MMPreviewTimelineServices Services()
    {
        static_assert(int(PreviewAnimationKind::Entrance) == MM_PREVIEW_MARK_ENTRANCE &&
                          int(PreviewAnimationKind::Freeze) == MM_PREVIEW_MARK_FREEZE,
                      "Preview marker kinds");
        MMPreviewTimelineServices s = {};
        s.context = this;
        s.timeline.context = this;
        s.timeline.motion = EngineActorMotion::Services();
        s.timeline.diagnostic = EngineActorTimeline::Diagnostic;
        s.timeline.resolve = [](void *v, MMI32 action, MMI32 cel, MMI32 *out) -> int {
            auto &h = *static_cast<TimelineHost *>(v);
            return h.Guard([&] { return mm_brender_template_distance(h.actor->Ptmpl(), action, cel, out); }) > 0;
        };
        s.reset = [](void *v) { *static_cast<TimelineHost *>(v)->output = {}; };
        s.fault = [](void *v) { return static_cast<TimelineHost *>(v)->failure; };
        s.tags = mm_tag_local_cache_services();
        mm_brender_actor_effect_renderer(&renderer);
        s.renderer = &renderer;
        s.marker = [](void *v, const MMPreviewMarker *e) -> int {
            auto &h = *static_cast<TimelineHost *>(v);
            return h.Guard([&] {
                h.Marker(*e);
                return 1;
            });
        };
        s.span = [](void *v, const MMPreviewSpan *p) -> int {
            auto &h = *static_cast<TimelineHost *>(v);
            return h.Guard([&] {
                h.output->spans.push_back(
                    {Frame(p->first), Frame(p->end), p->action, p->frozen != 0, h.Name(p->action)});
                return 1;
            });
        };
        s.extend = [](void *v, MMWide end) {
            static_cast<TimelineHost *>(v)->output->spans.back().endFrame = Frame(end);
        };
        return s;
    }
};
#endif
