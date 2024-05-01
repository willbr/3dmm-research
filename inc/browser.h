/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/**************************************************************

   Browser Class

    Author : *****
    Review Status: Reviewed

    Studio Independent Browsers:
    BASE --> CommandHandler --> KidspaceGraphicObject	-->	BrowserDisplay  (Browser display class)
    BrowserDisplay --> BrowserList  (Browser list class; chunky based)
    BrowserDisplay --> BRWT  (Browser text class)
    BrowserDisplay --> BrowserList --> BRWN  (Browser named list class)

    Studio Dependent Browsers:
    BrowserDisplay --> BRWR  (Roll call class)
    BrowserDisplay --> BRWT --> BRWA  (Browser action class)
    BrowserDisplay --> BrowserList --> BRWP	(Browser prop/actor class)
    BrowserDisplay --> BrowserList --> BRWB	(Browser background class)
    BrowserDisplay --> BrowserList --> BRWC	(Browser camera class)
    BrowserDisplay --> BrowserList --> BRWN --> BRWM (Browser music class)
    BrowserDisplay --> BrowserList --> BRWN --> BRWM --> BRWI (Browser import sound class)

    Note: An "frm" refers to the displayed frames on any page.
    A "thum" is a generic Browser Thumbnail, which may be a
    chid, cno, cnoPar, gob, stn, etc.	A browser will display,
    over multiple pages, as many browser entities as there are
    thum's.

***************************************************************/

#ifndef BRWD_H
#define BRWD_H

const long kcmhlBrowser = 0x11000; // nice medium level for the Browser
const long kcbMaxCrm = 300000;
const long kdwTotalPhysLim = 10240000; // 10MB	heuristic
const long kdwAvailPhysLim = 1024000;  // 1MB heuristic
const auto kBrwsScript = (kstDefault << 16) | kchidBrowserDismiss;

/************************************

    Browser Context	CLass
    Optional context to carry over
    between successive instantiations
    of the same browser

*************************************/
#define BrowserContext_PAR BASE
#define kclsBrowserContext 'BRCN'
typedef class BrowserContext *PBrowserContext;
class BrowserContext : public BrowserContext_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BrowserContext(void){};

  public:
    long brwdid;
    long ithumPageFirst;
};

/************************************

   Browser ThumbFile Cki Struct

*************************************/
struct TFC
{
    short bo;
    short osk;
    union {
        struct
        {
            ChunkTag ctg;
            ChunkNumber cno;
        };
        struct
        {
            ulong grfontMask;
            ulong grfont;
        };
        struct
        {
            ChunkTag ctg;
            ChildChunkID chid;
        };
    };
};
const ByteOrderMask kbomTfc = 0x5f000000;

