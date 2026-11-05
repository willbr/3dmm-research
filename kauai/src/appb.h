/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    The base application class.  Most apps will need to subclass this.

***************************************************************************/
#ifndef APPB_H
#define APPB_H
#include "gob.h"
class DocumentBase;
typedef DocumentBase *PDocumentBase;
class SoundManager;
typedef SoundManager *PSoundManager;

/***************************************************************************
    Misc types
***************************************************************************/
#ifdef KAUAI_SDL3
#include "host_event.h"
typedef KauaiInput::Event EVT;
#elif defined(MAC)
typedef EventRecord EVT;
#else
// Native events remain opaque to clients of the application interface. The
// native backend supplies their definition; SDL uses the complete host event.
typedef CoreNative::MessageEvent EVT;
#endif
typedef EVT *PEVT;

/***************************************************************************
    The base application class.
***************************************************************************/
const long kcmhlAppb = klwMax; // appb goes at the end of the cmh list

enum
{
    fappNil = 0x0,
    fappOffscreen = 0x1,
    fappOnscreen = 0x2,
};

typedef class ApplicationBase *PApplicationBase;
#define ApplicationBase_PAR CommandHandler
#define kclsApplicationBase 'APPB'
class ApplicationBase : public ApplicationBase_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(ApplicationBase)

  protected:
    // marked region - for fast updating
    struct MKRGN
    {
        CoreNative::WindowHandle hwnd;
        PRegion pregn;
    };

    // map from a property id to its value
    struct PROP
    {
        long prid;
        long lw;
    };

    // modal context
    struct MODCX
    {
        long cactLongOp;
        PCommandExecutionManager pcex;
        PUniversalScalableApplicationClock pusac;
        ulong luScale;
    };

#ifdef DEBUG
    bool _fCheckForLostMem : 1; // whether to check for lost mem at idle
    bool _fInAssert : 1;        // whether we're in an assert
    bool _fRefresh : 1;         // whether to refresh the entire display
#endif                          // DEBUG
    bool _fQuit : 1;            // whether we're in the process of quitting
    bool _fOffscreen : 1;       // whether to do offscreen updates by default
    bool _fFullScreen : 1;      // when maximized, we hide the caption, etc
    bool _fToolTip : 1;         // whether we're in tool-tip mode
    bool _fForeground : 1;      // whether we're the foreground app
    bool _fEndModal : 1;        // set to end the topmost modal loop

    PDynamicArray _pglmkrgn;        // list of marked regions for fast updating
    long _onnDefFixed;    // default fixed pitch font
    long _onnDefVariable; // default variable pitched font
    PGraphicsPort _pgptOff;        // cached offscreen GraphicsPort for offscreen updates
    long _dxpOff;         // size of the offscreen GraphicsPort
    long _dypOff;

    long _xpMouse; // location of mouse on last reported mouse move
    long _ypMouse;
    PGraphicsObject _pgobMouse;     // gob mouse was last over
    ulong _grfcustMouse; // cursor state on last mouse move

    // for determining the multiplicity of a click
    long _tsMouse;   // time of last mouse click
    long _cactMouse; // multiplicity of last mouse click

    // for tool tips
    ulong _tsMouseEnter;     // when the mouse entered _pgobMouse
    ulong _dtsToolTip;       // time lag for tool tip
    PGraphicsObject _pgobToolTipTarget; // if there is a tool tip up, it's for this gob

    PCursor _pcurs;     // current cursor
    PCursor _pcursWait; // cursor to use for long operations
    long _cactLongOp; // long operation count
#ifdef KAUAI_SDL3
    long _cactSDLHide;
    long _cactTrueColorOffscreen;
    KauaiInput::Queue _hostEvents;
#endif
    ulong _grfcust;   // current cursor state

    long _gft;     // transition to apply during next fast update
    long _lwGft;   // parameter for transition
    ulong _dtsGft; // how much time to give the transition
    PDynamicArray _pglclr;   // palette to transition to
    AbstractColor _acr;      // intermediate color to transition to

    PDynamicArray _pglprop; // the properties

    PDynamicArray _pglmodcx;   // The modal context stack
    long _lwModal;   // Return value from modal loop
    long _cactModal; // how deep we are in application loops

    // initialization, running and clean up
    virtual bool _FInit(ulong grfapp, ulong grfgob, long ginDef);
#ifdef DEBUG
    virtual bool _FInitDebug(void);
#endif // DEBUG
    virtual bool _FInitOS(void);
    virtual bool _FInitMenu(void);
    virtual void _Loop(void);
    virtual void _CleanUp(void);
    virtual bool _FInitSound(long wav);

    // event fetching and dispatching
    virtual bool _FGetNextEvt(PEVT pevt);
    virtual void _DispatchEvt(PEVT pevt);
    virtual bool _FTranslateKeyEvt(EVT *pevt, PCMD_KEY pcmd);
#ifdef KAUAI_SDL3
    // Window commands remain available while a control tracks the pointer.
    virtual bool _FDispatchWindowShortcut(const EVT &event) { return false; }
    virtual bool _FDispatchShortcut(const EVT &event) { return false; }
    void _HostMouseDown(const EVT &event);
