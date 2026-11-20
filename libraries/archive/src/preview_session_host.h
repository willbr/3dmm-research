#ifndef MM_PREVIEW_SESSION_HOST_H
#define MM_PREVIEW_SESSION_HOST_H
// Typed archive/path, palette, resource-cache and BRender services for C sessions.
struct Preview::Impl::SessionHost
{
    Preview *preview;
    Impl *impl;
    Archive *input = nullptr;
    Error *error = nullptr;
    std::exception_ptr exception;
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
    bool Result(int result, bool content = false)
    {
        if (result == MM_PREVIEW_SESSION_MEMORY)
            return Fail(*error, ErrorCode::out_of_memory,
                        content ? "Insufficient memory to load content archive."
                                : "Insufficient memory to open preview resources.");
        if (exception)
            std::rethrow_exception(exception);
        return result == MM_PREVIEW_SESSION_OK;
    }
    int Report(int result)
    {
        return Guard([&] {
            switch (result)
            {
            case MM_PREVIEW_SESSION_TRANSFER:
                Fail(*error, ErrorCode::invalid_argument, "Open the movie archive before transferring it to preview.");
                break;
            case MM_PREVIEW_SESSION_PALETTE_MISSING:
                Fail(*error, ErrorCode::unsupported_format,
                     "Resources must contain the GLCR/0 palette from 3dmovie.chk.");
                break;
            case MM_PREVIEW_SESSION_PALETTE:
                Fail(*error, ErrorCode::unsupported_format,
                     "Resources require a 256-colour GLCR/0 palette and TMAP/0 shade table.");
                break;
            case MM_PREVIEW_SESSION_RUNTIME:
                Fail(*error, ErrorCode::invalid_argument, "Another preview owns the BRender runtime. Close it first.");
                break;
            case MM_PREVIEW_SESSION_CACHE:
                Fail(*error, ErrorCode::decode_failed, "Cannot initialize the asset resource reader.");
                break;
            case MM_PREVIEW_SESSION_SHADE:
                Fail(*error, ErrorCode::decode_failed, "Cannot load the resource shade table.");
                break;
            case MM_PREVIEW_SESSION_CONTENT_ARGUMENT:
                Fail(*error, ErrorCode::invalid_argument,
                     "Open preview resources and specify a positive content source ID.");
                break;
            case MM_PREVIEW_SESSION_CONTENT_CACHE:
                Fail(*error, ErrorCode::decode_failed, "Cannot initialize the content resource reader.");
                break;
            }
            return 1;
        });
    }
    static bool WithinBudget(const Archive &archive, Error &error)
    {
        MMChunkEntry failed;
        const int result = mm_preview_archive_budget(archive.Reader(), &failed);
        if (result == MM_PREVIEW_BUDGET_HEADER)
            return Fail(error, ErrorCode::decode_failed, "Invalid compressed preview dependency.");
        if (result == MM_PREVIEW_BUDGET_LIMIT)
        {
            error.hasChunk = true;
            error.chunk = {failed.type, failed.number};
            return Fail(error, ErrorCode::resource_limit,
                        "Preview archives are limited to 64 MiB per chunk and 512 MiB of decoded dependencies.");
        }
        return true;
    }
    MMPreviewSessionServices Services()
    {
        MMPreviewSessionServices s = {};
        s.context = this;
        s.begin = [](void *v) {
            auto &h = *static_cast<SessionHost *>(v);
            *h.error = {};
            Internal::ClearDiagnostic();
        };
        s.healthy = [](void *) -> int { return !Internal::AssertionFailed(); };
        s.report = [](void *v, int r) { return static_cast<SessionHost *>(v)->Report(r); };
        s.input_present = [](void *v) -> int { return static_cast<SessionHost *>(v)->input->NativeFile() != nullptr; };
        s.candidate_new = [](void *v, void **out) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                *out = new Impl;
                return 1;
            });
        };
        s.state = [](void *, void *p) { return &static_cast<Impl *>(p)->session; };
        s.take_assets = [](void *v, void *p) {
            auto &h = *static_cast<SessionHost *>(v);
            static_cast<Impl *>(p)->assets._impl.swap(h.input->_impl);
        };
        s.open_resources = [](void *v, void *p, const void *path) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                return static_cast<Impl *>(p)->resources.OpenResources(*static_cast<const std::string *>(path), *h.error);
            });
        };
        s.has_assets = [](void *, void *p) -> int { return static_cast<Impl *>(p)->assets.NativeFile() != nullptr; };
        s.budget = [](void *v, void *p, int resource) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                auto i = static_cast<Impl *>(p);
                const auto &a = resource ? i->resources : i->assets;
                return WithinBudget(a, *h.error);
            });
        };
        s.resource = [](void *v, void *p, MMU32 type, MMU32 number, MMReadStream *out) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                return mm_preview_archive_resource(static_cast<Impl *>(p)->resources.Reader(), type, number, out);
            });
        };
        s.palette_output = [](void *, void *p) {
            return reinterpret_cast<unsigned char *>(static_cast<Impl *>(p)->palette);
        };
        s.display_palette = [](void *v, void *p) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                Internal::ResolveDisplayPalette(static_cast<Impl *>(p)->palette);
                return 1;
            });
        };
        s.runtime_begin = [](void *) {
            BrBegin();
            return 1;
        };
        s.depth_begin = [](void *) {
            BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
            return 1;
        };
        s.cache_new = [](void *v, void *p, void **out) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                auto i = static_cast<Impl *>(p);
                i->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(i->assets.NativeFile()), 32L << 20);
                *out = i->cache;
                return *out != nullptr;
            });
        };
        s.shade_load = [](void *v, const MMReadStream *stream) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                return Material_MTRL::FSetShadeTable(*stream);
            });
        };
        s.publish = [](void *v, void **p) {
            auto &h = *static_cast<SessionHost *>(v);
            auto old = h.preview->_impl.release();
            h.preview->_impl.reset(static_cast<Impl *>(*p));
            *p = old;
        };
        s.candidate_release = [](void *, void *p) { delete static_cast<Impl *>(p); };
        s.same_path = [](void *, void *p, const void *path) -> int {
            return static_cast<Source *>(p)->archive.Path() == *static_cast<const std::string *>(path);
        };
        s.source_new = [](void *v, void **out) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                *out = new Source;
                return 1;
            });
        };
        s.source_open = [](void *v, void *p, const void *path) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                return static_cast<Source *>(p)->archive.OpenForPreview(*static_cast<const std::string *>(path), *h.error);
            });
        };
        s.source_budget = [](void *v, void *p) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                auto &a = static_cast<Source *>(p)->archive;
                return WithinBudget(a, *h.error);
            });
        };
        s.source_cache_new = [](void *v, void *p) -> int {
            auto &h = *static_cast<SessionHost *>(v);
            return h.Guard([&] {
                auto i = static_cast<Source *>(p);
                if (!i->archive.PrepareNativeFile(*h.error))
                    return false;
                i->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(i->archive.NativeFile()), 32L << 20);
                return i->cache != nullptr;
            });
        };
        s.source_release = [](void *, void *p) { delete static_cast<Source *>(p); };
        s.clear_asset = [](void *v) { static_cast<SessionHost *>(v)->impl->ClearAsset(); };
        s.dispose_live = [](void *v) {
            auto &h = *static_cast<SessionHost *>(v);
            MMPreviewLiveServices live = {};
            live.actor_release = [](void *, void *a) { delete static_cast<Internal::PreviewActor *>(a); };
            mm_preview_live_dispose(&h.impl->live, &live);
        };
        s.cache_release = [](void *v, void *p) {
            auto &h = *static_cast<SessionHost *>(v);
            auto value = static_cast<PChunkyResourceFile>(p);
            ReleasePpo(&value);
            h.impl->cache = nullptr;
        };
        s.shared_material_release = [](void *) { Body::ReleaseSharedMaterial(); };
        s.shade_release = [](void *) { Material_MTRL::ReleaseShadeTable(); };
        s.depth_end = [](void *) { BrZbEnd(); };
        s.runtime_end = [](void *) { BrEnd(); };
        return s;
    }
};
PChunkyResourceFile Preview::Impl::Resolve(const TAGOnFile &tag)
{
    const auto contains = [](void *context, void *source, MMU32 type, MMU32 number) -> int {
        auto file = source ? static_cast<Source *>(source)->cache : static_cast<Impl *>(context)->cache;
        return file->Pcfl()->FFind(type, number);
    };
    void *source;
    if (mm_preview_session_resolve(&session, tag.sid, tag.ctg, tag.cno, contains, this, &source))
        return source ? static_cast<Source *>(source)->cache : cache;
    DescribeMissingResource(tag);
    return nullptr;
}
bool Preview::Impl::ResolveSoundGraph(const TAGOnFile &tag, MMMovieSessionGraph &graph)
{
    const auto readGraph = [](void *context, void *source, MMMovieSessionGraph *out) {
        auto &archive = source ? static_cast<Source *>(source)->archive : static_cast<Impl *>(context)->assets;
        *out = archive.PlaybackGraph();
    };
    if (mm_preview_session_resolve_graph(&session, tag.sid, tag.ctg, tag.cno, readGraph, this, &graph))
        return true;
    DescribeMissingResource(tag);
    return false;
}
void Preview::Impl::DescribeMissingResource(const TAGOnFile &tag)
{
    resolveError = "Missing source " + std::to_string(tag.sid) + ": " + Identity({tag.ctg, tag.cno}) +
                   ". Add the content archive for this source.";
}
Preview::Impl::~Impl()
{
    SessionHost host = {nullptr, this};
    auto services = host.Services();
    mm_preview_session_dispose(&session, &services);
}
#endif