/************************************

   Browser Display Class

*************************************/
#define BrowserDisplay_PAR KidspaceGraphicObject
#define kclsBrowserDisplay 'BRWD'
#define brwdidNil ivNil
typedef class BrowserDisplay *PBrowserDisplay;
class BrowserDisplay : public BrowserDisplay_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(BrowserDisplay)

  protected:
    long _kidFrmFirst;     // kid of first frame
    long _kidControlFirst; // kid of first control button
    long _dxpFrmOffset;    // x inset of thumb in frame
    long _dypFrmOffset;    // y inset of thumb in frame
    long _sidDefault;      // default sid
    long _thumDefault;     // default thum
    PBrowserContext _pbrcn;          // context carryover
    long _idsFont;         // string id of Font
    long _kidThumOverride; // projects may override one thum gobid
    long _ithumOverride;   // projects may override one thum gobid
    PTGOB _ptgobPage;      // for page numbers
    PStudio _pstdio;

    // Display State variables
    long _cthumCD;          // Non-user content
    long _ithumSelect;      // Hilited frame
    long _ithumPageFirst;   // Index to thd of first frame on current page
    long _cfrmPageCur;      // Number of visible thumbnails per current page
    long _cfrm;             // Total frames possible per page
    long _cthumScroll;      // #items to scroll on fwd/back.  default ivNil -> page scrolling
    bool _fWrapScroll;      // Wrap around.  Default = fTrue;
    bool _fNoRepositionSel; // Don't reposition selection : default = fFalse;

  protected:
    void _SetScrollState(void);
    long _CfrmCalc(void);
    static bool _FBuildGcb(GraphicsObjectBlock *pgcb, long kidPar, long kidBrws);
    bool _FInitGok(PResourceCache prca, long kidGlass);
    void _SetVarForOverride(void);

    virtual long _Cthum(void)
    {
        AssertThis(0);
        return 0;
    }
    virtual bool _FSetThumFrame(long ithum, PGraphicsObject pgobPar)
    {
        AssertThis(0);
        return fFalse;
    }
    virtual bool _FClearHelp(long ifrm)
    {
        return fTrue;
    }
    virtual void _ReleaseThumFrame(long ifrm)
    {
    }
    virtual long _IthumFromThum(long thum, long sid)
    {
        return thum;
    }
    virtual void _GetThumFromIthum(long ithum, void *pThumSelect, long *psid);
    virtual void _ApplySelection(long thumSelect, long sid)
    {
    }
    virtual void _ProcessSelection(void)
    {
    }
    virtual bool _FUpdateLists()
    {
        return fTrue;
    }
    virtual void _SetCbPcrmMin(void)
    {
    }
    void _CalcIthumPageFirst(void);
    bool _FIsIthumOverride(long ithum)
    {
        return FPure(ithum == _ithumOverride);
    }
    PGraphicsObject _PgobFromIfrm(long ifrm);
    long _KidThumFromIfrm(long ifrm);
    void _UnhiliteCurFrm(void);
    bool _FHiliteFrm(long ifrmSelect);
    void _InitStateVars(PCommand pcmd, PStudio pstdio, bool fWrapScroll, long cthumScroll);
    void _InitFromData(PCommand pcmd, long ithumSelect, long ithumDisplay);
    virtual void _CacheContext(void);

  public:
    //
    // Constructors and destructors
    //
    BrowserDisplay(PGraphicsObjectBlock pgcb) : BrowserDisplay_PAR(pgcb)
    {
        _ithumOverride = -1;
        _kidThumOverride = -1;
    }
    ~BrowserDisplay(void);

    static PBrowserDisplay PbrwdNew(PResourceCache prca, long kidPar, long kidBrwd);
    void Init(PCommand pcmd, long ithumSelect, long ithumDisplay, PStudio pstdio, bool fWrapScroll = fTrue,
              long cthumScroll = ivNil);
    bool FDraw(void);
    bool FCreateAllTgob(void); // For any text based browsers

    //
    // Command Handlers
    // Selection does not exit the browser
    //
    bool FCmdFwd(PCommand pcmd);  // Page fwd
    bool FCmdBack(PCommand pcmd); // Page back
    bool FCmdSelect(PCommand pcmd);
    bool FCmdSelectThum(PCommand pcmd); // Set viewing page
    virtual void Release(void);
    virtual bool FCmdCancel(PCommand pcmd); // See brwb
    virtual bool FCmdDel(PCommand pcmd)
    {
        return fTrue;
    } // See brwm
    virtual bool FCmdOk(PCommand pcmd);
    virtual bool FCmdFile(PCommand pcmd)
    {
        return fTrue;
    } // See brwm
    virtual bool FCmdChangeCel(PCommand pcmd)
    {
        return fTrue;
    } // See brwa
};

/************************************

    Browser List Context CLass
    Optional context to carry over
    between successive instantiations
    of the same browser

*************************************/
#define BRCNL_PAR BrowserContext
#define kclsBRCNL 'brcl'
typedef class BRCNL *PBRCNL;
class BRCNL : public BRCNL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ~BRCNL(void);

  public:
    long cthumCD;
    ChunkIdentification ckiRoot;
    PDynamicArray pglthd;
    PStringTable_GST pgst;
    PChunkyResourceManager pcrm;
};

//
//	Thumbnail descriptors : one per thumbnail
//
const long kglstnGrow = 5;
const long kglthdGrow = 10;
struct ThumbnailDescriptor
{
    union {
        TAG tag; // TAG pointing to content
        struct
        {
            long lwFill1; // sid
            long lwFill2; // pcrf
            ulong grfontMask;
            ulong grfont;
        };
        struct
        {
            long lwFill1;
            long lwFill2;
            ChunkTag ctg;
            ChildChunkID chid; // ChildChunkID of CD content
        };
    };

