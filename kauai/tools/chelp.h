/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    Main include file for the help authoring tool.

***************************************************************************/
#ifndef CHELP_H
#define CHELP_H

#include "kidframe.h"
#include "chelpexp.h"
#include "chelpres.h"

extern PStringRegistry vpstrg;
extern SC_LID vsclid;
extern PSPLC vpsplc;

enum
{
    khidLigButton = khidLimFrame,
    khidLigPicture,
};

// creator type for the help editor
#define kctgChelp 'CHLP'

typedef class LID *PLID;
typedef class LIG *PLIG;
typedef class HETD *PHETD;

/***************************************************************************
    App class
***************************************************************************/
#define APP_PAR ApplicationBase
#define kclsAPP 'APP'
class APP : public APP_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(APP)

  protected:
    PChunkyResourceManager _pcrm;
    PLID _plidPicture;
    PLID _plidButton;

    virtual bool _FInit(ulong grfapp, ulong grfgob, long ginDef);
    virtual void _FastUpdate(PGraphicsObject pgob, PREGN pregnClip, ulong grfapp = fappNil, PGPT pgpt = pvNil);

  public:
    virtual void GetStnAppName(PSTN pstn);
    virtual void UpdateHwnd(HWND hwnd, RC *prc, ulong grfapp = fappNil);

    virtual bool FCmdOpen(PCMD pcmd);
    virtual bool FCmdLoadResFile(PCMD pcmd);
    virtual bool FCmdChooseLanguage(PCMD pcmd);
    virtual bool FEnableChooseLanguage(PCMD pcmd, ulong *pgrfeds);

    PLIG PligNew(bool fButton, PGCB pgcb, PTextDocument ptxhd);
    bool FLoadResFile(PFilename pfni);
    bool FOpenDocFile(PFilename pfni, long cid = cidOpen);
};
extern APP vapp;

/***************************************************************************
    List document
***************************************************************************/
#define LID_PAR DocumentBase
#define kclsLID 'LID'
class LID : public LID_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    struct CACH
    {
        PChunkyResourceFile pcrf;
        ChunkNumber cno;
        ChunkNumber cnoMbmp;
    };

    PChunkyResourceManager _pcrm;   // where to look for the chunks
    ChunkTag _ctg;     // what ctg to look for
    ChildChunkID _chid;   // what chid value the MBMP should be at (if _ctg is not MBMP)
    PDynamicArray _pglcach; // list of the chunks that we found

    LID(void);
    ~LID(void);

    bool _FInit(PChunkyResourceManager pcrm, ChunkTag ctg, ChildChunkID chid);

  public:
    static PLID PlidNew(PChunkyResourceManager pcrm, ChunkTag ctg, ChildChunkID chid = 0);

    bool FRefresh(void);
    long Ccki(void);
    void GetCki(long icki, ChunkIdentification *pcki, PChunkyResourceFile *ppcrf = pvNil);
    PMBMP PmbmpGet(long icki);
};

/***************************************************************************
    List display gob
***************************************************************************/
const long kdxpCellLig = kdzpInch * 2;
const long kdypCellLig = kdzpInch;

typedef class LIG *PLIG;
#define LIG_PAR DocumentDisplayGraphicsObject
#define kclsLIG 'LIG'
class LIG : public LIG_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(LIG)

  protected:
    PTextDocument _ptxhd;  // the document to put the chunk in
    PSCB _pscb;    // our scroll bar
    long _dypCell; // how tall are our cells

    LIG(PLID plid, GraphicsObjectBlock *pgcb);
    bool _FInit(PTextDocument ptxhd, long dypCell);

  public:
    static PLIG PligNew(PLID plid, GraphicsObjectBlock *pgcb, PTextDocument ptxhd, long dypCell = kdypCellLig);

    PLID Plid(void);
    void Refresh(void);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdScroll(PCMD pcmd);
};

/***************************************************************************
    Color chooser GraphicsObject.
***************************************************************************/
const long kcacrCcg = 8;
const long kdxpCcg = 78;
const long kdxpFrameCcg = 2;

typedef class CCG *PCCG;
#define CCG_PAR GraphicsObject
#define kclsCCG 'CCG'
class CCG : public CCG_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PTextDocument _ptxhd;     // the document to put the color in
    long _cacrRow;    // how many colors to put on a row
    bool _fForeColor; // whether this sets the foreground or background color

    bool _FGetAcrFromPt(long xp, long yp, AbstractColor *pacr, RC *prc = pvNil, long *piscr = pvNil);

  public:
    CCG(GraphicsObjectBlock *pgcb, PTextDocument ptxhd, bool fForeColor, long cacrRow = kcacrCcg);

    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd);

    virtual bool FEnsureToolTip(PGraphicsObject *ppgobCurTip, long xpMouse, long ypMouse);
};

/***************************************************************************
    Color chooser tool tip.
***************************************************************************/
typedef class CCGT *PCCGT;
#define CCGT_PAR GraphicsObject
#define kclsCCGT 'CCGT'
class CCGT : public CCGT_PAR
{
    RTCLASS_DEC

