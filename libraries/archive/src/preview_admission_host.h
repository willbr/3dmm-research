#ifndef MM_PREVIEW_ADMISSION_HOST_H
#define MM_PREVIEW_ADMISSION_HOST_H
// Typed resource, SDK and presentation primitives for C admission.
struct Preview::Impl::AdmissionHost
{
    static_assert(int(sevtAddActr) == MM_SCENE_ADD_ACTOR && int(sevtSetBkgd) == MM_SCENE_SET_BACKGROUND &&
                  int(sevtChngCamera) == MM_SCENE_CHANGE_CAMERA, "Admission event kinds");
    static_assert(knfrmInvalid == 2147483647, "Admission offstage frame");
    Preview *preview;
    Impl *impl;
    Error *error;
    std::exception_ptr exception;
    std::string actorError;
    bool missingBackground = false;
    MMMovieSceneSource sceneSource = {};
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return -2;
        }
    }
    MMPreviewAdmission State(Id asset) const
    {
        MMPreviewAdmission p = {};
        p.live = &impl->live;
        p.type = asset.type;
        p.number = asset.number;
        p.cache = impl->cache != nullptr;
        p.tmpl = impl->tmpl;
        p.body = impl->body;
        p.model = impl->model;
        p.action_count = static_cast<MMU32>(impl->info.actions.size());
        p.kind = asset.type == kctgTmpl   ? MM_PREVIEW_TEMPLATE
                 : asset.type == kctgBmdl ? MM_PREVIEW_MODEL
                 : asset.type == kctgBkgd ? MM_PREVIEW_BACKGROUND
                 : asset.type == kctgScen ? MM_PREVIEW_SCENE
                 : asset.type == kctgActr ? MM_PREVIEW_ACTOR
                                          : MM_PREVIEW_UNKNOWN;
        return p;
    }
    bool Result(const MMPreviewAdmission &p, int r)
    {
        if (r == MM_PREVIEW_ADMIT_MEMORY)
            return Fail(*error, ErrorCode::out_of_memory, "Insufficient memory to load preview asset.");
        if (exception)
            std::rethrow_exception(exception);
        if (p.mark_chunk)
        {
            error->hasChunk = true;
            error->chunk = {p.type, p.number};
        }
        switch (r)
        {
        case MM_PREVIEW_ADMIT_OK:
            return true;
        case MM_PREVIEW_ADMIT_CACHE:
            return Fail(*error, ErrorCode::invalid_argument, "Open preview resources first.");
        case MM_PREVIEW_ADMIT_VIRTUAL_BACKGROUND:
            return Fail(*error, ErrorCode::decode_failed, "Cannot load the new scene background.");
        case MM_PREVIEW_ADMIT_SCENE_HEADER:
            return Fail(*error, ErrorCode::decode_failed, "Cannot read the scene header.");
        case MM_PREVIEW_ADMIT_START_MISSING:
            return Fail(*error, ErrorCode::decode_failed, "Scene start events are missing.");
        case MM_PREVIEW_ADMIT_START_READ:
            return Fail(*error, ErrorCode::decode_failed, "Cannot read scene start events.");
        case MM_PREVIEW_ADMIT_ACTOR_CHILD:
            return Fail(*error, ErrorCode::decode_failed,
                        ("Scene references missing actor child " + std::to_string(p.detail) + ".").c_str());
        case MM_PREVIEW_ADMIT_BACKGROUND:
            return Fail(*error, ErrorCode::decode_failed,
                        missingBackground ? impl->resolveError.c_str() : "Cannot load scene background.");
        case MM_PREVIEW_ADMIT_FRAME_MISSING:
            return Fail(*error, ErrorCode::decode_failed, "Scene frame events are missing.");
        case MM_PREVIEW_ADMIT_FRAME_READ:
            return Fail(*error, ErrorCode::decode_failed, "Cannot read scene frame events.");
        case MM_PREVIEW_ADMIT_ACTOR_HEADER:
            return Fail(*error, ErrorCode::decode_failed, "Cannot read the actor header.");
        case MM_PREVIEW_ADMIT_OFFSTAGE:
            return Fail(*error, ErrorCode::unsupported_format,
                        "This actor has no on-stage frames. Select its template to inspect its geometry.");
        case MM_PREVIEW_ADMIT_ACTOR_LOAD:
            return Fail(*error, ErrorCode::decode_failed, actorError.c_str());
        case MM_PREVIEW_ADMIT_EXTERNAL:
            return false;
        case MM_PREVIEW_ADMIT_UNSUPPORTED:
            return Fail(*error, ErrorCode::unsupported_format, "This chunk does not yet have a 3D preview.");
        case MM_PREVIEW_ADMIT_POSE_RANGE:
            return Fail(*error, ErrorCode::invalid_argument, "Action or cel is out of range.");
        case MM_PREVIEW_ADMIT_POSE:
            return Fail(*error, ErrorCode::decode_failed, "Cannot apply this action pose.");
        default:
            return bool(*error) ? false
                                : Fail(*error, ErrorCode::decode_failed,
                                       "The engine could not load this asset and its pose dependencies.");
        }
    }
    MMPreviewAdmissionServices Services()
    {
        MMPreviewAdmissionServices s = {};
        s.context = this;
        s.begin = [](void *) { Internal::ClearDiagnostic(); };
        s.healthy = [](void *) -> int { return !Internal::AssertionFailed(); };
        s.clear = [](void *v, int preserve) { static_cast<AdmissionHost *>(v)->impl->ClearAsset(preserve != 0); };
        s.publish = [](void *v, const MMPreviewAdmission *) { static_cast<AdmissionHost *>(v)->impl->PublishInfo(); };
        s.template_fetch = [](void *v, MMU32 type, MMU32 number, void **out) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                h.impl->tmpl = static_cast<PTemplate>(h.impl->cache->PbacoFetch(type, number, Template::FReadTmpl));
                *out = h.impl->tmpl;
                return *out != nullptr;
            });
        };
        s.template_bind = [](void *v, void *t) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                h.impl->BindTemplate(static_cast<PTemplate>(t));
                return 1;
            });
        };
        s.body_create = [](void *v, void *t, void **out) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                h.impl->body = static_cast<PTemplate>(t)->PbodyCreate();
                *out = h.impl->body;
                return *out != nullptr;
            });
        };
        s.prop = [](void *, void *t) -> int { return static_cast<PTemplate>(t)->FIsProp(); };
        s.actions = [](void *, void *t) -> MMI32 { return static_cast<PTemplate>(t)->Cactn(); };
        s.cels = [](void *v, void *t, MMU32 action, MMI32 *out) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                LegacyLong n = 0;
                bool ok = static_cast<PTemplate>(t)->FGetCcelActn(action, &n);
                *out = n;
                return ok;
            });
        };
        s.child = [](void *v, MMU32 type, MMU32 number, MMU32 slot, int actor, MMPreviewChunk *out) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            ChildChunkIdentification child;
            if (!h.impl->cache->Pcfl()->FGetKidChidCtg(type, number, slot, actor ? kctgActr : kctgActn, &child))
                return 0;
            *out = {};
            out->type = child.cki.ctg;
            out->number = child.cki.cno;
            return 1;
        };
        s.chunks = [](void *v) -> MMU32 {
            return static_cast<MMU32>(static_cast<AdmissionHost *>(v)->impl->assets.Chunks().size());
        };
        s.chunk = [](void *v, MMU32 n, MMPreviewChunk *out) {
            const auto &c = static_cast<AdmissionHost *>(v)->impl->assets.Chunks()[n];
            *out = {c.id.type, c.id.number, c.storedBytes, c.packed, !c.name.empty()};
        };
        s.action_add = [](void *v, MMU32 action, MMU32 count, MMI32 name) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                PreviewAction a;
                a.name = name < 0 ? "Action " + std::to_string(action)
                                  : std::to_string(action) + ": " + h.impl->assets.Chunks()[name].name;
                a.celCount = count;
                h.impl->info.actions.push_back(a);
                return 1;
            });
        };
        s.action_cels = [](void *v, MMU32 n) -> MMU32 {
            return static_cast<AdmissionHost *>(v)->impl->info.actions[n].celCount;
        };
        s.pose = [](void *v, void *t, void *b, MMU32 action, MMU32 cel) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] { return static_cast<PTemplate>(t)->FSetActnCel(static_cast<PBody>(b), action, cel); });
        };
        s.rest = [](void *, void *t, unsigned short *angles) {
            static_cast<PTemplate>(t)->GetRestOrien(angles, angles + 1, angles + 2);
        };
        s.rotate = [](void *, MMActorMatrix *m, int axis, unsigned short angle, int post) {
            BMAT34 native;
            static_assert(sizeof(native) == sizeof(*m), "Admission matrix layout");
            if (post)
                std::memcpy(&native, m, sizeof(native));
            if (axis == 0)
                BrMatrix34RotateX(&native, angle);
            else if (axis == 1)
                BrMatrix34PostRotateY(&native, angle);
            else
                BrMatrix34PostRotateZ(&native, angle);
            std::memcpy(m, &native, sizeof(native));
        };
        s.locate = [](void *, void *b, const MMActorMatrix *m) {
            BMAT34 native;
            std::memcpy(&native, m, sizeof(native));
            static_cast<PBody>(b)->LocateOrient(rZero, rZero, rZero, &native);
        };
        s.model_fetch = [](void *v, MMU32 type, MMU32 number, void **out) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                h.impl->model = static_cast<PModel>(h.impl->cache->PbacoFetch(type, number, Model::FReadModl));
                *out = h.impl->model;
                return *out != nullptr;
            });
        };
        s.background = [](void *v, const MMPreviewAdmissionTag *t, int virtualScene) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                auto source = h.impl->cache;
                if (t)
                {
                    TAGOnFile tag = {t->source, 0, virtualScene ? kctgBkgd : t->type, t->number};
                    source = h.impl->Resolve(tag);
                }
                h.missingBackground = !source;
                return source && h.impl->LoadBackground(source, t ? t->number : h.impl->live.status.asset_number);
            });
        };
        if (impl->cache)
        {
            auto file = impl->cache->Pcfl();
            sceneSource = {file == impl->assets.NativeFile() ? impl->assets.MovieSession() : nullptr,
                           EngineMovieGraph::FromFile(file)};
        }
        s.scene = mm_movie_scene_reader(&sceneSource);
        s.actor_header = [](void *v, MMU32 number, MMI32 *first, MMI32 *last) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                DataBlock block;
                ActorChunkOnFile header;
                if (!h.impl->cache->Pcfl()->FFind(kctgActr, number, &block) || !ActorData::FReadHeader(&block, &header))
                    return false;
                *first = header.nfrmFirst;
                *last = header.nfrmLast;
                return true;
            });
        };
        s.actor_load = [](void *v, MMU32 number, void **out) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                std::unique_ptr<Internal::PreviewActor> actor(new Internal::PreviewActor);
                auto impl = h.impl;
                bool ok = actor->Load(
                    impl->cache, number, [impl](const TAGOnFile &tag) { return impl->Resolve(tag); },
                    [impl](PTemplate t) { impl->BindTemplate(t); }, impl->visibilityClock);
                *out = actor.release();
                return ok;
            });
        };
        s.actor_release = [](void *, void *a) { delete static_cast<Internal::PreviewActor *>(a); };
        s.actor_range = [](void *, void *a, MMI32 *first, MMI32 *last) {
            auto actor = static_cast<Internal::PreviewActor *>(a);
            *first = actor->FirstFrame();
            *last = actor->LastFrame();
        };
        s.sync = [](void *v) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] { return h.impl->SyncActors(*h.error); });
        };
        s.seek = [](void *v, MMI32 frame) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] { return h.preview->SetFrame(frame, *h.error); });
        };
        s.save_actor_error = [](void *v) -> int {
            auto &h = *static_cast<AdmissionHost *>(v);
            return h.Guard([&] {
                h.actorError = h.impl->resolveError.empty() ? "Cannot load the actor route, events or template."
                                                            : h.impl->resolveError;
                return 1;
            });
        };
        s.stats = [](void *v) { static_cast<AdmissionHost *>(v)->impl->RefreshStats(); };
        return s;
    }
};
#endif