    ChunkNumber cno;       // KidspaceGraphicObjectDescriptor cno
    ChildChunkID chidThum; // KidspaceGraphicObjectDescriptor's parent's ChildChunkID (relative to KidspaceGraphicObjectDescriptor parent's parent)
    long ithd;     // Original index for this ThumbnailDescriptor, before sorting (used to
                   // retrieve proper String for the BRWN-derived browsers)
};

/* Browser Content List Base --  create one of these when you want a list of a
    specific kind of content and you don't care about the names. */
#define BrowserContentList_PAR BASE
typedef class BrowserContentList *PBrowserContentList;
#define kclsBrowserContentList 'BCL'
class BrowserContentList : public BrowserContentList_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ChunkTag _ctgRoot;
    ChunkNumber _cnoRoot;
    ChunkTag _ctgContent;
    bool _fDescend;
    PDynamicArray _pglthd;

  protected:
    BrowserContentList(void)
    {
        _pglthd = pvNil;
    }
    ~BrowserContentList(void)
    {
        ReleasePpo(&_pglthd);
    }

    bool _FInit(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PDynamicArray pglthd);
    bool _FAddGokdToThd(PChunkyFile pcfl, long sid, ChunkIdentification *pcki);
    bool _FAddFileToThd(PChunkyFile pcfl, long sid);
    bool _FBuildThd(PChunkyResourceManager pcrm);

    virtual bool _FAddGokdToThd(PChunkyFile pcfl, long sid, ChildChunkIdentification *pkid);

  public:
    static PBrowserContentList PbclNew(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PDynamicArray pglthd = pvNil, bool fOnlineOnly = fFalse);

    PDynamicArray Pglthd(void)
    {
        return _pglthd;
    }
    void GetThd(long ithd, ThumbnailDescriptor *pthd)
    {
        _pglthd->Get(ithd, pthd);
    }
    long IthdMac(void)
    {
        return _pglthd->IvMac();
    }
};

/* Browser Content List with Strings -- create one of these when you need to
    browse content by name */
#define BCLS_PAR BrowserContentList
typedef class BCLS *PBCLS;
#define kclsBCLS 'BCLS'
class BCLS : public BCLS_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PStringTable_GST _pgst;

  protected:
    BCLS(void)
    {
        _pgst = pvNil;
    }
    ~BCLS(void)
    {
        ReleasePpo(&_pgst);
    }

    bool _FInit(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PStringTable_GST pgst, PDynamicArray pglthd);
    bool _FSetNameGst(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);

    virtual bool _FAddGokdToThd(PChunkyFile pcfl, long sid, ChildChunkIdentification *pkid);

  public:
    static PBCLS PbclsNew(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PDynamicArray pglthd = pvNil, PStringTable_GST pgst = pvNil,
                          bool fOnlineOnly = fFalse);

    PStringTable_GST Pgst(void)
    {
        return _pgst;
    }
};

/************************************

   Browser List Class
   Derived from the Display Class

*************************************/
#define BrowserList_PAR BrowserDisplay
#define kclsBrowserList 'BRWL'
typedef class BrowserList *PBrowserList;

// Browser Selection Flags
// This specifies what the sorting is based on
enum BrowserSelectionFlags
{
    kbwsIndex = 1,
    kbwsChid = 2,
    kbwsCnoRoot = 3, // defaults to CnoRoot if ctg of Par is ctgNil
    kbwsLim
};

