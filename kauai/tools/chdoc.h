/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    CHED document class

***************************************************************************/
#ifndef CHDOC_H
#define CHDOC_H

using namespace Chunky;
using namespace ScriptInterpreter;

typedef class DOC *PDOC;
typedef class DOCE *PDOCE;
typedef class DOCH *PDOCH;
typedef class DOCG *PDOCG;
typedef class DOCI *PDOCI;
typedef class DOCPIC *PDOCPIC;
typedef class DOCMBMP *PDOCMBMP;
typedef class SEL *PSEL;
typedef class DCD *PDCD;
typedef class DCH *PDCH;
typedef class DCGB *PDCGB;
typedef class DCGL *PDCGL;
typedef class DCGG *PDCGG;
typedef class DCST *PDCST;
typedef class DCPIC *PDCPIC;
typedef class DCMBMP *PDCMBMP;

bool FGetCtgFromStn(ChunkTag *pctg, PSTN pstn);

#define lnNil (-1L)

/***************************************************************************

    Various document classes. DOC is the chunky file based document.
    DOCE is a virtual class for documents that represent an individual
    chunk in a DOC. A DOCE is a child document of a DOC. All other
    document classes in this header are derived from DOCE.

***************************************************************************/

/***************************************************************************
    chunky file doc
***************************************************************************/
#define DOC_PAR DocumentBase
#define kclsDOC 'DOC'
class DOC : public DOC_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PChunkyFile _pcfl; // the chunky file

    DOC(void);
    ~DOC(void);

  public:
    static PDOC PdocNew(Filename *pfni);

    PChunkyFile Pcfl(void)
    {
        return _pcfl;
    }
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
    virtual bool FGetFni(Filename *pfni);
    virtual bool FGetFniSave(Filename *pfni);
    virtual bool FSaveToFni(Filename *pfni, bool fSetFni);
};

/***************************************************************************
    Chunky editing doc - abstract class for editing a single chunk in a
    Chunky file. An instance of this class is a child doc of a DOC. Many
    document classes below are all derived from this.
***************************************************************************/
#define DOCE_PAR DocumentBase
#define kclsDOCE 'DOCE'
class DOCE : public DOCE_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PChunkyFile _pcfl; // which chunk is being edited
    ChunkTag _ctg;
    ChunkNumber _cno;

    DOCE(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    bool _FInit(void);

    virtual bool _FSaveToChunk(ChunkTag ctg, ChunkNumber cno, bool fRedirect);
    virtual bool _FWrite(PDataBlock pblck, bool fRedirect) = 0;
    virtual long _CbOnFile(void) = 0;
    virtual bool _FRead(PDataBlock pblck) = 0;

  public:
    static PDOCE PdoceFromChunk(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    static void CloseDeletedDoce(PDocumentBase pdocb);

    virtual void GetName(PSTN pstn);
    virtual bool FSave(long cid);
};

/***************************************************************************
    Hex editor document - for editing any chunk as a hex stream.
***************************************************************************/
#define DOCH_PAR DOCE
#define kclsDOCH 'DOCH'
class DOCH : public DOCH_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    FileByteStream _bsf; // the byte stream

    DOCH(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    virtual bool _FWrite(PDataBlock pblck, bool fRedirect);
    virtual long _CbOnFile(void);
    virtual bool _FRead(PDataBlock pblck);

  public:
    static PDOCH PdochNew(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
};

/***************************************************************************
    Group editor document - for editing DynamicArray, AllocatedArray, GeneralGroup, AllocatedGroup, StringTable, and AllocatedStringTable.
***************************************************************************/
#define DOCG_PAR DOCE
#define kclsDOCG 'DOCG'
class DOCG : public DOCG_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGroupBase _pgrpb;
    long _cls; // which class the group belongs to
    short _bo;
    short _osk;

    DOCG(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, long cls);
    ~DOCG(void);
    virtual bool _FWrite(PDataBlock pblck, bool fRedirect);
    virtual long _CbOnFile(void);
    virtual bool _FRead(PDataBlock pblck);

  public:
    static PDOCG PdocgNew(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, long cls);
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);

    PDOCI PdociFromItem(long iv, long dln);
    void CloseDeletedDoci(long iv, long cvDel);
    PGroupBase Pgrpb(void)
    {
        return _pgrpb;
    }
};

