/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Base document class and its supporting gobs.

***************************************************************************/
#include "frame.h"
ASSERTNAME

#define dsnoNil 0
long DocumentBase::_cactLast = 0;
PDocumentBase DocumentBase::_pdocbFirst = pvNil;

BEGIN_CMD_MAP(DocumentDisplayGraphicsObject, GraphicsObject)
ON_CID_GEN(cidClose, &DocumentDisplayGraphicsObject::FCmdCloseDoc, pvNil)
ON_CID_GEN(cidSaveAndClose, &DocumentDisplayGraphicsObject::FCmdCloseDoc, pvNil)
ON_CID_GEN(cidSave, &DocumentDisplayGraphicsObject::FCmdSave, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidSaveAs, &DocumentDisplayGraphicsObject::FCmdSave, pvNil)
ON_CID_GEN(cidSaveCopy, &DocumentDisplayGraphicsObject::FCmdSave, pvNil)
ON_CID_GEN(cidSelectAll, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidCut, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidCopy, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidPaste, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidPasteSpecial, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidClear, &DocumentDisplayGraphicsObject::FCmdClip, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidUndo, &DocumentDisplayGraphicsObject::FCmdUndo, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
ON_CID_GEN(cidRedo, &DocumentDisplayGraphicsObject::FCmdUndo, &DocumentDisplayGraphicsObject::FEnableDdgCmd)
END_CMD_MAP_NIL()

RTCLASS(DocumentBase)
RTCLASS(DTE)
RTCLASS(DocumentDisplayGraphicsObject)
RTCLASS(DocumentMDIWindow)
RTCLASS(DMW)
RTCLASS(DSG)
RTCLASS(DSSP)
RTCLASS(DSSM)
RTCLASS(UndoBase)

/***************************************************************************
    Constructor for DocumentBase
***************************************************************************/
DocumentBase::DocumentBase(PDocumentBase pdocb, ulong grfdoc) : CMH(khidDoc)
{
    _pdocbChd = pvNil;
    if (pvNil == pdocb)
    {
        _pdocbPar = pvNil;
        _pdocbSib = _pdocbFirst;
        _pdocbFirst = this;
    }
    else if (grfdoc & fdocSibling)
    {
        AssertPo(pdocb, 0);
        _pdocbPar = pdocb->_pdocbPar;
        _pdocbSib = pdocb->_pdocbSib;
        pdocb->_pdocbSib = this;
    }
    else
    {
        AssertPo(pdocb, 0);
        _pdocbPar = pdocb;
        _pdocbSib = pdocb->_pdocbChd;
        pdocb->_pdocbChd = this;
    }

    _cundbMax = 10;
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    First calls Release on all direct child docb's of this DocumentBase.
    Finally calls delete on itself.
***************************************************************************/
void DocumentBase::Release(void)
{
    AssertThis(fobjAssertFull);
    PDocumentBase pdocb;
    PUndoBase pundb;

    if (--_cactRef > 0)
        return;

    Assert(Cddg() == 0, "why are there still DocumentDisplayGraphicsObject's open on this DocumentBase?");
    Assert(!_fFreeing, "we're recursing into the DocumentBase::Release!");
    _fFreeing = fTrue;

    if (pvNil != _pglpundb)
    {
        while (_pglpundb->FPop(&pundb))
            ReleasePpo(&pundb);
        ReleasePpo(&_pglpundb);
        _ipundbLimDone = 0;
    }

    while (pvNil != (pdocb = _pdocbChd))
    {
        pdocb->CloseAllDdg();
        if (pdocb == _pdocbChd)
        {
            // REVIEW shonk: Release: is this the right thing to do?  What if
            // someone else has a reference count to this child DocumentBase?
            Bug("why wasn't this child doc released?");
            ReleasePpo(&pdocb);
        }
    }

    delete this;
}

/***************************************************************************
    Close all DDGs on this DocumentBase.
***************************************************************************/
void DocumentBase::CloseAllDdg(void)
{
    PDocumentDisplayGraphicsObject pddg;
    PDocumentMDIWindow pdmd;

    if (pvNil != _pglpddg)
    {
        // the pddg's are removed from _hplpddg in RemoveDdg
        // Note that freeing one DocumentMDIWindow may end up nuking more than
        // one DocumentDisplayGraphicsObject.
        // REVIEW shonk: this assumes that no one else has a
        // reference count open on one of these DMDs or DDGs.
        AddRef(); // so we aren't freed in the loop
        while (_pglpddg->IvMac() > 0)
        {
            _pglpddg->Get(0, &pddg);
            if (pvNil != (pdmd = pddg->Pdmd()))
                ReleasePpo(&pdmd); // close the MDI window
            else
                ReleasePpo(&pddg); // close just the DocumentDisplayGraphicsObject
        }
        Release(); // balance our AddRef
    }
}

/***************************************************************************
    Destructor for the document class.
***************************************************************************/
DocumentBase::~DocumentBase(void)
{
    AssertThis(fobjAssertFull);
    PDocumentBase *ppdocb;

    Assert(_fFreeing, "Release not called first!");
    Assert(pvNil == _pdocbChd, "docb still has children");
    Assert(pvNil == _pglpddg || _pglpddg->IvMac() == 0, "doc is still being displayed by a window");

    if (vpclip->FDocIsClip(this))
    {
        Bug("The clipboard document is going away!");
        // these AddRef's are so our destructor doesn't get called when the
        // clipboard releases us!
        AddRef();
        AddRef();
        vpclip->Set();
    }

    // remove it from the sibling list
    for (ppdocb = pvNil != _pdocbPar ? &_pdocbPar->_pdocbChd : &_pdocbFirst; *ppdocb != this && pvNil != *ppdocb;
         ppdocb = &(*ppdocb)->_pdocbSib)
    {
    }
    if (*ppdocb == this)
        *ppdocb = _pdocbSib;
    else
        Bug("corrupt docb tree");

    ReleasePpo(&_pglpddg);
}

/***************************************************************************
    Static method: calls FQueryClose on all open docs.
***************************************************************************/
bool DocumentBase::FQueryCloseAll(ulong grfdoc)
{
    PDocumentBase pdocb;

    for (pdocb = _pdocbFirst; pvNil != pdocb; pdocb = pdocb->_pdocbSib)
    {
        if (!pdocb->FQueryClose(grfdoc))
            return fFalse;
    }
    return fTrue;
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
    tribool tRet;
    DTE dte;
    PDocumentBase pdocb;
    ulong grfdte;
    bool fForce = FPure(grfdoc & fdocForceClose);

    dte.Init(this);
    while (dte.FNextDoc(&pdocb, &grfdte))
    {
        if (!(grfdte & fdtePost) || !pdocb->FDirty())
            continue;

        if (!(grfdoc & fdocAssumeYes))
        {
            tRet = pdocb->_TQuerySave(fForce);
            if (tNo == tRet)
                continue;
            if (tMaybe == tRet)
                return false;
        }
        if (!pdocb->FSave())
        {
            if (!fForce)
                return false;
        }
    }
    return true;
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
    PDocumentDisplayGraphicsObject pddg;
    PDocumentMDIWindow pdmdT;
    long ipddg;

    if (pvNil == _pglpddg || _pglpddg->IvMac() == 0)
    {
        Bug("why are there no DDGs for this doc if there's a DocumentMDIWindow?");
        return fTrue;
    }

    for (ipddg = _pglpddg->IvMac(); ipddg-- != 0;)
    {
        _pglpddg->Get(ipddg, &pddg);
        AssertBasePo(pddg, 0);
        pdmdT = pddg->Pdmd();
        if (pdmdT != pdmd && pdmdT != pvNil)
        {
            // there's another window on this doc, so let this one close
            return fTrue;
        }
    }

    return FQueryClose(fdocNil);
}

/***************************************************************************
    Return whether this is an internal document.
***************************************************************************/
bool DocumentBase::FInternal(void)
{
    AssertThis(0);
    return _fInternal || vpclip->FDocIsClip(this);
}

/***************************************************************************
    Change this document's internal status.
***************************************************************************/
void DocumentBase::SetInternal(bool fInternal)
{
    AssertThis(0);
    _fInternal = FPure(fInternal);
}

/***************************************************************************
    Static method to return the DOC open on this fni (if there is one).
***************************************************************************/
PDocumentBase DocumentBase::PdocbFromFni(Filename *pfni)
{
    AssertPo(pfni, 0);
    PDocumentBase pdocb;
    Filename fni;

    for (pdocb = _pdocbFirst; pvNil != pdocb; pdocb = pdocb->_pdocbSib)
    {
        if (!pdocb->FGetFni(&fni))
            continue;
        if (fni.FEqual(pfni))
            return pdocb;
    }
    return pvNil;
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
    Filename fni;

    switch (cid)
    {
    default:
        Bug("why are we here?");
        return fFalse;

    case cidSave:
        if (FGetFni(&fni))
            break;
        // fall through
    case cidSaveAs:
    case cidSaveCopy:
        if (!FGetFniSave(&fni))
            return fFalse;
        break;
    }

    if (!FSaveToFni(&fni, cid != cidSaveCopy))
    {
        PushErc(ercCantSave);
        return fFalse;
    }
    if (cid != cidSaveCopy)
        UpdateName();
    return fTrue;
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
                            "", PszLit("All files\0*.*\0"), vwig.hwndApp);
}

/***************************************************************************
    Add the DocumentDisplayGraphicsObject to the list of DDGs displaying this document
***************************************************************************/
bool DocumentBase::FAddDdg(PDocumentDisplayGraphicsObject pddg)
{
    AssertThis(fobjAssertFull);
    AssertPo(pddg, 0);
    bool fT;

    if (pvNil == _pglpddg && pvNil == (_pglpddg = DynamicArray::PglNew(size(PDocumentDisplayGraphicsObject), 1)))
    {
        return fFalse;
    }
    fT = _pglpddg->FAdd(&pddg);
    AssertThis(fobjAssertFull);
    return fT;
}

/***************************************************************************
    Find the position of the pddg in the DocumentBase's list.
***************************************************************************/
bool DocumentBase::_FFindDdg(PDocumentDisplayGraphicsObject pddg, long *pipddg)
{
    AssertThis(0);
    AssertVarMem(pipddg);
    long ipddg, cpddg;
    PDocumentDisplayGraphicsObject pddgT;

    if (_pglpddg == pvNil)
        goto LFail;

    cpddg = _pglpddg->IvMac();
    for (ipddg = 0; ipddg < cpddg; ipddg++)
    {
        _pglpddg->Get(ipddg, &pddgT);
        if (pddgT == pddg)
        {
            *pipddg = ipddg;
            return fTrue;
        }
    }

LFail:
    TrashVar(pipddg);
    return fFalse;
}

/***************************************************************************
    Remove the pddg from the list of DDGs for this doc.
***************************************************************************/
void DocumentBase::RemoveDdg(PDocumentDisplayGraphicsObject pddg)
{
    AssertThis(fobjAssertFull);
    long ipddg;

    if (_FFindDdg(pddg, &ipddg))
        _pglpddg->Delete(ipddg);
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Make this DocumentDisplayGraphicsObject the first one in the DocumentBase's list.
***************************************************************************/
void DocumentBase::MakeFirstDdg(PDocumentDisplayGraphicsObject pddg)
{
    long ipddg;

    if (!_FFindDdg(pddg, &ipddg))
    {
        BugVar("pddg not in docb's list", &pddg);
        return;
    }

    if (ipddg != 0)
        _pglpddg->Move(ipddg, 0);
}

/***************************************************************************
    Return the iddg'th DocumentDisplayGraphicsObject displaying this doc.  If iddg is too big,
    return pvNil.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentBase::PddgGet(long iddg)
{
    AssertThis(0);
    AssertIn(iddg, 0, klwMax);
    PDocumentDisplayGraphicsObject pddg;

    if (pvNil == _pglpddg || iddg >= _pglpddg->IvMac())
        return pvNil;
    _pglpddg->Get(iddg, &pddg);
    AssertPo(pddg, 0);
    return pddg;
}

/***************************************************************************
    If there is an active DocumentDisplayGraphicsObject for this doc, return it.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentBase::PddgActive(void)
{
    AssertThis(0);
    PDocumentDisplayGraphicsObject pddg;

    pddg = PddgGet(0);
    if (pvNil == pddg || !pddg->FActive())
        return pvNil;
    return pddg;
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
    long ipddg;
    PDocumentDisplayGraphicsObject pddg;
    PDocumentMDIWindow pdmd;

    for (ipddg = 0; pvNil != (pddg = PddgGet(ipddg)); ipddg++)
    {
        pdmd = (PDocumentMDIWindow)pddg->PgobParFromCls(kclsDocumentMDIWindow);
        if (pvNil != pdmd)
        {
            GraphicsObject::MakeHwndActive(pdmd->HwndContainer());
            return;
        }
    }
}

/***************************************************************************
    Create a DMW for the document.
***************************************************************************/
PDMW DocumentBase::PdmwNew(PGCB pgcb)
{
    AssertThis(fobjAssertFull);
    return DMW::PdmwNew(this, pgcb);
}

/***************************************************************************
    Create a new DSG for the doc in the given DMW.
***************************************************************************/
PDSG DocumentBase::PdsgNew(PDMW pdmw, PDSG pdsgSplit, ulong grfdsg, long rel)
{
    AssertThis(fobjAssertFull);
    return DSG::PdsgNew(pdmw, pdsgSplit, grfdsg, rel);
}

/***************************************************************************
    Create a new DocumentDisplayGraphicsObject for the doc in the given DSG.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentBase::PddgNew(PGCB pgcb)
{
    AssertThis(fobjAssertFull);
    return DocumentDisplayGraphicsObject::PddgNew(this, pgcb);
}

/***************************************************************************
    Get the name of a default (untitled) document.
***************************************************************************/
void DocumentBase::GetName(PSTN pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    Filename fni;

    // REVIEW shonk: clipboard constant string.
    if (vpclip->FDocIsClip(this))
        *pstn = PszLit("Clipboard");
    else if (FGetFni(&fni))
        fni.GetLeaf(pstn);
    else
    {
        if (_cactUntitled == 0)
            _cactUntitled = ++_cactLast;
        pstn->FFormatSz(PszLit("Untitled %d"), _cactUntitled);
    }
}

/***************************************************************************
    Makes sure all windows displaying this document have the correct title.
***************************************************************************/
void DocumentBase::UpdateName(void)
{
    DTE dte;
    ulong grfdte;
    STN stn;
    long ipddg;
    PDocumentDisplayGraphicsObject pddg;
    PDocumentBase pdocb;
    PDocumentMDIWindow pdmd;

    dte.Init(this);
    while (dte.FNextDoc(&pdocb, &grfdte, fdteNil))
    {
        if (!(grfdte & fdtePre))
            continue;
        pdocb->GetName(&stn);
        for (ipddg = 0; pvNil != (pddg = pdocb->PddgGet(ipddg++));)
        {
            if (pvNil != (pdmd = pddg->Pdmd()))
                pdmd->SetHwndName(&stn);
        }
    }
}

/***************************************************************************
    Does a single Undo off the undo list.
***************************************************************************/
bool DocumentBase::FUndo()
{
    AssertThis(fobjAssertFull);
    PUndoBase pundb;

    if (pvNil == _pglpundb || _ipundbLimDone <= 0)
        return fFalse;

    _pglpundb->Get(_ipundbLimDone - 1, &pundb);
    if (!pundb->FUndo(this))
        return fFalse;
    _ipundbLimDone--;
    return fTrue;
}

/***************************************************************************
    Redoes a single undo off the undo list.
***************************************************************************/
bool DocumentBase::FRedo()
{
    AssertThis(fobjAssertFull);
    PUndoBase pundb;

    if (pvNil == _pglpundb || _ipundbLimDone >= _pglpundb->IvMac())
        return fFalse;

    _pglpundb->Get(_ipundbLimDone, &pundb);
    if (!pundb->FDo(this))
        return fFalse;
    _ipundbLimDone++;
    return fTrue;
}

/***************************************************************************
    Adds a single undo to the undo list, removing any Redo items.  Increments
    the ref count on the pundb if we keep a reference to it.  Assumes
    the action has already been done.
***************************************************************************/
bool DocumentBase::FAddUndo(PUndoBase pundb)
{
    AssertThis(fobjAssertFull);
    PUndoBase pundbT;
    bool fRet;

    if (_cundbMax == 0)
        return fTrue;

    if (pvNil == _pglpundb && pvNil == (_pglpundb = DynamicArray::PglNew(size(PUndoBase), 1)))
    {
        return fFalse;
    }

    ClearRedo();
    if (_cundbMax <= _pglpundb->IvMac())
    {
        _pglpundb->Get(0, &pundbT);
        _pglpundb->Delete(0);
        ReleasePpo(&pundbT);
    }

    while (!(fRet = _pglpundb->FPush(&pundb)) && _pglpundb->IvMac() > 0)
    {
        _pglpundb->Get(0, &pundbT);
        _pglpundb->Delete(0);
        ReleasePpo(&pundbT);
    }
    if (fRet)
        pundb->AddRef();

    _ipundbLimDone = _pglpundb->IvMac();
    AssertThis(fobjAssertFull);
    return fRet;
}

/***************************************************************************
    Delete all undo and redo records.
***************************************************************************/
void DocumentBase::ClearUndo(void)
{
    PUndoBase pundb;

    if (pvNil == _pglpundb)
        return;

    while (_pglpundb->FPop(&pundb))
        ReleasePpo(&pundb);
    _ipundbLimDone = 0;
}

/***************************************************************************
    Delete all redo records.
***************************************************************************/
void DocumentBase::ClearRedo(void)
{
    PUndoBase pundb;

    if (pvNil == _pglpundb)
        return;

    while (_ipundbLimDone < _pglpundb->IvMac())
    {
        AssertDo(_pglpundb->FPop(&pundb), 0);
        ReleasePpo(&pundb);
    }
}

/***************************************************************************
    Set the maximum allowable number of undoable operations.
***************************************************************************/
void DocumentBase::SetCundbMax(long cundbMax)
{
    AssertThis(fobjAssertFull);
    AssertIn(cundbMax, 0, kcbMax);
    long ipundbLimNew;
    PUndoBase pundb;

    _cundbMax = cundbMax;
    if (pvNil == _pglpundb || _pglpundb->IvMac() <= cundbMax)
        return;

    ipundbLimNew = LwMax(_ipundbLimDone, cundbMax);
    while (_pglpundb->IvMac() > ipundbLimNew)
    {
        AssertDo(_pglpundb->FPop(&pundb), 0);
        ReleasePpo(&pundb);
    }

    while (_pglpundb->IvMac() > cundbMax)
    {
        Assert(_ipundbLimDone == _pglpundb->IvMac(), 0);
        _pglpundb->Get(0, &pundb);
        _pglpundb->Delete(0);
        _ipundbLimDone--;
        ReleasePpo(&pundb);
    }
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Return the maximum number of undoable operations for this doc
***************************************************************************/
long DocumentBase::CundbMax(void)
{
    AssertThis(0);
    return _cundbMax;
}

/***************************************************************************
    Return the number of operations that can currently be undone.
***************************************************************************/
long DocumentBase::CundbUndo(void)
{
    AssertThis(0);
    return _ipundbLimDone;
}

/***************************************************************************
    Return the number of operations that can currently be redone.
***************************************************************************/
long DocumentBase::CundbRedo(void)
{
    if (pvNil == _pglpundb)
        return 0;
    return _pglpundb->IvMac() - _ipundbLimDone;
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
void DocumentBase::AssertValid(ulong grfdocb)
{
    long ipddg;
    PDocumentDisplayGraphicsObject pddg;
    long ipundb;
    PUndoBase pundb;

    DocumentBase_PAR::AssertValid(grfdocb & fobjAssertFull);
    AssertNilOrPo(_pglpddg, 0);

    if (!(grfdocb & fobjAssertFull))
        return;

    if (pvNil != _pglpddg)
    {
        for (ipddg = _pglpddg->IvMac(); ipddg-- != 0;)
        {
            _pglpddg->Get(ipddg, &pddg);
            AssertBasePo(pddg, 0);
        }
    }

    if (pvNil != _pglpundb)
    {
        AssertIn(_ipundbLimDone, 0, _pglpundb->IvMac() + 1);
        AssertIn(_pglpundb->IvMac(), 0, _cundbMax + 1);
        for (ipundb = _pglpundb->IvMac(); ipundb-- != 0;)
        {
            _pglpundb->Get(ipundb, &pundb);
            AssertPo(pundb, 0);
        }
    }
    else
    {
        Assert(_ipundbLimDone == 0, 0);
        AssertIn(_cundbMax, 0, kcbMax);
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
    MarkMemObj(_pglpddg);
    if (pvNil != _pglpundb)
    {
        MarkMemObj(_pglpundb);

        for (ipundb = _pglpundb->IvMac(); ipundb-- != 0;)
        {
            _pglpundb->Get(ipundb, &pundb);
            MarkMemObj(pundb);
        }
    }
}
#endif // DEBUG

/***************************************************************************
    Constructor for a document tree enumerator.
***************************************************************************/
DTE::DTE(void)
{
    _es = esDone;
}

/***************************************************************************
    Initialize a document tree enumerator.
***************************************************************************/
void DTE::Init(PDocumentBase pdocb)
{
    _pdocbRoot = pdocb;
    _pdocbCur = pvNil;
    _es = pdocb == pvNil ? esDone : esStart;
}

/***************************************************************************
    Goes to the next node in the sub tree being enumerated.  Returns false
    iff the enumeration is done.
***************************************************************************/
bool DTE::FNextDoc(PDocumentBase *ppdocb, ulong *pgrfdteOut, ulong grfdte)
{
    PDocumentBase pdocbT;

    *pgrfdteOut = fdteNil;
    switch (_es)
    {
    case esStart:
        _pdocbCur = _pdocbRoot;
        *pgrfdteOut |= fdteRoot;
        goto LCheckForKids;

    case esGoDown:
        if (!(grfdte & fdteSkipToSib))
        {
            pdocbT = _pdocbCur->_pdocbChd;
            if (pdocbT != pvNil)
            {
                _pdocbCur = pdocbT;
                goto LCheckForKids;
            }
        }
        // fall through
    case esGoLeft:
        // go to the sibling (if there is one) or parent
        if (_pdocbCur == _pdocbRoot)
        {
            _es = esDone;
            return fFalse;
        }
        pdocbT = _pdocbCur->_pdocbSib;
        if (pdocbT != pvNil)
        {
            _pdocbCur = pdocbT;
        LCheckForKids:
            *pgrfdteOut |= fdtePre;
            if (_pdocbCur->_pdocbChd == pvNil)
            {
                *pgrfdteOut |= fdtePost;
                _es = esGoLeft;
            }
            else
                _es = esGoDown;
        }
        else
        {
            // no more siblings, go to parent
            _pdocbCur = _pdocbCur->_pdocbPar;
            *pgrfdteOut |= fdtePost;
            if (_pdocbCur == _pdocbRoot)
            {
                _es = esDone;
                *pgrfdteOut |= fdteRoot;
            }
            else
                _es = esGoLeft;
        }
        break;

    case esDone:
        return fFalse;
    }

    *ppdocb = _pdocbCur;
    return fTrue;
}

/***************************************************************************
    Static method to create a new DocumentDisplayGraphicsObject.
***************************************************************************/
PDocumentDisplayGraphicsObject DocumentDisplayGraphicsObject::PddgNew(PDocumentBase pdocb, PGCB pgcb)
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
DocumentDisplayGraphicsObject::DocumentDisplayGraphicsObject(PDocumentBase pdocb, PGCB pgcb) : GraphicsObject(pgcb)
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
bool DocumentDisplayGraphicsObject::FEnableDdgCmd(PCMD pcmd, ulong *pgrfeds)
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
bool DocumentDisplayGraphicsObject::FCmdClip(PCMD pcmd)
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
bool DocumentDisplayGraphicsObject::FCmdCloseDoc(PCMD pcmd)
{
    if (_pdocb->FQueryClose(pcmd->cid == cidSaveAndClose ? fdocAssumeYes : fdocNil))
        _pdocb->CloseAllDdg();
    return fTrue;
}

/***************************************************************************
    Handle a save, save as or save a copy command.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdSave(PCMD pcmd)
{
    _pdocb->FSave(pcmd->cid);
    return fTrue;
}

/***************************************************************************
    Handle a save, save as or save a copy command.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdUndo(PCMD pcmd)
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
void DocumentDisplayGraphicsObject::Draw(PGNV pgnv, RC *prcClip)
{
    AssertThis(0);
    pgnv->FillRc(prcClip, _fActive ? kacrBlue : kacrMagenta);
}

/***************************************************************************
    Activate the selection.  Default activates the DocumentDisplayGraphicsObject.
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdActivateSel(PCMD pcmd)
{
    Activate(fTrue);
    return fTrue;
}

/***************************************************************************
    Scroll the DCD
***************************************************************************/
bool DocumentDisplayGraphicsObject::FCmdScroll(PCMD pcmd)
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
    PSCB pscb;
    PGraphicsObject pgob;

    pgob = PgobPar();
    if (pgob->FIs(kclsDSG))
    {
        if (pvNil != (pscb = (PSCB)pgob->PgobFromHid(khidVScroll)))
            pscb->SetValMinMax(_scvVert, 0, _ScvMax(fTrue));
        if (pvNil != (pscb = (PSCB)pgob->PgobFromHid(khidHScroll)))
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
void DocumentDisplayGraphicsObject::AssertValid(ulong grfobj)
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
    it and add a DMW.
***************************************************************************/
PDocumentMDIWindow DocumentMDIWindow::PdmdNew(PDocumentBase pdocb)
{
    AssertPo(pdocb, 0);
    PDocumentMDIWindow pdmd;
    STN stn;
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
    if (pvNil == WSB::PwsbNew(pdmd, fgobNil))
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
DocumentMDIWindow::DocumentMDIWindow(PDocumentBase pdocb, PGCB pgcb) : GraphicsObject(pgcb)
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
    GTE gte;
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
bool DocumentMDIWindow::FCmdCloseWnd(PCMD pcmd)
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
    Static method to create a new DMW (document window) based on the given
    document.
***************************************************************************/
PDMW DMW::PdmwNew(PDocumentBase pdocb, PGCB pgcb)
{
    PDMW pdmw;

    pdmw = NewObj DMW(pdocb, pgcb);
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
DMW::DMW(PDocumentBase pdocb, PGCB pgcb) : GraphicsObject(pgcb)
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
void DMW::Release(void)
{
    AssertThis(fobjAssertFull);
    if (_cactRef <= 1)
    {
        Assert(!_fFreeing, "we're recursing into the destructor!");
        _fFreeing = fTrue;
        ReleasePpo(&_paldsed);
        _idsedRoot = ivNil;
    }
    DMW_PAR::Release();
}

/***************************************************************************
    Create the actual mdi window, etc.
***************************************************************************/
bool DMW::_FInit(void)
{
    _fCreating = fTrue;

    // create a lone dsg
    if (pvNil == _pdocb->PdsgNew(this, pvNil, fdsgNil, krelOne))
        return fFalse;
    AssertPo(_paldsed, 0);
    Assert(_paldsed->IvMac() > 0, "the DSG wasn't added to the DMW");

    _fCreating = fFalse;
    return fTrue;
}

/***************************************************************************
    The DMW has been resized, make sure no DSGs are too small.
***************************************************************************/
void DMW::_NewRc(void)
{
    AssertThis(0);
    _Layout(_idsedRoot);
}

/***************************************************************************
    Add the dsg to the dmw (the dsg is already a child gob - we now promote
    it to a full fledged child dsg).
***************************************************************************/
bool DMW::FAddDsg(PDSG pdsg, PDSG pdsgSplit, ulong grfdsg, long rel)
{
    AssertThis(fobjAssertFull);
    AssertIn(rel, 0, krelOne + 1);
    DSED dsed;
    DSED *qdsed;
    long idsedSplit, idsedPar, idsedEdge, idsedNew;

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
        Bug("pdsgSplit is not a registered DSG");
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
void DMW::RemoveDsg(PDSG pdsg)
{
    long idsedStart;

    if (!_fFreeing)
    {
        _RemoveDsg(pdsg, &idsedStart);
        _Layout(idsedStart);
    }
}

/***************************************************************************
    Remove the DSG from the tree and set its _dsno to nil.
***************************************************************************/
void DMW::_RemoveDsg(PDSG pdsg, long *pidsedStartLayout)
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
long DMW::_IdsedEdge(long idsed, long idsedRoot)
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
long DMW::_IdsedNext(long idsed, long idsedRoot)
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
    Re-layout the DSGs in the DMW.  If any become too small, delete them.
***************************************************************************/
void DMW::_Layout(long idsedStart)
{
    AssertThis(0);
    RC rc, rcDsg;
    long dxpDmw, dypDmw;
    long idsed;
    DSED *qdsed;
    RC rcRel;
    PDSG pdsg;

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
            // internal node - no DSG
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
            // DSG is becoming too small and it's not the only one, so nuke it
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
void DMW::_SplitRcRel(long idsed, RC *prcLeft, RC *prcRight)
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
long DMW::Cdsg(void)
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
void DMW::GetRcSplit(PDSG pdsg, RC *prcBounds, RC *prcSplit)
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
        prcSplit->ypTop = prcSplit->ypBottom - SCB::DypNormal();
    }
    else
    {
        rel = qdsed->rcRel.xpLeft + LwMulDiv(qdsed->rel, qdsed->rcRel.Dxp(), krelOne);
        prcSplit->xpRight = LwMulDiv(rc.xpRight, rel, krelOne);
        prcSplit->xpLeft = prcSplit->xpRight - SCB::DxpNormal();
    }
}

/***************************************************************************
    Move the split corresponding to the given DSG.
***************************************************************************/
void DMW::MoveSplit(PDSG pdsg, long relNew)
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
tribool DMW::TVert(PDSG pdsg)
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
    Assert the validity of the DMW.
***************************************************************************/
void DMW::AssertValid(ulong grfobj)
{
    long cdsed, idsed;
    DSED dsed;
    DSED *qdsed;

    DMW_PAR::AssertValid(grfobj);
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
            Assert(dsed.pdsg->_dsno == idsed + 1, "bad _dsno in DSG");
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
    Mark memory used by the DMW.
***************************************************************************/
void DMW::MarkMem(void)
{
    DMW_PAR::MarkMem();
    MarkMemObj(_paldsed);
}
#endif // DEBUG

BEGIN_CMD_MAP(DSG, GraphicsObject)
ON_CID_ME(cidDoScroll, &DSG::FCmdScroll, pvNil)
ON_CID_ME(cidEndScroll, &DSG::FCmdScroll, pvNil)
END_CMD_MAP_NIL()

/***************************************************************************
    Static method to create a new DSG.
***************************************************************************/
PDSG DSG::PdsgNew(PDMW pdmw, PDSG pdsgSplit, ulong grfdsg, long rel)
{
    AssertPo(pdmw, 0);
    Assert(pvNil != pdsgSplit || pdmw->Cdsg() == 0, "must split an existing DSG");
    PDSG pdsg;
    GraphicsObjectBlock gcb(khidDsg, pdmw);

    if (pvNil == (pdsg = NewObj DSG(&gcb)))
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
    Constructor for DSG.
***************************************************************************/
DSG::DSG(PGCB pgcb) : GraphicsObject(pgcb)
{
    _dsno = dsnoNil;
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Destructor for DSG - remove ourselves from the DMW.
***************************************************************************/
DSG::~DSG(void)
{
    Pdmw()->RemoveDsg(this);
}

/***************************************************************************
    Create the scroll bars, the DocumentDisplayGraphicsObject and do any other DSG initialization.
***************************************************************************/
bool DSG::_FInit(PDSG pdsgSplit, ulong grfdsg, long rel)
{
    Assert(pvNil != pdsgSplit || Pdmw()->Cdsg() == 0, "must split an existing DSG");
    PDMW pdmw;
    PDocumentBase pdocb;

    _fCreating = fTrue;

    // create the split mover
    if (pvNil == DSSM::PdssmNew(this))
        return fFalse;

    // Create the scroll bars and split boxes
    GraphicsObjectBlock gcb(khidVScroll, this);
    SCB::GetStandardRc(fscbVert | fscbShowBottom | fscbShowRight, &gcb._rcAbs, &gcb._rcRel);
    gcb._rcAbs.ypTop += DSSP::DypNormal();
    if (pvNil == SCB::PscbNew(&gcb, fscbVert) || pvNil == DSSP::PdsspNew(this, fdsspVert))
    {
        return fFalse;
    }

    gcb._hid = khidHScroll;
    SCB::GetStandardRc(fscbHorz | fscbShowBottom | fscbShowRight, &gcb._rcAbs, &gcb._rcRel);
    gcb._rcAbs.xpLeft += DSSP::DxpNormal();
    if (pvNil == SCB::PscbNew(&gcb, fscbHorz) || pvNil == DSSP::PdsspNew(this, fdsspHorz))
    {
        return fFalse;
    }

    // create a ddg
    pdmw = Pdmw();
    AssertBasePo(pdmw, 0);
    pdocb = pdmw->Pdocb();
    AssertBasePo(pdocb, 0);
    gcb._hid = khidDdg;
    SCB::GetClientRc(fscbHorz | fscbVert | fscbShowBottom | fscbShowRight, &gcb._rcAbs, &gcb._rcRel);
    if (pvNil == (_pddg = pdocb->PddgNew(&gcb)))
        return fFalse;

    // add ourselves to the pdmw
    if (!pdmw->FAddDsg(this, pdsgSplit, grfdsg, rel))
        return fFalse;

    _fCreating = fFalse;
    return fTrue;
}

/***************************************************************************
    Get the min and max sizes for the DSG.
***************************************************************************/
void DSG::GetMinMax(RC *prcMinMax)
{
    AssertThis(0);
    long dxpScb = SCB::DxpNormal();
    long dypScb = SCB::DypNormal();

    AssertPo(_pddg, 0);
    _pddg->GetMinMax(prcMinMax);

    // impose our own min and add the scroll bar dimensions
    prcMinMax->xpLeft = dxpScb + LwMax(prcMinMax->xpLeft, DSSP::DxpNormal() + dxpScb);
    prcMinMax->ypTop = dypScb + LwMax(prcMinMax->ypTop, DSSP::DypNormal() + dypScb);
    prcMinMax->xpRight = LwMax(prcMinMax->xpLeft, dxpScb + prcMinMax->xpRight);
    prcMinMax->ypBottom = LwMax(prcMinMax->ypTop, dypScb + prcMinMax->ypBottom);
}

/***************************************************************************
    Split the DSG into two dsg's.
***************************************************************************/
void DSG::Split(ulong grfdsg, long rel)
{
    AssertThis(fobjAssertFull);
    PDMW pdmw;
    PDocumentBase pdocb;

    Assert(_dsno != dsnoNil, "why are we splitting an unattached DSG?");
    pdmw = Pdmw();
    pdocb = pdmw->Pdocb();
    pdocb->PdsgNew(pdmw, this, grfdsg, rel);
}

/***************************************************************************
    A scroll bar has been hit.  Do the scroll.
***************************************************************************/
bool DSG::FCmdScroll(PCMD pcmd)
{
    // just pass it on to the DocumentDisplayGraphicsObject
    AssertThis(0);
    CMD cmd = *pcmd;

    cmd.pcmh = _pddg;
    cmd.pgg = pvNil;
    return _pddg->FCmdScroll(&cmd);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the DSG.
***************************************************************************/
void DSG::AssertValid(ulong grfobj)
{
    PDMW pdmw;

    DSG_PAR::AssertValid(grfobj);
    pdmw = (PDMW)PgobPar();
    AssertBasePo(pdmw, 0);
}
#endif // DEBUG

/***************************************************************************
    Constructor for the splitter.
***************************************************************************/
DSSP::DSSP(PGCB pgcb) : GraphicsObject(pgcb)
{
    AssertThis(0);
}

/***************************************************************************
    Static method to create a new split box.
***************************************************************************/
PDSSP DSSP::PdsspNew(PDSG pdsg, ulong grfdssp)
{
    Assert(FPure(grfdssp & fdsspHorz) != FPure(grfdssp & fdsspVert),
           "must specify exactly one of (fdsspVert,fdsspHorz)");
    AssertPo(pdsg, 0);
    GraphicsObjectBlock gcb((grfdssp & fdsspVert) ? khidDsspVert : khidDsspHorz, pdsg);

    if (grfdssp & fdsspVert)
    {
        gcb._rcRel.xpLeft = gcb._rcRel.xpRight = krelOne;
        gcb._rcRel.ypTop = gcb._rcRel.ypBottom = krelZero;
        gcb._rcAbs.xpLeft = -SCB::DxpNormal();
        gcb._rcAbs.ypTop = 0;
        gcb._rcAbs.xpRight = 0;
        gcb._rcAbs.ypBottom = DypNormal();
    }
    else
    {
        gcb._rcRel.xpLeft = gcb._rcRel.xpRight = krelZero;
        gcb._rcRel.ypTop = gcb._rcRel.ypBottom = krelOne;
        gcb._rcAbs.xpLeft = 0;
        gcb._rcAbs.ypTop = -SCB::DypNormal();
        gcb._rcAbs.xpRight = DxpNormal();
        gcb._rcAbs.ypBottom = 0;
    }
    return NewObj DSSP(&gcb);
}

/***************************************************************************
    Draw the split box.
***************************************************************************/
void DSSP::Draw(PGNV pgnv, RC *prcClip)
{
    AssertThis(0);
    RC rc;

    pgnv->GetRcSrc(&rc);
    pgnv->FillRc(&rc, kacrBlack);
}

/***************************************************************************
    See if the parent DSG can be split.  If so, track the mouse and draw
    the gray outline until the user releases the mouse.
***************************************************************************/
void DSSP::MouseDown(long xp, long yp, long cact, ulong grfcust)
{
    AssertThis(0);

    long dzpMinDsg, dzpDsg, zpMin, zpLast, dzp, zp;
    RC rc;
    bool fVert = Hid() == khidDsspVert;
    PDSG pdsg = (PDSG)PgobPar();
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
DSSM::DSSM(PGCB pgcb) : GraphicsObject(pgcb)
{
}

/***************************************************************************
    Static method to create a new split mover.
***************************************************************************/
PDSSM DSSM::PdssmNew(PDSG pdsg)
{
    AssertPo(pdsg, 0);
    PDSSM pdssm;
    GraphicsObjectBlock gcb(khidDssm, pdsg);

    gcb._rcRel.xpLeft = gcb._rcRel.xpRight = gcb._rcRel.ypTop = gcb._rcRel.ypBottom = krelOne;
    gcb._rcAbs.xpLeft = -SCB::DxpNormal();
    gcb._rcAbs.ypTop = -SCB::DypNormal();
    gcb._rcAbs.xpRight = gcb._rcAbs.ypBottom = 0;
    pdssm = NewObj DSSM(&gcb);
    return pdssm;
}

/***************************************************************************
    Draw the split mover.
***************************************************************************/
void DSSM::Draw(PGNV pgnv, RC *prcClip)
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
void DSSM::MouseDown(long xp, long yp, long cact, ulong grfcust)
{
    AssertThis(0);
    long zp, zpMin, zpLast, zpOrig, dzpMinDsg, dzp;
    RC rc, rcSplit;
    PDSG pdsg;
    PDMW pdmw;
    tribool tVert;
    PT pt(xp, yp);

    tVert = TVert();
    if (tMaybe == tVert)
        return;

    pdsg = (PDSG)PgobPar();
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
tribool DSSM::TVert(void)
{
    PDSG pdsg;
    PDMW pdmw;

    pdsg = (PDSG)PgobPar();
    pdmw = pdsg->Pdmw();
    return pdmw->TVert(pdsg);
}