class BrowserList : public BrowserList_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    bool _fEnableAccel;

    // Thumnail descriptor lists
    PChunkyResourceManager _pcrm;  // Chunky resource manager
    PDynamicArray _pglthd; // Thumbnail descriptor	gl
    PStringTable_GST _pgst;  // Chunk name

    // Browser Search (List) parameters
    BrowserSelectionFlags _bws;         // Selection type flag
    bool _fSinglePar; // Single parent search
    ChunkIdentification _ckiRoot;     // Grandparent cno=cnoNil => global search
    ChunkTag _ctgContent;  // Parent

  protected:
    // BrowserList List
    bool _FInitNew(PCommand pcmd, BrowserSelectionFlags bws, long ThumSelect, ChunkIdentification ckiRoot, ChunkTag ctgContent);
    bool _FCreateBuildThd(ChunkIdentification ckiRoot, ChunkTag ctgContent, bool fBuildGl = fTrue);
    virtual bool _FGetContent(PChunkyResourceManager pcrm, ChunkIdentification *pcki, ChunkTag ctg, bool fBuildGl);
    virtual long _Cthum(void)
    {
        AssertThis(0);
        return _pglthd->IvMac();
    }
    virtual bool _FSetThumFrame(long ithd, PGraphicsObject pgobPar);
    virtual bool _FUpdateLists()
    {
        return fTrue;
    } // Eg, to include user sounds

    // BrowserList util
    void _SortThd(void);
    virtual void _GetThumFromIthum(long ithum, void *pThumSelect, long *psid);
    virtual void _ReleaseThumFrame(long ifrm);
    virtual long _IthumFromThum(long thum, long sid);
    virtual void _CacheContext(void);
    virtual void _SetCbPcrmMin(void);

  public:
    //
    // Constructors and destructors
    //
    BrowserList(PGraphicsObjectBlock pgcb) : BrowserList_PAR(pgcb)
    {
    }
    ~BrowserList(void);

    static PBrowserList PbrwlNew(PResourceCache prca, long kidPar, long kidBrwl);
    virtual bool FInit(PCommand pcmd, BrowserSelectionFlags bws, long ThumSelect, long sidSelect, ChunkIdentification ckiRoot, ChunkTag ctgContent, PStudio pstdio,
                       PBRCNL pbrcnl = pvNil, bool fWrapScroll = fTrue, long cthumScroll = ivNil);
};

/************************************

   Browser Text Class
   Derived from the Display Class

*************************************/
#define BRWT_PAR BrowserDisplay
#define kclsBRWT 'BRWT'
typedef class BRWT *PBRWT;
class BRWT : public BRWT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PStringTable_GST _pgst;
    bool _fEnableAccel;

    virtual long _Cthum(void)
    {
        AssertThis(0);
        return _pgst->IvMac();
    }
    virtual bool _FSetThumFrame(long istn, PGraphicsObject pgobPar);
    virtual void _ReleaseThumFrame(long ifrm)
    {
    } // No gob to release

  public:
    //
    // Constructors and destructors
    //
    BRWT(PGraphicsObjectBlock pgcb) : BRWT_PAR(pgcb)
    {
        _idsFont = idsNil;
    }
    ~BRWT(void);

    static PBRWT PbrwtNew(PResourceCache prca, long kidPar, long kidBrwt);
    void SetGst(PStringTable_GST pgst);
    bool FInit(PCommand pcmd, long thumSelect, long thumDisplay, PStudio pstdio, bool fWrapScroll = fTrue,
               long cthumScroll = ivNil);
};

/************************************

   Browser Named List Class
   Derived from the Browser List Class

*************************************/
#define BRWN_PAR BrowserList
#define kclsBRWN 'BRWN'
typedef class BRWN *PBRWN;
class BRWN : public BRWN_PAR
{
    RTCLASS_DEC

  protected:
    virtual bool _FGetContent(PChunkyResourceManager pcrm, ChunkIdentification *pcki, ChunkTag ctg, bool fBuildGl);
    virtual long _Cthum(void)
    {
        return _pglthd->IvMac();
    }
    virtual bool _FSetThumFrame(long ithd, PGraphicsObject pgobPar);
    virtual void _ReleaseThumFrame(long ifrm);

  public:
    //
    // Constructors and destructors
    //
    BRWN(PGraphicsObjectBlock pgcb) : BRWN_PAR(pgcb)
    {
    }
    ~BRWN(void){};
    virtual bool FInit(PCommand pcmd, BrowserSelectionFlags bws, long ThumSelect, long sidSelect, ChunkIdentification ckiRoot, ChunkTag ctgContent, PStudio pstdio,
                       PBRCNL pbrcnl = pvNil, bool fWrapScroll = fTrue, long cthumScroll = ivNil);