/***************************************************************************
    Item hex editor document - for editing an item in a GroupBase. An instance
    of this class is normally a child doc of a DOCG (but doesn't have to be).
***************************************************************************/
#define DOCI_PAR DocumentBase
#define kclsDOCI 'DOCI'
class DOCI : public DOCI_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGroupBase _pgrpb; // the group the data came from and gets written to.
    long _cls;
    long _iv; // which item is being edited
    long _dln;
    bool _fFixed; // indicates if the data is fixed length
    FileByteStream _bsf;     // the byte stream we're editing

    DOCI(PDocumentBase pdocb, PGroupBase pgrpb, long cls, long iv, long dln);
    bool _FInit(void);

    virtual bool _FSaveToItem(long iv, bool fRedirect);
    virtual bool _FWrite(long iv);
    virtual HQ _HqRead();

  public:
    static PDOCI PdociNew(PDocumentBase pdocb, PGroupBase pgrpb, long cls, long iv, long dln);
    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);

    long Iv(void)
    {
        return _iv;
    }
    long Dln(void)
    {
        return _dln;
    }

    virtual void GetName(PSTN pstn);
    virtual bool FSave(long cid);
};

/***************************************************************************
    Picture display document.
***************************************************************************/
#define DOCPIC_PAR DOCE
#define kclsDOCPIC 'dcpc'
class DOCPIC : public DOCPIC_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PPIC _ppic;

    DOCPIC(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    ~DOCPIC(void);

    virtual bool _FWrite(PDataBlock pblck, bool fRedirect);
    virtual long _CbOnFile(void);
    virtual bool _FRead(PDataBlock pblck);

  public:
    static PDOCPIC PdocpicNew(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);

    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
    PPIC Ppic(void)
    {
        return _ppic;
    }
};

/***************************************************************************
    MBMP display document.
***************************************************************************/
#define DOCMBMP_PAR DOCE
#define kclsDOCMBMP 'docm'
class DOCMBMP : public DOCMBMP_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMBMP _pmbmp;

    DOCMBMP(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);
    ~DOCMBMP(void);

    virtual bool _FWrite(PDataBlock pblck, bool fRedirect);
    virtual long _CbOnFile(void);
    virtual bool _FRead(PDataBlock pblck);

  public:
    static PDOCMBMP PdocmbmpNew(PDocumentBase pdocb, PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);

    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
    PMBMP Pmbmp(void)
    {
        return _pmbmp;
    }
};

/***************************************************************************
    Document editing window classes follow. These are all DocumentDisplayGraphicsObject's.
    Most are also DCLB's (the first class defined below).  DCLB is
    an abstract class that handles a line based editing window.
    The DCD class is for displaying a DOC (chunky file document).
***************************************************************************/

/***************************************************************************
    abstract class for line based document windows
***************************************************************************/
#define DCLB_PAR DocumentDisplayGraphicsObject
#define kclsDCLB 'DCLB'
class DCLB : public DCLB_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    long _onn;       // fixed width font to use
    long _dypHeader; // height of the header
    long _dypLine;   // height of one line
    long _dxpChar;   // width of a character

    DCLB(PDocumentBase pdocb, PGCB pgcb);
    virtual void _Scroll(long scaHorz, long scaVert, long scvHorz = 0, long scvVert = 0);
    virtual void _ScrollDxpDyp(long dxp, long dyp);
    virtual void GetMinMax(RC *prcMinMax);

    long _YpFromLn(long ln)
    {
        return LwMul(ln - _scvVert, _dypLine) + _dypHeader;
    }
    long _XpFromIch(long ich)
    {
        return LwMul(ich - _scvHorz + 1, _dxpChar);
    }
    long _LnFromYp(long yp);

    void _GetContent(RC *prc);
};

/***************************************************************************
    SEL: used to track a selection in a chunky file doc
***************************************************************************/
enum
{
    fselNil = 0,
    fselCki = 1,
    fselKid = 2
};

#define SEL_PAR BASE
#define kclsSEL 'SEL'
class SEL : public SEL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PChunkyFile _pcfl;
    long _icki;
    long _ikid;
    ChunkIdentification _cki;
    ChildChunkIdentification _kid;
    long _ln;
    long _lnLim;         // this is lnNil if we haven't yet calculated the lim
    PDynamicArray _pglctg;         // the ctgs to filter on
    bool _fHideList : 1; // whether to hide the ctgs in the list or show them
    bool _fHideKids : 1; // whether to hide the kids

    void _SetNil(void);
    bool _FFilter(ChunkTag ctg, ChunkNumber cno);

  public:
    SEL(PChunkyFile pcfl);
    SEL(SEL &selT);
    ~SEL(void);
    SEL &operator=(SEL &selT);

    void Adjust(bool fExact = fFalse);

    long Icki(void)
    {
        return _icki;
    }
    long Ikid(void)
    {
        return _ikid;
    }
    long Ln(void)
    {
        return _ln;
    }
    ulong GrfselGetCkiKid(ChunkIdentification *pcki, ChildChunkIdentification *pkid);

    bool FSetLn(long ln);
    bool FAdvance(void);
    bool FRetreat(void);
    bool FSetCkiKid(ChunkIdentification *pcki, ChildChunkIdentification *pkid = pvNil, bool fExact = fTrue);
    long LnLim(void);
    void InvalLim(void)
    {
        _lnLim = lnNil;
    }

    bool FHideKids(void)
    {
        return _fHideKids;
    }
    void HideKids(bool fHide);

    bool FHideList(void)
    {
        return _fHideList;
    }
    void HideList(bool fHide);
    bool FGetCtgFilter(long ictg, ChunkTag *pctg);
    void FreeFilterList(void);
    bool FAddCtgFilter(ChunkTag ctg);
};