#endif

#ifdef MAC
    // event handlers
    virtual void _MouseDownEvt(EVT *pevt);
    virtual void _MouseUpEvt(EVT *pevt);
    virtual void _UpdateEvt(EVT *pevt);
    virtual void _ActivateEvt(EVT *pevt);
    virtual void _DiskEvt(EVT *pevt);
    virtual void _ActivateApp(EVT *pevt);
    virtual void _DeactivateApp(EVT *pevt);
    virtual void _MouseMovedEvt(EVT *pevt);
#endif

    // fast updating
    virtual void _FastUpdate(PGraphicsObject pgob, PRegion pregnClip, ulong grfapp = fappNil, PGraphicsPort pgpt = pvNil);
    virtual void _CopyPixels(PGraphicsEnvironment pgvnSrc, RC *prcSrc, PGraphicsEnvironment pgnvDst, RC *prcDst);
    void _MarkRegnRc(PRegion pregn, RC *prc, PGraphicsObject pgobCoo);
    void _UnmarkRegnRc(PRegion pregn, RC *prc, PGraphicsObject pgobCoo);

    // to borrow the common offscreen GraphicsPort
    virtual PGraphicsPort _PgptEnsure(RC *prc);

    // property list management
    bool _FFindProp(long prid, PROP *pprop, long *piprop = pvNil);
    bool _FSetProp(long prid, long lw);

    // tool tip support
    void _TakeDownToolTip(void);
    void _EnsureToolTip(void);

#ifdef KAUAI_SDL3
    bool _FInitSDLWindow(const char *title, int width, int height);
    void _CloseSDLWindow();
    bool _FBindSDLNativeWindow();
    void _ReleaseSDLNativeWindow();
#ifdef DEBUG
    LegacyLong _TmcSDLAssert(PString message, PString details, PString stack);
#endif
#endif
// Optional native callback interop. The native backend owns message decoding.
#ifdef WIN
    static CoreNative::MessageResult KAUAI_NATIVE_CALLBACK _LuWndProc(CoreNative::WindowHandle hwnd, std::uint32_t wm,
                                                                    CoreNative::MessageWord wParam, CoreNative::MessageLong lParam);
    static CoreNative::MessageResult KAUAI_NATIVE_CALLBACK _LuMdiWndProc(CoreNative::WindowHandle hwnd, std::uint32_t wm,
                                                                       CoreNative::MessageWord wParam, CoreNative::MessageLong lParam);

    // plwRet is the message-result slot returned to Win32 as LRESULT.
    // Was `long *` historically — correct on x86 (LRESULT == long) but
    // 4 bytes too narrow on Win64 (LRESULT == LONG_PTR == __int64).
    virtual bool _FFrameWndProc(CoreNative::WindowHandle hwnd, std::uint32_t wm, CoreNative::MessageWord wParam,
                                CoreNative::MessageLong lw, CoreNative::MessageResult *plwRet);
    virtual bool _FMdiWndProc(CoreNative::WindowHandle hwnd, std::uint32_t wm, CoreNative::MessageWord wParam,
                              CoreNative::MessageLong lw, CoreNative::MessageResult *plwRet);
    virtual bool _FCommonWndProc(CoreNative::WindowHandle hwnd, std::uint32_t wm, CoreNative::MessageWord wParam,
                                 CoreNative::MessageLong lw, CoreNative::MessageResult *plwRet);

    // remove ourself from the clipboard viewer chain
    void _ShutDownViewer(void);
#endif // WIN

    // Activation
    virtual void _Activate(bool fActive);

  public:
#ifdef KAUAI_SDL3
    // Pump one host event during an embedded automation/modal loop. Existing
    // controls and command handlers receive the same path as the main loop.
    bool PumpHostEvent();
#endif
    ApplicationBase(void);
    ~ApplicationBase(void);

#ifdef MAC
    // setting up the heap
    static void _SetupHeap(long cbExtraStack, long cactMoreMasters);
    virtual void SetupHeap(void);
#elif defined(WIN)
    static void CreateConsole();
