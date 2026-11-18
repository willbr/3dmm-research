#ifndef MM_PREVIEW_LIVE_HOST_H
#define MM_PREVIEW_LIVE_HOST_H
// Typed actor/resource and status marshaling for the C live session. Included in MM.
struct Preview::Impl::LiveHost
{
    Preview *preview;
    Impl *impl;
    Error *error;
    std::exception_ptr exception;
    int Exception()
    {
        exception = std::current_exception();
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::bad_alloc &)
        {
            return -1;
        }
        catch (...)
        {
            return -2;
        }
    }
    void Rethrow(int result)
    {
        if (exception)
            std::rethrow_exception(exception);
        if (result == MM_PREVIEW_LIVE_MEMORY)
            throw std::bad_alloc();
    }
    bool FrameResult(int result)
    {
        if (result == MM_PREVIEW_LIVE_SEEK_MEMORY)
        {
            exception = {};
            return Fail(*error, ErrorCode::out_of_memory, "Insufficient memory to evaluate scene frame.");
        }
        Rethrow(result);
        switch (result)
        {
        case MM_PREVIEW_LIVE_OK:
            return true;
        case MM_PREVIEW_LIVE_FRAME:
            return Fail(*error, ErrorCode::invalid_argument, "Scene frame is out of range.");
        case MM_PREVIEW_LIVE_CAMERA:
            return false; // SetCamera supplied the error.
        default:
            return Fail(*error, ErrorCode::decode_failed,
                        impl->resolveError.empty() ? "Cannot evaluate this scene frame." : impl->resolveError.c_str());
        }
    }
    MMPreviewLiveServices Services()
    {
        MMPreviewLiveServices s = {};
        s.context = this;
        s.actor_new = [](void *v) -> void * {
            auto &h = *static_cast<LiveHost *>(v);
            try
            {
                return new Internal::PreviewActor;
            }
            catch (...)
            {
                h.Exception();
                return nullptr;
            }
        };
        s.actor_create = [](void *v, void *a, const MMPreviewLiveProp *p) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            try
            {
                PreviewPropDefinition d;
                d.scene = p->scene;
                d.number = p->number;
                d.templateNumber = p->template_number;
                d.source = p->source;
                d.actorId = p->actor_id;
                d.first = p->first;
                d.last = p->last;
                std::copy(p->point, p->point + 3, d.point);
                auto impl = h.impl;
                return static_cast<Internal::PreviewActor *>(a)->Create(
                    d, [impl](const TAGOnFile &tag) { return impl->Resolve(tag); },
                    [impl](PTemplate value) { impl->BindTemplate(value); }, impl->visibilityClock);
            }
            catch (...)
            {
                return h.Exception();
            }
        };
        s.actor_seek = [](void *v, void *a, MMI32 frame) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            try
            {
                return static_cast<Internal::PreviewActor *>(a)->Seek(frame);
            }
            catch (...)
            {
                return h.Exception();
            }
        };
        s.actor_number = [](void *, void *a) -> MMU32 { return static_cast<Internal::PreviewActor *>(a)->number; };
        s.actor_release = [](void *, void *a) { delete static_cast<Internal::PreviewActor *>(a); };
        s.camera = [](void *v, MMU32 camera) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            try
            {
                return h.preview->SetCamera(camera, *h.error);
            }
            catch (...)
            {
                return h.Exception();
            }
        };
        s.begin_seek = [](void *) { Internal::ClearDiagnostic(); };
        s.healthy = [](void *) -> int { return !Internal::AssertionFailed(); };
        s.publish = [](void *v, const MMPreviewLive *) { static_cast<LiveHost *>(v)->impl->PublishInfo(); };
        s.stats = [](void *v) { static_cast<LiveHost *>(v)->impl->RefreshStats(); };
        s.clear_asset = [](void *v) { static_cast<LiveHost *>(v)->impl->ClearAsset(); };
        return s;
    }
};
bool Preview::Impl::SyncActors(Error &error)
{
    LiveHost host = {nullptr, this, &error};
    auto services = host.Services();
    const auto result = mm_preview_live_sync(&live, &services);
    PublishInfo();
    host.Rethrow(result);
    return result == MM_PREVIEW_LIVE_OK ||
           Fail(error, ErrorCode::decode_failed, "Cannot create the selected object and its pose dependencies.");
}
#endif