/***************************************************************************
    Display for chunky document - displays a DOC.
***************************************************************************/
#define DCD_PAR DCLB
#define kclsDCD 'DCD'
class DCD : public DCD_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(DCD)
    ASSERT
    MARKMEM

  protected:
    long _dypBorder; // height of border (included in _dypLine)
    PChunkyFile _pcfl;      // the chunky file
    SEL _sel;        // the current selection

    DCD(PDocumentBase pdocb, PChunkyFile pcfl, PGCB pgcb);
    void _DrawSel(PGNV pgnv);
    void _HiliteLn(long ln);
    void _SetSel(long ln, ChunkIdentification *pcki = pvNil, ChildChunkIdentification *pkid = pvNil);
    void _ShowSel(void);

    virtual void _Activate(bool fActive);
    virtual long _ScvMax(bool fVert);
    bool _FAddChunk(ChunkTag ctgDef, ChunkIdentification *pcki, bool *pfCreated);
    bool _FEditChunkInfo(ChunkIdentification *pckiOld);
    bool _FChangeChid(ChunkIdentification *pcki, ChildChunkIdentification *pkid);

    bool _FDoAdoptChunkDlg(ChunkIdentification *pcki, ChildChunkIdentification *pkid);
    void _EditCki(ChunkIdentification *pcki, long cid);

    void _InvalCkiKid(ChunkIdentification *pcki = pvNil, ChildChunkIdentification *pkid = pvNil);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);
    virtual void _ClearSel(void);
    virtual bool _FPaste(PClipboardObject pclip, bool fDoIt, long cid);

  public:
    static PDCD PdcdNew(PDocumentBase pdocb, PChunkyFile pcfl, PGCB pgcb);
    static void InvalAllDcd(PDocumentBase pdocb, PChunkyFile pcfl, ChunkIdentification *pcki = pvNil, ChildChunkIdentification *pkid = pvNil);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    virtual bool FCmdKey(PCMD_KEY pcmd);

    virtual bool FEnableDcdCmd(PCMD pcmd, ulong *pgrfeds);
    virtual bool FCmdAddChunk(PCMD pcmd);
    virtual bool FCmdDeleteChunk(PCMD pcmd);
    virtual bool FCmdAdoptChunk(PCMD pcmd);
    virtual bool FCmdUnadoptChunk(PCMD pcmd);
    virtual bool FCmdEditChunk(PCMD pcmd);
    virtual bool FCmdAddPicChunk(PCMD pcmd);
    virtual bool FCmdAddBitmapChunk(PCMD pcmd);
    virtual bool FCmdImportScript(PCMD pcmd);
    virtual bool FCmdTestScript(PCMD pcmd);
    virtual bool FCmdDisasmScript(PCMD pcmd);
    virtual bool FCmdAddFileChunk(PCMD pcmd);
    virtual bool FCmdEditChunkInfo(PCMD pcmd);
    virtual bool FCmdChangeChid(PCMD pcmd);
    virtual bool FCmdSetColorTable(PCMD pcmd);
    virtual bool FCmdFilterChunk(PCMD pcmd);
    virtual bool FCmdPack(PCMD pcmd);
    virtual bool FCmdStopSound(PCMD pcmd);
    virtual bool FCmdCloneChunk(PCMD pcmd);
    virtual bool FCmdReopen(PCMD pcmd);

    bool FTestScript(ChunkTag ctg, ChunkNumber cno, long cbCache = 0x00300000L);
    bool FPlayMidi(ChunkTag ctg, ChunkNumber cno);
    bool FPlayWave(ChunkTag ctg, ChunkNumber cno);
};