  protected:
    AbstractColor _acr;
    STN _stn;

  public:
    CCGT(PGCB pgcb, AbstractColor acr = kacrBlack, PSTN pstn = pvNil);

    void SetAcr(AbstractColor acr, PSTN pstn = pvNil);
    AbstractColor AcrCur(void)
    {
        return _acr;
    }

    virtual void Draw(PGNV pgnv, RC *prcClip);
};

/***************************************************************************
    Help editor doc - consists of a ChunkyFile containing (possibly) multiple
    topics.
***************************************************************************/
typedef class HEDO *PHEDO;
#define HEDO_PAR DocumentBase
#define kclsHEDO 'HEDO'
class HEDO : public HEDO_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PChunkyFile _pcfl; // the chunky file
    PRCA _prca; // the resources

    HEDO(void);
    ~HEDO(void);

  public:
    static PHEDO PhedoNew(Filename *pfni, PRCA prca);

    PChunkyFile Pcfl(void)
    {
        return _pcfl;
    }
    PRCA Prca(void)
    {
        return _prca;
    }
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
    virtual bool FGetFni(Filename *pfni);
    virtual bool FGetFniSave(Filename *pfni);
    virtual bool FSaveToFni(Filename *pfni, bool fSetFni);

    virtual void InvalAllDdg(ChunkNumber cno);
    virtual bool FExportText(void);
    virtual void DoFindNext(PHETD phetd, ChunkNumber cno, bool fAdvance = fTrue);

    virtual PHETD PhetdOpenNext(PHETD phetd);
    virtual PHETD PhetdOpenPrev(PHETD phetd);
};

/***************************************************************************
    TSEL: used to track a selection in a chunky file doc
***************************************************************************/
#define TSEL_PAR BASE
#define kclsTSEL 'TSEL'
class TSEL : public TSEL_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PChunkyFile _pcfl;
    long _icki;
    ChunkNumber _cno;

    void _SetNil(void);

  public:
    TSEL(PChunkyFile pcfl);

    void Adjust(void);

    long Icki(void)
    {
        return _icki;
    }
    ChunkNumber Cno(void)
    {
        return _cno;
    }

    bool FSetIcki(long icki);
    bool FSetCno(ChunkNumber cno);
};

/***************************************************************************
    Help editor document display GraphicsObject - displays a HEDO.
***************************************************************************/
typedef class HEDG *PHEDG;
#define HEDG_PAR DocumentDisplayGraphicsObject
#define kclsHEDG 'HEDG'
class HEDG : public HEDG_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(HEDG)
    ASSERT

  protected:
    long _onn;       // fixed width font to use
    long _dypHeader; // height of the header
    long _dypLine;   // height of one line
    long _dxpChar;   // width of a character
    long _dypBorder; // height of border (included in _dypLine)
    PChunkyFile _pcfl;      // the chunky file
    TSEL _tsel;      // the selection

    HEDG(PHEDO phedo, PChunkyFile pcfl, PGCB pgcb);
    virtual void _Scroll(long scaHorz, long scaVert, long scvHorz = 0, long scvVert = 0);
    virtual void _ScrollDxpDyp(long dxp, long dyp);

    long _YpFromIcki(long icki)
    {
        return LwMul(icki - _scvVert, _dypLine) + _dypHeader;
    }
    long _XpFromIch(long ich)
    {
        return LwMul(ich - _scvHorz + 1, _dxpChar);
    }
    long _IckiFromYp(long yp);
    void _GetContent(RC *prc);

    void _DrawSel(PGNV pgnv);
    void _SetSel(long icki, ChunkNumber cno = cnoNil);
    void _ShowSel(void);
    void _EditTopic(ChunkNumber cno);

    virtual void _Activate(bool fActive);
    virtual long _ScvMax(bool fVert);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);
    virtual void _ClearSel(void);
    virtual bool _FPaste(PClipboardObject pclip, bool fDoIt, long cid);

#ifdef WIN
    void _StartPage(PGNV pgnv, PSTN pstnDoc, long lwPage, RC *prcPage, long onn);
#endif // WIN

  public:
    static PHEDG PhedgNew(PHEDO phedo, PChunkyFile pcfl, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    virtual bool FCmdKey(PCMD_KEY pcmd);

    virtual void InvalCno(ChunkNumber cno);
    virtual bool FEnableHedgCmd(PCMD pcmd, ulong *pgrfeds);
    virtual bool FCmdNewTopic(PCMD pcmd);
    virtual bool FCmdEditTopic(PCMD pcmd);
    virtual bool FCmdDeleteTopic(PCMD pcmd);
    virtual bool FCmdExport(PCMD pcmd);
    virtual bool FCmdFind(PCMD pcmd);
    virtual bool FCmdPrint(PCMD pcmd);
    virtual bool FCmdCheckSpelling(PCMD pcmd);
    virtual bool FCmdDump(PCMD pcmd);

    PHEDO Phedo(void)
    {
        return (PHEDO)_pdocb;
    }
};

