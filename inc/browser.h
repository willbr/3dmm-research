/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/**************************************************************

   Browser Class

    Author : *****
    Review Status: Reviewed

    Studio Independent Browsers:
    BASE --> CommandHandler --> KidspaceGraphicObject	-->	BRWD  (Browser display class)
    BRWD --> BRWL  (Browser list class; chunky based)
    BRWD --> BRWT  (Browser text class)
    BRWD --> BRWL --> BRWN  (Browser named list class)

    Studio Dependent Browsers:
    BRWD --> BRWR  (Roll call class)
    BRWD --> BRWT --> BRWA  (Browser action class)
    BRWD --> BRWL --> BRWP	(Browser prop/actor class)
    BRWD --> BRWL --> BRWB	(Browser background class)
    BRWD --> BRWL --> BRWC	(Browser camera class)
    BRWD --> BRWL --> BRWN --> BRWM (Browser music class)
    BRWD --> BRWL --> BRWN --> BRWM --> BRWI (Browser import sound class)

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
#define BRCN_PAR BASE
#define kclsBRCN 'BRCN'
typedef class BRCN *PBRCN;
class BRCN : public BRCN_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    BRCN(void){};

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
#define BRWD_PAR KidspaceGraphicObject
#define kclsBRWD 'BRWD'
#define brwdidNil ivNil
typedef class BRWD *PBRWD;
class BRWD : public BRWD_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(BRWD)

  protected:
    long _kidFrmFirst;     // kid of first frame
    long _kidControlFirst; // kid of first control button
    long _dxpFrmOffset;    // x inset of thumb in frame
    long _dypFrmOffset;    // y inset of thumb in frame
    long _sidDefault;      // default sid
    long _thumDefault;     // default thum
    PBRCN _pbrcn;          // context carryover
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
    bool _FInitGok(PRCA prca, long kidGlass);
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
    BRWD(PGCB pgcb) : BRWD_PAR(pgcb)
    {
        _ithumOverride = -1;
        _kidThumOverride = -1;
    }
    ~BRWD(void);

    static PBRWD PbrwdNew(PRCA prca, long kidPar, long kidBrwd);
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
#define BRCNL_PAR BRCN
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
    PStringTable pgst;
    PChunkyResourceManager pcrm;
};

//
//	Thumbnail descriptors : one per thumbnail
//
const long kglstnGrow = 5;
const long kglthdGrow = 10;
struct THD
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
    long ithd;     // Original index for this THD, before sorting (used to
                   // retrieve proper STN for the BRWN-derived browsers)
};

/* Browser Content List Base --  create one of these when you want a list of a
    specific kind of content and you don't care about the names. */
#define BCL_PAR BASE
typedef class BCL *PBCL;
#define kclsBCL 'BCL'
class BCL : public BCL_PAR
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
    BCL(void)
    {
        _pglthd = pvNil;
    }
    ~BCL(void)
    {
        ReleasePpo(&_pglthd);
    }

    bool _FInit(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PDynamicArray pglthd);
    bool _FAddGokdToThd(PChunkyFile pcfl, long sid, ChunkIdentification *pcki);
    bool _FAddFileToThd(PChunkyFile pcfl, long sid);
    bool _FBuildThd(PChunkyResourceManager pcrm);

    virtual bool _FAddGokdToThd(PChunkyFile pcfl, long sid, ChildChunkIdentification *pkid);

  public:
    static PBCL PbclNew(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PDynamicArray pglthd = pvNil, bool fOnlineOnly = fFalse);

    PDynamicArray Pglthd(void)
    {
        return _pglthd;
    }
    void GetThd(long ithd, THD *pthd)
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
#define BCLS_PAR BCL
typedef class BCLS *PBCLS;
#define kclsBCLS 'BCLS'
class BCLS : public BCLS_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PStringTable _pgst;

  protected:
    BCLS(void)
    {
        _pgst = pvNil;
    }
    ~BCLS(void)
    {
        ReleasePpo(&_pgst);
    }

    bool _FInit(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PStringTable pgst, PDynamicArray pglthd);
    bool _FSetNameGst(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno);

    virtual bool _FAddGokdToThd(PChunkyFile pcfl, long sid, ChildChunkIdentification *pkid);

  public:
    static PBCLS PbclsNew(PChunkyResourceManager pcrm, ChunkIdentification *pckiRoot, ChunkTag ctgContent, PDynamicArray pglthd = pvNil, PStringTable pgst = pvNil,
                          bool fOnlineOnly = fFalse);

    PStringTable Pgst(void)
    {
        return _pgst;
    }
};

/************************************

   Browser List Class
   Derived from the Display Class

*************************************/
#define BRWL_PAR BRWD
#define kclsBRWL 'BRWL'
typedef class BRWL *PBRWL;

// Browser Selection Flags
// This specifies what the sorting is based on
enum BWS
{
    kbwsIndex = 1,
    kbwsChid = 2,
    kbwsCnoRoot = 3, // defaults to CnoRoot if ctg of Par is ctgNil
    kbwsLim
};

