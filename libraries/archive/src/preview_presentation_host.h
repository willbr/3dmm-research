#ifndef MM_PREVIEW_PRESENTATION_HOST_H
#define MM_PREVIEW_PRESENTATION_HOST_H
// Typed validated group, header, caption and output providers for C cue traversal.
struct PresentationHost
{
    PChunkyFile file;
    PreviewPresentation *output;
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
    MMPreviewPresentationServices Services()
    {
        static_assert(witNil == 0 && witLim == 4, "Preview pause admission");
        MMPreviewPresentationServices s = {};
        s.context = this;
        s.reset = [](void *v) { *static_cast<PresentationHost *>(v)->output = {}; };
        s.transition = [](void *v, MMI32 t) { static_cast<PresentationHost *>(v)->output->transition = t; };
        s.header = [](void *v, MMU32 number, MMI32 *out) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            return h.Guard([&] {
                SceneData metadata;
                if (!metadata.FRead(h.file, number))
                    return false;
                *out = metadata.transition;
                return true;
            });
        };
        s.group = [](void *v, MMU32 number, int kind, MMPreviewCueGroup *out) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            return h.Guard([&] {
                ChildChunkIdentification child;
                DataBlock block;
                auto parent = kind == MM_PREVIEW_CUE_ACTOR ? kctgActr : kctgScen;
                auto type = kind == MM_PREVIEW_CUE_ACTOR    ? kctgGgae
                            : kind == MM_PREVIEW_CUE_STARTS ? kctgStartGg
                                                            : kctgFrmGg;
                if (!h.file->FGetKidChidCtg(parent, number, kind == MM_PREVIEW_CUE_STARTS ? 1 : 0, type, &child) ||
                    !h.file->FFind(child.cki.ctg, child.cki.cno, &block))
                    return false;
                short order = kboCur;
                auto group = kind == MM_PREVIEW_CUE_ACTOR
                                 ? ActorData::PggReadEvents(&block)
                                 : SceneData::PggReadEvents(&block, &order, kind == MM_PREVIEW_CUE_STARTS);
                if (!group)
                    return false;
                out->handle = group;
                out->count = group->IvMac();
                out->kind = kind;
                out->swap = order == kboOther;
                return true;
            });
        };
        s.release = [](void *, MMPreviewCueGroup *g) {
            auto group = static_cast<PGeneralGroup>(g->handle);
            ReleasePpo(&group);
            g->handle = nullptr;
        };
        s.event = [](void *, const MMPreviewCueGroup *g, MMU32 row, MMI32 *kind, MMI32 *frame) {
            auto group = static_cast<PGeneralGroup>(g->handle);
            if (g->kind == MM_PREVIEW_CUE_ACTOR)
            {
                MMActorEvent event;
                group->GetFixed(row, &event);
                *kind = event.aet;
                *frame = event.nfrm;
            }
            else
            {
                SceneEvent event;
                group->GetFixed(row, &event);
                if (g->swap)
                    SwapBytesBom(&event, kbomSev);
                *kind = event.sevt;
                *frame = event.nfrm;
            }
        };
        s.word = [](void *, const MMPreviewCueGroup *g, MMU32 row, MMU32 word) -> MMU32 {
            MMU32 value;
            static_cast<PGeneralGroup>(g->handle)->GetRgb(row, word * 4, 4, &value);
            if (g->swap)
                SwapBytesRglw(&value, 1);
            return value;
        };
        s.child = [](void *v, MMU32 scene, MMU32 slot, int actor, MMU32 *out) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            ChildChunkIdentification child;
            if (!h.file->FGetKidChidCtg(kctgScen, scene, slot, actor ? kctgActr : kctgTbox, &child))
                return 0;
            *out = child.cki.cno;
            return 1;
        };
        s.actor_header = [](void *v, MMU32 number, MMActorHeader *out) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            return h.Guard([&] {
                DataBlock block;
                ActorChunkOnFile header = {};
                if (!h.file->FFind(kctgActr, number, &block) || !ActorData::FReadHeader(&block, &header))
                    return false;
                static_assert(sizeof(header) == sizeof(*out), "Presentation actor header layout");
                std::memcpy(out, &header, sizeof(header));
                return true;
            });
        };
        s.actor_sound = [](void *, const MMPreviewCueGroup *g, MMU32 row, MMActorSound *out) {
            static_cast<PGeneralGroup>(g->handle)->Get(row, out);
        };
        s.caption = [](void *v, MMU32 number) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            return h.Guard([&] {
                PreviewCaption caption;
                if (!Caption(h.file, number, caption))
                    return false;
                h.output->captions.push_back(std::move(caption));
                return true;
            });
        };
        s.pause = [](void *v, MMI32 frame, MMI32 kind, MMI32 duration) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            return h.Guard([&] {
                h.output->pauses.push_back({frame, kind, duration});
                return 1;
            });
        };
        s.cue = [](void *v, const MMPreviewCue *cue) -> int {
            auto &h = *static_cast<PresentationHost *>(v);
            return h.Guard([&] {
                h.output->sounds.push_back(PreviewCue(*cue));
                return 1;
            });
        };
        return s;
    }
};
#endif
