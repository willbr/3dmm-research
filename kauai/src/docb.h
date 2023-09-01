/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    A base document class and its supporting gob classes.

***************************************************************************/
#ifndef DOCB_H
#define DOCB_H

/***************************************************************************
    base undo class
***************************************************************************/
typedef class UndoBase *PUndoBase;
#define UndoBase_PAR BASE
#define kclsUndoBase 'UNDB'
class UndoBase : public UndoBase_PAR
{
    RTCLASS_DEC
    NOCOPY(UndoBase)

  protected:
    UndoBase(void)
    {
    }

  public:
    // General undo funtionality
    virtual bool FUndo(PDocumentBase pdocb) = 0;
    virtual bool FDo(PDocumentBase pdocb) = 0;
};

/***************************************************************************
    base document class
***************************************************************************/
enum
{
    fdocNil = 0,
    fdocSibling = 1,
    fdocForceClose = 2, // for FQueryClose, etc
    fdocAssumeYes = 4,  // for FQueryClose, etc

    fdocUpdate = 8, // update associated DDGs
    fdocInval = 16, // invalidate associated DDGs
};

#define DocumentBase_PAR CommandHandler
#define kclsDocumentBase 'DOCB'
class DocumentBase : public DocumentBase_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

    friend class DTE;

  protected:
    static long _cactLast;
    static PDocumentBase _pdocbFirst;

    PDocumentBase _pdocbPar;
    PDocumentBase _pdocbSib;
    PDocumentBase _pdocbChd;

    long _cactUntitled; // 0 if titled
    bool _fDirty : 1;
    bool _fFreeing : 1;
    bool _fInternal : 1;
    PDynamicArray _pglpddg; // keep track of the DDGs based on this doc

    PDynamicArray _pglpundb; // keep track of undo items
    long _ipundbLimDone;
    long _cundbMax;

    bool _FFindDdg(PDocumentDisplayGraphicsObject pddg, long *pipddg);
    virtual tribool _TQuerySave(bool fForce);

    DocumentBase(PDocumentBase pdocb = pvNil, ulong grfdoc = fdocNil);
    ~DocumentBase(void);

  public:
    static bool FQueryCloseAll(ulong grfdoc);
    static PDocumentBase PdocbFromFni(Filename *pfni);

    static PDocumentBase PdocbFirst(void)
    {
        return _pdocbFirst;
    }
    PDocumentBase PdocbPar(void)
    {
        return _pdocbPar;
    }
    PDocumentBase PdocbSib(void)
    {
        return _pdocbSib;
    }
    PDocumentBase PdocbChd(void)
    {
        return _pdocbChd;
    }

    virtual void Release(void);

    // high level call to create a new MDI window based on the doc.
    virtual PDocumentMDIWindow PdmdNew(void);
    void ActivateDmd(void);

    // low level calls - generally not for public consumption
    virtual PDocumentMainWindow PdmwNew(PGCB pgcb);
    virtual PDocumentScrollGraphicsObject PdsgNew(PDocumentMainWindow pdwm, PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel);
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);

    // DocumentDisplayGraphicsObject management - only to be called by DDGs
    bool FAddDdg(PDocumentDisplayGraphicsObject pddg);
    void RemoveDdg(PDocumentDisplayGraphicsObject pddg);
    void MakeFirstDdg(PDocumentDisplayGraphicsObject pddg);
    void CloseAllDdg(void);

    // General DocumentDisplayGraphicsObject management
    long Cddg(void)
    {
        return pvNil == _pglpddg ? 0 : _pglpddg->IvMac();
    }
    PDocumentDisplayGraphicsObject PddgGet(long ipddg);
    PDocumentDisplayGraphicsObject PddgActive(void);

    virtual void UpdateName(void);
    virtual void GetName(PSTN pstn);
    virtual bool FQueryClose(ulong grfdoc);
    virtual bool FQueryCloseDmd(PDocumentMDIWindow pdmd);
    virtual bool FSave(long cid = cidSave);

    virtual bool FGetFni(Filename *pfni);
    virtual bool FGetFniSave(Filename *pfni);
    virtual bool FSaveToFni(Filename *pfni, bool fSetFni);
    virtual bool FDirty(void)
    {
        return _fDirty && !FInternal();
    }
    virtual void SetDirty(bool fDirty = fTrue)
    {
        _fDirty = FPure(fDirty);
    }

    // General undo funtionality
    virtual bool FUndo(void);
    virtual bool FRedo(void);
    virtual bool FAddUndo(PUndoBase pundb);
    virtual void ClearUndo(void);
    virtual void ClearRedo(void);
    virtual void SetCundbMax(long cundbMax);
    virtual long CundbMax(void);
    virtual long CundbUndo(void);
    virtual long CundbRedo(void);

    bool FInternal(void);
    void SetAsClipboard(void);
    void SetInternal(bool fInternal = fTrue);

    virtual void ExportFormats(PClipboardObject pclip);
    virtual bool FGetFormat(long cls, PDocumentBase *ppdocb = pvNil);
};

