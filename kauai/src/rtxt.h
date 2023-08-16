/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Rich text document and supporting views.

***************************************************************************/
#ifndef RTXT_H
#define RTXT_H

using namespace Chunky;

/***************************************************************************
    Character properties - if you change this, make sure to update
    FetchChp and _TGetLwFromChp.
***************************************************************************/
struct CHP
{
    ulong grfont;   // bold, italic, etc
    long onn;       // which font
    long dypFont;   // size of the font
    long dypOffset; // sub/superscript (-128 to 127)
    AbstractColor acrFore;    // text color
    AbstractColor acrBack;    // background color

    void Clear(void)
    {
        ClearPb(this, offset(CHP, acrFore));
        acrFore = kacrBlack;
        acrBack = kacrBlack;
    }
};
typedef CHP *PCHP;

/***************************************************************************
    Paragraph properties - if you change these, make sure to update
    FetchPap and _TGetLwFromPap.  The dypExtraLine and numLine fields are
    used to change the height of lines from their default.  The line height
    used is calculated as (numLine / 256) * dyp + dypExtraLine, where dyp
    is the natural height of the line.
***************************************************************************/
enum
{
    jcMin,
    jcLeft = jcMin,
    jcRight,
    jcCenter,
    jcLim
};

enum
{
    ndMin,
    ndNone = ndMin,
    ndFirst, // just on the left
    ndRest,  // just on the left
    ndAll,   // on both sides
    ndLim
};

struct PAP
{
    byte jc;
    byte nd;
    short dxpTab;
    short numLine;
    short dypExtraLine;
    short numAfter;
    short dypExtraAfter;
};
typedef PAP *PPAP;
const short kdenLine = 256;
const short kdenAfter = 256;
const short kdxpTabDef = (kdzpInch / 4);
const short kdxpDocDef = (kdzpInch * 6);

const achar kchObject = 1;

/***************************************************************************
    Generic text document base class
***************************************************************************/
const long kcchMaxTxtbCache = 512;
typedef class TextDocumentBase *PTextDocumentBase;
#define TextDocumentBase_PAR DocumentBase
#define kclsTextDocumentBase 'TXTB'
class TextDocumentBase : public TextDocumentBase_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PFIL _pfil;
    PFileByteStream _pbsf;
    AbstractColor _acrBack;
    long _dxpDef; // default width of the document
    long _cpMinCache;
    long _cpLimCache;
    achar _rgchCache[kcchMaxTxtbCache];

    long _cactSuspendUndo; // > 0 means don't set up undo records.
    long _cactCombineUndo; // determines whether we can combine undo records.

    TextDocumentBase(PDocumentBase pdocb = pvNil, ulong grfdoc = fdocNil);
    ~TextDocumentBase(void);
    virtual bool _FInit(PFilename pfni = pvNil, PFileByteStream pbsf = pvNil, short osk = koskCur);
    virtual bool _FLoad(short osk = koskCur);
    virtual achar _ChFetch(long cp);
    virtual void _CacheRange(long cpMin, long cpLim);
    virtual void _InvalCache(long cp, long ccpIns, long ccpDel);

  public:
    virtual void InvalAllDdg(long cp, long ccpIns, long ccpDel, ulong grfdoc = fdocUpdate);

    // REVIEW shonk: this is needed for using a text document as input to a lexer.
    // The bsf returned is read-only!!!!
    PFileByteStream Pbsf(void)
    {
        AssertThis(0);
        return _pbsf;
    }

    long CpMac(void);
    bool FMinPara(long cp);
    long CpMinPara(long cp);
    long CpLimPara(long cp);
    long CpPrev(long cp, bool fWord = fFalse);
    long CpNext(long cp, bool fWord = fFalse);
    AbstractColor AcrBack(void)
    {
        return _acrBack;
    }
    void SetAcrBack(AbstractColor acr, ulong grfdoc = fdocUpdate);
    long DxpDef(void)
    {
        return _dxpDef;
    }
    virtual void SetDxpDef(long dxp);

    virtual void FetchRgch(long cp, long ccp, achar *prgch);
    virtual bool FReplaceRgch(void *prgch, long ccpIns, long cp, long ccpDel, ulong grfdoc = fdocUpdate);
    virtual bool FReplaceFlo(PFLO pflo, bool fCopy, long cp, long ccpDel, short osk = koskCur,
                             ulong grfdoc = fdocUpdate);
    virtual bool FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc = fdocUpdate);
    virtual bool FReplaceTxtb(PTextDocumentBase ptxtbSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel,
                              ulong grfdoc = fdocUpdate);
    virtual bool FGetObjectRc(long cp, PGNV pgnv, PCHP pchp, RC *prc);
    virtual bool FDrawObject(long cp, PGNV pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip);

    virtual bool FGetFni(Filename *pfni);

    virtual void HideSel(void);
    virtual void SetSel(long cp1, long cp2, long gin = kginDraw);
    virtual void ShowSel(void);

    virtual void SuspendUndo(void);
    virtual void ResumeUndo(void);
    virtual bool FSetUndo(long cp1, long cp2, long ccpIns);
    virtual void CancelUndo(void);
    virtual void CommitUndo(void);
    virtual void BumpCombineUndo(void);

    virtual bool FFind(achar *prgch, long cch, long cpStart, long *pcpMin, long *pcpLim, bool fCaseSensitive = fFalse);

    virtual void ExportFormats(PClipboardObject pclip);
};

