#ifndef TEXT_COPY_HOST_H
#define TEXT_COPY_HOST_H
#include "mm/text_copy.h"
struct RichTextDocument::CopyHost : ReplaceHost
{
    RichTextDocument *from;
    PropertyHost source;
    long *position = nullptr, *byteCount = nullptr, *x = nullptr, y = 0;
    PGraphicsEnvironment graphics = nullptr;
    PCHP objectChp = nullptr;
    RC *rect = nullptr, *clip = nullptr;
    explicit CopyHost(RichTextDocument *p, RichTextDocument *src = nullptr)
        : ReplaceHost(p), from(src ? src : p), source(src ? src : p)
    {
    }
    MMTextCopyServices Copy()
    {
        MMTextCopyServices s = {};
        s.context = this;
        s.target = Services();
        s.source = source.Properties();
        s.source_characters = [](void *v) -> MMI32 { return static_cast<CopyHost *>(v)->from->CpMac(); };
        s.source_arguments = [](void *v) { return static_cast<CopyHost *>(v)->source.ArgumentRows(); };
        s.source_stream = [](void *v) -> void * { return static_cast<CopyHost *>(v)->from->_pbsf; };
        s.check_tail = [](void *v, MMI32 cp, MMI32 inserted, MMI32 deleted) -> int {
            auto &h = *static_cast<CopyHost *>(v);
            return h.Guard([&] {
#ifdef DEBUG
                achar ch;
                h.from->FetchRgch(cp, 1, &ch);
                Assert(ch == kchReturn, "trying to replace trailing EOP");
#endif
                Assert(deleted > 0 && inserted > 0, "bad parameters to FReplaceTxrd");
                return 1;
            });
        };
        s.check_properties = [](void *v, MMI32 from, MMI32 at, MMI32 src, MMI32 dst) {
            auto &h = *static_cast<CopyHost *>(v);
            AssertPo(h.rich, 0);
            AssertPo(h.from, 0);
            AssertIn(from, 0, h.from->CpMac());
            AssertIn(at, 0, h.rich->CpMac());
            AssertIn(src, 1, h.from->CpMac() + 1 - from);
            AssertIn(dst, 1, h.rich->CpMac() + 1 - at);
        };
        s.warning = [](void *, int kind) {
            if (kind == MM_TEXT_COPY_GROW)
            {
                Warn("growing _pglmpe failed");
            }
            else
            {
                Warn("ensure in ag failed");
            }
        };
        return s;
    }
    MMTextObjectServices ObjectServices()
    {
        MMTextObjectServices s = {};
        s.context = this;
        s.unit = sizeof(achar);
        s.target = Services();
        s.arguments = [](void *v) { return static_cast<CopyHost *>(v)->ArgumentRows(); };
        s.allocate = [](void *v, MMI32 bytes, void **out) -> int {
            auto &h = *static_cast<CopyHost *>(v);
            return h.Guard([&] { return FAllocPv(out, bytes, fmemNil, mprNormal); });
        };
        s.free = [](void *, void *p) { FreePpv(&p); };
        s.publish_at = [](void *v, MMI32 at) {
            auto &h = *static_cast<CopyHost *>(v);
            if (h.position)
                *h.position = at;
        };
        s.publish_bytes = [](void *v, MMI32 bytes) {
            auto &h = *static_cast<CopyHost *>(v);
            if (h.byteCount)
                *h.byteCount = bytes;
        };
        s.dispatch = [](void *v, int draw, MMI32 index, unsigned kind) -> int {
            auto &h = *static_cast<CopyHost *>(v);
            return h.Guard([&] {
                return draw ? h.rich->_FDrawObject(index, static_cast<byte>(kind), h.graphics, h.x, h.y, h.objectChp,
                                                   h.clip)
                            : h.rich->_FGetObjectRc(index, static_cast<byte>(kind), h.graphics, h.objectChp, h.rect);
            });
        };
        s.diagnostic = [](void *, int kind) {
            if (kind == MM_TEXT_OBJECT_NOT_AT)
            {
                Bug("cp not an object");
            }
            else
            {
                Assert(false, "should have been ensured");
            }
        };
        return s;
    }
};
#endif
