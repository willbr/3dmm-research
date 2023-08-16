/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Graphic object class.

***************************************************************************/
#ifndef GOB_H
#define GOB_H

enum
{
    fgobNil = 0,
    fgobSibling = 1,    // for Constructors
    fgobEnsureHwnd = 2, // for FInitScreen
    fgobNoVis = 0,      // for DrawTree
    fgobAutoVis = 4,    // for DrawTree
    fgobUseVis = 8,     // for DrawTree
};

// GraphicsObject invalidation types
enum
{
    ginNil,
    kginDraw,
    kginMark,
    kginSysInval,
    kginDefault
};

const long krelOne = 0x00010000L; // denominator for relative rectangles
const long krelZero = 0;

#ifdef MAC
inline void GetClientRect(HWND hwnd, RCS *prcs)
{
    *prcs = hwnd->port.portRect;
}
inline void InvalHwndRcs(HWND hwnd, RCS *prcs)
{
    PPRT pprt;

    GetPort(&pprt);
    SetPort(&hwnd->port);
    InvalRect(prcs);
    SetPort(pprt);
}
inline void ValidHwndRcs(HWND hwnd, RCS *prcs)
{
    PPRT pprt;

    GetPort(&pprt);
    SetPort(&hwnd->port);
    ValidRect(prcs);
    SetPort(pprt);
}
#endif // MAC
#ifdef WIN
inline void InvalHwndRcs(HWND hwnd, RCS *prcs)
{
    InvalidateRect(hwnd, prcs, fFalse);
}
inline void ValidHwndRcs(HWND hwnd, RCS *prcs)
{
    ValidateRect(hwnd, prcs);
}
#endif // WIN

// coordinates
enum
{
    cooLocal,  // top-left is (0,0)
    cooParent, // relative to parent
    cooGpt,    // relative to the UI port
    cooHwnd,   // relative to the enclosing hwnd
    cooGlobal, // global coordinates
    cooLim
};

/****************************************
    GraphicsObject creation block
****************************************/
struct GraphicsObjectBlock
{
    long _hid;
    PGraphicsObject _pgob;
    ulong _grfgob;
    long _gin;
    RC _rcAbs;
    RC _rcRel;

    GraphicsObjectBlock(void)
    {
    }
    GraphicsObjectBlock(long hid, PGraphicsObject pgob, ulong grfgob = fgobNil, long gin = kginDefault, RC *prcAbs = pvNil, RC *prcRel = pvNil)
    {
        Set(hid, pgob, grfgob, gin, prcAbs, prcRel);
    }
    void Set(long hid, PGraphicsObject pgob, ulong grfgob = fgobNil, long gin = kginDefault, RC *prcAbs = pvNil,
             RC *prcRel = pvNil);
};
typedef GraphicsObjectBlock *PGCB;