/***************************************************************************
    Plain text document class
***************************************************************************/
typedef class PlainTextDocument *PPlainTextDocument;
#define PlainTextDocument_PAR TextDocumentBase
#define kclsPlainTextDocument 'TXPD'
class PlainTextDocument : public PlainTextDocument_PAR
{
    RTCLASS_DEC

  protected:
    PlainTextDocument(PDocumentBase pdocb = pvNil, ulong grfdoc = fdocNil);

  public:
    static PPlainTextDocument PtxpdNew(PFilename pfni = pvNil, PFileByteStream pbsf = pvNil, short osk = koskCur, PDocumentBase pdocb = pvNil,
                          ulong grfdoc = fdocNil);

    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
    virtual bool FSaveToFni(Filename *pfni, bool fSetFni);
};

/***************************************************************************
    Rich text document class.
***************************************************************************/
const long kcpMaxTxrd = 0x00800000; // 8MB
typedef class RichTextUndo *PRichTextUndo;

typedef class RichTextDocument *PRichTextDocument;
#define RichTextDocument_PAR TextDocumentBase
#define kclsRichTextDocument 'TXRD'
class RichTextDocument : public RichTextDocument_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // WARNING: changing these values affects the file format
    // NOTE: Originally, _FSprmInAg was a virtual RichTextDocument method called to
    // determine if a sprm stored its value in the AllocatedGroup. This didn't work
    // well when a subclass had a sprm in an AllocatedGroup (it broke undo for that
    // sprm). To fix this I (shonk) made _FSprmInAg static and made it
    // know exactly which sprms use the AllocatedGroup. For sprms in the client range
    // or above sprmMinObj, odd ones are _not_ in the AllocatedGroup and even ones _are_
    // in the AllocatedGroup. I couldn't just use the odd/even rule throughout the
    // range, because that would have required changing values of old
    // sprms, which would have broken existing rich text documents.
    enum
    {
        sprmNil = 0,

        // character properties
        sprmMinChp = 1,
        sprmStyle = 1,     // bold, italic, etc, font size, dypOffset
        sprmFont = 2,      // font - in the AllocatedGroup
        sprmForeColor = 3, // foreground color
        sprmBackColor = 4, // background color
        sprmLimChp,

        // client character properties start at 64
        sprmMinChpClient = 64, // for subclassed character properties

        // paragraph properties
        sprmMinPap = 128,
        sprmHorz = 128,  // justification, indenting and dxpTab
        sprmVert = 129,  // numLine and dypExtraLine
        sprmAfter = 130, // numAfter and dypExtraAfter
        sprmLimPap,

        // client paragraph properties
        sprmMinPapClient = 160, // for subclassed paragraph properties

        // objects - these apply to a single character, not a range
        sprmMinObj = 192,
        sprmObject = 192,
    };

    // map property entry
    struct MPE
    {
        ulong spcp; // sprm in the high byte and cp in the low 3 bytes
        long lw;    // the associated value - meaning depends on the sprm,
                    // but 0 is _always_ the default
    };

    // sprm, value, mask triple
    struct SPVM
    {
        byte sprm;
        long lw;
        long lwMask;
    };

    // rich text document properties
    struct RDOP
    {
        short bo;
        short oskFont;
        long dxpDef;
        long dypFont;
        long lwAcrBack;
        // byte rgbStnFont[]; font name
    };
