#ifndef RICH_TEXT_LIVE_STORE_HOST_H
#define RICH_TEXT_LIVE_STORE_HOST_H
#include "rich_text_store_host.h"
#include "mm/text_live.h"
namespace RichTextStoreHost
{
struct LiveHost : Host
{
    DataBlock textBlock;
    DataBlock *active;
    PFileByteStream stream;
    LiveHost(PChunkyFile file, PFileByteStream stream) : Host{file}, active(&block), stream(stream)
    {
    }
    static LiveHost &From(void *p)
    {
        return *static_cast<LiveHost *>(static_cast<Host *>(p));
    }
    MMRichTextLiveServices Services()
    {
        MMRichTextLiveServices s = {};
        s.write = Write();
        s.context = this;
        s.write.add_child = [](void *v, MMU32 root, MMU32 kind, MMU32 bytes) -> int {
            auto &h = From(v);
            return h.Guard([&] {
                h.active = kind == MM_TEXT_BYTES ? &h.textBlock : &h.block;
                ChunkNumber child;
                return h.file->FAddChild(kctgRichText, root, 0, bytes, kind, &child, h.active);
            });
        };
        s.write.write = [](void *v, MMU32 offset, const void *p, MMU32 n) -> int {
            auto &h = From(v);
            return h.Guard([&] { return h.active->FWriteRgb(const_cast<void *>(p), n, offset); });
        };
        s.move_text = [](void *v, MMU32 n) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] { return h.textBlock.FMoveMin(n); });
        };
        s.text = [](void *v) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] { return h.stream->FWriteRgb(&h.textBlock); });
        };
        s.destination = [](void *v, MMTextLocation *out) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] {
                FileLocation location;
                if (!h.textBlock.FGetFlo(&location))
                    return false;
                *out = Location(location);
                return true;
            });
        };
        s.redirect = [](void *v, const MMTextLocation *p, MMI32 bytes) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] {
                auto location = Location(*p);
                return h.stream->FReplaceFlo(&location, false, 0, bytes);
            });
        };
        s.assertion = [](void *, int stage) {
            (void)stage;
            Assert(false, "Failed live rich-text text view");
        };
        s.error = [](void *) { PushErc(ercRtxdSaveFailed); };
        return s;
    }
};
} // namespace RichTextStoreHost
#endif
