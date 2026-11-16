/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef TEXT_BOX_RUNTIME_HOST_H
#define TEXT_BOX_RUNTIME_HOST_H
// Native document, scene and view primitives for C89 text-box frame policy.
struct TextBoxRuntimeHost
{
    TextBox *box;
    std::exception_ptr exception;
    const RC *nativeBounds = nullptr;
    MMTextBoxBounds *liveBounds = nullptr;
    int Check(int result)
    {
        if (exception) std::rethrow_exception(exception);
        return result;
    }
    MMTextBoxRuntimeServices Services()
    {
        MMTextBoxRuntimeServices services = {this, Call};
        return services;
    }
    static int Call(void *context, MMTextBoxOperation op, MMTextBoxRequest *q)
    {
        auto &h = *static_cast<TextBoxRuntimeHost *>(context);
        try
        {
            switch (op)
            {
            case MM_TBOX_VIEW_COUNT: q->value = h.box->Cddg(); break;
            case MM_TBOX_VIEW:
                q->object = static_cast<PTextBoxGobject>(h.box->PddgGet(0));
                AssertNilOrPo(static_cast<PTextBoxGobject>(q->object), 0);
                break;
            case MM_TBOX_CREATE_VIEW:
            {
                GraphicsObjectBlock block;
                RC bounds;
                h.box->GetRc(&bounds);
                block.Set(khidDdg, h.box->Pscen()->Pmvie()->PddgActive(), fgobNil, kginMark, &bounds, pvNil);
                q->object = TextBoxBase::PtbxbNew(h.box, &block);
                break;
            }
            case MM_TBOX_PLAYING: q->value = h.box->Pscen()->Pmvie()->FPlaying(); break;
            case MM_TBOX_DEACTIVATE: static_cast<PTextBoxBase>(q->object)->Activate(false); break;
            case MM_TBOX_RELEASE_VIEW:
            {
                auto border = static_cast<PTextBoxGobject>(q->object)->Ptbxb();
                ReleasePpo(&border);
                break;
            }
            case MM_TBOX_DIRTY:
                h.box->Pscen()->MarkDirty(q->value != 0);
                // SetRc reads the caller's rectangle after dirty notification.
                if (h.nativeBounds)
                    *h.liveBounds = {h.nativeBounds->xpLeft, h.nativeBounds->ypTop,
                                     h.nativeBounds->xpRight, h.nativeBounds->ypBottom};
                break;
            case MM_TBOX_SCENE_FRAME: q->value = static_cast<PScene>(q->object)->Nfrm(); break;
            case MM_TBOX_SCENE_LAST: q->value = h.box->Pscen()->NfrmLast(); break;
            case MM_TBOX_IS_SELECTED: q->value = h.box->Pscen()->PtboxSelected() == h.box; break;
            case MM_TBOX_DESELECT: h.box->Pscen()->SelectTbox(pvNil); break;
            case MM_TBOX_INVALIDATE:
                static_cast<PTextBoxGobject>(q->object)->Ptbxb()->InvalRc(pvNil); break;
            case MM_TBOX_ACTIVATE:
                AssertPo(static_cast<PTextBoxGobject>(q->object), 0);
                static_cast<PTextBoxGobject>(q->object)->Ptbxb()->Activate(q->value != 0); break;
            case MM_TBOX_ATTACH_MOUSE:
                static_cast<PTextBoxGobject>(q->object)->Ptbxb()->AttachToMouse(); break;
            case MM_TBOX_NEEDS_SCROLL:
                AssertPo(static_cast<PTextBoxGobject>(q->object), 0);
                q->value = static_cast<PTextBoxGobject>(q->object)->FNeedToScroll(); break;
            case MM_TBOX_SCROLL:
                AssertPo(static_cast<PTextBoxGobject>(q->object), 0);
                static_cast<PTextBoxGobject>(q->object)->Scroll(scaLineDown); break;
            case MM_TBOX_TEXT_SELECTED:
                AssertPo(static_cast<PTextBoxGobject>(q->object), 0);
                q->value = static_cast<PTextBoxGobject>(q->object)->FTextSelected(); break;
            case MM_TBOX_DIAGNOSTIC:
                switch (q->value)
                {
                case MM_TBOX_MULTIPLE_VIEWS: Assert(false, "Multiple views on text boxes not allowed"); break;
                case MM_TBOX_FRAME_OVERFLOW: Assert(false, "Current frame too big"); break;
                case MM_TBOX_GOTO_FAILED: Assert(false, "Could not goto frame"); break;
                }
                break;
            }
            return 1;
        }
        catch (const std::bad_alloc &) { h.exception = std::current_exception(); return -1; }
        catch (...) { h.exception = std::current_exception(); return -2; }
    }
};
#endif