#define kbomRdop 0x5FC00000

    PChunkyFile _pcfl;
    PDynamicArray _pglmpe;
    PAllocatedGroup _pagcact; // for sprm's that have more than a long's worth of data

    long _onnDef; // default font and font size
    long _dypFontDef;
    short _oskFont;  // osk for the default font
    STN _stnFontDef; // name of default font

    // cached CHP and PAP (from FetchChp and FetchPap)
    CHP _chp;
    long _cpMinChp, _cpLimChp;
    PAP _pap;
    long _cpMinPap, _cpLimPap;

    // current undo record
    PRichTextUndo _prtun;

    RichTextDocument(PDocumentBase pdocb = pvNil, ulong grfdoc = fdocNil);
    ~RichTextDocument(void);
    bool _FInit(PFilename pfni = pvNil, ChunkTag ctg = kctgRichText);
    virtual bool _FReadChunk(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, bool fCopyText);
    virtual bool _FOpenArg(long icact, byte sprm, short bo, short osk);

    ulong _SpcpFromSprmCp(byte sprm, long cp)
    {
        return ((ulong)sprm << 24) | (cp & 0x00FFFFFF);
    }
    byte _SprmFromSpcp(ulong spcp)
    {
        return B3Lw(spcp);
    }
    long _CpFromSpcp(ulong spcp)
    {
        return (long)(spcp & 0x00FFFFFF);
    }
    bool _FFindMpe(ulong spcp, MPE *pmpe, long *pcpLim = pvNil, long *pimpe = pvNil);
    bool _FFetchProp(long impe, byte *psprm, long *plw = pvNil, long *pcpMin = pvNil, long *pcpLim = pvNil);
    bool _FEnsureInAg(byte sprm, void *pv, long cb, long *pjv);
    void _ReleaseInAg(long jv);
    void _AddRefInAg(long jv);
    void _ReleaseSprmLw(byte sprm, long lw);
    void _AddRefSprmLw(byte sprm, long lw);
    tribool _TGetLwFromChp(byte sprm, PCHP pchpNew, PCHP pchpOld, long *plw, long *plwMask);
    tribool _TGetLwFromPap(byte sprm, PPAP ppapNew, PPAP ppapOld, long *plw, long *plwMask);
    bool _FGetRgspvmFromChp(PCHP pchp, PCHP pchpDiff, SPVM *prgspvm, long *pcspvm);
    bool _FGetRgspvmFromPap(PPAP ppap, PPAP ppapDiff, SPVM *prgspvm, long *pcspvm);
    void _ReleaseRgspvm(SPVM *prgspvm, long cspvm);
    void _ApplyRgspvm(long cp, long ccp, SPVM *prgspvm, long cspvm);
    void _GetParaBounds(long *pcpMin, long *pccp, bool fExpand);
    void _AdjustMpe(long cp, long ccpIns, long ccpDel);
    void _CopyProps(PRichTextDocument ptxrd, long cpSrc, long cpDst, long ccpSrc, long ccpDst, byte sprmMin, byte sprmLim);

    virtual bool _FGetObjectRc(long icact, byte sprm, PGNV pgnv, PCHP pchp, RC *prc);
    virtual bool _FDrawObject(long icact, byte sprm, PGNV pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip);

    bool _FReplaceCore(void *prgch, PFLO pflo, bool fCopy, PFileByteStream pbsf, long cpSrc, long ccpIns, long cp, long ccpDel,
                       PCHP pchp, PPAP ppap, ulong grfdoc);

    static bool _FSprmInAg(byte sprm);

  public:
    static PRichTextDocument PtxrdNew(PFilename pfni = pvNil);
    static PRichTextDocument PtxrdReadChunk(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, bool fCopyText = fTrue);

    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);

    void FetchChp(long cp, PCHP pchp, long *pcpMin = pvNil, long *pcpLim = pvNil);
    void FetchPap(long cp, PPAP ppap, long *pcpMin = pvNil, long *pcpLim = pvNil);

    bool FApplyChp(long cp, long ccp, PCHP pchp, PCHP pchpDiff = pvNil, ulong grfdoc = fdocUpdate);
    bool FApplyPap(long cp, long ccp, PPAP ppap, PPAP ppapDiff, long *pcpMin = pvNil, long *pcpLim = pvNil,
                   bool fExpand = fTrue, ulong grfdoc = fdocUpdate);

    virtual bool FReplaceRgch(void *prgch, long ccpIns, long cp, long ccpDel, ulong grfdoc = fdocUpdate);
    virtual bool FReplaceFlo(PFLO pflo, bool fCopy, long cp, long ccpDel, short osk = koskCur,
                             ulong grfdoc = fdocUpdate);
    virtual bool FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc = fdocUpdate);
    virtual bool FReplaceTxtb(PTextDocumentBase ptxtbSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel,
                              ulong grfdoc = fdocUpdate);
    bool FReplaceRgch(void *prgch, long ccpIns, long cp, long ccpDel, PCHP pchp, PPAP ppap = pvNil,
                      ulong grfdoc = fdocUpdate);
    bool FReplaceFlo(PFLO pflo, bool fCopy, long cp, long ccpDel, PCHP pchp, PPAP ppap = pvNil, short osk = koskCur,
                     ulong grfdoc = fdocUpdate);
    bool FReplaceBsf(PFileByteStream pbsfSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, PCHP pchp, PPAP ppap = pvNil,
                     ulong grfdoc = fdocUpdate);
    bool FReplaceTxtb(PTextDocumentBase ptxtbSrc, long cpSrc, long ccpSrc, long cpDst, long ccpDel, PCHP pchp, PPAP ppap = pvNil,
                      ulong grfdoc = fdocUpdate);
    bool FReplaceTxrd(PRichTextDocument ptxrd, long cpSrc, long ccpSrc, long cpDst, long ccpDel, ulong grfdoc = fdocUpdate);

    bool FFetchObject(long cpMin, long *pcp, void **ppv = pvNil, long *pcb = pvNil);
    virtual bool FInsertObject(void *pv, long cb, long cp, long ccpDel, PCHP pchp = pvNil, ulong grfdoc = fdocUpdate);
    virtual bool FApplyObjectProps(void *pv, long cb, long cp, ulong grfdoc = fdocUpdate);

    virtual bool FGetObjectRc(long cp, PGNV pgnv, PCHP pchp, RC *prc);
    virtual bool FDrawObject(long cp, PGNV pgnv, long *pxp, long yp, PCHP pchp, RC *prcClip);

    virtual bool FGetFni(Filename *pfni);
    virtual bool FGetFniSave(Filename *pfni);
    virtual bool FSaveToFni(Filename *pfni, bool fSetFni);
    virtual bool FSaveToChunk(PChunkyFile pcfl, ChunkIdentification *pcki, bool fRedirectText = fFalse);

    virtual bool FSetUndo(long cp1, long cp2, long ccpIns);
    virtual void CancelUndo(void);
    virtual void CommitUndo(void);
};

