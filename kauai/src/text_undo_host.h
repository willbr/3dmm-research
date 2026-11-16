#ifndef TEXT_UNDO_HOST_H
#define TEXT_UNDO_HOST_H
#include "mm/text_undo.h"
struct RichTextDocument::UndoHost : TextDocumentFactoryHost
{
    RichTextDocument *doc;
    explicit UndoHost(RichTextDocument *p) : doc(p)
    {
    }
    MMTextUndoTransactionServices Services()
    {
        MMTextUndoTransactionServices s = {};
        s.context = this;
        s.control = &doc->_textUndo;
        s.maximum = [](void *v) -> MMI32 { return static_cast<UndoHost *>(v)->doc->_cundbMax; };
        s.done = [](void *v) -> MMI32 { return static_cast<UndoHost *>(v)->doc->_ipundbLimDone; };
        s.resume = [](void *v) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                h.doc->ResumeUndo();
                return 1;
            });
        };
        s.create = [](void *v, MMI32 combine, MMI32 first, MMI32 end, MMI32 inserted, void **out) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                *out = RichTextUndo::PrtunNew(combine, h.doc, first, end, inserted);
                return *out != nullptr;
            });
        };
        s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
        s.previous = [](void *v, MMI32 i) -> void * {
            PRichTextUndo p;
            static_cast<UndoHost *>(v)->doc->_pglpundb->Get(i, &p);
            return p;
        };
        s.is_record = [](void *, void *p) -> int { return static_cast<PUndoBase>(p)->FIs(kclsRichTextUndo); };
        s.combine = [](void *v, void *previous, void *pending) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard(
                [&] { return static_cast<PRichTextUndo>(previous)->FCombine(static_cast<PRichTextUndo>(pending)); });
        };
        s.clear_redo = [](void *v) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                h.doc->ClearRedo();
                return 1;
            });
        };
        s.add = [](void *v, void *p) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] { return h.doc->FAddUndo(static_cast<PRichTextUndo>(p)); });
        };
        s.check_pending = [](void *, void *p) { AssertPo(static_cast<PRichTextUndo>(p), 0); };
        s.diagnostic = [](void *, int) { Assert(false, "why is there an active undo record?"); };
        return s;
    }
};
struct RichTextUndo::UndoHost : TextDocumentFactoryHost
{
    MMTextUndoRecordServices Services()
    {
        MMTextUndoRecordServices s = {};
        s.context = this;
        s.allocate = [](void *v, void **out) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                *out = NewObj RichTextUndo;
                return *out != nullptr;
            });
        };
        s.record = [](void *, void *p) -> MMTextUndoRecord * { return &static_cast<PRichTextUndo>(p)->_undo; };
        s.document = [](void *v, void **out) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                *out = RichTextDocument::PtxrdNew();
                return *out != nullptr;
            });
        };
        s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
        s.characters = [](void *, void *p) -> MMI32 { return static_cast<PRichTextDocument>(p)->CpMac(); };
        s.accept = [](void *, void *p) -> int { return static_cast<PDocumentBase>(p)->FIs(kclsRichTextDocument); };
        s.internal = [](void *v, void *p) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                static_cast<PRichTextDocument>(p)->SetInternal();
                return 1;
            });
        };
        s.suspend = [](void *v, void *p) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                static_cast<PRichTextDocument>(p)->SuspendUndo();
                return 1;
            });
        };
        s.resume = [](void *v, void *p) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                static_cast<PRichTextDocument>(p)->ResumeUndo();
                return 1;
            });
        };
        s.copy = [](void *v, void *dst, void *src, MMI32 from, MMI32 count, MMI32 at, MMI32 deleted) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                return static_cast<PRichTextDocument>(dst)->FReplaceTxrd(static_cast<PRichTextDocument>(src), from,
                                                                         count, at, deleted);
            });
        };
        s.remove = [](void *v, void *p, MMI32 at, MMI32 count) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] { return static_cast<PRichTextDocument>(p)->FReplaceRgch(nullptr, 0, at, count); });
        };
        s.selection = [](void *v, void *p, int kind, MMI32 first, MMI32 end) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            auto doc = static_cast<PRichTextDocument>(p);
            return h.Guard([&] {
                if (kind == MM_TEXT_UNDO_HIDE)
                    doc->HideSel();
                else if (kind == MM_TEXT_UNDO_SHOW)
                    doc->ShowSel();
                else if (kind == MM_TEXT_UNDO_SELECT_QUIET)
                    doc->SetSel(first, end, ginNil);
                else
                    doc->SetSel(first, end);
                return 1;
            });
        };
        s.check_saved = [](void *, void *p) { AssertPo(static_cast<PRichTextDocument>(p), 0); };
        s.check_range = [](void *, void *p, MMI32 first) {
            AssertIn(first, 0, static_cast<PRichTextDocument>(p)->CpMac());
        };
        s.diagnostic = [](void *, int) { Bug("This rich text undo object cannot be applied to this document"); };
        return s;
    }
};
#endif