/***************************************************************************
    document tree enumerator
***************************************************************************/
enum
{
    // inputs
    fdteNil = 0,
    fdteSkipToSib = 1, // legal to FNextDoc

    // outputs
    fdtePre = 2,
    fdtePost = 4,
    fdteRoot = 8
};

#define DTE_PAR BASE
#define kclsDTE 'DTE'
class DTE : public DTE_PAR
{
    RTCLASS_DEC
    ASSERT

  private:
    // enumeration states
    enum
    {
        esStart,
        esGoDown,
        esGoLeft,
        esDone
    };

    long _es;
    PDocumentBase _pdocbRoot;
    PDocumentBase _pdocbCur;

  public:
    DTE(void);
    void Init(PDocumentBase pdocb);
    bool FNextDoc(PDocumentBase *ppdocb, ulong *pgrfdteOut, ulong grfdteIn = fdteNil);
};

/***************************************************************************
    document display gob - normally a child of a DocumentScrollGraphicsObject but can be a child
    of any gob (for doc previewing, etc)
***************************************************************************/
#define DocumentDisplayGraphicsObject_PAR GraphicsObject
#define kclsDocumentDisplayGraphicsObject 'DDG'
class DocumentDisplayGraphicsObject : public DocumentDisplayGraphicsObject_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(DocumentDisplayGraphicsObject)
    ASSERT
    MARKMEM

  protected:
    PDocumentBase _pdocb;
    bool _fActive;
    long _scvVert; // scroll values
    long _scvHorz;

    DocumentDisplayGraphicsObject(PDocumentBase pdocb, PGCB pgcb);
    ~DocumentDisplayGraphicsObject(void);

    virtual bool _FInit(void);
    virtual void _Activate(bool fActive);
    virtual void _NewRc(void);

    // scrolling support
    virtual long _ScvMax(bool fVert);
    virtual void _SetScrollValues(void);
    virtual void _Scroll(long scaHorz, long scaVert, long scvHorz = 0, long scvVert = 0);
    virtual void _ScrollDxpDyp(long dxp, long dyp);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);
    virtual void _ClearSel(void);
    virtual bool _FPaste(PClipboardObject pclip, bool fDoIt, long cid);

  public:
    static PDocumentDisplayGraphicsObject PddgNew(PDocumentBase pdocb, PGCB pgcb);

    PDocumentBase Pdocb(void)
    {
        return _pdocb;
    }
    PDocumentMDIWindow Pdmd(void);

    // activation
    virtual void Activate(bool fActive);
    bool FActive(void)
    {
        return _fActive;
    }

    // members of GraphicsObject
    virtual void Draw(PGraphicsEnvironment pgnv, RC *prcClip);
    virtual bool FCmdActivateSel(PCommand pcmd);

    virtual bool FCmdScroll(PCommand pcmd);
    virtual bool FCmdCloseDoc(PCommand pcmd);
    virtual bool FCmdSave(PCommand pcmd);
    virtual bool FCmdClip(PCommand pcmd);
    virtual bool FEnableDdgCmd(PCommand pcmd, ulong *pgrfeds);
    virtual bool FCmdUndo(PCommand pcmd);
};

/***************************************************************************
    Document mdi window - this communicates with the docb to coordinate
    closing and querying the user about saving
***************************************************************************/
#define DocumentMDIWindow_PAR GraphicsObject
#define kclsDocumentMDIWindow 'DMD'
class DocumentMDIWindow : public DocumentMDIWindow_PAR
{
    RTCLASS_DEC

  protected:
    PDocumentBase _pdocb;

    DocumentMDIWindow(PDocumentBase pdocb, PGCB pgcb);
    virtual void _ActivateHwnd(bool fActive);

  public:
    static PDocumentMDIWindow PdmdNew(PDocumentBase pdocb);
    static PDocumentMDIWindow PdmdTop(void);

    PDocumentBase Pdocb(void)
    {
        return _pdocb;
    }
    virtual void ActivateNext(PDocumentDisplayGraphicsObject pddg);
    virtual bool FCmdCloseWnd(PCommand pcmd);
};