class BRWL : public BRWL_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    bool _fEnableAccel;

    // Thumnail descriptor lists
    PChunkyResourceManager _pcrm;  // Chunky resource manager
    PDynamicArray _pglthd; // Thumbnail descriptor	gl
    PStringTable _pgst;  // Chunk name

    // Browser Search (List) parameters
    BWS _bws;         // Selection type flag
    bool _fSinglePar; // Single parent search
    ChunkIdentification _ckiRoot;     // Grandparent cno=cnoNil => global search
    ChunkTag _ctgContent;  // Parent

  protected:
    // BRWL List
    bool _FInitNew(PCommand pcmd, BWS bws, long ThumSelect, ChunkIdentification ckiRoot, ChunkTag ctgContent);
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

    // BRWL util
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
    BRWL(PGCB pgcb) : BRWL_PAR(pgcb)
    {
    }
    ~BRWL(void);

    static PBRWL PbrwlNew(PRCA prca, long kidPar, long kidBrwl);
    virtual bool FInit(PCommand pcmd, BWS bws, long ThumSelect, long sidSelect, ChunkIdentification ckiRoot, ChunkTag ctgContent, PStudio pstdio,
                       PBRCNL pbrcnl = pvNil, bool fWrapScroll = fTrue, long cthumScroll = ivNil);
};

/************************************

   Browser Text Class
   Derived from the Display Class

*************************************/
#define BRWT_PAR BRWD
#define kclsBRWT 'BRWT'
typedef class BRWT *PBRWT;
class BRWT : public BRWT_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PStringTable _pgst;
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
    BRWT(PGCB pgcb) : BRWT_PAR(pgcb)
    {
        _idsFont = idsNil;
    }
    ~BRWT(void);

    static PBRWT PbrwtNew(PRCA prca, long kidPar, long kidBrwt);
    void SetGst(PStringTable pgst);
    bool FInit(PCommand pcmd, long thumSelect, long thumDisplay, PStudio pstdio, bool fWrapScroll = fTrue,
               long cthumScroll = ivNil);
};

/************************************

   Browser Named List Class
   Derived from the Browser List Class

*************************************/
#define BRWN_PAR BRWL
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
    BRWN(PGCB pgcb) : BRWN_PAR(pgcb)
    {
    }
    ~BRWN(void){};
    virtual bool FInit(PCommand pcmd, BWS bws, long ThumSelect, long sidSelect, ChunkIdentification ckiRoot, ChunkTag ctgContent, PStudio pstdio,
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
    BRWA(PGCB pgcb) : BRWA_PAR(pgcb)
    {
        _idsFont = idsActionFont;
        _celnStart = 0;
    }
    ~BRWA(void)
    {
    }

    static PBRWA PbrwaNew(PRCA prca);
    bool FBuildApe(PActor pactr);
    bool FBuildGst(PScene pscen);
    virtual bool FCmdChangeCel(PCommand pcmd);
};

/************************************

   Browser Prop & Actor Class
   Derived from the Browser List Class

*************************************/
#define BRWP_PAR BRWL
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
    BRWP(PGCB pgcb) : BRWP_PAR(pgcb)
    {
    }
    ~BRWP(void){};

    static PBRWP PbrwpNew(PRCA prca, long kidGlass);
};

/************************************

   Browser Background Class
   Derived from the Browser List Class

*************************************/
#define BRWB_PAR BRWL
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
    BRWB(PGCB pgcb) : BRWB_PAR(pgcb)
    {
    }
    ~BRWB(void){};

    static PBRWB PbrwbNew(PRCA prca);
    virtual bool FCmdCancel(PCommand pcmd);
};

/************************************

   Browser Camera Class
   Derived from the Browser List Class

*************************************/
#define BRWC_PAR BRWL
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
    BRWC(PGCB pgcb) : BRWC_PAR(pgcb)
    {
    }
    ~BRWC(void){};

    static PBRWC PbrwcNew(PRCA prca);

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
    bool _FAddThd(STN *pstn, ChunkIdentification *pcki);
    bool _FSndListed(ChunkNumber cno, long *pithd = pvNil);

  public:
    //
    // Constructors and destructors
    //
    BRWM(PGCB pgcb) : BRWM_PAR(pgcb)
    {
        _idsFont = idsSoundFont;
    }
    ~BRWM(void){};

    static PBRWM PbrwmNew(PRCA prca, long kidGlass, long sty, PStudio pstdio);
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
    BRWI(PGCB pgcb) : BRWI_PAR(pgcb)
    {
        _idsFont = idsSoundFont;
    }
    ~BRWI(void);

    static PBRWI PbrwiNew(PRCA prca, long kidGlass, long sty);
    bool FInit(PCommand pcmd, ChunkIdentification cki, PStudio pstdio);
};

/************************************

   Browser Roll Call Class
   Derived from the Display Class

*************************************/
#define BRWR_PAR BRWD
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
    BRWR(PGCB pgcb) : BRWR_PAR(pgcb)
    {
        _fApplyingSel = fFalse;
        _idsFont = idsRollCallFont;
    }
    ~BRWR(void);

    static PBRWR PbrwrNew(PRCA prca, long kid);
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