/***************************************************************************
    Help editor topic doc - for editing a single topic in a HEDO.
    An instance of this class is a child doc of a HEDO.
***************************************************************************/
#define HETD_PAR TextDocument
#define kclsHETD 'HETD'
class HETD : public HETD_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PChunkyFile _pcfl; // which chunk is being edited
    ChunkNumber _cno;
    PStringTable _pgst;   // string versions of stuff in Topic
    STN _stnDesc; // description

    HETD(PDocumentBase pdocb, PRCA prca, PChunkyFile pcfl, ChunkNumber cno);
    ~HETD(void);

    virtual bool _FReadChunk(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, bool fCopyText);

  public:
    static PHETD PhetdNew(PDocumentBase pdocb, PRCA prca, PChunkyFile pcfl, ChunkNumber cno);
    static PHETD PhetdFromChunk(PDocumentBase pdocb, ChunkNumber cno);
    static void CloseDeletedHetd(PDocumentBase pdocb);

    virtual PDocumentMDIWindow PdmdNew(void);
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
    virtual void GetName(PSTN pstn);
    virtual bool FSave(long cid);

    virtual bool FSaveToChunk(PChunkyFile pcfl, ChunkIdentification *pcki, bool fRedirectText = fFalse);

    void EditHtop(void);
    bool FDoFind(long cpMin, long *pcpMin, long *pcpLim);
    bool FDoReplace(long cp1, long cp2, long *pcpMin, long *pcpLim);

    PHEDO Phedo(void)
    {
        return (PHEDO)PdocbPar();
    }
    ChunkNumber Cno(void)
    {
        return _cno;
    }

    void GetHtopStn(long istn, PSTN pstn);
};

/***************************************************************************
    DocumentDisplayGraphicsObject for an HETD.  Help text document editing gob.
***************************************************************************/
typedef class HETG *PHETG;
#define HETG_PAR RichTextDocumentGraphicsObject
#define kclsHETG 'HETG'
class HETG : public HETG_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(HETG)

  protected:
    HETG(PHETD phetd, PGCB pgcb);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);

    // override these so we can put up our dialogs
    virtual bool _FGetOtherSize(long *pdypFont);
    virtual bool _FGetOtherSubSuper(long *pdypOffset);

    // we have our own ruler
    virtual long _DypTrul(void);
    virtual PTRUL _PtrulNew(PGCB pgcb);

    // override _DrawLinExtra so we can put boxes around grouped text.
    virtual void _DrawLinExtra(PGNV pgnv, PRC prcClip, LIN *plin, long dxp, long yp, ulong grftxtg);

  public:
    static PHETG PhetgNew(PHETD phetd, PGCB pgcb);

    virtual void InvalCp(long cp, long ccpIns, long ccpDel);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FInsertPicture(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno);
    virtual bool FInsertButton(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno);

    virtual bool FCmdGroupText(PCMD pcmd);
    virtual bool FCmdFormatPicture(PCMD pcmd);
    virtual bool FCmdFormatButton(PCMD pcmd);
    virtual bool FEnableHetgCmd(PCMD pcmd, ulong *pgrfeds);
    virtual bool FCmdEditHtop(PCMD pcmd);
    virtual bool FCmdInsertEdit(PCMD pcmd);
    virtual bool FCmdFormatEdit(PCMD pcmd);
    virtual bool FCmdFind(PCMD pcmd);
    virtual bool FCmdPrint(PCMD pcmd);
    virtual bool FCmdLineSpacing(PCMD pcmd);
    virtual bool FCmdNextTopic(PCMD pcmd);
    virtual bool FCmdCheckSpelling(PCMD pcmd);
    virtual bool FCmdFontDialog(PCMD pcmd);

    virtual bool FCheckSpelling(long *pcactChanges);

    PHETD Phetd(void)
    {
        return (PHETD)_ptxtb;
    }

    long DypLine(long ilin);
};

const long kstidFind = 1;
const long kstidReplace = 2;

/***************************************************************************
    The ruler for a help text document.
***************************************************************************/
typedef class HTRU *PHTRU;
#define HTRU_PAR TRUL
#define kclsHTRU 'HTRU'
class HTRU : public HTRU_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    // ruler track type
    enum
    {
        rttNil,
        krttTab,
        krttDoc
    };

    PTextDocumentGraphicsObject _ptxtg;
    long _dxpTab;
    long _dxpDoc;
    long _dyp;
    long _xpLeft;
    long _dxpTrack;
    long _rtt;
    long _onn;
    long _dypFont;
    ulong _grfont;

    HTRU(GraphicsObjectBlock *pgcb, PTextDocumentGraphicsObject ptxtg);

  public:
    static PHTRU PhtruNew(GraphicsObjectBlock *pgcb, PTextDocumentGraphicsObject ptxtg, long dxpTab, long dxpDoc, long dypDoc, long xpLeft, long onn,
                          long dypFont, ulong grfont);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd);

    virtual void SetDxpTab(long dxpTab);
    virtual void SetDxpDoc(long dxpDoc);
    virtual void SetXpLeft(long xpLeft);

    virtual void SetDypHeight(long dyp);
};

#endif //! CHELP_H
