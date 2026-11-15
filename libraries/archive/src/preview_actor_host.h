#ifndef MM_PREVIEW_ACTOR_HOST_H
#define MM_PREVIEW_ACTOR_HOST_H
// Typed track, resource and renderer services for the C Preview actor owner.
struct PreviewActor::Host
{
    PreviewActor *actor;
    PChunkyResourceFile file = nullptr;
    const std::function<void(PTemplate)> *binder = nullptr;
    std::exception_ptr exception;
    int failure = 0;
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
        s.timeline.motion = EngineActorMotion::Services();
        s.timeline.context = this;
        s.timeline.resolve = [](void *v, MMI32 action, MMI32 cel, MMI32 *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                BRS value = *out;
                bool ok = static_cast<PTemplate>(h.actor->core.tmpl)->FGetDwrActnCel(action, cel, &value);
                *out = value;
                return ok;
            }) > 0;
        };
        s.timeline.put_event = [](void *g, MMI32 i, const MMActorEvent *e) {
            static_cast<PGeneralGroup>(g)->PutFixed(i, const_cast<MMActorEvent *>(e));
        };
        s.timeline.diagnostic = EngineActorTimeline::Diagnostic;
        s.fault = [](void *v) { return static_cast<Host *>(v)->failure; };
        s.header = [](void *v, MMU32 n, MMActorHeader *out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                DataBlock block;
                ActorChunkOnFile value;
                static_assert(sizeof(value) == sizeof(*out), "Preview actor header layout");
                std::memcpy(&value, out, sizeof(value));
                if (!h.file->Pcfl()->FFind(kctgActr, n, &block))
                    return false;
                bool ok = ActorData::FReadHeader(&block, &value);
                std::memcpy(out, &value, sizeof(value));
                return ok;
            });
        };
        s.route = [](void *v, MMU32 n, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                DataBlock block;
                ChildChunkIdentification child;
                auto f = h.file->Pcfl();
                if (!f->FGetKidChidCtg(kctgActr, n, 0, kctgPath, &child) ||
                    !f->FFind(child.cki.ctg, child.cki.cno, &block))
                    return false;
                *out = h.actor->track.AdoptRoute(ActorData::PglReadRoute(&block));
                return *out != nullptr;
            });
        };
        s.events = [](void *v, MMU32 n, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                DataBlock block;
                ChildChunkIdentification child;
                auto f = h.file->Pcfl();
                if (!f->FGetKidChidCtg(kctgActr, n, 0, kctgGgae, &child) ||
                    !f->FFind(child.cki.ctg, child.cki.cno, &block))
                    return false;
                *out = h.actor->track.AdoptEvents(ActorData::PggReadEvents(&block));
                return *out != nullptr;
            });
        };
        s.template_child = [](void *v, MMU32 n, MMU32 *out) -> int {
            auto &h = *static_cast<Host *>(v);
            ChildChunkIdentification child;
            if (!h.file->Pcfl()->FGetKidChidCtg(kctgActr, n, 0, kctgTmpl, &child))
                return 0;
            *out = child.cki.cno;
            return 1;
        };
        s.fetch = [](void *v, const MMActorHeader *header, void **out) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                TAGOnFile tag = {header->source_id, header->wire_pointer_pad, header->template_type,
                                 header->template_number};
                auto source = h.actor->resolve(tag);
                if (!source)
                    return false;
                h.actor->tmpl = static_cast<PTemplate>(source->PbacoFetch(tag.ctg, tag.cno, Template::FReadTmpl));
                *out = h.actor->tmpl;
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
                h.actor->body = static_cast<PTemplate>(t)->PbodyCreate();
                *out = h.actor->body;
                return *out != nullptr;
            });
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
        s.track_create = [](void *v, void **route, void **events) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                bool ok = h.actor->track.FCreate();
                *route = h.actor->track.Route();
                *events = h.actor->track.Events();
                return ok;
            });
        };
        s.route_insert = [](void *v, void *g, MMI32 i, const MMActorRoutePoint *p) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard(
                [&] { return static_cast<PDynamicArray>(g)->FInsert(i, const_cast<MMActorRoutePoint *>(p)); });
        };
        s.event_insert = [](void *v, void *g, MMI32 i, MMU32 n, const void *p, const MMActorEvent *e) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                return static_cast<PGeneralGroup>(g)->FInsert(i, n, const_cast<void *>(p),
                                                              const_cast<MMActorEvent *>(e));
            });
        };
        s.rest = [](void *, void *t, unsigned short *a) { static_cast<PTemplate>(t)->GetRestOrien(a, a + 1, a + 2); };
        s.scalar = [](float v) -> MMI32 { return BrFloatToScalar(v); };
        s.scalar_float = [](MMI32 v) -> float { return BrScalarToFloat(v); };
        s.locate = [](void *, void *b, const MMActorPoint *p, const MMActorMatrix *m) {
            BMAT34 value;
            std::memcpy(&value, m, sizeof(value));
            static_cast<PBody>(b)->LocateOrient(p->dxr, p->dyr, p->dzr, &value);
        };
        s.pose = [](void *v, void *t, void *b, MMI32 action, MMI32 cel) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] { return static_cast<PTemplate>(t)->FSetActnCel(static_cast<PBody>(b), action, cel); });
        };
        s.apply = [](void *v, MMI32 index) -> int {
            auto &h = *static_cast<Host *>(v);
            return h.Guard([&] {
                return h.actor->ApplyEvent(h.actor->core.state, h.actor->track, index, false, &h.actor->motionSounds,
                                           &h.actor->sounds);
            });
        };
        s.clear_cues = [](void *v) { static_cast<Host *>(v)->actor->sounds.clear(); };
        s.clear_motion = [](void *v) { static_cast<Host *>(v)->actor->motionSounds.Clear(); };
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
                    ActorEvent::Sound native;
                    std::memcpy(&native, value, sizeof(native));
                    actor->sounds.push_back(actor->SoundCue(native));
                    return 1;
                };
                return mm_actor_sound_motion(h.actor->motionSounds.Core(), h.actor->core.state.action,
                                             h.actor->core.state.cel, &sound);
            });
        };
        return s;
    }
};
#endif
