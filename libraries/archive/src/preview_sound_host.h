#ifndef MM_PREVIEW_SOUND_HOST_H
#define MM_PREVIEW_SOUND_HOST_H
// Typed source/chunk/output primitives for the C sound remapping controller.
struct Preview::Impl::SoundHost
{
    Impl *impl;
    PreviewAudio *output;
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
    MMPreviewSoundServices Services()
    {
        MMPreviewSoundServices s = {};
        s.context = this;
        s.reset = [](void *v) { *static_cast<SoundHost *>(v)->output = {}; };
        s.silent = [](void *v) { static_cast<SoundHost *>(v)->output->silent = true; };
        s.source = [](void *v, MMI32 source) { static_cast<SoundHost *>(v)->output->source = source; };
        s.child = [](void *v, MMU32 type, MMU32 number, MMU32 slot, MMU32 *out) -> int {
            auto &h = *static_cast<SoundHost *>(v);
            ChildChunkIdentification child;
            if (!h.impl->cache || !h.impl->cache->Pcfl()->FGetKidChidCtg(type, number, slot, kctgMsnd, &child))
                return 0;
            *out = child.cki.cno;
            return 1;
        };
        s.chunks = [](void *v) -> MMU32 {
            return static_cast<MMU32>(static_cast<SoundHost *>(v)->impl->assets.Chunks().size());
        };
        s.chunk = [](void *v, MMU32 i, MMU32 *type, MMU32 *number) {
            const auto &chunk = static_cast<SoundHost *>(v)->impl->assets.Chunks()[i];
            *type = chunk.id.type;
            *number = chunk.id.number;
        };
        s.resolve = [](void *v, MMI32 sid, MMU32 type, MMU32 number, void **file) -> int {
            auto &h = *static_cast<SoundHost *>(v);
            return h.Guard([&] {
                TAGOnFile tag = {sid, 0, type, number};
                auto source = h.impl->Resolve(tag);
                *file = source ? source->Pcfl() : nullptr;
                return source != nullptr;
            });
        };
        s.audio = [](void *v, void *file, MMU32 type, MMU32 number) -> int {
            auto &h = *static_cast<SoundHost *>(v);
            return h.Guard(
                [&] { return Internal::ReadAudio(static_cast<PChunkyFile>(file), type, number, *h.output); });
        };
        return s;
    }
};
#endif