/***************************************************************************
    Display chunk in hex - displays a FileByteStream (byte stream), but
    doesn't necessarily display a DOCH.
***************************************************************************/
#define DCH_PAR DCLB
#define kclsDCH 'DCH'
class DCH : public DCH_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PFileByteStream _pbsf;   // the byte stream
    long _cbLine; // number of bytes per line

    // the selection
    long _ibAnchor;
    long _ibOther;

    bool _fSelOn : 1;    // selection is showing
    bool _fRightSel : 1; // selection if on a line boundary is at the right edge
    bool _fHalfSel : 1;  // second half of hex character is selected
    bool _fHexSel : 1;   // hex area active
    bool _fFixed : 1;    // indicates if the data is fixed length

    DCH(PDocumentBase pdocb, PFileByteStream pbsf, bool fFixed, PGCB pgcb);

    virtual void _Activate(bool fActive);
    virtual long _ScvMax(bool fVert);

    long _IchFromCb(long cb, bool fHex, bool fNoTrailSpace = fFalse);
    long _XpFromCb(long cb, bool fHex, bool fNoTrailSpace = fFalse);
    long _XpFromIb(long ib, bool fHex);
    long _YpFromIb(long ib);
    long _IbFromPt(long xp, long yp, tribool *ptHex, bool *pfRight = pvNil);

    void _SetSel(long ibAnchor, long ibOther, bool fRight);
    void _SetHalfSel(long ib);
    void _SetHexSel(bool fHex);
    void _SwitchSel(bool fOn);
    void _ShowSel(void);
    void _InvertSel(PGNV pgnv);
    void _InvertIbRange(PGNV pgnv, long ib1, long ib2, bool fHex);

    bool _FReplace(byte *prgb, long cb, long ibMin, long ibLim, bool fHalfSel = fFalse);
    void _InvalAllDch(long ib, long cbIns, long cbDel);
    void _InvalIb(long ib, long cbIns, long cbDel);

    void _DrawHeader(PGNV pgnv);

    // clipboard support
    virtual bool _FCopySel(PDocumentBase *ppdocb = pvNil);
    virtual void _ClearSel(void);
    virtual bool _FPaste(PClipboardObject pclip, bool fDoIt, long cid);

  public:
    static PDCH PdchNew(PDocumentBase pdocb, PFileByteStream pbsf, bool fFixed, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    virtual bool FCmdKey(PCMD_KEY pcmd);
};

/***************************************************************************
    Virtual class that supports displaying a group chunk - displays a GroupBase.
    Usually displays a DOCG, but doesn't have to.
***************************************************************************/
#define DCGB_PAR DCLB
#define kclsDCGB 'DCGB'
class DCGB : public DCGB_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(DCGB)
    ASSERT
    MARKMEM

  protected:
    long _dypBorder;  // height of border (included in _dypLine)
    long _clnItem;    // number of lines for each item
    long _ivCur;      // which item is selected
    long _dlnCur;     // which line in the item is selected
    PGroupBase _pgrpb;     // the group we're displaying
    long _cls;        // the class of the group
    bool _fAllocated; // whether the class is allocated or general

    DCGB(PDocumentBase pdocb, PGroupBase pgrpb, long cls, long clnItem, PGCB pgcb);

    virtual void _Activate(bool fActive);
    virtual long _ScvMax(bool fVert);
    long _YpFromIvDln(long iv, long dln)
    {
        return _YpFromLn(LwMul(iv, _clnItem) + dln);
    }
    long _LnFromIvDln(long iv, long dln)
    {
        return LwMul(iv, _clnItem) + dln;
    }
    long _LnLim(void)
    {
        return LwMul(_pgrpb->IvMac(), _clnItem);
    }
    void _SetSel(long ln);
    void _ShowSel(void);
    void _DrawSel(PGNV pgnv);
    void _InvalIv(long iv, long cvIns, long cvDel);
    void _EditIvDln(long iv, long dln);
    void _DeleteIv(long iv);

  public:
    static void InvalAllDcgb(PDocumentBase pdocb, PGroupBase pgrpb, long iv, long cvIns, long cvDel);
    virtual bool FCmdKey(PCMD_KEY pcmd);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);

    virtual bool FEnableDcgbCmd(PCMD pcmd, ulong *pgrfeds);
    virtual bool FCmdEditItem(PCMD pcmd);
    virtual bool FCmdDeleteItem(PCMD pcmd);
    virtual bool FCmdAddItem(PCMD pcmd) = 0;
};

/***************************************************************************
    Display DynamicArray or AllocatedArray chunk.
***************************************************************************/
#define DCGL_PAR DCGB
#define kclsDCGL 'DCGL'
class DCGL : public DCGL_PAR
{
    RTCLASS_DEC