/***************************************************************************
    Rich text undo object.
***************************************************************************/
typedef class RichTextUndo *PRichTextUndo;
#define RichTextUndo_PAR UndoBase
#define kclsRichTextUndo 'RTUN'
class RichTextUndo : public RichTextUndo_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    long _cactCombine; // RTUNs with different values can't be combined
    PRichTextDocument _ptxrd;      // copy of replaced text
    long _cpMin;       // where the text came from in the original RTXD
    long _ccpIns;      // how many characters the original text was replaced with

  public:
    static PRichTextUndo PrtunNew(long cactCombine, PRichTextDocument ptxrd, long cp1, long cp2, long ccpIns);
    ~RichTextUndo(void);

    virtual bool FUndo(PDocumentBase pdocb);
    virtual bool FDo(PDocumentBase pdocb);

    bool FCombine(PRichTextUndo prtun);
};

/***************************************************************************
    Text document display GraphicsObject - DocumentDisplayGraphicsObject for a TextDocumentBase.
***************************************************************************/
const long kdxpIndentTxtg = (kdzpInch / 8);
const long kcchMaxLineTxtg = 512;
typedef class TRUL *PTRUL;

enum
{
    ftxtgNil = 0,
    ftxtgNoColor = 1,
};

typedef class TextDocumentGraphicsObject *PTextDocumentGraphicsObject;
#define TextDocumentGraphicsObject_PAR DocumentDisplayGraphicsObject
#define kclsTextDocumentGraphicsObject 'TXTG'
class TextDocumentGraphicsObject : public TextDocumentGraphicsObject_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // line information
    struct LIN
    {
        long cpMin;  // the cp of the first character in this line
        long dypTot; // the total height of lines up to this line
        short ccp;
        short xpLeft;
        short dyp;
        short dypAscent;
    };

    PTextDocumentBase _ptxtb;
    PDynamicArray _pgllin;
    long _ilinDisp;
    long _cpDisp;
    long _dypDisp;
    long _ilinInval; // LINs from here on have wrong cpMin and dypTot values
    PGNV _pgnv;

    // the selection
    long _cpAnchor;
    long _cpOther;
    ulong _tsSel;
    long _xpSel;
    bool _fSelOn : 1;
    bool _fXpValid : 1;
    bool _fMark : 1;
    bool _fClear : 1;
    bool _fSelByWord : 1;

    // the ruler
    PTRUL _ptrul;

    TextDocumentGraphicsObject(PTextDocumentBase ptxtb, PGCB pgcb);
    ~TextDocumentGraphicsObject(void);

    virtual bool _FInit(void);
    virtual void _Activate(bool fActive);

    virtual long _DxpDoc(void);
    virtual void _FetchChp(long cp, PCHP pchp, long *pcpMin = pvNil, long *pcpLim = pvNil) = 0;
    virtual void _FetchPap(long cp, PPAP ppap, long *pcpMin = pvNil, long *pcpLim = pvNil) = 0;

    void _CalcLine(long cpMin, long dyp, LIN *plin);
    void _Reformat(long cp, long ccpIns, long ccpDel, long *pyp = pvNil, long *pdypIns = pvNil, long *pdypDel = pvNil);
    void _ReformatAndDraw(long cp, long ccpIns, long ccpDel);

    void _FetchLin(long ilin, LIN *plin, long *pilinActual = pvNil);
    void _FindCp(long cpFind, LIN *plin, long *pilin = pvNil, bool fCalcLines = fTrue);
    void _FindDyp(long dypFind, LIN *plin, long *pilin = pvNil, bool fCalcLines = fTrue);

    bool _FGetCpFromPt(long xp, long yp, long *pcp, bool fClosest = fTrue);
    bool _FGetCpFromXp(long xp, LIN *plin, long *pcp, bool fClosest = fTrue);
    void _GetXpYpFromCp(long cp, long *pypMin, long *pypLim, long *pxp, long *pdypBaseLine = pvNil, bool fView = fTrue);
    long _DxpFromCp(long cpLine, long cp);
    void _SwitchSel(bool fOn, long gin = kginDraw);
    void _InvertSel(PGNV pgnv, long gin = kginDraw);
    void _InvertCpRange(PGNV pgnv, long cp1, long cp2, long gin = kginDraw);

    virtual long _ScvMax(bool fVert);
    virtual void _Scroll(long scaHorz, long scaVert, long scvHorz = 0, long scvVert = 0);
    virtual void _ScrollDxpDyp(long dxp, long dyp);
    virtual long _DypTrul(void);
    virtual PTRUL _PtrulNew(PGCB pgcb);
    virtual void _DrawLinExtra(PGNV pgnv, RC *prcClip, LIN *plin, long dxp, long yp, ulong grftxtg);

  public:
    virtual void DrawLines(PGNV pgnv, RC *prcClip, long dxp, long dyp, long ilinMin, long ilinLim = klwMax,
                           ulong grftxtg = ftxtgNil);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd);
    virtual bool FCmdKey(PCMD_KEY pcmd);
    virtual bool FCmdSelIdle(PCMD pcmd);
    virtual void InvalCp(long cp, long ccpIns, long ccpDel);

    virtual void HideSel(void);
    virtual void GetSel(long *pcpAnchor, long *pcpOther);
    virtual void SetSel(long cpAnchor, long cpOther, long gin = kginDraw);
    virtual bool FReplace(achar *prgch, long cch, long cp1, long cp2);
    void ShowSel(void);
    PTextDocumentBase Ptxtb(void)
    {
        return _ptxtb;
    }

    virtual void ShowRuler(bool fShow = fTrue);
    virtual void SetDxpTab(long dxp);
    virtual void SetDxpDoc(long dxp);
    virtual void GetNaturalSize(long *pdxp, long *pdyp);
};

