/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef TEXT_BOX_EDIT_HOST_H
#define TEXT_BOX_EDIT_HOST_H
// Typed allocation, document, scene and view primitives. Policy lives in C89.
struct TextBoxEditHost
{
    PTBOX box = nullptr;
    PScene scene = nullptr;
    const MMMovieArchiveEditor *editor = nullptr;
    MMChunkOwnerRead *reader = nullptr;
    const RC *bounds = nullptr;
    PTBOX *boxOutput = nullptr;
    PTextBoxUndoEdit *undoOutput = nullptr;
    PTextBoxClipboard *clipboardOutput = nullptr;
    PCHP characterOutput = nullptr;
    ulong *maskOutput = nullptr;
    std::exception_ptr exception;
    int Check(int result) { if (exception) std::rethrow_exception(exception); return result; }
    static MMTextCharacter Character(const CHP &c)
    {
        return {c.grfont, c.onn, static_cast<MMI32>(c.dypFont), static_cast<MMI32>(c.dypOffset),
                static_cast<MMU32>(c.acrFore.LwGet()), static_cast<MMU32>(c.acrBack.LwGet())};
    }
    static CHP Character(const MMTextCharacter &c)
    {
        CHP value;
        value.grfont = c.style; value.onn = c.font; value.dypFont = c.height; value.dypOffset = c.offset;
        value.acrFore.SetFromLw(c.foreground); value.acrBack.SetFromLw(c.background);
        return value;
    }
    static RC Bounds(const MMTextBoxBounds &b) { return RC(b.left, b.top, b.right, b.bottom); }
    static MMTextBoxBounds Bounds(const RC &b) { return {b.xpLeft, b.ypTop, b.xpRight, b.ypBottom}; }
    static MMTextBoxUndo *Undo(void *, void *object)
    {
        auto p = static_cast<PMovieUndo>(object);
        if (p->FIs(kclsTextBoxUndoType)) return static_cast<PTextBoxUndoType>(p)->Backend();
        if (p->FIs(kclsTextBoxUndoSize)) return static_cast<PTextBoxUndoSize>(p)->Backend();
        if (p->FIs(kclsTextBoxUndoHide)) return static_cast<PTextBoxUndoHide>(p)->Backend();
        if (p->FIs(kclsTextBoxUndoEdit)) return static_cast<PTextBoxUndoEdit>(p)->Backend();
        return static_cast<PTextBoxUndoColor>(p)->Backend();
    }
    MMTextBoxEditServices Services()
    {
        MMTextBoxEditServices s = {};
        s.context = this; s.call = Call;
        s.state = [](void *, void *p) { return &static_cast<PTBOX>(p)->_data; };
        s.runtime = [](void *, void *p) { return &static_cast<PTBOX>(p)->_runtime; };
        s.undo = Undo;
        s.clipboard = [](void *, void *p) { return &static_cast<PTextBoxClipboard>(p)->_clipboard; };
        s.black = static_cast<MMU32>(kacrBlack.LwGet()); s.clear = static_cast<MMU32>(kacrClear.LwGet());
        s.update = fdocUpdate;
        return s;
    }
    static int Call(void *context, MMTextBoxEditOperation op, MMTextBoxEditRequest *q)
    {
        auto &h = *static_cast<TextBoxEditHost *>(context);
        try
        {
            switch (op)
            {
            case MM_BOX_ALLOCATE: q->object = NewObj TextBox; break;
            case MM_BOX_INITIALIZE: return static_cast<PTBOX>(q->object)->_FInit(nullptr);
            case MM_BOX_RELEASE: static_cast<BASE *>(q->object)->Release(); break;
            case MM_BOX_RETAIN: static_cast<BASE *>(q->object)->AddRef(); break;
            case MM_BOX_INPUT_BOUNDS:
                q->value = h.bounds != nullptr;
                if (h.bounds) q->bounds = Bounds(*h.bounds);
                break;
            case MM_BOX_DEFAULTS:
                q->value = static_cast<PTBOX>(q->object)->_onnDef;
                q->second = static_cast<PTBOX>(q->object)->_dypFontDef;
                break;
            case MM_BOX_SET_DEFAULTS:
                static_cast<PTBOX>(q->object)->_onnDef = q->value;
                static_cast<PTBOX>(q->object)->_dypFontDef = q->second;
                break;
            case MM_BOX_SET_WIDTH: static_cast<PTBOX>(q->object)->_dxpDef = q->value; break;
            case MM_BOX_BACKGROUND: q->word = static_cast<MMU32>(static_cast<PTBOX>(q->object)->AcrBack().LwGet()); break;
            case MM_BOX_SET_BACKGROUND:
            {
                AbstractColor color; color.SetFromLw(q->word);
                static_cast<PTBOX>(q->object)->SetAcrBack(color, q->flags); break;
            }
            case MM_BOX_UNDO_MAXIMUM: q->value = static_cast<PScene>(q->other)->Pmvie()->CundbMax(); break;
            case MM_BOX_SET_UNDO_LIMIT: static_cast<PTBOX>(q->object)->SetCundbMax(q->value); break;
            case MM_BOX_PUBLISH:
                if (h.boxOutput) *h.boxOutput = static_cast<PTBOX>(q->object);
                if (h.undoOutput) *h.undoOutput = static_cast<PTextBoxUndoEdit>(q->object);
                if (h.clipboardOutput) *h.clipboardOutput = static_cast<PTextBoxClipboard>(q->object);
                break;
            case MM_BOX_READ_TEXT:
                return static_cast<PTBOX>(q->object)->_FReadArchive(*h.editor, h.reader, q->chunk.type,
                                                                 q->chunk.number, true);
            case MM_BOX_DIAGNOSTIC:
                switch (q->value)
                {
                case MM_BOX_BAD_FILE: PushErc(ercSocBadFile); break;
                case MM_BOX_UNDO_LIMIT: Assert(false, "TextBoxUndoEdit assumes there is one level of undo!"); break;
                case MM_BOX_BAD_TEXT_VIEW: Bug("DocumentDisplayGraphicsObject isn't a text view"); break;
                }
                break;
            case MM_BOX_NEW_COPY:
            {
                RC bounds = Bounds(q->bounds);
                q->object = TextBox::PtboxNew(nullptr, &bounds); break;
            }
            case MM_BOX_SUSPEND_UNDO: static_cast<PTBOX>(q->object)->SuspendUndo(); break;
            case MM_BOX_RESUME_UNDO: static_cast<PTBOX>(q->object)->ResumeUndo(); break;
            case MM_BOX_CHARACTERS: q->value = static_cast<PTBOX>(q->object)->CpMac(); break;
            case MM_BOX_REPLACE:
                return static_cast<PTBOX>(q->object)->FReplaceTxrd(static_cast<PTBOX>(q->other), 0, q->value, 0, q->second);
            case MM_BOX_SET_TYPE: static_cast<PTBOX>(q->object)->SetTypeCore(q->value != 0); break;
            case MM_BOX_ALLOCATE_UNDO:
                switch (q->value)
                {
                case MM_BOX_UNDO_TYPE: q->object = NewObj TextBoxUndoType; break;
                case MM_BOX_UNDO_SIZE: q->object = NewObj TextBoxUndoSize; break;
                case MM_BOX_UNDO_HIDE: q->object = NewObj TextBoxUndoHide; break;
                case MM_BOX_UNDO_EDIT: q->object = NewObj TextBoxUndoEdit; break;
                default: q->object = NewObj TextBoxUndoColor; break;
                }
                break;
            case MM_BOX_ADD_UNDO:
                return static_cast<PTBOX>(q->other)->Pscen()->Pmvie()->FAddUndo(static_cast<PMovieUndo>(q->object));
            case MM_BOX_PARENT_ADD_UNDO:
                return static_cast<PTBOX>(q->object)->TextBox_PAR::FAddUndo(static_cast<PUndoBase>(q->other));
            case MM_BOX_PARENT_CLEAR_UNDO: static_cast<PTBOX>(q->object)->ParClearUndo(); break;
            case MM_BOX_CLEAR_UNDO:
                (h.scene ? h.scene : h.box->Pscen())->Pmvie()->ClearUndo(); break;
            case MM_BOX_SHOW: return static_cast<PTBOX>(q->object)->FShowCore();
            case MM_BOX_HIDE: static_cast<PTBOX>(q->object)->HideCore(); break;
            case MM_BOX_REMOVE: return static_cast<PTBOX>(q->object)->Pscen()->FRemTbox(static_cast<PTBOX>(q->object));
            case MM_BOX_SCENE_BOX:
                q->object = static_cast<PTBOX>(q->other)->Pscen()->PtboxFromItbox(q->value); break;
            case MM_BOX_VIEW:
                q->object = static_cast<PTextBoxGobject>(static_cast<PTBOX>(q->object)->PddgGet(0));
                AssertNilOrPo(static_cast<PTextBoxGobject>(q->object), 0); break;
            case MM_BOX_ACTIVE_VIEW: q->object = static_cast<PTBOX>(q->object)->PddgActive(); break;
            case MM_BOX_CHECK_TEXT_VIEW:
                q->value = static_cast<PDocumentDisplayGraphicsObject>(q->object)->FIs(
                    q->value ? kclsTextBoxGobject : kclsTextDocumentGraphicsObject); break;
            case MM_BOX_APPLY_CHARACTER:
            {
                CHP value = Character(q->character), difference = Character(q->difference);
                return static_cast<PTextBoxGobject>(q->object)->FApplyChp(&value, &difference);
            }
            case MM_BOX_SELECTION:
            {
                long first, end;
                static_cast<PTextDocumentGraphicsObject>(q->object)->GetSel(&first, &end);
                q->value = first; q->second = end; break;
            }
            case MM_BOX_FETCH_CHARACTER:
            {
                CHP value; long first, end;
                static_cast<PTBOX>(q->object)->FetchChp(q->value, &value, &first, &end);
                q->character = Character(value); q->second = end; break;
            }
            case MM_BOX_PUBLISH_CHARACTER: *h.characterOutput = Character(q->character); break;
            case MM_BOX_PUBLISH_MASK: *h.maskOutput = q->word; break;
            case MM_BOX_ALLOCATE_CLIPBOARD: q->object = NewObj TextBoxClipboard; break;
            case MM_BOX_WORKSPACE:
                q->value = h.scene->Pmvie()->Pmcc()->Dxp(); q->second = h.scene->Pmvie()->Pmcc()->Dyp(); break;
            case MM_BOX_DUPLICATE:
            {
                PTBOX result = nullptr;
                bool success = static_cast<PTBOX>(q->object)->FDup(&result);
                q->object = result; return success;
            }
            case MM_BOX_GET_BOUNDS:
            {
                RC bounds; static_cast<PTBOX>(q->object)->GetRc(&bounds); q->bounds = Bounds(bounds); break;
            }
            case MM_BOX_SET_BOUNDS:
            {
                RC bounds = Bounds(q->bounds); static_cast<PTBOX>(q->object)->SetRc(&bounds); break;
            }
            case MM_BOX_ADD: return h.scene->FAddTbox(static_cast<PTBOX>(q->object));
            case MM_BOX_SELECT: h.scene->SelectTbox(static_cast<PTBOX>(q->object)); break;
            case MM_BOX_ATTACH_MOUSE: static_cast<PTBOX>(q->object)->AttachToMouse(); break;
            default: return -2;
            }
            return 1;
        }
        catch (const std::bad_alloc &) { h.exception = std::current_exception(); return -1; }
        catch (...) { h.exception = std::current_exception(); return -2; }
    }
};
struct TextBoxUndoHost : TextBoxEditHost
{
    PMovieUndo owner = nullptr;
    MMTextBoxEditServices Services()
    {
        auto services = TextBoxEditHost::Services(); services.context = this; services.call = Call;
        return services;
    }
    static int Call(void *context, MMTextBoxEditOperation op, MMTextBoxEditRequest *q)
    {
        auto &h = *static_cast<TextBoxUndoHost *>(context);
        try
        {
            switch (op)
            {
            case MM_BOX_UNDO_SCENE: q->value = h.owner->Iscen(); break;
            case MM_BOX_UNDO_FRAME: q->value = h.owner->Nfrm(); break;
            case MM_BOX_CURRENT_SCENE: q->value = h.owner->Pmvie()->Iscen(); break;
            case MM_BOX_SWITCH_SCENE: return h.owner->Pmvie()->FSwitchScen(q->value);
            case MM_BOX_SEEK_SCENE: return h.owner->Pmvie()->Pscen()->FGotoFrm(q->value);
            case MM_BOX_SCENE_BOX: q->object = h.owner->Pmvie()->Pscen()->PtboxFromItbox(q->value); break;
            case MM_BOX_SELECT: h.owner->Pmvie()->Pscen()->SelectTbox(static_cast<PTBOX>(q->object)); break;
            case MM_BOX_FLUSH: mm_frame_sound_queue_clear(h.owner->Pmvie()->SoundQueue()); break;
            case MM_BOX_CLEAR_UNDO: h.owner->Pmvie()->ClearUndo(); break;
            case MM_BOX_STORY: q->value = static_cast<PTBOX>(q->object)->FStory(); break;
            case MM_BOX_POSITION_VIEW:
            {
                RC bounds = Bounds(q->bounds);
                auto view = static_cast<PTextBoxGobject>(static_cast<PTBOX>(q->object)->PddgGet(0));
                AssertPo(view, 0); view->Ptbxb()->SetPos(&bounds); break;
            }
            case MM_BOX_GOTO: return static_cast<PTBOX>(q->object)->FGotoFrame(q->value);
            case MM_BOX_REPLAY_EDIT:
                return q->value ? static_cast<PUndoBase>(q->object)->FUndo(static_cast<PTBOX>(q->other))
                                : static_cast<PUndoBase>(q->object)->FDo(static_cast<PTBOX>(q->other));
            default: return TextBoxEditHost::Call(&h, op, q);
            }
            return 1;
        }
        catch (const std::bad_alloc &) { h.exception = std::current_exception(); return -1; }
        catch (...) { h.exception = std::current_exception(); return -2; }
    }
};
static_assert(kdzpBorderTbox == 5 && kxpDefaultTbox == 177 && kypDefaultTbox == 78 &&
              fontBold == 1 && fontItalic == 2 && kgrfchpAll == 15, "C89 text-box policy constants");
#endif
