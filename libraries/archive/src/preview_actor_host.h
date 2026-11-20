#ifndef MM_PREVIEW_ACTOR_HOST_H
#define MM_PREVIEW_ACTOR_HOST_H
// Typed track, resource and renderer services for the C Preview actor owner.
struct PreviewActor::Host
{
    PreviewActor *actor;
    const MMMovieSessionGraph *graph = nullptr;
    const std::function<void(PTemplate)> *binder = nullptr;
    std::exception_ptr exception;
    int failure = 0;
    MMStorageAllocator trackAllocator = CollectionAllocator();
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
    bool Result(int r)
    {
        if (exception)
            std::rethrow_exception(exception);
        return r > 0;
    }
    MMPreviewActorServices Services()
    {
        MMPreviewActorServices s = {};
        s.context = this;
        s.tags = &EngineActorTagServices();
        s.track_allocator = &trackAllocator;
        s.trash_unused = DebugShip(1, 0);
        s.timeline = mm_actor_track_timeline_services();
        s.timeline.context = this;
        s.timeline.resolve = [](void *v, MMI32 action, MMI32 cel, MMI32 *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                BRS value = *out;
                bool ok = h.actor->Ptmpl()->FGetDwrActnCel(action, cel, &value);
                *out = value;
                return ok;
            }) > 0;
        };
        s.timeline.diagnostic = EngineActorTimeline::Diagnostic;
        s.fault = [](void *v) { return static_cast<Host *>(v)->failure; };
        s.header = [](void *v, MMU32 n, MMActorHeader *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return mm_movie_actor_header(h.graph, n, out) == MM_MOVIE_OK; });
        };
        s.route = [](void *v, MMU32 n, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                const MMActorPathFactory factory = {&h.trackAllocator, DebugShip(1, 0)};
                MMActorPath *path = nullptr;
                const int result = mm_actor_movie_path(h.graph, n, &factory, &path);
                *out = path;
                return result == MM_MOVIE_OK;
            });
        };
        s.events = [](void *v, MMU32 n, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                MMActorEventFactory factory = {&h.trackAllocator, DebugShip(1, 0)};
                MMActorEventTable *events = nullptr;
                const int result = mm_actor_movie_events(h.graph, n, &factory, &events);
                *out = events;
                return result == MM_MOVIE_OK;
            });
        };
        s.fetch = [](void *v, const MMActorHeader *header, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                TAGOnFile tag = {header->source_id, header->wire_pointer_pad, header->template_type,
                                 header->template_number};
                auto source = h.actor->resolve(tag);
                if (!source)
                    return false;
                *out = static_cast<PTemplate>(source->PbacoFetch(tag.ctg, tag.cno, Template::FReadTmpl));
                return *out != nullptr;
            });
        };
        s.bind = [](void *v, void *t) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                (*h.binder)(static_cast<PTemplate>(t));
                return 1;
            });
        };
        s.body_create = [](void *v, void *t, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                *out = static_cast<PTemplate>(t)->PbodyCreate();
                return *out != nullptr;
            });
        };
        s.duplicate_body = [](void *v, void *b, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                *out = static_cast<PBody>(b)->PbodyDup();
                return *out != nullptr;
            });
        };
        s.adjust_text_body = [](void *v, void *t, void *b) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return static_cast<PThreeDText>(t)->FAdjustBody(static_cast<PBody>(b)); });
        };
        s.prop = [](void *, void *t) -> int { return static_cast<PTemplate>(t)->FIsProp(); };
        s.text = [](void *, void *t) -> int { return static_cast<PTemplate>(t)->FIsTdt(); };
        s.release_body = [](void *, void *b) {
            auto value = static_cast<PBody>(b);
            ReleasePpo(&value);
        };
        s.release_template = [](void *, void *t) {
            auto value = static_cast<PTemplate>(t);
            ReleasePpo(&value);
        };
        s.rest = [](void *v, void *t, unsigned short *a) {
            static_cast<Host *>(v)->Guard([&] {
                static_cast<PTemplate>(t)->GetRestOrien(a, a + 1, a + 2);
                return 1;
            });
        };
        s.scalar = [](float v) -> MMI32 { return BrFloatToScalar(v); };
        s.scalar_float = [](MMI32 v) -> float { return BrScalarToFloat(v); };
        s.locate = [](void *v, void *b, const MMActorBodyPose *pose) {
            static_cast<Host *>(v)->Guard([&] {
                static_cast<PBody>(b)->ApplyPose(*pose);
                return 1;
            });
        };
        s.pose = [](void *v, void *t, void *b, MMI32 action, MMI32 cel) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return static_cast<PTemplate>(t)->FSetActnCel(static_cast<PBody>(b), action, cel); });
        };
        s.apply = [](void *v, MMI32 index) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                return h.actor->ApplyEvent(h.actor->core.state, h.actor->Track(), index, false, &h.actor->motionSounds,
                                           &h.actor->sounds);
            });
        };
        s.clear_cues = [](void *v) { static_cast<Host *>(v)->actor->sounds.clear(); };
        s.clear_motion = [](void *v) { mm_actor_sounds_clear(&static_cast<Host *>(v)->actor->motionSounds); };
        s.default_costume = [](void *v, void *t, void *b) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return static_cast<PTemplate>(t)->FSetDefaultCost(static_cast<PBody>(b)); });
        };
        s.motion_cues = [](void *v) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                MMActorSoundServices sound = {};
                sound.context = h.actor;
                sound.cues_only = 1;
                sound.cel_count = [](void *a, MMI32, MMI32 *out) {
                    *out = static_cast<PreviewActor *>(a)->core.state.celCount;
                    return 1;
                };
                sound.cue = [](void *a, const MMActorSound *value) {
                    auto actor = static_cast<PreviewActor *>(a);
                    actor->sounds.push_back(actor->SoundCue(*value));
                    return 1;
                };
                return mm_actor_sound_motion(&h.actor->motionSounds, h.actor->core.state.action,
                                             h.actor->core.state.cel, &sound);
            });
        };
        return s;
    }
};
#endif