/***************************************************************************
    Line text document display gob
***************************************************************************/
typedef class LineTextGraphicsDocument *PLineTextGraphicsDocument;
#define LineTextGraphicsDocument_PAR TextDocumentGraphicsObject
#define kclsLineTextGraphicsDocument 'TXLG'
class LineTextGraphicsDocument : public LineTextGraphicsDocument_PAR
{
    RTCLASS_DEC

  protected:
    // the font
    long _onn;
    ulong _grfont;
    long _dypFont;
    long _dxpChar;
    long _cchTab;

    LineTextGraphicsDocument(PTextDocumentBase ptxtb, PGCB pgcb, long onn, ulong grfont, long dypFont, long cchTab);

    virtual long _DxpDoc(void);
    virtual void _FetchChp(long cp, PCHP pchp, long *pcpMin = pvNil, long *pcpLim = pvNil);
    virtual void _FetchPap(long cp, PPAP ppap, long *pcpMin = pvNil, long *pcpLim = pvNil);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);
    virtual void _ClearSel(void);
    virtual bool _FPaste(PClipboardObject pclip, bool fDoIt, long cid);

  public:
    static PLineTextGraphicsDocument PtxlgNew(PTextDocumentBase ptxtb, PGCB pgcb, long onn, ulong grfont, long dypFont, long cchTab);

    virtual void SetDxpTab(long dxp);
    virtual void SetDxpDoc(long dxp);
};