  protected:
    DCGL(PDocumentBase pdocb, PVirtualArray pglb, long cls, PGCB pgcb);

  public:
    static PDCGL PdcglNew(PDocumentBase pdocb, PVirtualArray pglb, long cls, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdAddItem(PCMD pcmd);
};

/***************************************************************************
    Display GeneralGroup or AllocatedGroup chunk.
***************************************************************************/
#define DCGG_PAR DCGB
#define kclsDCGG 'DCGG'
class DCGG : public DCGG_PAR
{
    RTCLASS_DEC

  protected:
    DCGG(PDocumentBase pdocb, PVirtualGroup pggb, long cls, PGCB pgcb);

  public:
    static PDCGG PdcggNew(PDocumentBase pdocb, PVirtualGroup pggb, long cls, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdAddItem(PCMD pcmd);
};

/***************************************************************************
    Display StringTable or AllocatedStringTable chunk.
***************************************************************************/
#define DCST_PAR DCGB
#define kclsDCST 'DCST'
class DCST : public DCST_PAR
{
    RTCLASS_DEC

  protected:
    DCST(PDocumentBase pdocb, PVirtualStringTable pgstb, long cls, PGCB pgcb);

  public:
    static PDCST PdcstNew(PDocumentBase pdocb, PVirtualStringTable pgstb, long cls, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
    virtual bool FCmdAddItem(PCMD pcmd);
};

/***************************************************************************
    Display a picture chunk.
***************************************************************************/
#define DCPIC_PAR DocumentDisplayGraphicsObject
#define kclsDCPIC 'dpic'
class DCPIC : public DCPIC_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PPIC _ppic;

    DCPIC(PDocumentBase pdocb, PPIC ppic, PGCB pgcb);
    virtual void GetMinMax(RC *prcMinMax);

  public:
    static PDCPIC PdcpicNew(PDocumentBase pdocb, PPIC ppic, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
};

/***************************************************************************
    Display a MBMP chunk.
***************************************************************************/
#define DCMBMP_PAR DocumentDisplayGraphicsObject
#define kclsDCMBMP 'dmbp'
class DCMBMP : public DCMBMP_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PMBMP _pmbmp;

    DCMBMP(PDocumentBase pdocb, PMBMP pmbmp, PGCB pgcb);
    virtual void GetMinMax(RC *prcMinMax);

  public:
    static PDCMBMP PdcmbmpNew(PDocumentBase pdocb, PMBMP pmbmp, PGCB pgcb);

    virtual void Draw(PGNV pgnv, RC *prcClip);
};

/***************************************************************************
    Main Kidspace world for testing a script.
***************************************************************************/
typedef class TSCG *PTSCG;
#define TSCG_PAR WorldOfKidspace
#define kclsTSCG 'TSCG'
class TSCG : public TSCG_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(TSCG)

  public:
    TSCG(PGCB pgcb) : TSCG_PAR(pgcb)
    {
    }

    virtual void Draw(PGNV pgnv, RC *prcClip);
};

/***************************************************************************
    Text doc for the chunky editor.
***************************************************************************/
typedef class CHTXD *PCHTXD;
#define CHTXD_PAR PlainTextDocument
#define kclsCHTXD 'chtx'
class CHTXD : public CHTXD_PAR
{
  protected:
    CHTXD(PDocumentBase pdocb = pvNil, ulong grfdoc = fdocNil);

  public:
    static PCHTXD PchtxdNew(PFilename pfni = pvNil, PFileByteStream pbsf = pvNil, short osk = koskCur, PDocumentBase pdocb = pvNil,
                            ulong grfdoc = fdocNil);

    virtual PDocumentDisplayGraphicsObject PddgNew(PGCB pgcb);
};

/***************************************************************************
    Text display gob for the chunky editor.
***************************************************************************/
typedef class CHTDD *PCHTDD;
#define CHTDD_PAR LineTextGraphicsDocument
#define kclsCHTDD 'chtd'
class CHTDD : public CHTDD_PAR
{
    CMD_MAP_DEC(CHTDD)

  protected:
    CHTDD(PTextDocumentBase ptxtb, PGCB pgcb, long onn, ulong grfont, long dypFont, long cchTab);

  public:
    static PCHTDD PchtddNew(PTextDocumentBase ptxtb, PGCB pgcb, long onn, ulong grfont, long dypFont, long cchTab);

    virtual bool FCmdCompileChunky(PCMD pcmd);
    virtual bool FCmdCompileScript(PCMD pcmd);
};

void OpenSinkDoc(PMSFIL pmsfil);

#endif //! CHDOC_H