    virtual bool FCmdOk(PCommand pcmd);
};

/************************************

   Studio Specific Browser Classes

*************************************/
/************************************

   Browser Action Class
   Derived from the Browser Text Class
   Actions are separately classed for
   previews

*************************************/
#define BRWA_PAR BRWT
#define kclsBRWA 'BRWA'
typedef class BRWA *PBRWA;
class BRWA : public BRWA_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    long _celnStart;              // Starting cel number
    PActorPreviewEntity _pape;                   // Actor Preview Entity
    void _ProcessSelection(void); // Action Preview
    virtual void _ApplySelection(long thumSelect, long sid);

  public:
    //
    // Constructors and destructors
    //
    BRWA(PGraphicsObjectBlock pgcb) : BRWA_PAR(pgcb)
    {
        _idsFont = idsActionFont;
        _celnStart = 0;
    }
    ~BRWA(void)
    {
    }

    static PBRWA PbrwaNew(PResourceCache prca);
    bool FBuildApe(PActor pactr);
    bool FBuildGst(PScene pscen);
    virtual bool FCmdChangeCel(PCommand pcmd);
};

/************************************

   Browser Prop & Actor Class
   Derived from the Browser List Class

*************************************/
#define BRWP_PAR BrowserList
#define kclsBRWP 'BRWP'
typedef class BRWP *PBRWP;
class BRWP : public BRWP_PAR
{
    RTCLASS_DEC

  protected:
    virtual void _ApplySelection(long thumSelect, long sid);

  public:
    //
    // Constructors and destructors
    //
    BRWP(PGraphicsObjectBlock pgcb) : BRWP_PAR(pgcb)
    {
    }
    ~BRWP(void){};

    static PBRWP PbrwpNew(PResourceCache prca, long kidGlass);
};

/************************************

   Browser Background Class
   Derived from the Browser List Class

*************************************/
#define BRWB_PAR BrowserList
#define kclsBRWB 'BRWB'
typedef class BRWB *PBRWB;
class BRWB : public BRWB_PAR
{
    RTCLASS_DEC

  protected:
    virtual void _ApplySelection(long thumSelect, long sid);

  public:
    //
    // Constructors and destructors
    //
    BRWB(PGraphicsObjectBlock pgcb) : BRWB_PAR(pgcb)
    {
    }
    ~BRWB(void){};

    static PBRWB PbrwbNew(PResourceCache prca);
    virtual bool FCmdCancel(PCommand pcmd);
};

/************************************

   Browser Camera Class
   Derived from the Browser List Class

*************************************/
#define BRWC_PAR BrowserList
#define kclsBRWC 'BRWC'
typedef class BRWC *PBRWC;
class BRWC : public BRWC_PAR
{
    RTCLASS_DEC

  protected:
    virtual void _ApplySelection(long thumSelect, long sid);
    virtual void _SetCbPcrmMin(void)
    {
    }

  public:
    //
    // Constructors and destructors
    //
    BRWC(PGraphicsObjectBlock pgcb) : BRWC_PAR(pgcb)
    {
    }
    ~BRWC(void){};

    static PBRWC PbrwcNew(PResourceCache prca);

    virtual bool FCmdCancel(PCommand pcmd);
};

/************************************

   Browser Music Class (midi, speech & fx)
   Derived from the Browser Named List Class

*************************************/
#define BRWM_PAR BRWN
#define kclsBRWM 'brwm'
typedef class BRWM *PBRWM;
class BRWM : public BRWM_PAR
{
    RTCLASS_DEC

  protected:
    long _sty;  // Identifies type of sound
    PChunkyResourceFile _pcrf; // NOT created here (autosave or BRWI file)

    virtual void _ApplySelection(long thumSelect, long sid);
    virtual bool _FUpdateLists(); // By all entries in pcrf of correct type
    void _ProcessSelection(void); // Sound Preview
    bool _FAddThd(String *pstn, ChunkIdentification *pcki);
    bool _FSndListed(ChunkNumber cno, long *pithd = pvNil);

