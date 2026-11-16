#ifndef DOCUMENT_UNDO_HOST_H
#define DOCUMENT_UNDO_HOST_H
struct DocumentBase::UndoHost
{
    DocumentBase *doc;
    MMStorageAllocator oldAllocator, provider;
    std::exception_ptr exception;
    int status = 1;
    explicit UndoHost(DocumentBase *p)
        : doc(p), oldAllocator(p->_undoList.storage.allocator), provider(Group::CollectionAllocator())
    {
        // Each nested operation binds its own exception boundary to the same
        // primitive allocator, restoring the previous binding before returning.
        doc->_undoList.storage.allocator = {
            this,
            [](void *v, void *data, MMU32 oldBytes, MMU32 newBytes, int priority) -> void * {
                auto &h = *static_cast<UndoHost *>(v);
                if (h.status < 0)
                    return nullptr;
                void *out = nullptr;
                h.Guard([&] {
                    out = h.provider.resize(h.provider.context, data, oldBytes, newBytes, priority);
                    return 1;
                });
                return out;
            },
            [](void *v, void *data, MMU32 bytes) {
                auto &h = *static_cast<UndoHost *>(v);
                h.Guard([&] {
                    h.provider.release(h.provider.context, data, bytes);
                    return 1;
                });
            }};
    }
    ~UndoHost()
    {
        doc->_undoList.storage.allocator = oldAllocator;
    }
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            status = -1;
            return status;
        }
        catch (...)
        {
            exception = std::current_exception();
            status = -2;
            return status;
        }
    }
    void Rethrow()
    {
        if (exception)
            std::rethrow_exception(exception);
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