/***************************************************************************
    Rich text document display gob
***************************************************************************/
typedef class RichTextDocumentGraphicsObject *PRichTextDocumentGraphicsObject;
#define RichTextDocumentGraphicsObject_PAR TextDocumentGraphicsObject
#define kclsRichTextDocumentGraphicsObject 'TXRG'
class RichTextDocumentGraphicsObject : public RichTextDocumentGraphicsObject_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(RichTextDocumentGraphicsObject)
    ASSERT

  protected:
    RichTextDocumentGraphicsObject(PRichTextDocument ptxrd, PGCB pgcb);

    CHP _chpIns;
    bool _fValidChp;

    virtual void _FetchChp(long cp, PCHP pchp, long *pcpMin = pvNil, long *pcpLim = pvNil);
    virtual void _FetchPap(long cp, PPAP ppap, long *pcpMin = pvNil, long *pcpLim = pvNil);

    virtual bool _FGetOtherSize(long *pdypFont);
    virtual bool _FGetOtherSubSuper(long *pdypOffset);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);
    virtual void _ClearSel(void);
    virtual bool _FPaste(PClipboardObject pclip, bool fDoIt, long cid);

    void _FetchChpSel(long cp1, long cp2, PCHP pchp);
    void _EnsureChpIns(void);

  public:
    static PRichTextDocumentGraphicsObject PtxrgNew(PRichTextDocument ptxrd, PGCB pgcb);

    virtual void SetSel(long cpAnchor, long cpOther, long gin = kginDraw);
    virtual bool FReplace(achar *prgch, long cch, long cp1, long cp2);
    virtual bool FApplyChp(PCHP pchp, PCHP pchpDiff = pvNil);
    virtual bool FApplyPap(PPAP ppap, PPAP ppapDiff = pvNil, bool fExpand = fTrue);

    virtual bool FCmdApplyProperty(PCMD pcmd);
    virtual bool FEnablePropCmd(PCMD pcmd, ulong *pgrfeds);
    bool FSetColor(AbstractColor *pacrFore, AbstractColor *pacrBack);

    virtual void SetDxpTab(long dxp);
};

/***************************************************************************
    The ruler for a rich text document.
***************************************************************************/
typedef class TRUL *PTRUL;
#define TRUL_PAR GraphicsObject
#define kclsTRUL 'TRUL'
class TRUL : public TRUL_PAR
{
    RTCLASS_DEC

  protected:
    TRUL(GraphicsObjectBlock *pgcb) : TRUL_PAR(pgcb)
    {
    }

  public:
    virtual void SetDxpTab(long dxpTab) = 0;
    virtual void SetDxpDoc(long dxpDoc) = 0;
    virtual void SetXpLeft(long xpLeft) = 0;
};

#endif //! RTXT_H
