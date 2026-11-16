#ifndef DOCUMENT_LIFECYCLE_HOST_H
#define DOCUMENT_LIFECYCLE_HOST_H
struct DocumentBase::LifecycleHost
{
    std::exception_ptr exception;
    Filename name;
    Filename *lookup = nullptr;
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
    void Rethrow()
    {
        if (exception)
            std::rethrow_exception(exception);
    }
    static DocumentBase *Owner(MMDocumentNode *p)
    {
        return static_cast<DocumentBase *>(p->owner);
    }
    MMDocumentLifecycleServices Services()
    {
        MMDocumentLifecycleServices s = {};
        s.context = this;
        s.drop_reference = [](void *, MMDocumentNode *n) -> MMI32 { return --Owner(n)->_cactRef; };
        s.retain = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                Owner(n)->AddRef();
                return 1;
            });
        };
        s.release = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                Owner(n)->Release();
                return 1;
            });
        };
        s.destroy = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                delete Owner(n);
                return 1;
            });
        };
        s.dispose_undo = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                auto doc = Owner(n);
                UndoHost undo(doc);
                auto services = undo.Services();
                int result = mm_document_undo_dispose(&doc->_undoList, &services);
                undo.Rethrow();
                return result;
            });
        };
        s.views_present = [](void *, MMDocumentNode *n) -> int { return Owner(n)->_pglpddg != nullptr; };
        s.view_count = [](void *, MMDocumentNode *n) -> MMI32 { return Owner(n)->Cddg(); };
        s.view = [](void *, MMDocumentNode *n, MMI32 i) -> void * {
            PDocumentDisplayGraphicsObject view;
            Owner(n)->_pglpddg->Get(i, &view);
            return view;
        };
        s.window = [](void *, void *p) -> void * {
            auto view = static_cast<PDocumentDisplayGraphicsObject>(p);
            AssertBasePo(view, 0);
            return view->Pdmd();
        };
        s.release_view = [](void *v, void *p) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                static_cast<BASE *>(p)->Release();
                return 1;
            });
        };
        s.close_views = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                Owner(n)->CloseAllDdg();
                return 1;
            });
        };
        s.dispose_views = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                ReleasePpo(&Owner(n)->_pglpddg);
                return 1;
            });
        };
        s.clipboard = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return vpclip->FDocIsClip(Owner(n)); });
        };
        s.clear_clipboard = [](void *v) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                vpclip->Set();
                return 1;
            });
        };
        s.dirty = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FDirty(); });
        };
        s.query_save = [](void *v, MMDocumentNode *n, int force) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->_TQuerySave(force != 0); });
        };
        s.save = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FSave(); });
        };
        s.query_close = [](void *v, MMDocumentNode *n, MMU32 flags) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FQueryClose(flags); });
        };
        s.match_name = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FGetFni(&h.name) && h.name.FEqual(h.lookup); });
        };
        s.current_name = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FGetFni(&h.name); });
        };
        s.choose_name = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FGetFniSave(&h.name); });
        };
        s.write_name = [](void *v, MMDocumentNode *n, int adopt) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] { return Owner(n)->FSaveToFni(&h.name, adopt != 0); });
        };
        s.update_name = [](void *v, MMDocumentNode *n) -> int {
            auto &h = *static_cast<LifecycleHost *>(v);
            return h.Guard([&] {
                Owner(n)->UpdateName();
                return 1;
            });
        };
        s.save_error = [](void *) { PushErc(ercCantSave); };
        s.diagnostic = [](void *, int kind) {
            switch (kind)
            {
            case MM_DOC_OPEN_VIEWS:
                Assert(false, "why are there still DocumentDisplayGraphicsObject's open on this DocumentBase?");
                break;
            case MM_DOC_RECURSIVE_RELEASE:
                Assert(false, "we're recursing into the DocumentBase::Release!");
                break;
            case MM_DOC_CHILD_RETAINED:
                Bug("why wasn't this child doc released?");
                break;
            case MM_DOC_CLIPBOARD_RELEASE:
                Bug("The clipboard document is going away!");
                break;
            case MM_DOC_CORRUPT_TREE:
                Bug("corrupt docb tree");
                break;
            case MM_DOC_NO_VIEWS:
                Bug("why are there no DDGs for this doc if there's a DocumentMDIWindow?");
                break;
            default:
                Bug("why are we here?");
                break;
            }
        };
        return s;
    }
};
#endif
