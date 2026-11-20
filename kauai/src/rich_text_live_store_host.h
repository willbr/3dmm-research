#ifndef RICH_TEXT_LIVE_STORE_HOST_H
#define RICH_TEXT_LIVE_STORE_HOST_H
#include "rich_text_store_host.h"
#include "mm/text_live.h"
namespace RichTextStoreHost
{
struct LiveHost : Host
{
    MovieArchiveHost textArchive;
    MMMovieArchiveEditor textEditor;
    PFileByteStream stream;
    LiveHost(PChunkyFile file, PFileByteStream stream)
        : Host(file), textArchive{file, {}, {}}, textEditor(textArchive.Editor()), stream(stream)
    {
    }
    MMRichTextLiveServices Services()
    {
        MMRichTextLiveServices s = {};
        s.write = Write(&textEditor);
        s.context = this;
        s.move_text = [](void *v, MMU32 n) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] { return h.textArchive.output.FMoveMin(n); });
        };
        s.text = [](void *v) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] { return h.stream->FWriteRgb(&h.textArchive.output); });
        };
        s.destination = [](void *v, MMTextLocation *out) -> int {
            auto &h = *static_cast<LiveHost *>(v);
            return h.Guard([&] {
                FileLocation location;
                if (!h.textArchive.output.FGetFlo(&location))
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