#endif

    bool FQuitting(void)
    {
        return _fQuit;
    }
    bool FForeground(void)
    {
        return _fForeground;
    }

    // initialization, running and quitting
    virtual void Run(ulong grfapp, ulong grfgob, long ginDef);
    virtual void Quit(bool fForce);
    virtual void Abort(void);
    virtual void TopOfLoop(void);

    // look for the next key event in the system event queue
    virtual bool FGetNextKeyFromOsQueue(PCMD_KEY pcmd);

    // Look for mouse events and get the mouse location
    // GrfcustCur() is synchronized with this
    void TrackMouse(PGraphicsObject pgob, PT *ppt);

    // app name
    virtual void GetStnAppName(PString pstn);

    // command handler stuff
    virtual void BuryCmh(PCommandHandler pcmh);
    virtual PCommandHandler PcmhFromHid(long hid);

    // drawing
    virtual void UpdateHwnd(CoreNative::WindowHandle hwnd, RC *prc, ulong grfapp = fappNil);
    virtual void MarkRc(RC *prc, PGraphicsObject pgobCoo);
    virtual void MarkRegn(PRegion pregn, PGraphicsObject pgobCoo);
    virtual void UnmarkRc(RC *prc, PGraphicsObject pgobCoo);
    virtual void UnmarkRegn(PRegion pregn, PGraphicsObject pgobCoo);
    virtual bool FGetMarkedRc(CoreNative::WindowHandle hwnd, RC *prc);
    virtual void UpdateMarked(void);
    virtual void InvalMarked(CoreNative::WindowHandle hwnd);
    virtual void SetGft(long gft, long lwGft, ulong dts = kdtsSecond, PDynamicArray pglclr = pvNil, AbstractColor acr = kacrClear);

    // default fonts
    virtual long OnnDefVariable(void);
    virtual long OnnDefFixed(void);
    virtual long DypTextDef(void);

    // basic alert handling
    virtual tribool TGiveAlertSz(PZString psz, long bk, long cok);

    // common commands
    virtual bool FCmdQuit(PCommand pcmd);
    virtual bool FCmdShowClipboard(PCommand pcmd);
    virtual bool FEnableAppCmd(PCommand pcmd, ulong *pgrfeds);
    virtual bool FCmdIdle(PCommand pcmd);
    virtual bool FCmdChooseWnd(PCommand pcmd);
#ifdef MAC
    virtual bool FCmdOpenDA(PCommand pcmd);
#endif // MAC

#ifdef DEBUG
    virtual bool FAssertProcApp(PSZS pszsFile, long lwLine, PSZS pszsMsg, void *pv, long cb);
    virtual void WarnProcApp(PSZS pszsFile, long lwLine, PSZS pszsMsg);
#endif // DEBUG

    // cursor stuff
    virtual void SetCurs(PCursor pcurs, bool fLongOp = fFalse);
    virtual void SetCursCno(PResourceCache prca, ChunkNumber cno, bool fLongOp = fFalse);
    virtual void RefreshCurs(void);
    virtual ulong GrfcustCur(bool fAsynch = fFalse);
    virtual void ModifyGrfcust(ulong grfcustOr, ulong grfcustXor);
    virtual void HideCurs(void);
    virtual void ShowCurs(void);
    virtual void PositionCurs(long xpScreen, long ypScreen);
    // Editor tracking uses gob-local coordinates, independent of host scaling.
    void PositionCurs(PGraphicsObject gob, long x, long y);
    // Restrict placement to the containing window; nullptr releases it.
    void ConfineCurs(PGraphicsObject gob);
#ifdef KAUAI_SDL3
    void SetSlowKeyRepeat(bool slow);
    void RequestTrueColorOffscreen(bool acquire);
#endif
    virtual void BeginLongOp(void);
    virtual void EndLongOp(bool fAll = fFalse);

    // setting and fetching properties
    virtual bool FSetProp(long prid, long lw);
    virtual bool FGetProp(long prid, long *plw);

    // clipboard importing - normally only called by the clipboard object
    virtual bool FImportClip(long clfm, void *pv = pvNil, long cb = 0, PDocumentBase *ppdocb = pvNil, bool *pfDelay = pvNil);

    // reset tooltip tracking.
    virtual void ResetToolTip(void);

    // modal loop support
    virtual bool FPushModal(PCommandExecutionManager pcex = pvNil);
    virtual bool FModalLoop(long *plwRet);
    virtual void EndModal(long lwRet);
    virtual void PopModal(void);
    virtual bool FCmdEndModal(PCommand pcmd);
    long CactModal(void)
    {
        return _cactModal;
    }
    virtual void BadModalCmd(PCommand pcmd);

    // Query save changes for a document
    virtual tribool TQuerySaveDoc(PDocumentBase pdocb, bool fForce);

    // flush user generated events from the system event queue.
    virtual void FlushUserEvents(ulong grfevt = kgrfevtAll);

    // whether to allow a screen saver to come up
    virtual bool FAllowScreenSaver(void);
};

extern PApplicationBase vpappb;
/* vpcex now declared in cmd.h so kauai-core consumers (clok, msnd,
   ...) can see it without including appb.h. Definition still lives
   in kauai/src/appb.cpp. */
extern PSoundManager vpsndm;

// main entry point for the client app
void FrameMain(void);

// Append a category-tagged diagnostic message to %TEMP%\3dmmforever-crash.txt
// so user-visible error/assert dialogs can be inspected on disk later. The
// category is a short uppercase tag (e.g. "ABNORMAL_EXIT", "ASSERT",
// "GENERIC_ERROR", "CANT_FIND_FILE") used to grep entries by class. Best-
// effort: silently swallows I/O failure since callers are already in error
// paths and a logging failure shouldn't mask the original.
void AppendCrashLog(const char *pszCategory, const char *pszBody);

// alert button kinds
enum
{
    bkOk,
    bkOkCancel,
    bkYesNo,
    bkYesNoCancel,
};

// alert icon kinds
enum
{
    cokNil,
    cokInformation, // general info to/from the user
    cokQuestion,    // ask the user something
    cokExclamation, // warn the user and/or ask something
    cokStop,        // inform the user that we can't do that
};

#endif //! APPB_H