/****************************************
    Graphics object
****************************************/
#define GraphicsObject_PAR CMH
#define kclsGraphicsObject 'GOB'
class GraphicsObject : public GraphicsObject_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(GraphicsObject)
    ASSERT
    MARKMEM

    friend class GTE;

  private:
    static PGraphicsObject _pgobScreen;

    HWND _hwnd;   // the OS window (may be nil)
    PGPT _pgpt;   // the graphics port (may be shared with _pgobPar)
    PCURS _pcurs; // the cursor to show over this gob

    RC _rcCur; // current position
    RC _rcVis; // current visible rectangle (in its parent)
    RC _rcAbs; //_rcAbs and _rcRel describe the position of this
    RC _rcRel; // gob in its parent.

    // tree management
    PGraphicsObject _pgobPar;
    PGraphicsObject _pgobChd;
    PGraphicsObject _pgobSib;

    // variables
    PDynamicArray _pglrtvm;

    void _SetRcCur(void);
    HWND _HwndGetDptFromCoo(PT *pdpt, long coo);

  protected:
    static long _ginDefGob;
    static long _gridLast;

    long _grid;
    long _ginDefault : 8;
    long _fFreeing : 1;
    long _fCreating : 1;

    ~GraphicsObject(void);

    static HWND _HwndNewMdi(PSTN pstnTitle);
    static void _DestroyHwnd(HWND hwnd);

    void _Init(PGCB pgcb);
    HWND _HwndGetRc(RC *prc);
    virtual void _NewRc(void)
    {
    }
    virtual void _ActivateHwnd(bool fActive)
    {
    }

  public:
    static bool FInitScreen(ulong grfgob, long ginDef);
    static void ShutDown(void);
    static PGraphicsObject PgobScreen(void)
    {
        return _pgobScreen;
    }
    static PGraphicsObject PgobFromHwnd(HWND hwnd);
    static PGraphicsObject PgobFromClsScr(long cls);
    static PGraphicsObject PgobFromHidScr(long hid);
    static void MakeHwndActive(HWND hwnd);
    static void ActivateHwnd(HWND hwnd, bool fActive);
    static HWND HwndMdiActive(void);
    static PGraphicsObject PgobMdiActive(void);
    static PGraphicsObject PgobFromPtGlobal(long xp, long yp, PT *pptLocal = pvNil);
    static long GinDefault(void)
    {
        return _ginDefGob;
    }

    GraphicsObject(GraphicsObjectBlock *pgcb);
    GraphicsObject(long hid);
    virtual void Release(void);

    // hwnd stuff
    bool FAttachHwnd(HWND hwnd);
    bool FCreateAndAttachMdi(PSTN pstnTitle);
    HWND Hwnd(void)
    {
        return _hwnd;
    }
    HWND HwndContainer(void);
    virtual void GetMinMax(RC *prcMinMax);
    void SetHwndName(PSTN pstn);

    // unique gob run-time id.
    long Grid(void)
    {
        return _grid;
    }

    // tree management
    PGraphicsObject PgobPar(void)
    {
        return _pgobPar;
    }
    PGraphicsObject PgobFirstChild(void)
    {
        return _pgobChd;
    }
    PGraphicsObject PgobLastChild(void);
    PGraphicsObject PgobNextSib(void)
    {
        return _pgobSib;
    }
    PGraphicsObject PgobPrevSib(void);
    PGraphicsObject PgobFromCls(long cls);
    PGraphicsObject PgobChildFromCls(long cls);
    PGraphicsObject PgobParFromCls(long cls);
    PGraphicsObject PgobFromHid(long hid);
    PGraphicsObject PgobChildFromHid(long hid);
    PGraphicsObject PgobParFromHid(long hid);
    PGraphicsObject PgobFromGrid(long grid);
    void BringToFront(void);
    void SendBehind(PGraphicsObject pgobBefore);

    // rectangle management
    void SetPos(RC *prcAbs, RC *prcRel = pvNil);
    void GetPos(RC *prcAbs, RC *prcRel);
    void GetRc(RC *prc, long coo);
    void GetRcVis(RC *prc, long coo);
    void SetRcFromHwnd(void);
    virtual void Maximize(void);

    void MapPt(PT *ppt, long cooSrc, long cooDst);
    void MapRc(RC *prc, long cooSrc, long cooDst);

    // variables
    virtual PDynamicArray *Ppglrtvm(void);

    PGPT Pgpt(void)
    {
        return _pgpt;
    }
    void InvalRc(RC *prc, long gin = kginDefault);
    void ValidRc(RC *prc, long gin = kginDefault);
    bool FGetRcInval(RC *prc, long gin = kginDefault);
    void Scroll(RC *prc, long dxp, long dyp, long gin, RC *prcBad1 = pvNil, RC *prcBad2 = pvNil);

    virtual void Clean(void);
    virtual void DrawTree(PGPT pgpt, RC *prc, RC *prcUpdate, ulong grfgob);
    virtual void DrawTreeRgn(PGPT pgpt, RC *prc, REGN *pregn, ulong grfgob);
    virtual void Draw(PGNV pgnv, RC *prcClip);

    // mouse handling and hit testing
    void GetPtMouse(PT *ppt, bool *pfDown);
    virtual PGraphicsObject PgobFromPt(long xp, long yp, PT *pptLocal = pvNil);
    virtual bool FPtIn(long xp, long yp);
    virtual bool FPtInBounds(long xp, long yp);
    virtual void MouseDown(long xp, long yp, long cact, ulong grfcust);
    virtual long ZpDragRc(RC *prc, bool fVert, long zp, long zpMin, long zpLim, long zpMinActive, long zpLimActive);
    void SetCurs(PCURS pcurs);
    void SetCursCno(PRCA prca, ChunkNumber cno);

#ifdef MAC
    virtual void TrackGrow(PEVT pevt);
#endif // MAC

    // command functions
    virtual bool FCmdCloseWnd(PCMD pcmd);
    virtual bool FCmdTrackMouse(PCMD_MOUSE pcmd);
    bool FCmdTrackMouseCore(PCMD pcmd)
    {
        return FCmdTrackMouse((PCMD_MOUSE)pcmd);
    }
    virtual bool FCmdMouseMove(PCMD_MOUSE pcmd);
    bool FCmdMouseMoveCore(PCMD pcmd)
    {
        return FCmdMouseMove((PCMD_MOUSE)pcmd);
    }

    // key commands
    virtual bool FCmdKey(PCMD_KEY pcmd);
    bool FCmdKeyCore(PCMD pcmd)
    {
        return FCmdKey((PCMD_KEY)pcmd);
    }
    virtual bool FCmdBadKey(PCMD_BADKEY pcmd);
    bool FCmdBadKeyCore(PCMD pcmd)
    {
        return FCmdBadKey((PCMD_BADKEY)pcmd);
    }
    virtual bool FCmdSelIdle(PCMD pcmd);
    virtual bool FCmdActivateSel(PCMD pcmd);

    // tool tips
    virtual bool FEnsureToolTip(PGraphicsObject *ppgobCurTip, long xpMouse, long ypMouse);

    // gob state (for automated testing)
    virtual long LwState(void);

#ifdef DEBUG
    void MarkGobTree(void);
#endif // DEBUG
};

/****************************************
    Gob Tree Enumerator
****************************************/
enum
{
    // inputs
    fgteNil = 0x0000,
    fgteSkipToSib = 0x0001,   // legal to FNextGob
    fgteBackToFront = 0x0002, // legal to Init

    // outputs
    fgtePre = 0x0010,
    fgtePost = 0x0020,
    fgteRoot = 0x0040
};

#define GTE_PAR BASE
#define kclsGTE 'GTE'
class GTE : public GTE_PAR
{
    RTCLASS_DEC
    ASSERT

  private:
    // enumeration states
    enum
    {
        esStart,
        esGoDown,
        esGoRight,
        esDone
    };

    long _es;
    bool _fBackWards; // which way to walk sibling lists
    PGraphicsObject _pgobRoot;
    PGraphicsObject _pgobCur;

  public:
    GTE(void);
    void Init(PGraphicsObject pgob, ulong grfgte);
    bool FNextGob(PGraphicsObject *ppgob, ulong *pgrfgteOut, ulong grfgteIn);
};

#endif //! GOB_H
