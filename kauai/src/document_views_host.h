#ifndef DOCUMENT_VIEWS_HOST_H
#define DOCUMENT_VIEWS_HOST_H
struct DocumentBase::ViewHost : DocumentStorageHost
{
    DocumentBase *doc;
    PDocumentDisplayGraphicsObject view = nullptr;
    Filename file;
    String title;
    PString output = nullptr;
    explicit ViewHost(DocumentBase *p, bool bind = false)
        : DocumentStorageHost(bind ? &p->_viewList.storage : nullptr), doc(p)
    {
    }
    static DocumentBase *Owner(MMDocumentNode *n)
    {
        return static_cast<DocumentBase *>(n->owner);
    }
    MMDocumentViewServices Services()
    {
        MMDocumentViewServices s = {};
        s.context = this;
        s.status = [](void *v) -> int { return static_cast<ViewHost *>(v)->status; };
        s.validate = [](void *v) { AssertPo(static_cast<ViewHost *>(v)->doc, fobjAssertFull); };
        s.missing = [](void *v) { BugVar("pddg not in docb's list", &static_cast<ViewHost *>(v)->view); };
        s.get = [](void *, MMDocumentNode *n, MMI32 i) -> void * { return Owner(n)->PddgGet(i); };
        s.active = [](void *v, void *p) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] { return static_cast<PDocumentDisplayGraphicsObject>(p)->FActive(); });
        };
        s.window = [](void *, void *p) -> void * { return static_cast<PDocumentDisplayGraphicsObject>(p)->Pdmd(); };
        s.activate_window = [](void *v, void *p) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] {
                GraphicsObject::MakeHwndActive(static_cast<PDocumentMDIWindow>(p)->HwndContainer());
                return 1;
            });
        };
        s.read_name = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] {
                Owner(n)->GetName(&h.title);
                return 1;
            });
        };
        s.set_title = [](void *v, void *p) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] {
                static_cast<PDocumentMDIWindow>(p)->SetHwndName(&h.title);
                return 1;
            });
        };
        s.clipboard = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] { return vpclip->FDocIsClip(Owner(n)); });
        };
        s.current_name = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] { return Owner(n)->FGetFni(&h.file); });
        };
        s.emit_name = [](void *v, int kind, MMI32 number) -> int {
            auto &h = *static_cast<ViewHost *>(v);
            return h.Guard([&] {
                if (kind == MM_DOC_NAME_CLIPBOARD)
                    *h.output = PszLit("Clipboard");
                else if (kind == MM_DOC_NAME_FILE)
                    h.file.GetLeaf(h.output);
                else
                    h.output->FFormatSz(PszLit("Untitled %d"), static_cast<LegacyLong>(number));
                return 1;
            });
        };
        return s;
    }
};
#endif