/***************************************************************************
    Document main window
    provides basic pane management - including splitting, etc
***************************************************************************/
#define DocumentMainWindow_PAR GraphicsObject
#define kclsDocumentMainWindow 'DMW'
class DocumentMainWindow : public DocumentMainWindow_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // DocumentScrollGraphicsObject edge struct - these form a locally-balanced binary tree
    // with DSGs as the leafs.  Locally-balanced means that a node has a left
    // child iff it has a right child.
    struct DSED
    {
        bool fVert; // splits its parent vertically, so the edge is horizontal
        long rel;   // where it splits its parent
        RC rcRel;   // current relative rectangle (in the DocumentMainWindow)
        long idsedLeft;
        long idsedRight;
        long idsedPar;
        PDocumentScrollGraphicsObject pdsg;
    };
    PAllocatedArray _paldsed; // the tree of DSEDs
    long _idsedRoot;
    PDocumentBase _pdocb;

    DocumentMainWindow(PDocumentBase pdocb, PGCB pgcb);

    virtual bool _FInit(void);
    virtual void _NewRc(void);

    void _Layout(long idsedStart);
    long _IdsedNext(long idsed, long idsedRoot);
    long _IdsedEdge(long idsed, long idsedRoot);
    void _RemoveDsg(PDocumentScrollGraphicsObject pdsg, long *pidsedStartLayout);
    DSED *_Qdsed(long idsed)
    {
        return (DSED *)_paldsed->QvGet(idsed);
    }
    void _SplitRcRel(long idsed, RC *prcLeft, RC *prcRight);

  public:
    static PDocumentMainWindow PdmwNew(PDocumentBase pdocb, PGCB pgcb);

    PDocumentBase Pdocb(void)
    {
        return _pdocb;
    }

    bool FAddDsg(PDocumentScrollGraphicsObject pdsg, PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel);
    void RemoveDsg(PDocumentScrollGraphicsObject pdsg);
    long Cdsg(void);

    void GetRcSplit(PDocumentScrollGraphicsObject pdsg, RC *prcBounds, RC *prcSplit);
    void MoveSplit(PDocumentScrollGraphicsObject pdsg, long relNew);
    tribool TVert(PDocumentScrollGraphicsObject pdsg);

    virtual void Release(void);
};

/***************************************************************************
    document scroll gob - child gob of a DocumentMainWindow
    holds any scroll bars, splitter boxes and split movers
    dialogs tightly with DocumentMainWindow and DocumentDisplayGraphicsObject
***************************************************************************/
#define DocumentScrollGraphicsObject_PAR GraphicsObject
#define kclsDocumentScrollGraphicsObject 'DSG'
class DocumentScrollGraphicsObject : public DocumentScrollGraphicsObject_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(DocumentScrollGraphicsObject)
    ASSERT

    friend DocumentMainWindow;

  private:
    long _dsno; // this is how the DocumentMainWindow refers to this DocumentScrollGraphicsObject
    PDocumentDisplayGraphicsObject _pddg;

  protected:
    DocumentScrollGraphicsObject(PGCB pgcb);
    ~DocumentScrollGraphicsObject(void);

    virtual bool _FInit(PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel);

  public:
    static PDocumentScrollGraphicsObject PdsgNew(PDocumentMainWindow pdmw, PDocumentScrollGraphicsObject pdsgSplit, ulong grfdsg, long rel);
    virtual void GetMinMax(RC *prcMinMax);

    PDocumentMainWindow Pdmw(void)
    {
        return (PDocumentMainWindow)PgobPar();
    }

    virtual void Split(ulong grfdsg, long rel);
    virtual bool FCmdScroll(PCommand pcmd);
};

enum
{
    fdsgNil = 0,
    fdsgVert = 1, // for splitting and PdsgNew
    fdsgHorz = 2, // for splitting and PdsgNew
    fdsgAfter = 4 // for PdsgNew
};

/***************************************************************************
    document scroll window splitter - must be a child of a DocumentScrollGraphicsObject
***************************************************************************/
typedef class DocumentScrollWindowSplitter *PDocumentScrollWindowSplitter;
#define DocumentScrollWindowSplitter_PAR GraphicsObject
#define kclsDocumentScrollWindowSplitter 'DSSP'
class DocumentScrollWindowSplitter : public DocumentScrollWindowSplitter_PAR
{
    RTCLASS_DEC

  protected:
    DocumentScrollWindowSplitter(PGCB pgcb);

  public:
    static long DypNormal(void)
    {
        return SCB::DypNormal() / 2;
    }
    static long DxpNormal(void)
    {
        return SCB::DxpNormal() / 2;
    }
    static PDocumentScrollWindowSplitter PdsspNew(PDocumentScrollGraphicsObject pdsg, ulong grfdssp);

    virtual void Draw(PGraphicsEnvironment pgnv, RC *prcClip);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
};

enum
{
    fdsspNil = 0,
    fdsspVert = 1,
    fdsspHorz = 2
};

/***************************************************************************
    document scroll split mover - must be a child of a DocumentScrollGraphicsObject
***************************************************************************/
typedef class DSSM *PDSSM;
#define DSSM_PAR GraphicsObject
#define kclsDSSM 'DSSM'
class DSSM : public DSSM_PAR
{
    RTCLASS_DEC

  private:
    bool _fVert;

  protected:
    DSSM(PGCB pgcb);

    void _DrawTrackBar(PGraphicsEnvironment pgnv, RC *prcOld, RC *prcNew);

  public:
    static PDSSM PdssmNew(PDocumentScrollGraphicsObject pdsg);

    virtual void Draw(PGraphicsEnvironment pgnv, RC *prcClip);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    tribool TVert(void);
};

#endif //! DOCB_H
