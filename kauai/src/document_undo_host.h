#ifndef DOCUMENT_UNDO_HOST_H
#define DOCUMENT_UNDO_HOST_H
struct DocumentBase::UndoHost : DocumentStorageHost
{
    DocumentBase *doc;
    explicit UndoHost(DocumentBase *p) : DocumentStorageHost(&p->_undoList.storage), doc(p)
    {
    }
    MMDocumentUndoServices Services()
    {
        MMDocumentUndoServices s = {};
        s.context = this;
        s.status = [](void *v) -> int { return static_cast<UndoHost *>(v)->status; };
        s.replay = [](void *v, void *record, int redo) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                auto p = static_cast<PUndoBase>(record);
                return redo ? p->FDo(h.doc) : p->FUndo(h.doc);
            });
        };
        s.retain = [](void *v, void *p) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                static_cast<PUndoBase>(p)->AddRef();
                return 1;
            });
        };
        s.release = [](void *v, void *p) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                static_cast<PUndoBase>(p)->Release();
                return 1;
            });
        };
        s.clear_redo = [](void *v) -> int {
            auto &h = *static_cast<UndoHost *>(v);
            return h.Guard([&] {
                h.doc->ClearRedo();
                return 1;
            });
        };
        s.validate = [](void *v) { AssertPo(static_cast<UndoHost *>(v)->doc, fobjAssertFull); };
        s.diagnostic = [](void *) { Assert(false, 0); };
        return s;
    }
};
#endif
