/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Base document class and its supporting gobs.

***************************************************************************/
#include <exception>
#include <new>
#include "frame.h"
ASSERTNAME
#include "document_storage_host.h"
#include "document_undo_host.h"
#include "document_views_host.h"
#include "document_lifecycle_host.h"

#define dsnoNil 0
MMDocumentRegistry DocumentBase::_documents = {};

BEGIN_CMD_MAP(DocumentDisplayGraphicsObject, GraphicsObject)
ON_CID_GEN(cidClose, &DocumentDisplayGraphicsObject::FCmdCloseDoc, pvNil)
ON_CID_GEN(cidSaveAndClose, &DocumentDisplayGraphicsObject::FCmdCloseDoc, pvNil)
ON_CID_GEN(cidSave, &DocumentDisplayGraphicsObject::FCmdSave, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidSaveAs, &DocumentDisplayGraphicsObject::FCmdSave, pvNil)
ON_CID_GEN(cidSaveCopy, &DocumentDisplayGraphicsObject::FCmdSave, pvNil)
ON_CID_GEN(cidCut, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidCopy, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidPaste, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidPasteSpecial, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidClear, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidUndo, &DocumentDisplayGraphicsObject::FCmdUndo, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidRedo, &DocumentDisplayGraphicsObject::FCmdUndo, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
END_CMD_MAP_NIL()

RTCLASS(DocumentBase)
RTCLASS(DocumentTreeEnumerator)
RTCLASS(DocumentDisplayGraphicsObject)
RTCLASS(DocumentMDIWindow)
RTCLASS(DocumentMainWindow)
RTCLASS(DocumentScrollGraphicsObject)
RTCLASS(DocumentScrollWindowSplitter)
RTCLASS(DocumentScrollSplitMover)
RTCLASS(UndoBase)

/***************************************************************************
    Constructor for DocumentBase
***************************************************************************/
DocumentBase::DocumentBase(PDocumentBase pdocb, ulong grfdoc) : CommandHandler(khidDoc)
{
    AssertNilOrPo(pdocb, 0);
    mm_document_register(&_documents, &_document, this, pdocb ? &pdocb->_document : nullptr, (grfdoc & fdocSibling) != 0);

    const auto undoAllocator = Group::CollectionAllocator();
    int undoTrash = 0;
#ifdef DEBUG
    undoTrash = 1;
#endif
    AssertDo(mm_document_undo_init(&_undoList, &undoAllocator, undoTrash), "invalid undo storage");
    AssertDo(mm_document_views_init(&_viewList, &undoAllocator, undoTrash), "invalid view storage");
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    First calls Release on all direct child docb's of this DocumentBase.
    Finally calls delete on itself.
***************************************************************************/
void DocumentBase::Release(void)
{
    AssertThis(fobjAssertFull);
    LifecycleHost host;
    auto services = host.Services();
    mm_document_release(&_document, &services);
    host.Rethrow();
}

/***************************************************************************
    Close all DDGs on this DocumentBase.
***************************************************************************/
void DocumentBase::CloseAllDdg(void)
{
    LifecycleHost host;
    auto services = host.Services();
    mm_document_close_views(&_document, &services);
    host.Rethrow();
}

/***************************************************************************
    Destructor for the document class.
***************************************************************************/
DocumentBase::~DocumentBase(void)
{
    AssertThis(fobjAssertFull);
    Assert(_document.freeing, "Release not called first!");
    Assert(_document.child == nullptr, "docb still has children");
    Assert(_viewList.count == 0, "doc is still being displayed by a window");
    LifecycleHost host;
    auto services = host.Services();
    mm_document_finish(&_documents, &_document, &services);
    host.Rethrow();
}

/***************************************************************************
    Static method: calls FQueryClose on all open docs.
***************************************************************************/
bool DocumentBase::FQueryCloseAll(ulong grfdoc)
{
    LifecycleHost host;
    auto services = host.Services();
    int result = mm_document_query_all(&_documents, grfdoc, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    If the document is dirty, ask the user if they want to save changes
    (and save if they do).  Return false if the user cancels the operation.
    Don't allow cancel if fdocForceClose is set.  Don't ask about saving
    (assume yes) if fdocAssumeYes is set.  Doesn't assume doc is fni
    based (calls FSave() to perform the save).
***************************************************************************/
bool DocumentBase::FQueryClose(ulong grfdoc)
{
    LifecycleHost host;
    auto services = host.Services();
    int result = mm_document_query_close(&_document, grfdoc, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Ask the user if they want to save the document before closing it.
***************************************************************************/
tribool DocumentBase::_TQuerySave(bool fForce)
{
    AssertThis(0);

    return vpappb->TQuerySaveDoc(this, fForce) ? tYes : tNo;
}

/***************************************************************************
    If the document is dirty, and this is the only DocumentMDIWindow displaying the doc,
    ask the user if they want to save changes (and save if they do).
    Return false if the user cancels the operation or the save fails.
***************************************************************************/
bool DocumentBase::FQueryCloseDmd(PDocumentMDIWindow pdmd)
{
    LifecycleHost host;
    auto services = host.Services();
    int result = mm_document_query_window(&_document, pdmd, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Return whether this is an internal document.
***************************************************************************/
bool DocumentBase::FInternal(void)
{
    AssertThis(0);
    LifecycleHost host;
    auto services = host.Services();
    int result = mm_document_internal(&_document, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Change this document's internal status.
***************************************************************************/
void DocumentBase::SetInternal(bool fInternal)
{
    AssertThis(0);
    _document.internal = FPure(fInternal);
}

/***************************************************************************
    Static method to return the DOC open on this fni (if there is one).
***************************************************************************/
PDocumentBase DocumentBase::PdocbFromFni(Filename *pfni)
{
    AssertPo(pfni, 0);
    LifecycleHost host;
    host.lookup = pfni;
    auto services = host.Services();
    MMDocumentNode *found;
    mm_document_find(&_documents, &found, &services);
    host.Rethrow();
    return _DocumentOwner(found);
}

/***************************************************************************
    Get the current Filename for the doc.  Return false if the doc is not
    currently based on an Filename (it's a new doc or an internal one).
***************************************************************************/
bool DocumentBase::FGetFni(Filename *pfni)
{
    return fFalse;
}

/***************************************************************************
    High level save.
***************************************************************************/
bool DocumentBase::FSave(long cid)
{
    LifecycleHost host;
    auto services = host.Services();
    int kind = cid == cidSave ? MM_DOC_SAVE : cid == cidSaveAs ? MM_DOC_SAVE_AS : cid == cidSaveCopy ? MM_DOC_SAVE_COPY : -1;
    int result = mm_document_save(&_document, kind, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Save the document and optionally set this fni as the current one.
    If the doc is currently based on an Filename, pfni may be nil, indicating
    that this is a normal save (not save as).  If pfni is not nil and
    fSetFni is false, this just writes a copy of the doc but doesn't change
    the doc one bit.
***************************************************************************/
bool DocumentBase::FSaveToFni(Filename *pfni, bool fSetFni)
{
    return fFalse;
}

/***************************************************************************
    Ask the user what file they want to save to.  On Mac, assumes saving
    to a text file.
***************************************************************************/
bool DocumentBase::FGetFniSave(Filename *pfni)
{
    return FGetFniSaveMacro(pfni, 'TEXT',
                            "\x9"
                            "Save As: ",
                            "", PszLit("All files\0*.*\0"), (GraphicsObject::PgobScreen() ? GraphicsObject::PgobScreen()->Hwnd() : hNil));
}

/***************************************************************************
    Add the DocumentDisplayGraphicsObject to the list of DDGs displaying this document
***************************************************************************/
bool DocumentBase::FAddDdg(PDocumentDisplayGraphicsObject pddg)
{
    AssertThis(fobjAssertFull);
    AssertPo(pddg, 0);
    ViewHost host(this, true);
    auto services = host.Services();
    int result = mm_document_views_add(&_viewList, pddg, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Find the position of the pddg in the DocumentBase's list.
***************************************************************************/
bool DocumentBase::_FFindDdg(PDocumentDisplayGraphicsObject pddg, long *pipddg)
{
    AssertThis(0);
    AssertVarMem(pipddg);
    MMI32 at;
    int result = mm_document_views_find(&_viewList, pddg, &at);
    if (result) *pipddg = at;
    else TrashVar(pipddg);
    return result != 0;
}

/***************************************************************************
    Remove the pddg from the list of DDGs for this doc.
***************************************************************************/
void DocumentBase::RemoveDdg(PDocumentDisplayGraphicsObject pddg)
{
    AssertThis(fobjAssertFull);
    ViewHost host(this);
    auto services = host.Services();
    mm_document_views_remove(&_viewList, pddg, &services);
}

/***************************************************************************
    Make this DocumentDisplayGraphicsObject the first one in the DocumentBase's list.
***************************************************************************/
void DocumentBase::MakeFirstDdg(PDocumentDisplayGraphicsObject pddg)
{
    AssertThis(0);
    ViewHost host(this);
    host.view = pddg;
    auto services = host.Services();
    mm_document_views_first(&_viewList, pddg, &services);
}

/***************************************************************************
    Return the iddg'th DocumentDisplayGraphicsObject displaying this doc.  If iddg is too big,
    return pvNil.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentBase::PddgGet(long iddg)
{
    AssertThis(0);
    AssertIn(iddg, 0, klwMax);
    auto view = static_cast<PDocumentDisplayGraphicsObject>(mm_document_views_get(&_viewList, iddg));
    AssertNilOrPo(view, 0);
    return view;
}

/***************************************************************************
    If there is an active DocumentDisplayGraphicsObject for this doc, return it.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentBase::PddgActive(void)
{
    AssertThis(0);
    ViewHost host(this);
    auto services = host.Services();
    void *view;
    mm_document_view_active(&_document, &view, &services);
    host.Rethrow();
    return static_cast<PDocumentDisplayGraphicsObject>(view);
}

/***************************************************************************
    Create a new mdi window for this document.
***************************************************************************/
PDocumentMDIWindow DocumentBase::PdmdNew(void)
{
    AssertThis(fobjAssertFull);
    return DocumentMDIWindow::PdmdNew(this);
}

/***************************************************************************
    If this DocumentBase has a DocumentMDIWindow, make it the activate hwnd.
***************************************************************************/
void DocumentBase::ActivateDmd(void)
{
    AssertThis(fobjAssertFull);
    ViewHost host(this);
    auto services = host.Services();
    mm_document_view_activate(&_document, &services);
    host.Rethrow();
}

/***************************************************************************
    Create a DocumentMainWindow for the document.
***************************************************************************/
PDocumentMainWindow DocumentBase::PdmwNew(PGraphicsObjectBlock pgcb)
{
    AssertThis(fobjAssertFull);
    return DocumentMainWindow::PdmwNew(this, pgcb);
}

/***************************************************************************
    Create a new DocumentScrollGraphicsObject for the doc in the given DocumentMainWindow.
***************************************************************************/
PDocumentScrollGraphicsObject DocumentBase::PdsgNew(PDocumentMainWindow pdmw, PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel)
{
    AssertThis(fobjAssertFull);
    return DocumentScrollGraphicsObject::PdsgNew(pdmw, pdsgSplit, grfdsg, rel);
}

/***************************************************************************
    Create a new DocumentDisplayGraphicsObject for the doc in the given DocumentScrollGraphicsObject.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentBase::PddgNew(PGraphicsObjectBlock pgcb)
{
    AssertThis(fobjAssertFull);
    return DocumentDisplayGraphicsObject::PddgNew(this, pgcb);
}

/***************************************************************************
    Get the name of a default (untitled) document.
***************************************************************************/
void DocumentBase::GetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    ViewHost host(this);
    host.output = pstn;
    auto services = host.Services();
    mm_document_name(&_documents, &_document, &services);
    host.Rethrow();
}

/***************************************************************************
    Makes sure all windows displaying this document have the correct title.
***************************************************************************/
void DocumentBase::UpdateName(void)
{
    ViewHost host(this);
    auto services = host.Services();
    mm_document_titles(&_document, &services);
    host.Rethrow();
}

long DocumentBase::_UntitledNumber()
{
    return mm_document_untitled(&_documents, &_document);
}

bool DocumentBase::_FInitUndo()
{
    UndoHost host(this);
    auto services = host.Services();
    int result = mm_document_undo_prepare(&_undoList, &services);
    host.Rethrow();
    return result > 0;
}
PUndoBase DocumentBase::_PundbGet(long index)
{
    AssertIn(index, 0, _undoList.count);
    return static_cast<PUndoBase>(mm_document_undo_get(&_undoList, index));
}

/***************************************************************************
    Does a single Undo off the undo list.
***************************************************************************/
bool DocumentBase::FUndo()
{
    AssertThis(fobjAssertFull);
    UndoHost host(this);
    auto services = host.Services();
    int result = mm_document_undo_replay(&_undoList, 0, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Redoes a single undo off the undo list.
***************************************************************************/
bool DocumentBase::FRedo()
{
    AssertThis(fobjAssertFull);
    UndoHost host(this);
    auto services = host.Services();
    int result = mm_document_undo_replay(&_undoList, 1, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Adds a single undo to the undo list, removing any Redo items.  Increments
    the ref count on the pundb if we keep a reference to it.  Assumes
    the action has already been done.
***************************************************************************/
bool DocumentBase::FAddUndo(PUndoBase pundb)
{
    AssertThis(fobjAssertFull);
    UndoHost host(this);
    auto services = host.Services();
    int result = mm_document_undo_add(&_undoList, pundb, &services);
    host.Rethrow();
    return result > 0;
}

/***************************************************************************
    Delete all undo and redo records.
***************************************************************************/
void DocumentBase::ClearUndo(void)
{
    UndoHost host(this);
    auto services = host.Services();
    mm_document_undo_clear(&_undoList, &services);
    host.Rethrow();
}

/***************************************************************************
    Delete all redo records.
***************************************************************************/
void DocumentBase::ClearRedo(void)
{
    UndoHost host(this);
    auto services = host.Services();
    mm_document_undo_clear_redo(&_undoList, &services);
    host.Rethrow();
}

/***************************************************************************
    Set the maximum allowable number of undoable operations.
***************************************************************************/
void DocumentBase::SetCundbMax(long cundbMax)
{
    AssertThis(fobjAssertFull);
    AssertIn(cundbMax, 0, kcbMax);
    UndoHost host(this);
    auto services = host.Services();
    mm_document_undo_limit(&_undoList, cundbMax, &services);
    host.Rethrow();
}

/***************************************************************************
    Return the maximum number of undoable operations for this doc
***************************************************************************/
long DocumentBase::CundbMax(void)
{
    AssertThis(0);
    return _undoList.maximum;
}

/***************************************************************************
    Return the number of operations that can currently be undone.
***************************************************************************/
long DocumentBase::CundbUndo(void)
{
    AssertThis(0);
    return _undoList.done;
}

/***************************************************************************
    Return the number of operations that can currently be redone.
***************************************************************************/
long DocumentBase::CundbRedo(void)
{
    return mm_document_undo_redo_count(&_undoList);
}

/***************************************************************************
    Export this docb as the external clipboard.
***************************************************************************/
void DocumentBase::ExportFormats(PClipboardObject pclip)
{
    AssertThis(0);
    AssertPo(pclip, 0);
}

/***************************************************************************
    See if this document can be coerced to the given format.
***************************************************************************/
bool DocumentBase::FGetFormat(long cls, PDocumentBase *ppdocb)
{
    AssertThis(0);
    AssertNilOrVarMem(ppdocb);

    if (pvNil != ppdocb)
        *ppdocb = pvNil;
    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert validity of a DocumentBase
***************************************************************************/
void DocumentBase::AssertValid(LegacyUlong grfdocb)
{
    long ipddg;
    PDocumentDisplayGraphicsObject pddg;
    long ipundb;
    PUndoBase pundb;

    DocumentBase_PAR::AssertValid(grfdocb & fobjAssertFull);

    if (!(grfdocb & fobjAssertFull))
        return;

    if (_viewList.ready)
    {
        Assert(_viewList.count * sizeof(void *) <= _viewList.storage.buffers[0].capacity, "view storage too small");
        for (const auto &buffer : _viewList.storage.buffers)
        {
            Assert((buffer.capacity == 0) == (buffer.data == nullptr), "view storage size/pointer mismatch");
            if (buffer.data) AssertPvAlloced(buffer.data, buffer.capacity);
        }
        for (ipddg = _viewList.count; ipddg-- != 0;)
        {
            pddg = static_cast<PDocumentDisplayGraphicsObject>(mm_document_views_get(&_viewList, ipddg));
            AssertBasePo(pddg, 0);
        }
    }

    if (_undoList.ready)
    {
        Assert(_undoList.count * sizeof(void *) <= _undoList.storage.buffers[0].capacity, "undo storage too small");
        for (const auto &buffer : _undoList.storage.buffers)
        {
            Assert((buffer.capacity == 0) == (buffer.data == nullptr), "undo storage size/pointer mismatch");
            if (buffer.data) AssertPvAlloced(buffer.data, buffer.capacity);
        }
        AssertIn(_undoList.done, 0, _undoList.count + 1);
        AssertIn(_undoList.count, 0, _undoList.maximum + 1);
        for (ipundb = _undoList.count; ipundb-- != 0;)
        {
            pundb = _PundbGet(ipundb);
            AssertPo(pundb, 0);
        }
    }
    else
    {
        Assert(_undoList.done == 0, 0);
        AssertIn(_undoList.maximum, 0, kcbMax);
    }
}

/***************************************************************************
    Mark the memory used by the DocumentBase
***************************************************************************/
void DocumentBase::MarkMem(void)
{
    long ipundb;
    PUndoBase pundb;

    AssertThis(fobjAssertFull);
    DocumentBase_PAR::MarkMem();
    MarkPv(_viewList.storage.buffers[0].data);
    MarkPv(_viewList.storage.buffers[1].data);
    if (_undoList.ready)
    {
        MarkPv(_undoList.storage.buffers[0].data);
        MarkPv(_undoList.storage.buffers[1].data);

        for (ipundb = _undoList.count; ipundb-- != 0;)
        {
            pundb = _PundbGet(ipundb);
            MarkMemObj(pundb);
        }
    }
}
#endif // DEBUG

/***************************************************************************
    Constructor for a document tree enumerator.
***************************************************************************/
DocumentTreeEnumerator::DocumentTreeEnumerator(void)
{
    mm_document_tree_init(&_tree, nullptr);
}

/***************************************************************************
    Initialize a document tree enumerator.
***************************************************************************/
void DocumentTreeEnumerator::Init(PDocumentBase pdocb)
{
    mm_document_tree_init(&_tree, pdocb ? &pdocb->_document : nullptr);
}

/***************************************************************************
    Goes to the next node in the sub tree being enumerated.  Returns false
    iff the enumeration is done.
***************************************************************************/
bool DocumentTreeEnumerator::FNextDoc(PDocumentBase *ppdocb, ulong *pgrfdteOut, ulong grfdte)
{
    MMDocumentNode *node;
    MMU32 flags;
    int result = mm_document_tree_next(&_tree, &node, &flags, grfdte);
    *pgrfdteOut = flags;
    if (result)
        *ppdocb = DocumentBase::_DocumentOwner(node);
    return result != 0;
}

/***************************************************************************
    Static method to create a new DocumentDisplayGraphicsObject.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentDisplayGraphicsObject::PddgNew(PDocumentBase pdocb, PGraphicsObjectBlock pgcb)
{
    PDocumentDisplayGraphicsObject pddg;

    if (pvNil == (pddg = NewObj DocumentDisplayGraphicsObject(pdocb, pgcb)))
        return pvNil;

    if (!pddg->_FInit())
    {
        ReleasePpo(&pddg);
        return pvNil;
    }
    pddg->Activate(fTrue);

    AssertPo(pddg, 0);
    return pddg;
}

/***************************************************************************
    Constructor for a DocumentDisplayGraphicsObject.  AddRef's the DocumentBase.
***************************************************************************/
DocumentDisplayGraphicsObject::DocumentDisplayGraphicsObject(PDocumentBase pdocb, PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
    AssertBasePo(pdocb, 0);
    pdocb->AddRef();
    _pdocb = pdocb;
    _scvVert = _scvHorz = 0;
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Destructor for DocumentDisplayGraphicsObject - remove itself from the DocumentBase's list.  Releases
    the DocumentBase.
***************************************************************************/
DocumentDisplayGraphicsObject::~DocumentDisplayGraphicsObject(void)
{
    AssertBasePo(_pdocb, 0);
    PDocumentMDIWindow pdmd;

    _pdocb->RemoveDdg(this);
    if (_fActive && pvNil != (pdmd = Pdmd()))
        pdmd->ActivateNext(this);
    _pdocb->Release();
}

/***************************************************************************
    Initialize the DocumentDisplayGraphicsObject - including setting its position.
***************************************************************************/
bool DocumentDisplayGraphicsObject::_FInit(void)
{
    _fCreating = fTrue;

    if (!_pdocb->FAddDdg(this))
        return fFalse;

    _fCreating = fFalse;
    return fTrue;
}

/***************************************************************************
    If this DocumentDisplayGraphicsObject is contained in a DocumentMDIWindow, return the DocumentMDIWindow.  Otherwise, return
    pvNil.
***************************************************************************/
PDocumentMDIWindow DocumentDisplayGraphicsObject::Pdmd(void)
{
    return (PDocumentMDIWindow)GraphicsObject::PgobParFromCls(kclsDocumentMDIWindow);
}

/***************************************************************************
    Make this the active DocumentDisplayGraphicsObject for the docb or deactivate it according to
    fActive.
***************************************************************************/
void DocumentDisplayGraphicsObject::Activate(bool fActive)
{
    AssertThis(fobjAssertFull);
    PDocumentDisplayGraphicsObject pddg;
    PDocumentMDIWindow pdmd;

    if (FPure(fActive) == FPure(_fActive))
        return;

    if (fActive)
    {
        // deactivate the current active one
        pddg = _pdocb->PddgActive();
        if (pvNil != pddg)
        {
            pddg->_Activate(fFalse);
            pddg->_fActive = fFalse;
        }

        // activate ourself
        if (pvNil != (pdmd = Pdmd()))
        {
            // bring our parent chain to the front
            PGraphicsObject pgob;

            for (pgob = this; pgob != pdmd; pgob = pgob->PgobPar())
                pgob->BringToFront();
        }
        _pdocb->MakeFirstDdg(this);
    }
    _Activate(fActive);
    _fActive = FPure(fActive);
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Default for a DocumentDisplayGraphicsObject - add/remove itself to the command handler list.
***************************************************************************/
void DocumentDisplayGraphicsObject::_Activate(bool fActive)
{
    vpcex->RemoveCmh(this, 0);
    if (fActive)
        vpcex->FAddCmh(this, 0);
}

/***************************************************************************
    Handles enabling/disabling of common Ddg commands.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FEnableDdgCmd(PCommand pcmd, ulong *pgrfeds)
{
    AssertThis(0);

    switch (pcmd->cid)
    {
    case cidSave:
        if (!_pdocb->FDirty())
            goto LDisable;
        break;

    case cidCut:
    case cidCopy:
    case cidClear:
        if (!_FCopySel(pvNil))
            goto LDisable;
        break;

    case cidPaste:
    case cidPasteSpecial:
        if (vpclip->FDocIsClip(pvNil) || vpclip->FDocIsClip(_pdocb) || !_FPaste(vpclip, fFalse, pcmd->cid))
        {
            goto LDisable;
        }
        break;

    case cidUndo:
        if (_pdocb->CundbUndo() <= 0)
            goto LDisable;
        break;
    case cidRedo:
        if (_pdocb->CundbRedo() <= 0)
            goto LDisable;
        break;

    default:
        BugVar("unhandled cid in FEnableDdgCmd", &pcmd->cid);
    LDisable:
        *pgrfeds = fedsDisable;
        return fTrue;
    }

    *pgrfeds = fedsEnable;
    return fTrue;
}

/***************************************************************************
    Handles the Cut, Copy, Paste and Clear commands.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdClip(PCommand pcmd)
{
    AssertThis(fobjAssertFull);
    AssertVarMem(pcmd);
    PDocumentBase pdocb = pvNil;

    switch (pcmd->cid)
    {
    case cidCut:
    case cidCopy:
        // copy the selection
        if (!_FCopySel(&pdocb))
            return fTrue;
        vpclip->Set(pdocb);
        ReleasePpo(&pdocb);

        if (pcmd->cid == cidCopy)
            break;
        // fall thru

    case cidClear:
        // delete the selection
        _ClearSel();
        break;

    case cidPaste:
    case cidPasteSpecial:
        if (!vpclip->FDocIsClip(pvNil) && !vpclip->FDocIsClip(_pdocb))
            _FPaste(vpclip, fTrue, pcmd->cid);
        break;
    }

    return fTrue;
}

/***************************************************************************
    Default for copying a selection.  Just returns false so the Cut, Copy
    and Clear edit menu items are disabled.
***************************************************************************/
bool DocumentDisplayGraphicsObject::_FCopySel(PDocumentBase *ppdocb)
{
    return fFalse;
}

/***************************************************************************
    Default for clearing (deleting) a selection.
***************************************************************************/
void DocumentDisplayGraphicsObject::_ClearSel(void)
{
}

/***************************************************************************
    Default for pasting over a selection.  Just returns false so the Paste
    edit menu item is disabled.
***************************************************************************/
bool DocumentDisplayGraphicsObject::_FPaste(PClipboardObject pclip, bool fDoIt, long cid)
{
    return fFalse;
}

/***************************************************************************
    Handle a close command.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdCloseDoc(PCommand pcmd)
{
    if (_pdocb->FQueryClose(pcmd->cid == cidSaveAndClose ? fdocAssumeYes : fdocNil))
        _pdocb->CloseAllDdg();
    return fTrue;
}

/***************************************************************************
    Handle a save, save as or save a copy command.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdSave(PCommand pcmd)
{
    _pdocb->FSave(pcmd->cid);
    return fTrue;
}

/***************************************************************************
    Handle a save, save as or save a copy command.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdUndo(PCommand pcmd)
{
    if (pcmd->cid == cidUndo)
        _pdocb->FUndo();
    else
        _pdocb->FRedo();
    return fTrue;
}

/***************************************************************************
    Default for a DocumentDisplayGraphicsObject - for frame testing only.
***************************************************************************/
void DocumentDisplayGraphicsObject::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    pgnv->FillRc(prcClip, _fActive ? kacrBlue : kacrMagenta);
}

/***************************************************************************
    Activate the selection.  Default activates the DocumentDisplayGraphicsObject.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdActivateSel(PCommand pcmd)
{
    Activate(fTrue);
    return fTrue;
}

/***************************************************************************
    Scroll the DCD
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdScroll(PCommand pcmd)
{
    bool fVert;
    long scv;

    fVert = (pcmd->rglw[0] == khidVScroll);
    switch (pcmd->cid)
    {
    case cidDoScroll:
        if (pcmd->rglw[1] == scaToVal)
            break; // ignore thumb tracking

        if (fVert)
            _Scroll(scaNil, pcmd->rglw[1]);
        else
            _Scroll(pcmd->rglw[1], scaNil);
        break;

    case cidEndScroll:
        scv = pcmd->rglw[1];
        if (fVert)
            _Scroll(scaNil, scaToVal, 0, scv);
        else
            _Scroll(scaToVal, scaNil, scv);
        break;
    }

    return fTrue;
}

/***************************************************************************
    Scroll with the given scrolling actions.  Sets the scroll bar values
    accordingly.
***************************************************************************/
void DocumentDisplayGraphicsObject::_Scroll(long scaHorz, long scaVert, long scvHorz, long scvVert)
{
    _SetScrollValues();
}

/***************************************************************************
    Set the scroll bar values for the DocumentDisplayGraphicsObject to _scvHorz and _scvVert and
    the Max values.
***************************************************************************/
void DocumentDisplayGraphicsObject::_SetScrollValues(void)
{
    PScrollBar pscb;
    PGraphicsObject pgob;

    pgob = PgobPar();
    if (pgob->FIs(kclsDocumentScrollGraphicsObject))
    {
        if (pvNil != (pscb = (PScrollBar)pgob->PgobFromHid(khidVScroll)))
            pscb->SetValMinMax(_scvVert, 0, _ScvMax(fTrue));
        if (pvNil != (pscb = (PScrollBar)pgob->PgobFromHid(khidHScroll)))
            pscb->SetValMinMax(_scvHorz, 0, _ScvMax(fFalse));
    }
}

/***************************************************************************
    Actually move the bits for a scroll.  The _scvVert and _scvHorz
    member variables have already been updated.
***************************************************************************/
void DocumentDisplayGraphicsObject::_ScrollDxpDyp(long dxp, long dyp)
{
    Scroll(pvNil, -dxp, -dyp, kginDraw);
}

/***************************************************************************
    Return the scroll bound.
***************************************************************************/
long DocumentDisplayGraphicsObject::_ScvMax(bool fVert)
{
    return 0;
}

/***************************************************************************
    The DocumentDisplayGraphicsObject has changed sizes, reset the scroll bounds.
***************************************************************************/
void DocumentDisplayGraphicsObject::_NewRc(void)
{
    _SetScrollValues();
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a DocumentDisplayGraphicsObject.
***************************************************************************/
void DocumentDisplayGraphicsObject::AssertValid(LegacyUlong grfobj)
{
    DocumentDisplayGraphicsObject_PAR::AssertValid(grfobj | fobjAllocated);
    AssertPo(_pdocb, grfobj & fobjAssertFull);
}

/***************************************************************************
    Mark memory for the DocumentDisplayGraphicsObject.
***************************************************************************/
void DocumentDisplayGraphicsObject::MarkMem(void)
{
    AssertValid(fobjAssertFull);
    DocumentDisplayGraphicsObject_PAR::MarkMem();
    MarkMemObj(_pdocb);
}
#endif // DEBUG

/***************************************************************************
    Static method: create a new Document MDI window.  Put a size box in
    it and add a DocumentMainWindow.
***************************************************************************/
PDocumentMDIWindow DocumentMDIWindow::PdmdNew(PDocumentBase pdocb)
{
    AssertPo(pdocb, 0);
    PDocumentMDIWindow pdmd;
    String stn;
    RC rcRel, rcAbs;

    GraphicsObjectBlock gcb(khidDmd, GraphicsObject::PgobScreen());
    if (pvNil == (pdmd = NewObj DocumentMDIWindow(pdocb, &gcb)))
        return pvNil;
    pdocb->GetName(&stn);
    if (!pdmd->FCreateAndAttachMdi(&stn))
        goto LFail;

    rcRel.xpLeft = rcRel.ypTop = 0;
    rcRel.xpRight = rcRel.ypBottom = krelOne;
    rcAbs.xpLeft = rcAbs.ypTop = 0;
    rcAbs.xpRight = rcAbs.ypBottom = 1;
    if (pvNil == WindowSizeBox::PwsbNew(pdmd, fgobNil))
        goto LFail;
    gcb.Set(khidDmw, pdmd, fgobNil, kginDefault, &rcAbs, &rcRel);
    if (pvNil == pdocb->PdmwNew(&gcb))
    {
    LFail:
        ReleasePpo(&pdmd);
        return pvNil;
    }

    AssertPo(pdmd, 0);
    return pdmd;
}

/***************************************************************************
    Static method: returns the currently active DocumentMDIWindow (if there is one).
***************************************************************************/
PDocumentMDIWindow DocumentMDIWindow::PdmdTop(void)
{
    PGraphicsObject pgob;

    if (pvNil == (pgob = GraphicsObject::PgobMdiActive()))
        return pvNil;
    AssertPo(pgob, 0);
    if (!pgob->FIs(kclsDocumentMDIWindow))
        return pvNil;
    AssertPo((PDocumentMDIWindow)pgob, 0);
    return (PDocumentMDIWindow)pgob;
}

/***************************************************************************
    Constructor for document mdi window.
***************************************************************************/
DocumentMDIWindow::DocumentMDIWindow(PDocumentBase pdocb, PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
    AssertPo(pdocb, 0);
    _pdocb = pdocb;
}

/***************************************************************************
    Activate the next DocumentDisplayGraphicsObject (after the given one).
***************************************************************************/
void DocumentMDIWindow::ActivateNext(PDocumentDisplayGraphicsObject pddg)
{
    AssertThis(fobjAssertFull);
    GraphicsObjectTreeEnumerator gte;
    ulong grfgte;
    PGraphicsObject pgob;

    if (_fFreeing)
        return;

    gte.Init(this, fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (pgob->FIs(kclsDocumentDisplayGraphicsObject) && pgob != pddg)
        {
            ((PDocumentDisplayGraphicsObject)pgob)->Activate(fTrue);
            return;
        }
    }
}

/***************************************************************************
    Handle activation/deactivation of the hwnd.
***************************************************************************/
void DocumentMDIWindow::_ActivateHwnd(bool fActive)
{
    AssertThis(0);
    PDocumentDisplayGraphicsObject pddg;

    pddg = (PDocumentDisplayGraphicsObject)PgobFromCls(kclsDocumentDisplayGraphicsObject);
    if (FPure(fActive) != FPure(pddg->FActive()))
        pddg->Activate(fActive);
}

/***************************************************************************
    Handles cidCloseWnd.
***************************************************************************/
bool DocumentMDIWindow::FCmdCloseWnd(PCommand pcmd)
{
    // ask the user about saving the doc
    if (!_pdocb->FQueryCloseDmd(this))
    {
        pcmd->cid = cidNil;
        return fTrue;
    }
    Release();
    return fTrue;
}

/***************************************************************************
    Static method to create a new DocumentMainWindow (document window) based on the given
    document.
***************************************************************************/
PDocumentMainWindow DocumentMainWindow::PdmwNew(PDocumentBase pdocb, PGraphicsObjectBlock pgcb)
{
    PDocumentMainWindow pdmw;

    pdmw = NewObj DocumentMainWindow(pdocb, pgcb);
    if (!pdmw->_FInit())
    {
        ReleasePpo(&pdmw);
        return pvNil;
    }

    AssertPo(pdmw, 0);
    return pdmw;
}

/***************************************************************************
    Constructor for document window class
***************************************************************************/
DocumentMainWindow::DocumentMainWindow(PDocumentBase pdocb, PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
    AssertPo(pdocb, 0);
    _pdocb = pdocb;
    _paldsed = pvNil;
    _idsedRoot = ivNil;
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Free the DSED tree so we don't bother with the tree manipulations
    during freeing.  Then call GraphicsObject::Free.
***************************************************************************/
void DocumentMainWindow::Release(void)
{
    AssertThis(fobjAssertFull);
    if (_references.count <= 1)
    {
        Assert(!_fFreeing, "we're recursing into the destructor!");
        _fFreeing = fTrue;
        ReleasePpo(&_paldsed);
        _idsedRoot = ivNil;
    }
    DocumentMainWindow_PAR::Release();
}

/***************************************************************************
    Create the actual mdi window, etc.
***************************************************************************/
bool DocumentMainWindow::_FInit(void)
{
    _fCreating = fTrue;

    // create a lone dsg
    if (pvNil == _pdocb->PdsgNew(this, pvNil, fdsgNil, krelOne))
        return fFalse;
    AssertPo(_paldsed, 0);
    Assert(_paldsed->IvMac() > 0, "the DocumentScrollGraphicsObject wasn't added to the DocumentMainWindow");

    _fCreating = fFalse;
    return fTrue;
}

/***************************************************************************
    The DocumentMainWindow has been resized, make sure no DSGs are too small.
***************************************************************************/
void DocumentMainWindow::_NewRc(void)
{
    AssertThis(0);
    _Layout(_idsedRoot);
}

/***************************************************************************
    Add the dsg to the dmw (the dsg is already a child gob - we now promote
    it to a full fledged child dsg).
***************************************************************************/
bool DocumentMainWindow::FAddDsg(PDocumentScrollGraphicsObject pdsg, PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel)
{
    AssertThis(fobjAssertFull);
    AssertIn(rel, 0, krelOne + 1);
    DSED dsed;
    DSED *qdsed;
    LegacyLong idsedSplit, idsedPar, idsedEdge, idsedNew;

    ClearPb(&dsed, size(dsed));
    dsed.idsedLeft = ivNil;
    dsed.idsedRight = ivNil;
    dsed.idsedPar = ivNil;
    if (pvNil == _paldsed || _paldsed->IvMac() == 0)
    {
        // this is the first one
        Assert(pvNil == pdsgSplit, "no DSGs yet, so can't split one");
        if (pvNil == _paldsed && pvNil == (_paldsed = AllocatedArray::PalNew(size(DSED), 1)))
            return fFalse;
        dsed.pdsg = pdsg;
        if (!_paldsed->FAdd(&dsed, &_idsedRoot))
            return fFalse;
        pdsg->_dsno = _idsedRoot + 1;
        idsedEdge = _idsedRoot;
        goto LDone;
    }

    AssertIn(_idsedRoot, 0, _paldsed->IvMac());
    if ((idsedSplit = pdsgSplit->_dsno) == dsnoNil)
    {
        Bug("pdsgSplit is not a registered DocumentScrollGraphicsObject");
        return fFalse;
    }
    idsedSplit--;

    if (!_paldsed->FEnsureSpace(2, fgrpNil))
        return fFalse;
    AssertDo(_paldsed->FAdd(&dsed, &idsedEdge), 0);
    AssertDo(_paldsed->FAdd(&dsed, &idsedNew), 0);

    // fix the links on the one being split
    qdsed = _Qdsed(idsedSplit);
    Assert(qdsed->pdsg == pdsgSplit, "DSED tree bad");
    idsedPar = qdsed->idsedPar;
    qdsed->idsedPar = idsedEdge;

    // fix the links on the parent
    if (ivNil == idsedPar)
    {
        Assert(idsedSplit == _idsedRoot, "corrupt DSED tree");
        _idsedRoot = idsedEdge;
    }
    else
    {
        qdsed = _Qdsed(idsedPar);
        if (qdsed->idsedLeft == idsedSplit)
            qdsed->idsedLeft = idsedEdge;
        else
        {
            Assert(qdsed->idsedRight == idsedSplit, "DSED tree is trashed");
            qdsed->idsedRight = idsedEdge;
        }
    }

    // construct the new node
    qdsed = _Qdsed(idsedNew);
    qdsed->idsedPar = idsedEdge;
    qdsed->pdsg = pdsg;
    pdsg->_dsno = idsedNew + 1;

    // construct the Edge node
    qdsed = _Qdsed(idsedEdge);
    qdsed->fVert = FPure(grfdsg & fdsgVert);
    qdsed->rel = rel;
    qdsed->idsedPar = idsedPar;
    if (grfdsg & fdsgAfter)
    {
        qdsed->idsedLeft = idsedSplit;
        qdsed->idsedRight = idsedNew;
    }
    else
    {
        qdsed->idsedLeft = idsedNew;
        qdsed->idsedRight = idsedSplit;
    }

LDone:
    _Layout(idsedEdge);
    AssertThis(fobjAssertFull);
    return fTrue;
}

/***************************************************************************
    Remove the dsg from the list of active DSGs.
***************************************************************************/
void DocumentMainWindow::RemoveDsg(PDocumentScrollGraphicsObject pdsg)
{
    long idsedStart;

    if (!_fFreeing)
    {
        _RemoveDsg(pdsg, &idsedStart);
        _Layout(idsedStart);
    }
}

/***************************************************************************
    Remove the DocumentScrollGraphicsObject from the tree and set its _dsno to nil.
***************************************************************************/
void DocumentMainWindow::_RemoveDsg(PDocumentScrollGraphicsObject pdsg, long *pidsedStartLayout)
{
    AssertThis(0);
    AssertPo(pdsg, 0);
    long idsedDel, idsedGrandPar, idsedSib;
    DSED *qdsed;
    DSED dsed;

    *pidsedStartLayout = ivNil;
    if (dsnoNil == pdsg->_dsno)
        return;
    if (pvNil == _paldsed)
    {
        pdsg->_dsno = dsnoNil;
        return;
    }

    // delete the node
    idsedDel = pdsg->_dsno - 1;
    pdsg->_dsno = dsnoNil;
    _paldsed->Get(idsedDel, &dsed);
    _paldsed->Delete(idsedDel);

    if (ivNil == dsed.idsedPar)
    {
        Assert(_idsedRoot == idsedDel, "bad root value");
        _idsedRoot = ivNil;
        AssertThis(0);
        return;
    }

    // get info from the parent, then delete it
    qdsed = _Qdsed(dsed.idsedPar);
    idsedGrandPar = qdsed->idsedPar;
    if (idsedDel == qdsed->idsedLeft)
        idsedSib = qdsed->idsedRight;
    else
    {
        Assert(idsedDel == qdsed->idsedRight, "DSED tree bad");
        idsedSib = qdsed->idsedLeft;
    }
    _paldsed->Delete(dsed.idsedPar);

    // fix the sibling and grandparent
    if (ivNil == idsedGrandPar)
    {
        Assert(_idsedRoot == dsed.idsedPar, "DSED root value wrong");
        _idsedRoot = idsedSib;
    }
    else
    {
        qdsed = _Qdsed(idsedGrandPar);
        if (dsed.idsedPar == qdsed->idsedLeft)
            qdsed->idsedLeft = idsedSib;
        else
        {
            Assert(dsed.idsedPar == qdsed->idsedRight, "DSED tree bad");
            qdsed->idsedRight = idsedSib;
        }
    }

    // fix the sib
    qdsed = _Qdsed(idsedSib);
    qdsed->idsedPar = idsedGrandPar;

    *pidsedStartLayout = idsedSib;
    AssertThis(0);
}

/***************************************************************************
    Find the edge corresponding to the given leaf restricted to the subtree
    pointed to by idsedRoot.  We get to the edge by going up until we
    either hit the root (return ivNil) or we just went up a left arc (return
    the parent).
***************************************************************************/
long DocumentMainWindow::_IdsedEdge(long idsed, long idsedRoot)
{
    // Don't call AssertThis because AssertValid calls this
    AssertBaseThis(0);
    DSED *qdsed;
    long idsedPar;

    qdsed = _Qdsed(idsed);
    idsedPar = qdsed->idsedPar;
    while (ivNil != idsedPar)
    {
        qdsed = _Qdsed(idsedPar);
        if (qdsed->idsedLeft == idsed)
        {
            Assert(ivNil != qdsed->idsedRight, "bad node");
            return idsedPar;
        }
        Assert(qdsed->idsedRight == idsed, "bad DSED tree");
        if (idsedPar == idsedRoot)
            return ivNil;
        idsed = idsedPar;
        idsedPar = qdsed->idsedPar;
        // keep going up until we're on a left branch
    }
    // we went right all the way up, so we're done
    return ivNil;
}

/***************************************************************************
    Find the next dsed to visit in the sub-tree traversal based at
    idsedStart (pre-order traversal).
***************************************************************************/
long DocumentMainWindow::_IdsedNext(long idsed, long idsedRoot)
{
    DSED *qdsed;

    qdsed = _Qdsed(idsed);
    if (ivNil != qdsed->idsedLeft)
        return qdsed->idsedLeft;

    Assert(ivNil == qdsed->idsedRight, "bad node");
    Assert(pvNil != qdsed->pdsg, "node should have a dsg");

    if (ivNil == (idsed = _IdsedEdge(idsed, idsedRoot)))
        return ivNil;
    return _Qdsed(idsed)->idsedRight;
}

/***************************************************************************
    Re-layout the DSGs in the DocumentMainWindow.  If any become too small, delete them.
***************************************************************************/
void DocumentMainWindow::_Layout(long idsedStart)
{
    AssertThis(0);
    RC rc, rcDsg;
    long dxpDmw, dypDmw;
    long idsed;
    DSED *qdsed;
    RC rcRel;
    PDocumentScrollGraphicsObject pdsg;

LRestart:
    if (ivNil == idsedStart)
        return;

    GetRc(&rc, cooLocal);
    dxpDmw = rc.Dxp();
    dypDmw = rc.Dyp();

    // set rcRel for idsedStart
    if (idsedStart == _idsedRoot)
    {
        // put full rcRel in the root
        rcRel.xpLeft = rcRel.ypTop = 0;
        rcRel.xpRight = rcRel.ypBottom = krelOne;
    }
    else
    {
        // get the rcRel for this node from its parent
        idsed = _Qdsed(idsedStart)->idsedPar;
        Assert(ivNil != idsed, "nil parent but not the root!");
        _SplitRcRel(idsed, &rcRel, &rc);
        if (_Qdsed(idsed)->idsedRight == idsedStart)
            rcRel = rc;
    }
    qdsed = _Qdsed(idsedStart);
    qdsed->rcRel = rcRel;

    for (idsed = idsedStart; ivNil != idsed; idsed = _IdsedNext(idsed, idsedStart))
    {
        qdsed = _Qdsed(idsed);
        if (ivNil != qdsed->idsedLeft)
        {
            // internal node - no DocumentScrollGraphicsObject
            DSED dsed;

            Assert(ivNil != qdsed->idsedRight, "bad node");
            dsed = *qdsed;
            _SplitRcRel(idsed, &rcRel, &rc);
            _Qdsed(dsed.idsedLeft)->rcRel = rcRel;
            _Qdsed(dsed.idsedRight)->rcRel = rc;
            continue;
        }

        // this is a leaf
        Assert(ivNil == qdsed->idsedRight, "bad node");
        rcRel = qdsed->rcRel;
        pdsg = qdsed->pdsg;
        AssertPo(pdsg, 0);
        pdsg->GetMinMax(&rc);
        rcDsg.xpLeft = LwMulDiv(dxpDmw, rcRel.xpLeft, krelOne);
        rcDsg.xpRight = LwMulDiv(dxpDmw, rcRel.xpRight, krelOne);
        rcDsg.ypTop = LwMulDiv(dypDmw, rcRel.ypTop, krelOne);
        rcDsg.ypBottom = LwMulDiv(dypDmw, rcRel.ypBottom, krelOne);
        if ((rcDsg.Dxp() < rc.xpLeft || rcDsg.Dyp() < rc.ypTop) && idsed != _idsedRoot)
        {
            // DocumentScrollGraphicsObject is becoming too small and it's not the only one, so nuke it
            // and restart this routine from the top.

            // Remove the dsg first so we don't recursively enter _Layout
            _RemoveDsg(pdsg, &idsedStart);
            ReleasePpo(&pdsg);
            goto LRestart;
        }
    }

    // now go through and actually set the positions
    for (idsed = idsedStart; ivNil != idsed; idsed = _IdsedNext(idsed, idsedStart))
    {
        qdsed = (DSED *)_paldsed->QvGet(idsed);
        if (ivNil != qdsed->idsedLeft)
            continue;

        // this is a leaf
        Assert(ivNil == qdsed->idsedRight, "bad node");
        rcRel = qdsed->rcRel;
        pdsg = qdsed->pdsg;
        AssertPo(pdsg, 0);

        pdsg->GetPos(pvNil, &rc);
        if (rc != rcRel)
            pdsg->SetPos(pvNil, &rcRel);
    }
    AssertThis(0);
}

/***************************************************************************
    Find the two child rc's from the DSED's rc.
***************************************************************************/
void DocumentMainWindow::_SplitRcRel(long idsed, RC *prcLeft, RC *prcRight)
{
    DSED *qdsed;

    qdsed = _Qdsed(idsed);
    *prcLeft = qdsed->rcRel;
    *prcRight = *prcLeft;
    if (qdsed->fVert)
    {
        prcRight->ypTop = prcLeft->ypBottom = prcLeft->ypTop + LwMulDiv(prcLeft->Dyp(), qdsed->rel, krelOne);
    }
    else
    {
        prcRight->xpLeft = prcLeft->xpRight = prcLeft->xpLeft + LwMulDiv(prcLeft->Dxp(), qdsed->rel, krelOne);
    }
}

/***************************************************************************
    Return the number of DSGs.
***************************************************************************/
long DocumentMainWindow::Cdsg(void)
{
    AssertThis(0);
    long idsed, cdsg;
    DSED *qdsed;

    if (pvNil == _paldsed)
        return 0;

    cdsg = 0;
    for (idsed = _paldsed->IvMac(); idsed-- != 0;)
    {
        if (_paldsed->FFree(idsed))
            continue;

        qdsed = _Qdsed(idsed);
        if (pvNil != qdsed->pdsg)
            cdsg++;
    }
    return cdsg;
}

/***************************************************************************
    Get the rectangles for the split associated with pdsg and for the area
    that the split affects.
***************************************************************************/
void DocumentMainWindow::GetRcSplit(PDocumentScrollGraphicsObject pdsg, RC *prcBounds, RC *prcSplit)
{
    AssertThis(0);
    AssertPo(pdsg, 0);
    RC rc;
    long idsedEdge;
    DSED *qdsed;
    long rel;

    if (dsnoNil == pdsg->_dsno)
        goto LZero;

    idsedEdge = _IdsedEdge(pdsg->_dsno - 1, ivNil);
    if (ivNil == idsedEdge)
    {
    LZero:
        prcBounds->Zero();
        prcSplit->Zero();
        return;
    }

    GetRc(&rc, cooLocal);
    qdsed = _Qdsed(idsedEdge);
    prcBounds->xpLeft = LwMulDiv(rc.xpRight, qdsed->rcRel.xpLeft, krelOne);
    prcBounds->xpRight = LwMulDiv(rc.xpRight, qdsed->rcRel.xpRight, krelOne);
    prcBounds->ypTop = LwMulDiv(rc.ypBottom, qdsed->rcRel.ypTop, krelOne);
    prcBounds->ypBottom = LwMulDiv(rc.ypBottom, qdsed->rcRel.ypBottom, krelOne);

    *prcSplit = *prcBounds;
    if (qdsed->fVert)
    {
        rel = qdsed->rcRel.ypTop + LwMulDiv(qdsed->rel, qdsed->rcRel.Dyp(), krelOne);
        prcSplit->ypBottom = LwMulDiv(rc.ypBottom, rel, krelOne);
        prcSplit->ypTop = prcSplit->ypBottom - ScrollBar::DypNormal();
    }
    else
    {
        rel = qdsed->rcRel.xpLeft + LwMulDiv(qdsed->rel, qdsed->rcRel.Dxp(), krelOne);
        prcSplit->xpRight = LwMulDiv(rc.xpRight, rel, krelOne);
        prcSplit->xpLeft = prcSplit->xpRight - ScrollBar::DxpNormal();
    }
}

/***************************************************************************
    Move the split corresponding to the given DocumentScrollGraphicsObject.
***************************************************************************/
void DocumentMainWindow::MoveSplit(PDocumentScrollGraphicsObject pdsg, long relNew)
{
    AssertThis(fobjAssertFull);
    AssertPo(pdsg, 0);
    AssertIn(relNew, 0, krelOne + 1);
    long idsedEdge;
    DSED *qdsed;

    if (dsnoNil == pdsg->_dsno)
        goto LZero;

    idsedEdge = _IdsedEdge(pdsg->_dsno - 1, ivNil);
    if (ivNil == idsedEdge)
    {
    LZero:
        Bug("split can't be moved");
        return;
    }

    // REVIEW shonk: if relNew is 0 or krelOne, we could nuke the
    // subtree first.  Then layout would be faster.
    qdsed = _Qdsed(idsedEdge);
    qdsed->rel = relNew;
    _Layout(idsedEdge);
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Determine whether the split corresponding to pdsg is vertical,
    horizontal or inactive.
***************************************************************************/
tribool DocumentMainWindow::TVert(PDocumentScrollGraphicsObject pdsg)
{
    AssertThis(0);
    AssertPo(pdsg, 0);
    long idsed;

    if (dsnoNil == pdsg->_dsno)
        return tMaybe;
    idsed = _IdsedEdge(pdsg->_dsno - 1, ivNil);
    if (ivNil == idsed)
        return tMaybe;
    return _Qdsed(idsed)->fVert ? tYes : tNo;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the DocumentMainWindow.
***************************************************************************/
void DocumentMainWindow::AssertValid(LegacyUlong grfobj)
{
    long cdsed, idsed;
    DSED dsed;
    DSED *qdsed;

    DocumentMainWindow_PAR::AssertValid(grfobj);
    AssertPo(_pdocb, grfobj & fobjAssertFull);

    if (pvNil == _paldsed || _paldsed->IvMac() == 0)
    {
        Assert(ivNil == _idsedRoot, "no paldsed but _idsedRoot not nil");
        return;
    }
    if (ivNil == _idsedRoot)
    {
        Assert(0 == _paldsed->IvMac(), "some DSEDs, but nil _idsedRoot");
        return;
    }

    if (!(grfobj & fobjAssertFull))
        return;

    // count the number of active dseds
    cdsed = 0;
    for (idsed = _paldsed->IvMac(); idsed-- != 0;)
    {
        if (!_paldsed->FFree(idsed))
            cdsed++;
    }

    for (idsed = _idsedRoot; ivNil != idsed;)
    {
        Assert(!_paldsed->FFree(idsed), "free node!");
        dsed = *_Qdsed(idsed);

        // verify the parent pointer
        Assert(FPure(idsed == _idsedRoot) == FPure(ivNil == dsed.idsedPar), "dsed.idsedPar isn't right");
        if (ivNil != dsed.idsedPar)
            Assert(!_paldsed->FFree(dsed.idsedPar), "free node!");

        if (ivNil == dsed.idsedLeft)
        {
            Assert(ivNil == dsed.idsedRight, "left nil, but right not");
            AssertPo(dsed.pdsg, 0);
            Assert(dsed.pdsg->_dsno == idsed + 1, "bad _dsno in DocumentScrollGraphicsObject");
        }
        else
        {
            Assert(!_paldsed->FFree(dsed.idsedLeft), "free left child!");
            Assert(ivNil != dsed.idsedRight, "non-nil left but nil right");
            Assert(!_paldsed->FFree(dsed.idsedRight), "free right child!");
            Assert(pvNil == dsed.pdsg, "nil pdsg expected");

            qdsed = _Qdsed(dsed.idsedLeft);
            Assert(qdsed->idsedPar == idsed, "left child's parent is wrong");
            qdsed = _Qdsed(dsed.idsedRight);
            Assert(qdsed->idsedPar == idsed, "right child's parent is wrong");
        }
        cdsed--;
        if (cdsed < 0)
        {
            Bug("dsed tree is mangled or has a loop");
            break;
        }

        // find the next node
        if (ivNil != dsed.idsedLeft)
        {
            idsed = dsed.idsedLeft;
            continue;
        }

        if (ivNil == (idsed = _IdsedEdge(idsed, ivNil)))
            break;
        Assert(!_paldsed->FFree(idsed), "bad edge");
        qdsed = _Qdsed(idsed);
        idsed = qdsed->idsedRight;
        Assert(ivNil != idsed, "idsedRight is nil");
    }

    Assert(cdsed == 0, "node count wrong");
}

/***************************************************************************
    Mark memory used by the DocumentMainWindow.
***************************************************************************/
void DocumentMainWindow::MarkMem(void)
{
    DocumentMainWindow_PAR::MarkMem();
    MarkMemObj(_paldsed);
}
#endif // DEBUG

BEGIN_CMD_MAP(DocumentScrollGraphicsObject, GraphicsObject)
ON_CID_ME(cidDoScroll, &DocumentScrollGraphicsObject::FCmdScroll, pvNil)
ON_CID_ME(cidEndScroll, &DocumentScrollGraphicsObject::FCmdScroll, pvNil)
END_CMD_MAP_NIL()

/***************************************************************************
    Static method to create a new DocumentScrollGraphicsObject.
***************************************************************************/
PDocumentScrollGraphicsObject DocumentScrollGraphicsObject::PdsgNew(PDocumentMainWindow pdmw, PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel)
{
    AssertPo(pdmw, 0);
    Assert(pvNil != pdsgSplit || pdmw->Cdsg() == 0, "must split an existing DocumentScrollGraphicsObject");
    PDocumentScrollGraphicsObject pdsg;
    GraphicsObjectBlock gcb(khidDsg, pdmw);

    if (pvNil == (pdsg = NewObj DocumentScrollGraphicsObject(&gcb)))
        return pvNil;

    if (!pdsg->_FInit(pdsgSplit, grfdsg, rel))
    {
        ReleasePpo(&pdsg);
        return pvNil;
    }
    AssertPo(pdsg, 0);
    return pdsg;
}

/***************************************************************************
    Constructor for DocumentScrollGraphicsObject.
***************************************************************************/
DocumentScrollGraphicsObject::DocumentScrollGraphicsObject(PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
    _dsno = dsnoNil;
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Destructor for DocumentScrollGraphicsObject - remove ourselves from the DocumentMainWindow.
***************************************************************************/
DocumentScrollGraphicsObject::~DocumentScrollGraphicsObject(void)
{
    Pdmw()->RemoveDsg(this);
}

/***************************************************************************
    Create the scroll bars, the DocumentDisplayGraphicsObject and do any other DocumentScrollGraphicsObject initialization.
***************************************************************************/
bool DocumentScrollGraphicsObject::_FInit(PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel)
{
    Assert(pvNil != pdsgSplit || Pdmw()->Cdsg() == 0, "must split an existing DocumentScrollGraphicsObject");
    PDocumentMainWindow pdmw;
    PDocumentBase pdocb;

    _fCreating = fTrue;

    // create the split mover
    if (pvNil == DocumentScrollSplitMover::PdssmNew(this))
        return fFalse;

    // Create the scroll bars and split boxes
    GraphicsObjectBlock gcb(khidVScroll, this);
    ScrollBar::GetStandardRc(fscbVert | fscbShowBottom | fscbShowRight, &gcb._rcAbs, &gcb._rcRel);
    gcb._rcAbs.ypTop += DocumentScrollWindowSplitter::DypNormal();
    if (pvNil == ScrollBar::PscbNew(&gcb, fscbVert) || pvNil == DocumentScrollWindowSplitter::PdsspNew(this, fdsspVert))
    {
        return fFalse;
    }

    gcb._hid = khidHScroll;
    ScrollBar::GetStandardRc(fscbHorz | fscbShowBottom | fscbShowRight, &gcb._rcAbs, &gcb._rcRel);
    gcb._rcAbs.xpLeft += DocumentScrollWindowSplitter::DxpNormal();
    if (pvNil == ScrollBar::PscbNew(&gcb, fscbHorz) || pvNil == DocumentScrollWindowSplitter::PdsspNew(this, fdsspHorz))
    {
        return fFalse;
    }

    // create a ddg
    pdmw = Pdmw();
    AssertBasePo(pdmw, 0);
    pdocb = pdmw->Pdocb();
    AssertBasePo(pdocb, 0);
    gcb._hid = khidDdg;
    ScrollBar::GetClientRc(fscbHorz | fscbVert | fscbShowBottom | fscbShowRight, &gcb._rcAbs, &gcb._rcRel);
    if (pvNil == (_pddg = pdocb->PddgNew(&gcb)))
        return fFalse;

    // add ourselves to the pdmw
    if (!pdmw->FAddDsg(this, pdsgSplit, grfdsg, rel))
        return fFalse;

    _fCreating = fFalse;
    return fTrue;
}

/***************************************************************************
    Get the min and max sizes for the DocumentScrollGraphicsObject.
***************************************************************************/
void DocumentScrollGraphicsObject::GetMinMax(RC *prcMinMax)
{
    AssertThis(0);
    long dxpScb = ScrollBar::DxpNormal();
    long dypScb = ScrollBar::DypNormal();

    AssertPo(_pddg, 0);
    _pddg->GetMinMax(prcMinMax);

    // impose our own min and add the scroll bar dimensions
    prcMinMax->xpLeft = dxpScb + LwMax(prcMinMax->xpLeft, DocumentScrollWindowSplitter::DxpNormal() + dxpScb);
    prcMinMax->ypTop = dypScb + LwMax(prcMinMax->ypTop, DocumentScrollWindowSplitter::DypNormal() + dypScb);
    prcMinMax->xpRight = LwMax(prcMinMax->xpLeft, dxpScb + prcMinMax->xpRight);
    prcMinMax->ypBottom = LwMax(prcMinMax->ypTop, dypScb + prcMinMax->ypBottom);
}

/***************************************************************************
    Split the DocumentScrollGraphicsObject into two dsg's.
***************************************************************************/
void DocumentScrollGraphicsObject::Split(ulong grfdsg, long rel)
{
    AssertThis(fobjAssertFull);
    PDocumentMainWindow pdmw;
    PDocumentBase pdocb;

    Assert(_dsno != dsnoNil, "why are we splitting an unattached DocumentScrollGraphicsObject?");
    pdmw = Pdmw();
    pdocb = pdmw->Pdocb();
    pdocb->PdsgNew(pdmw, this, grfdsg, rel);
}

/***************************************************************************
    A scroll bar has been hit.  Do the scroll.
***************************************************************************/
bool DocumentScrollGraphicsObject::FCmdScroll(PCommand pcmd)
{
    // just pass it on to the DocumentDisplayGraphicsObject
    AssertThis(0);
    Command cmd = *pcmd;

    cmd.pcmh = _pddg;
    cmd.pgg = pvNil;
    return _pddg->FCmdScroll(&cmd);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the DocumentScrollGraphicsObject.
***************************************************************************/
void DocumentScrollGraphicsObject::AssertValid(LegacyUlong grfobj)
{
    PDocumentMainWindow pdmw;

    DocumentScrollGraphicsObject_PAR::AssertValid(grfobj);
    pdmw = (PDocumentMainWindow)PgobPar();
    AssertBasePo(pdmw, 0);
}
#endif // DEBUG

/***************************************************************************
    Constructor for the splitter.
***************************************************************************/
DocumentScrollWindowSplitter::DocumentScrollWindowSplitter(PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
    AssertThis(0);
}

/***************************************************************************
    Static method to create a new split box.
***************************************************************************/
PDocumentScrollWindowSplitter DocumentScrollWindowSplitter::PdsspNew(PDocumentScrollGraphicsObject pdsg, ulong grfdssp)
{
    Assert(FPure(grfdssp & fdsspHorz) != FPure(grfdssp & fdsspVert),
           "must specify exactly one of (fdsspVert,fdsspHorz)");
    AssertPo(pdsg, 0);
    GraphicsObjectBlock gcb((grfdssp & fdsspVert) ? khidDsspVert : khidDsspHorz, pdsg);

    if (grfdssp & fdsspVert)
    {
        gcb._rcRel.xpLeft = gcb._rcRel.xpRight = krelOne;
        gcb._rcRel.ypTop = gcb._rcRel.ypBottom = krelZero;
        gcb._rcAbs.xpLeft = -ScrollBar::DxpNormal();
        gcb._rcAbs.ypTop = 0;
        gcb._rcAbs.xpRight = 0;
        gcb._rcAbs.ypBottom = DypNormal();
    }
    else
    {
        gcb._rcRel.xpLeft = gcb._rcRel.xpRight = krelZero;
        gcb._rcRel.ypTop = gcb._rcRel.ypBottom = krelOne;
        gcb._rcAbs.xpLeft = 0;
        gcb._rcAbs.ypTop = -ScrollBar::DypNormal();
        gcb._rcAbs.xpRight = DxpNormal();
        gcb._rcAbs.ypBottom = 0;
    }
    return NewObj DocumentScrollWindowSplitter(&gcb);
}

/***************************************************************************
    Draw the split box.
***************************************************************************/
void DocumentScrollWindowSplitter::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    RC rc;

    pgnv->GetRcSrc(&rc);
    pgnv->FillRc(&rc, kacrBlack);
}

/***************************************************************************
    See if the parent DocumentScrollGraphicsObject can be split.  If so, track the mouse and draw
    the gray outline until the user releases the mouse.
***************************************************************************/
void DocumentScrollWindowSplitter::MouseDown(long xp, long yp, long cact, ulong grfcust)
{
    AssertThis(0);

    long dzpMinDsg, dzpDsg, zpMin, zpLast, dzp, zp;
    RC rc;
    bool fVert = Hid() == khidDsspVert;
    PDocumentScrollGraphicsObject pdsg = (PDocumentScrollGraphicsObject)PgobPar();
    PT pt(xp, yp);

    MapPt(&pt, cooLocal, cooParent);
    pdsg->GetMinMax(&rc);
    dzpMinDsg = fVert ? rc.ypTop : rc.xpLeft;
    pdsg->GetRc(&rc, cooLocal);
    dzpDsg = fVert ? rc.Dyp() : rc.Dxp();
    if (dzpDsg <= 2 * dzpMinDsg)
        return;

    if (fVert)
    {
        dzp = DypNormal();
        zpMin = pt.yp - dzp;
        rc.ypBottom = rc.ypTop + dzp;
    }
    else
    {
        dzp = DxpNormal();
        zpMin = pt.xp - dzp;
        rc.xpRight = rc.xpLeft + dzp;
    }
    zpLast = zpMin + dzpDsg;

    zp = pdsg->ZpDragRc(&rc, fVert, zpMin + dzp, zpMin + dzp, zpLast + 1, zpMin + dzpMinDsg, zpLast - dzpMinDsg + 1);
    if (FIn(zp, zpMin + dzpMinDsg, zpLast - dzpMinDsg + 1))
    {
        long rel;

        rel = LwMulDiv(zp - zpMin, krelOne, zpLast - zpMin) + 1;
        pdsg->Split(fVert, rel);
    }
}

/***************************************************************************
    Constructor for the split mover.
***************************************************************************/
DocumentScrollSplitMover::DocumentScrollSplitMover(PGraphicsObjectBlock pgcb) : GraphicsObject(pgcb)
{
}

/***************************************************************************
    Static method to create a new split mover.
***************************************************************************/
PDocumentScrollSplitMover DocumentScrollSplitMover::PdssmNew(PDocumentScrollGraphicsObject pdsg)
{
    AssertPo(pdsg, 0);
    PDocumentScrollSplitMover pdssm;
    GraphicsObjectBlock gcb(khidDssm, pdsg);

    gcb._rcRel.xpLeft = gcb._rcRel.xpRight = gcb._rcRel.ypTop = gcb._rcRel.ypBottom = krelOne;
    gcb._rcAbs.xpLeft = -ScrollBar::DxpNormal();
    gcb._rcAbs.ypTop = -ScrollBar::DypNormal();
    gcb._rcAbs.xpRight = gcb._rcAbs.ypBottom = 0;
    pdssm = NewObj DocumentScrollSplitMover(&gcb);
    return pdssm;
}

/***************************************************************************
    Draw the split mover.
***************************************************************************/
void DocumentScrollSplitMover::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    RC rc;
    tribool tVert;

    tVert = TVert();
    if (tMaybe == tVert)
        return;

    // REVIEW shonk: split mover: need an appropriate icon
    GetRc(&rc, cooLocal);
    pgnv->FrameRc(&rc, kacrBlack);
    rc.Inset(1, 1);
    pgnv->FillRc(&rc, tVert == tYes ? kacrGreen : kacrRed);
}

/***************************************************************************
    Track the mouse and change the split location.
***************************************************************************/
void DocumentScrollSplitMover::MouseDown(long xp, long yp, long cact, ulong grfcust)
{
    AssertThis(0);
    long zp, zpMin, zpLast, zpOrig, dzpMinDsg, dzp;
    RC rc, rcSplit;
    PDocumentScrollGraphicsObject pdsg;
    PDocumentMainWindow pdmw;
    tribool tVert;
    PT pt(xp, yp);

    tVert = TVert();
    if (tMaybe == tVert)
        return;

    pdsg = (PDocumentScrollGraphicsObject)PgobPar();
    AssertPo(pdsg, 0);
    pdsg->GetMinMax(&rc);
    dzpMinDsg = (tYes == tVert) ? rc.ypTop : rc.xpLeft;
    pdmw = pdsg->Pdmw();
    AssertPo(pdmw, 0);

    MapPt(&pt, cooLocal, cooGlobal);
    pdmw->MapPt(&pt, cooGlobal, cooLocal);
    pdmw->GetRcSplit(pdsg, &rc, &rcSplit);
    if (tYes == tVert)
    {
        zpOrig = pt.yp;
        dzp = rcSplit.Dyp();
        zpMin = zpOrig + rc.ypTop - rcSplit.ypBottom;
        zpLast = zpOrig + rc.ypBottom - rcSplit.ypBottom;
    }
    else
    {
        zpOrig = pt.xp;
        dzp = rcSplit.Dxp();
        zpMin = zpOrig + rc.xpLeft - rcSplit.xpRight;
        zpLast = zpOrig + rc.xpRight - rcSplit.xpRight;
    }

    zp = pdmw->ZpDragRc(&rcSplit, tYes == tVert, zpOrig, zpMin + dzp, zpLast + 1, zpMin + dzpMinDsg,
                        zpLast + 1 - dzpMinDsg);
    if (zp != zpOrig)
    {
        long rel;

        if (zp < zpMin + dzpMinDsg)
            rel = 0;
        else if (zp > zpLast - dzpMinDsg)
            rel = krelOne;
        else
            rel = LwMulDiv(zp - zpMin, krelOne, zpLast - zpMin) + 1;
        pdmw->MoveSplit(pdsg, rel);
    }
}

/***************************************************************************
    Determine if we are disabled (tMaybe) or if not whether we are vertical
    (tYes) or horizontal (tNo).
***************************************************************************/
tribool DocumentScrollSplitMover::TVert(void)
{
    PDocumentScrollGraphicsObject pdsg;
    PDocumentMainWindow pdmw;

    pdsg = (PDocumentScrollGraphicsObject)PgobPar();
    pdmw = pdsg->Pdmw();
    return pdmw->TVert(pdsg);
}
