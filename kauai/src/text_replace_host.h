#ifndef TEXT_REPLACE_HOST_H
#define TEXT_REPLACE_HOST_H
#include "mm/text_replace.h"
struct TextDocumentBase::ReplaceHost : DocumentHost
{
    explicit ReplaceHost(TextDocumentBase *p) : DocumentHost(p)
    {
    }
    MMTextReplaceBaseServices Services()
    {
        MMTextReplaceBaseServices s = {};
        s.context = this;
        s.objects = Objects();
        s.translate = [](void *v, MMTextLocation *p, int system) -> int {
            auto &h = *static_cast<ReplaceHost *>(v);
            auto base = h.Base();
            return base.translate(base.context, p, system);
        };
        s.pieces = Pieces;
        s.invalidate = [](void *v, MMI32 at, MMI32 ins, MMI32 del) -> int {
            auto &h = *static_cast<ReplaceHost *>(v);
            return h.Guard([&] {
                h.doc->_InvalCache(at, ins, del);
                return 1;
            });
        };
        s.validate = [](void *v) { AssertPo(static_cast<ReplaceHost *>(v)->doc, fobjAssertFull); };
        s.notify = [](void *v, MMI32 at, MMI32 ins, MMI32 del, MMU32 flags) -> int {
            auto &h = *static_cast<ReplaceHost *>(v);
            return h.Guard([&] {
                h.doc->InvalAllDdg(at, ins, del, flags);
                return 1;
            });
        };
        return s;
    }
    static MMTextReplaceRequest Request(int kind, void *data, MMI32 ins, MMI32 at, MMI32 del, MMU32 flags)
    {
        MMTextReplaceRequest q = {};
        q.kind = kind;
        q.data = data;
        q.inserted = ins;
        q.at = at;
        q.deleted = del;
        q.flags = flags;
        return q;
    }
};
struct RichTextDocument::ReplaceHost : FormatHost
{
    TextDocumentBase::ReplaceHost base;
    explicit ReplaceHost(RichTextDocument *p) : FormatHost(p), base(p)
    {
    }
    void Rethrow()
    {
        base.Rethrow();
        FormatHost::Rethrow();
    }
    static MMTextReplaceRequest Request(int kind, void *data, MMI32 ins, MMI32 at, MMI32 del, MMU32 flags)
    {
        return TextDocumentBase::ReplaceHost::Request(kind, data, ins, at, del, flags);
    }
    int Call(const MMTextReplaceRequest &q, bool qualified)
    {
        return Guard([&] {
            if (q.kind == MM_TEXT_REPLACE_LOCATION)
            {
                auto location = RichTextStoreHost::Location(q.location);
                return qualified ? rich->TextDocumentBase::FReplaceFlo(&location, q.copy != 0, q.at, q.deleted,
                                                                       static_cast<short>(q.system), q.flags)
                                 : rich->FReplaceFlo(&location, q.copy != 0, q.at, q.deleted,
                                                     static_cast<short>(q.system), q.flags);
            }
            if (q.kind == MM_TEXT_REPLACE_STREAM)
            {
                auto stream = static_cast<PFileByteStream>(q.stream);
                return qualified
                           ? rich->TextDocumentBase::FReplaceBsf(stream, q.source, q.inserted, q.at, q.deleted, q.flags)
                           : rich->FReplaceBsf(stream, q.source, q.inserted, q.at, q.deleted, q.flags);
            }
            return qualified ? rich->TextDocumentBase::FReplaceRgch(const_cast<void *>(q.data), q.inserted, q.at,
                                                                    q.deleted, q.flags)
                             : rich->FReplaceRgch(const_cast<void *>(q.data), q.inserted, q.at, q.deleted, q.flags);
        });
    }
    MMTextReplaceRichServices Services()
    {
        MMTextReplaceRichServices s = {};
        s.context = this;
        s.base = base.Services();
        s.edit = Edit();
        s.has_character = chp != nullptr;
        s.has_paragraph = pap != nullptr;
        s.characters = [](void *v) -> MMI32 { return static_cast<ReplaceHost *>(v)->rich->CpMac(); };
        s.replace_base = [](void *v, const MMTextReplaceRequest *q) -> int {
            return static_cast<ReplaceHost *>(v)->Call(*q, true);
        };
        s.replace_virtual = [](void *v, const MMTextReplaceRequest *q) -> int {
            return static_cast<ReplaceHost *>(v)->Call(*q, false);
        };
        s.adjust = [](void *v, MMI32 at, MMI32 ins, MMI32 del) -> int {
            auto &h = *static_cast<ReplaceHost *>(v);
            return h.Guard([&] {
                h.rich->_AdjustMpe(at, ins, del);
                return 1;
            });
        };
        s.too_much = [](void *) { PushErc(ercRtxdTooMuchText); };
        s.validate_apply = [](void *v, MMI32 at, MMI32 span) {
            auto doc = static_cast<ReplaceHost *>(v)->rich;
            AssertIn(at, 0, doc->CpMac());
            AssertIn(span, 1, doc->CpMac() + 1 - at);
        };
        s.diagnostic = [](void *, int kind) {
            if (kind == MM_TEXT_REPLACE_BAD_LOCATION)
            {
                Assert(false, "bad flo");
            }
            else if (kind == MM_TEXT_REPLACE_BAD_SOURCE)
            {
                Assert(false, "bad cpSrc");
            }
            else if (kind == MM_TEXT_REPLACE_PARA_BEFORE)
            {
                Assert(false, "new para before old one");
            }
            else
            {
                Assert(false, "new para after old one");
            }
        };
        s.notify = [](void *v, MMI32 at, MMI32 ins, MMI32 del, MMU32 flags) -> int {
            auto &h = *static_cast<ReplaceHost *>(v);
            return h.Guard([&] {
                h.rich->InvalAllDdg(at, ins, del, flags);
                return 1;
            });
        };
        return s;
    }
};
#endif