  public:
    //
    // Constructors and destructors
    //
    BRWM(PGraphicsObjectBlock pgcb) : BRWM_PAR(pgcb)
    {
        _idsFont = idsSoundFont;
    }
    ~BRWM(void){};

    static PBRWM PbrwmNew(PResourceCache prca, long kidGlass, long sty, PStudio pstdio);
    virtual bool FCmdFile(PCommand pcmd); // Upon portfolio completion
    virtual bool FCmdDel(PCommand pcmd);  // Delete user sound
};

/************************************

   Browser Import Sound Class
   Derived from the Browser List Class
   Note: Inherits pgst from the list class

*************************************/
#define BRWI_PAR BRWM
#define kclsBRWI 'BRWI'
typedef class BRWI *PBRWI;
class BRWI : public BRWI_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // The following are already handled by BRWM
    // virtual void _ProcessSelection(void);
    virtual void _ApplySelection(long thumSelect, long sid);

  public:
    //
    // Constructors and destructors
    //
    BRWI(PGraphicsObjectBlock pgcb) : BRWI_PAR(pgcb)
    {
        _idsFont = idsSoundFont;
    }
    ~BRWI(void);

    static PBRWI PbrwiNew(PResourceCache prca, long kidGlass, long sty);
    bool FInit(PCommand pcmd, ChunkIdentification cki, PStudio pstdio);
};

/************************************

   Browser Roll Call Class
   Derived from the Display Class

*************************************/
#define BRWR_PAR BrowserDisplay
#define kclsBRWR 'BRWR'
typedef class BRWR *PBRWR;
class BRWR : public BRWR_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ChunkTag _ctg;
    PChunkyResourceManager _pcrm; // Chunky resource manager
    bool _fApplyingSel;

  protected:
    virtual long _Cthum(void);
    virtual bool _FSetThumFrame(long istn, PGraphicsObject pgobPar);
    virtual void _ReleaseThumFrame(long ifrm);
    virtual void _ApplySelection(long thumSelect, long sid);
    virtual void _ProcessSelection(void);
    virtual bool _FClearHelp(long ifrm);
    long _IaridFromIthum(long ithum, long iaridFirst = 0);
    long _IthumFromArid(long arid);

  public:
    //
    // Constructors and destructors
    //
    BRWR(PGraphicsObjectBlock pgcb) : BRWR_PAR(pgcb)
    {
        _fApplyingSel = fFalse;
        _idsFont = idsRollCallFont;
    }
    ~BRWR(void);

    static PBRWR PbrwrNew(PResourceCache prca, long kid);
    void Init(PCommand pcmd, long thumSelect, long thumDisplay, PStudio pstdio, bool fWrapScroll = fTrue,
              long cthumScroll = ivNil);
    bool FInit(PCommand pcmd, ChunkTag ctg, long ithumDisplay, PStudio pstdio);
    bool FUpdate(long arid, PStudio pstdio);
    bool FApplyingSel(void)
    {
        AssertBaseThis(0);
        return _fApplyingSel;
    }
};

const long kglcmgGrow = 8;
struct CMG // Gokd Cno Map
{
    ChunkNumber cnoTmpl; // Content cno
    ChunkNumber cnoGokd; // Thumbnail gokd cno
};

/************************************

   Fne for  Thumbnails
   Enumerates current product first

*************************************/
#define FNET_PAR BASE
#define kclsFNET 'FNET'
typedef class FNET *PFNET;
class FNET : public FNET_PAR
{
    RTCLASS_DEC

  protected:
    bool _fInitMSKDir;
    FileNameEnumerator _fne;
    FileNameEnumerator _fneDir;
    Filename _fniDirMSK;
    Filename _fniDir;
    Filename _fniDirProduct;
    bool _fInited;

  protected:
    bool _FNextFni(Filename *pfni, long *psid);

  public:
    //
    // Constructors and destructors
    //
    FNET(void) : FNET_PAR()
    {
        _fInited = fFalse;
    }
    ~FNET(void){};

    bool FInit(void);
    bool FNext(Filename *pfni, long *psid = pvNil);
};

#endif
