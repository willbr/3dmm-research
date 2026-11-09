/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    utest.cpp: Socrates main app class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    The Application class handles initialization of the product, and global
    actions such as resolution-switching, switching between the building
    and the studio, and quitting.

    The KidWorld (App KidWorld) is the parent of the gob tree in the product.
    It also is used to display a splash screen, and to find AVIs on the CD.

***************************************************************************/
#include "dialog_resource.h"
#include "studio_startup.h"
#include "studio_content.h"
#ifdef KAUAI_SDL3
#include "studio_iso.h"
#include "studio_pack_runtime.h"
#include "studio_settings.h"
#include "studio_missing_content.h"
#include "sdl_dialog.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <SDL3/SDL_misc.h>
#endif
#include "launch_context.h"
#include "studio_product_names.h"
#include "platform_files.h"
#include "platform_users.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "settings_store.h"
#ifdef _WIN32
#include "settings_store_win.h"
#else
#include "settings_store_posix.h"
#endif
#ifdef KAUAI_SDL3
#include "sdl_host.h"
#include "studio_shortcuts.h"
#endif
#include "studio.h"
#ifdef KAUAI_SDL3
#include "studio_pack_options.h"
#include "studio_v3dmm.h"
#endif
#if defined(WIN) && !defined(KAUAI_SDL3)
#include <shellapi.h>
#endif
#include "studio_preferences.h"
#include "studio_resources.h"
#include "socres.h"
#ifdef WIN
#include "mminstal.h"
#endif
#include "mcpserv.h"

// Expand shared resource constants before PszLit's Unicode token concatenation.
#define STUDIO_NAME_LITERAL(value) PszLit(value)

ASSERTNAME

static bool BatchPlaybackTest()
{
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    const auto value = std::getenv("THREEDMM_BATCH_TEST");
    return value && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}
static void BatchPlaybackExit(int code)
{
    std::fflush(stderr);
    // The isolated batch process has a live MCP reader. std::exit would
    // destroy its joinable static thread and replace this error with an abort.
    std::_Exit(code);
}
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
static void BatchMissingTag(PTAG tag)
{
    std::fprintf(stderr, "BATCH_MISSING_ASSET sid=%ld ctg=%08lx cno=%08lx\n", static_cast<long>(tag->sid),
                 static_cast<unsigned long>(tag->ctg), static_cast<unsigned long>(tag->cno));
    BatchPlaybackExit(4);
}
#endif

// If the following value is defined, 3DMM displays the dlidDesktopResizing
// and dlidDesktopResized dialogs before and immediately after res-switching.
// The current thought is that these dialogs are unnecessary since we only
// res-switch to 640x480, which should always be safe.  We still display a
// dialog if the res-switch failed (dlidDesktopWontResize).
// #define RES_SWITCH_DIALOGS

// If the following value is defined, 3DMM does a run-time performance test
// of the graphics, fixed-point math, and copying speed at startup and sets
// _fSlowCPU to fTrue if it thinks 3DMM is running on a slow computer.
// The current feeling is that we don't have the resources to tweak the
// thresholds and verify that this gives us the result we want on all
// computers.
//#define PERF_TEST

// Duration to display homelogo
const ulong kdtsHomeLogo = 4 * kdtsSecond;

// Duration to display splash screen
const ulong kdtsSplashScreen = 4 * kdtsSecond;

// Duration before res-switch dialog cancels itself
const ulong kdtsMaxResSwitchDlg = 15 * kdtsSecond;

// 2MB cache per source for TagManager
const ulong kcbCacheTagm = 2048 * 1024;

#ifdef KAUAI_SDL3
static PZString kpszAppWndCls = PszLit("3DMOVIE-SDL3");
#else
static PZString kpszAppWndCls = PszLit("3DMOVIE");
#endif
const PZString kpszOpenFile = PszLit("3DMMOpen.tmp");

const long klwOpenDoc = 0x12123434; // arbitrary wParam for WM_USER

BEGIN_CMD_MAP(Application, ApplicationBase)
ON_CID_GEN(cidInfo, &Application::FCmdInfo, pvNil)
ON_CID_GEN(cidLoadStudio, &Application::FCmdLoadStudio, pvNil)
ON_CID_GEN(cidLoadBuilding, &Application::FCmdLoadBuilding, pvNil)
ON_CID_GEN(cidTheaterOpen, &Application::FCmdTheaterOpen, pvNil)
ON_CID_GEN(cidTheaterClose, &Application::FCmdTheaterClose, pvNil)
ON_CID_GEN(cidPortfolioOpen, &Application::FCmdPortfolioOpen, pvNil)
ON_CID_GEN(cidPortfolioClear, &Application::FCmdPortfolioClear, pvNil)
ON_CID_GEN(cidDisableAccel, &Application::FCmdDisableAccel, pvNil)
ON_CID_GEN(cidEnableAccel, &Application::FCmdEnableAccel, pvNil)
ON_CID_GEN(cidInvokeSplot, &Application::FCmdInvokeSplot, pvNil)
ON_CID_GEN(cidExitStudio, &Application::FCmdExitStudio, pvNil)
ON_CID_GEN(cidDeactivate, &Application::FCmdDeactivate, pvNil)
END_CMD_MAP_NIL()

Application vapp;

RTCLASS(Application)
RTCLASS(KidWorld)

/***************************************************************************
    Entry point for a Kauai-based app.
***************************************************************************/
void FrameMain(void)
{
    Debug(vcactAV = 2;) // Speeds up the debug build
        vapp.Run(fappOffscreen, fgobNil, kginMark);
}

/******************************************************************************
    Run
        Overridden ApplicationBase::Run method, so that we can attempt to recover
        gracefully from a crash.

    Arguments:
        ulong grfapp -- app flags
        ulong grfgob -- GraphicsObject flags
        long ginDef  -- default GraphicsObject invalidation

************************************************************ PETED ***********/
void Application::Run(ulong grfapp, ulong grfgob, long ginDef)
{
    /* Don't bother w/ AssertThis; we'd have to use AssertBaseThis, or
        possibly the parent's AssertValid, which gets done almost right
        away anyway */

    _CleanupTemp();

#ifdef WIN
    // SEH context captured by the filter; used by the handler body below for
    // a diagnostic dialog. Lifetime extends through __except by being a local
    // of Run(), not the filter expression.
    DWORD lastExceptionCode = 0;
    void *lastExceptionAddr = pvNil;

    // Stack trace: capture the call frames as soon as the exception fires,
    // before SEH unwinds. Done inside the filter expression so the trace
    // reflects the faulting context, not the handler.
    const int kcStackFrames = 16;
    void *rgpvStack[kcStackFrames];
    USHORT cStackFrames = 0;

    __try
    {
        Application_PAR::Run(grfapp, grfgob, ginDef);
    }
    __except ((lastExceptionCode = GetExceptionInformation()->ExceptionRecord->ExceptionCode,
               lastExceptionAddr = GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
               cStackFrames = CaptureStackBackTrace(0, kcStackFrames, rgpvStack, NULL),
               UnhandledExceptionFilter(GetExceptionInformation())))
    {
        // Stash a diagnostic message describing what actually faulted, instead
        // of the bare "ended unexpectedly" dialog. Use MessageBox directly:
        // the kauai dialog plumbing might itself be in a bad state inside an
        // SEH handler.
        char szMsg[3072]; // header + ~16 stack frames at ~80 chars each
        const char *pszName = "unhandled exception";
        switch (lastExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:        pszName = "ACCESS VIOLATION (read/write to bad memory)"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:   pszName = "datatype misalignment"; break;
        case EXCEPTION_BREAKPOINT:              pszName = "breakpoint (debug check)"; break;
        case EXCEPTION_SINGLE_STEP:             pszName = "single step"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   pszName = "array bounds exceeded"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:      pszName = "float divide by zero"; break;
        case EXCEPTION_FLT_INVALID_OPERATION:   pszName = "float invalid operation"; break;
        case EXCEPTION_FLT_OVERFLOW:            pszName = "float overflow"; break;
        case EXCEPTION_FLT_STACK_CHECK:         pszName = "float stack check"; break;
        case EXCEPTION_FLT_UNDERFLOW:           pszName = "float underflow"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:      pszName = "integer divide by zero"; break;
        case EXCEPTION_INT_OVERFLOW:            pszName = "integer overflow"; break;
        case EXCEPTION_PRIV_INSTRUCTION:        pszName = "privileged instruction"; break;
        case EXCEPTION_IN_PAGE_ERROR:           pszName = "in-page error"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:     pszName = "illegal instruction"; break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:pszName = "noncontinuable exception"; break;
        case EXCEPTION_STACK_OVERFLOW:          pszName = "STACK OVERFLOW"; break;
        case EXCEPTION_INVALID_DISPOSITION:     pszName = "invalid disposition"; break;
        }
        // Resolve the module containing the fault address so the user can
        // tell whether it's our code, a system DLL, or a third-party
        // component without needing a debugger.
        char szModule[MAX_PATH] = "<unknown>";
        size_t moduleOffset = 0;
        HMODULE hModFault = NULL;
        if (lastExceptionAddr != pvNil &&
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)lastExceptionAddr, &hModFault) &&
            hModFault != NULL)
        {
            char szPath[MAX_PATH];
            if (GetModuleFileNameA(hModFault, szPath, MAX_PATH) > 0)
            {
                // Strip directory; show the leaf name only.
                const char *pszLeaf = szPath;
                for (const char *p = szPath; *p; p++)
                    if (*p == '\\' || *p == '/')
                        pszLeaf = p + 1;
                lstrcpynA(szModule, (LPSTR)pszLeaf, sizeof(szModule));
            }
            moduleOffset = (size_t)((BYTE *)lastExceptionAddr - (BYTE *)hModFault);
        }

        wsprintfA(szMsg,
                  "3D Movie Maker hit an %s and is shutting down.\r\n\r\n"
                  "Exception code: 0x%08lX\r\n"
                  "Address:        0x%p\r\n"
                  "Module:         %s + 0x%IX\r\n",
                  pszName, lastExceptionCode, lastExceptionAddr, szModule, moduleOffset);

        // Append a module-resolved stack trace. Each frame is "module+0x<offset>";
        // combined with a .map file, that's enough to locate the calling function.
        char szStack[2048];
        szStack[0] = 0;
        lstrcatA(szStack, (LPSTR)"\r\nStack:\r\n");
        for (int i = 0; i < cStackFrames; i++)
        {
            char szFrame[MAX_PATH + 64];
            char szFrameModule[MAX_PATH] = "<unknown>";
            size_t frameOffset = 0;
            HMODULE hModFrame = NULL;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)rgpvStack[i], &hModFrame) &&
                hModFrame != NULL)
            {
                char szPath[MAX_PATH];
                if (GetModuleFileNameA(hModFrame, szPath, MAX_PATH) > 0)
                {
                    const char *pszLeaf = szPath;
                    for (const char *p = szPath; *p; p++)
                        if (*p == '\\' || *p == '/')
                            pszLeaf = p + 1;
                    lstrcpynA(szFrameModule, (LPSTR)pszLeaf, sizeof(szFrameModule));
                }
                frameOffset = (size_t)((BYTE *)rgpvStack[i] - (BYTE *)hModFrame);
            }
            wsprintfA(szFrame, "  %2d. %s+0x%IX  (0x%p)\r\n", i, szFrameModule, frameOffset, rgpvStack[i]);
            // Cap total stack-trace text so we don't blow the szStack buffer.
            if (lstrlenA(szStack) + lstrlenA(szFrame) + 1 < (int)sizeof(szStack))
                lstrcatA(szStack, szFrame);
        }
        lstrcatA(szMsg, szStack);

        // Mirror the diagnostic to %TEMP%\3dmmforever-crash.txt so it can be
        // read programmatically without OCR'ing the dialog.
        AppendCrashLog("ABNORMAL_EXIT", szMsg);

        char szTempDir[MAX_PATH];
        DWORD cchTemp = GetTempPathA(MAX_PATH, szTempDir);
        char szTrailer[MAX_PATH + 96];
        if (cchTemp > 0 && cchTemp < MAX_PATH)
            wsprintfA(szTrailer,
                      "\r\n\r\nWritten to: %s3dmmforever-crash.txt\r\n\r\n"
                      "If this is reproducible, please report it with the above (and any steps to reproduce).",
                      szTempDir);
        else
            lstrcpynA(szTrailer,
                      (LPSTR) "\r\n\r\nIf this is reproducible, please report it with the above (and any steps to reproduce).",
                      sizeof(szTrailer));
        lstrcatA(szMsg, szTrailer);

        MessageBoxA(NULL, szMsg, "3D Movie Maker - Unexpected Exit", MB_ICONERROR | MB_OK);
#ifdef DEBUG
        OutputDebugStringA(szMsg);
#endif

        _fQuit = fTrue;
        MovieView::RestoreKeyboardRepeat();
#ifdef WIN
        ClipCursor(NULL);
#endif // WIN
        _CleanUp();
    }
#else
    Application_PAR::Run(grfapp, grfgob, ginDef);
#endif
}

/***************************************************************************
    Get the name for the app.  Note that the string comes from a resource
    in the exe rather than a chunky file since the app name is needed
    before any chunky files are opened.  The app name should not change
    even for different products, i.e., Socrates and Playdo will have
    different _stnProduct, but the same _stnAppName.
***************************************************************************/
void Application::GetStnAppName(PString pstn)
{
    AssertBaseThis(0);
    AssertPo(pstn, 0);

    if (_stnAppName.Cch() == 0)
    {
#ifdef KAUAI_SDL3
        _stnAppName = STUDIO_NAME_LITERAL(STUDIO_APPLICATION_NAME);
#elif defined(WIN)
        ZString sz;

        if (0 != LoadString(vwig.hinst, stidAppName, sz, kcchMaxSz))
            _stnAppName = sz;
        else
            Warn("Couldn't read app name");
#else  // MAC
        RawRtn();
#endif // MAC
    }
    *pstn = _stnAppName;
}

#ifdef DEBUG
struct DBINFO
{
    LegacyLong cactAV;
};
static_assert(sizeof(DBINFO) == 4, "debug preference layout must remain 32-bit");
#endif // DEBUG

/***************************************************************************
    Init the Application
***************************************************************************/
bool Application::_FInit(ulong grfapp, ulong grfgob, long ginDef)
{
    AssertBaseThis(0);

    ulong tsHomeLogo;
    ulong tsSplashScreen;
    Filename fniUserDoc;
    long fFirstTimeUser;
    LegacyLong fSkipSplashScreen = fFalse;

    _LoadPreferences();
    fSkipSplashScreen = CurrentStudioPreferences().skipStartupScreens;

    // Only allow one copy of 3DMM to run at a time:
    if (_FAppAlreadyRunning())
    {
        _TryToActivateWindow();
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }
#ifdef DEBUG
    {
        DBINFO dbinfo;

        dbinfo.cactAV = vcactAV;
        if (FGetSetRegKey(PszLit("DebugSettings"), &dbinfo, size(DBINFO), fregSetDefault | fregBinary))
        {
            vcactAV = dbinfo.cactAV;
        }
    }
#endif // DEBUG

    if (!_FEnsureOS())
    {
        _FGenericError(PszLit("_FEnsureOS"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FEnsureAudio())
    {
        _FGenericError(PszLit("_FEnsureAudio"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FEnsureVideo())
    {
        _FGenericError(PszLit("_FEnsureVideo"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    _ParseCommandLine();

    // If _ParseCommandLine doesn't set _stnProduct, set to 3dMovieMaker
    if (!_FEnsureProductNames())
    {
        _FGenericError(PszLit("_FEnsureProductNames"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FFindMsKidsDir())
    {
        _FGenericError(PszLit("_FFindMsKidsDir"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    // Init product names for tagman & _fniProductDir & potentially _stnProduct
    if (!_FInitProductNames())
    {
        // _FInitProductNames prints its own errors
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FOpenResourceFile())
    {
        _FGenericError(PszLit("_FOpenResourceFile"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FReadStringTables())
    {
        _FGenericError(PszLit("_FReadStringTables"));
        _fDontReportInitFailure = fTrue;

        goto LFail;
    }

    if (!_FEnsureDisplayResolution())
    { // may call _FInitOS
        _FGenericError(PszLit("_FEnsureDisplayResolution"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FEnsureColorDepth())
    {
        _FGenericError(PszLit("_FEnsureColorDepth"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!Application_PAR::_FInit(grfapp, grfgob, ginDef))
    { // calls _FInitOS
        _FGenericError(PszLit("Application_PAR::_FInit"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    /* Ensure default font.  Do it here just so we get the error reported
        early. */
    OnnDefVariable();

    if (!_FInitKidworld())
    {
        _FGenericError(PszLit("_FInitKidworld"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    _ApplyPreferences();
    if (!_FDisplayHomeLogo(fSkipSplashScreen))
    {
        _FGenericError(PszLit("_FDisplayHomeLogo"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    tsHomeLogo = TsCurrent();

    if (!_FDetermineIfSlowCPU())
    {
        _FGenericError(PszLit("_FDetermineIfSlowCPU"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    {
        bool fWireframe = fFalse;
        bool fNoTexture = fFalse;
        bool fShowPoseReadout = fFalse;
        if (FGetSetRegKey(kszWireframeValue, &fWireframe, size(bool), fregBinary))
            World::SetRenderWireframe(fWireframe);
        if (FGetSetRegKey(kszNoTextureValue, &fNoTexture, size(bool), fregBinary))
            World::SetNoTexture(fNoTexture);
        if (FGetSetRegKey(kszShowPoseReadoutValue, &fShowPoseReadout, size(bool), fregBinary))
            MovieView::SetShowPoseReadout(fShowPoseReadout);
#ifdef KAUAI_SDL3
        bool fIntegerScaling = fFalse;
        if (FGetSetRegKey(kszIntegerScalingValue, &fIntegerScaling, size(bool), fregBinary))
            KauaiSDL::Main().SetIntegerScaling(fIntegerScaling);
        LegacyLong renderScale = 1;
        if (FGetSetRegKey(kszRenderScaleValue, &renderScale, size(renderScale), fregBinary))
            World::SetDefaultRenderScale(renderScale);
#endif
    }

    if (!_FInitTdt())
    {
        _FGenericError(PszLit("_FInitTdt"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!Material_MTRL::FSetShadeTable(_pcfl, kctgTmap, 0))
    {
        _FGenericError(PszLit("FSetShadeTable"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!fSkipSplashScreen)
    {
        while (TsCurrent() - tsHomeLogo < kdtsHomeLogo)
            ; // spin until home logo has been up long enough
    }

    if (!fSkipSplashScreen)
    {
        if (!_FShowSplashScreen())
        {
            _FGenericError(PszLit("_FShowSplashScreen"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }
    tsSplashScreen = TsCurrent();

    if (!CurrentStudioPreferences().muteStartupSound)
    {
        if (!_FPlaySplashSound())
        {
            _FGenericError(PszLit("_FPlaySplashSound"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    }

    if (!_FGetUserName())
    {
        _FGenericError(PszLit("_FGetUserName"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FGetUserDirectories())
    {
        _FGenericError(PszLit("_FGetUserDirectories"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FReadUserData())
    {
        _FGenericError(PszLit("_FReadUserData"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!_FInitCrm())
    {
        _FGenericError(PszLit("_FInitCrm"));
        _fDontReportInitFailure = fTrue;
        goto LFail;
    }

    if (!fSkipSplashScreen)
    {
        while (TsCurrent() - tsSplashScreen < kdtsSplashScreen)
            ;                   // spin until splash screen has been up long enough
        Pkwa()->SetMbmp(pvNil); // bring down splash screen
    }

    // If the user specified a doc on the command line, go straight
    // to the studio.  Otherwise, start the building.
    GetPortfolioDoc(&fniUserDoc);

    // if (fniUserDoc.Ftg() == ftgNil)
    // {
    //     // Startup place depends on whether this is the user's first time in.
    //     if (!FGetProp(kpridFirstTimeUser, &fFirstTimeUser))
    //     {
    //         _FGenericError(PszLit("FGetProp: kpridFirstTimeUser"));
    //         _fDontReportInitFailure = fTrue;
    //         goto LFail;
    //     }

    //     if (!FSetProp(kpridBuildingGob, (fFirstTimeUser ? kgobCloset : kgobLogin)))
    //     {
    //         _FGenericError(PszLit("FSetProp: kpridBuildingGob"));
    //         _fDontReportInitFailure = fTrue;
    //         goto LFail;
    //     }
    //     if (!_FInitBuilding())
    //     {
    //         _FGenericError(PszLit("_FInitBuilding"));
    //         _fDontReportInitFailure = fTrue;
    //         goto LFail;
    //     }
    // }
    // else
    // {
        if (!FSetProp(kpridBuildingGob, kgobStudio1))
        {
            _FGenericError(PszLit("FSetProp: kpridBuildingGob"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
        if (!_FInitStudio(&fniUserDoc, fFalse))
        {
            _FGenericError(PszLit("_FInitStudio"));
            _fDontReportInitFailure = fTrue;
            goto LFail;
        }
    // }

    EnsureInteractive();
#ifdef KAUAI_SDL3
    if (World::DefaultRenderScale() > 1)
        KauaiSDL::Main().FitWindowScale(static_cast<int>(World::DefaultRenderScale()));
#endif

#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    // Bring up the embedded MCP server if `--mcp-server` was passed. No-op
    // otherwise, so this is safe to call in builds with test tools enabled.
    mcp::FInit();
#endif

    return fTrue;
LFail:
    _fQuit = fTrue;

    if (_fSwitchedResolution)
    {
        if (_FSwitch640480(fFalse)) // try to restore desktop
            _fSwitchedResolution = fFalse;
    }

    // _fDontReportInitFailure will be true if one of the above functions
    // has already posted an alert explaining why the app is shutting down.
    // If it's false, we put up a OOM or generic error dialog.
    if (!_fDontReportInitFailure)
    {
        PDialog pdlg;

        if (vpers->FIn(ercOomHq) || vpers->FIn(ercOomPv) || vpers->FIn(ercOomNew))
            pdlg = Dialog::PdlgNew(dlidInitFailedOOM, pvNil, pvNil);
        else
            pdlg = Dialog::PdlgNew(dlidInitFailed, pvNil, pvNil);
        if (pvNil != pdlg)
            pdlg->IditDo();
        ReleasePpo(&pdlg);
    }
    return fFalse;
}

/******************************************************************************
    _CleanupTemp
        Removes any temp files leftover from a previous execution.

************************************************************ PETED ***********/
void Application::_CleanupTemp(void)
{
    Filename fni;

    /* Attempt to cleanup any leftovers from a previous bad exit */
    vftgTemp = kftgSocTemp;
    if (fni.FGetTemp())
    {
        FileType ftgTmp = vftgTemp;
        FileNameEnumerator fne;

        if (fne.FInit(&fni, &ftgTmp, 1))
        {
            bool fFlushErs = fFalse;

            while (fne.FNextFni(&fni))
            {
                if (!fni.FDelete())
                    fFlushErs = fTrue;
            }

            if (fFlushErs)
                vpers->Flush(ercFniDelete);
        }
    }
}

/***************************************************************************
    Determine if another copy of 3DMM is already running by trying to
    create a named semaphore.
***************************************************************************/
bool Application::_FAppAlreadyRunning(void)
{
    AssertBaseThis(0);

#ifdef WIN
    HANDLE hsem;
    String stn;

    GetStnAppName(&stn);

#ifdef KAUAI_SDL3
    stn.FAppendSz(PszLit("-SDL3"));
#endif
    hsem = CreateSemaphore(NULL, 0, 1, stn.Psz());
    if (hsem != NULL && GetLastError() == ERROR_ALREADY_EXISTS && !CurrentStudioPreferences().allowMultipleInstances)
    {
        CloseHandle(hsem);
        return fTrue;
    }
    // Retain an allowed instance's handle until process exit, so a later
    // launch with multiple instances disabled can still detect this copy.
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
    return fFalse;
}

/***************************************************************************
    Try to find another instance of 3DMM and bring its window to front
    Also, if a document was on the command line, notify the other instance
***************************************************************************/
void Application::_TryToActivateWindow(void)
{
    AssertBaseThis(0);

#ifdef WIN
    HWND hwnd;
    String stn;
    Filename fniUserDoc;

    GetStnAppName(&stn);
#ifdef KAUAI_SDL3
    FGetStnApp(idsWindowTitle, &stn);
    stn.FAppendSz(PszLit(" [SDL3]"));
    hwnd = FindWindow(PszLit("SDL_app"), stn.Psz());
#else
    hwnd = FindWindow(kpszAppWndCls, stn.Psz());
#endif
    if (NULL != hwnd)
    {
        SetForegroundWindow(hwnd);
        ShowWindow(hwnd, SW_RESTORE); // in case it was minimized
        _ParseCommandLine();
        GetPortfolioDoc(&fniUserDoc);
        if (fniUserDoc.Ftg() != ftgNil)
            _FSendOpenDocCmd(hwnd, &fniUserDoc); // ignore failure
    }
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
}

/***************************************************************************
    If we're not running on at least Win95 or NT 3.51, complain and return
    fFalse.
***************************************************************************/
bool Application::_FEnsureOS(void)
{
    AssertBaseThis(0);
#ifdef KAUAI_SDL3
    // SDL's host initialization checks platform support; the historical
    // Windows 95/NT version dialog is not a portable startup requirement.
    return fTrue;
#endif
#ifdef WIN
    DWORD dwVersion;
    byte bVersionMajor;
    byte bVersionMinor;
    PDialog pdlg;

    dwVersion = GetVersion();
    bVersionMinor = (byte)((dwVersion & 0x0000ff00) >> 8);
    bVersionMajor = (byte)(dwVersion & 0x000000ff);

    if (bVersionMajor >= 4 || (bVersionMajor == 3 && bVersionMinor >= 51))
        return fTrue;

    // Put up an alert: OS too old
    pdlg = Dialog::PdlgNew(dlidBadOS, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;
    pdlg->IditDo();
    ReleasePpo(&pdlg);
    _fDontReportInitFailure = fTrue;
#endif // WIN
#ifdef MAC
    RawRtn();
#endif             // MAC
    return fFalse; // Bad OS
}

/***************************************************************************
    Notifies the user if there is no suitable wave-out and/or midi-out
    hardware.
***************************************************************************/
bool Application::_FEnsureAudio(void)
{
    AssertBaseThis(0);

#if defined(WIN) && !defined(KAUAI_SDL3)
#ifndef KAUAI_SDL3
    long cwod; // count of wave-out devices
#endif
    long cmod; // count of midi-out devices
    LegacyLong fShowMessage;
    PDialog pdlg;

#ifndef KAUAI_SDL3
    cwod = waveOutGetNumDevs();
    if (cwod <= 0)
    {
        fShowMessage = fTrue;
        if (!FGetSetRegKey(kszWaveOutMsgValue, &fShowMessage, size(fShowMessage), fregSetDefault | fregMachine))
        {
            Warn("Registry query failed");
        }
        if (fShowMessage)
        {
            // Put up an alert: no waveout
            pdlg = Dialog::PdlgNew(dlidNoWaveOut, pvNil, pvNil);
            if (pvNil == pdlg)
                return fFalse;
            pdlg->IditDo();
            fShowMessage = !pdlg->FGetCheck(1); // 1 is ID of checkbox
            ReleasePpo(&pdlg);
            if (!fShowMessage)
            {
                // ignore failure
                FGetSetRegKey(kszWaveOutMsgValue, &fShowMessage, size(fShowMessage), fregSetKey | fregMachine);
            }
        }
    }
    if (HWD_SUCCESS == wHaveWaveDevice(WAVE_FORMAT_2M08)) // 22kHz, Mono, 8bit is our minimum
    {
        if (wHaveACM())
        {
            // audio compression manager (sound mapper) not installed
            wInstallComp(IC_ACM);
            _fDontReportInitFailure = fTrue;
            return fFalse;
        }

        if (wHaveACMCodec(WAVE_FORMAT_ADPCM))
        {
            // audio codecs not installed
            wInstallComp(IC_ACM_ADPCM);
            _fDontReportInitFailure = fTrue;
            return fFalse;
        }
    } // have wave device
#endif // !KAUAI_SDL3: SDL owns WAVE devices and codecs.

    cmod = midiOutGetNumDevs();
    if (cmod <= 0)
    {
        fShowMessage = fTrue;
        if (!FGetSetRegKey(kszMidiOutMsgValue, &fShowMessage, size(fShowMessage), fregSetDefault | fregMachine))
        {
            Warn("Registry query failed");
        }
        if (fShowMessage)
        {
            // Put up an alert: no midiout
            pdlg = Dialog::PdlgNew(dlidNoMidiOut, pvNil, pvNil);
            if (pvNil == pdlg)
                return fFalse;
            pdlg->IditDo();
            fShowMessage = !pdlg->FGetCheck(1); // 1 is ID of checkbox
            ReleasePpo(&pdlg);
            if (!fShowMessage)
            {
                // ignore failure
                FGetSetRegKey(kszMidiOutMsgValue, &fShowMessage, size(fShowMessage), fregSetKey | fregMachine);
            }
        }
    }
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
    return fTrue;
}

/***************************************************************************
    Notifies the user if there is no suitable video playback devices
***************************************************************************/
bool Application::_FEnsureVideo(void)
{
#if defined(WIN) && !defined(KAUAI_SDL3)
    if (wHaveMCI("AVIVIDEO"))
    {
        // MCI for video is not installed
        wInstallComp(IC_MCI_VFW);
        _fDontReportInitFailure = fTrue;
        return fFalse;
    }

    if (HIC_SUCCESS != wHaveICMCodec(MS_VIDEO1))
    {
        //  video 1 codec not installed
        wInstallComp(IC_ICM_VIDEO1);
        _fDontReportInitFailure = fTrue;
        return fFalse;
    }

#endif // SDL uses its built-in AVI decoders; no MCI/ICM installation is needed.

    return fTrue;
}

/***************************************************************************
    Returns fTrue if color depth is >= 8 bits per pixel.  User is alerted
    if depth is less than 8, and function fails.  User is warned if depth
    is greater than 8, unless he has clicked the "don't show this messsage
    again" in the dialog before.
***************************************************************************/
bool Application::_FEnsureColorDepth(void)
{
    AssertBaseThis(0);

#if defined(WIN) && !defined(KAUAI_SDL3)
    HDC hdc;
    long cbitPixel;
    PDialog pdlg;
    LegacyLong fShowMessage;
    bool fDontShowAgain = fFalse;

    hdc = GetDC(NULL);
    if (NULL == hdc)
        return fFalse;
    cbitPixel = GetDeviceCaps(hdc, BITSPIXEL);
    ReleaseDC(NULL, hdc);

    if (cbitPixel < 8)
    {
        // Put up an alert: Not enough colors
        pdlg = Dialog::PdlgNew(dlidNotEnoughColors, pvNil, pvNil);
        if (pvNil == pdlg)
            return fFalse;
        pdlg->IditDo();
        ReleasePpo(&pdlg);
        _fDontReportInitFailure = fTrue;
        return fFalse;
    }
    /* FOONE: It's the future, we don't need to worry about running in true-color mode.
    if (cbitPixel > 8)
        {
        // warn user
        fShowMessage = fTrue;
        if (!FGetSetRegKey(kszGreaterThan8bppMsgValue, &fShowMessage,
                size(fShowMessage), fregSetDefault))
            {
            Warn("Registry query failed");
            }
        if (fShowMessage)
            {
            pdlg = Dialog::PdlgNew(dlidTooManyColors, pvNil, pvNil);
            if (pvNil != pdlg)
                {
                pdlg->IditDo();
                fDontShowAgain = pdlg->FGetCheck(1); // 1 is ID of checkbox
                }
            ReleasePpo(&pdlg);
            if (fDontShowAgain)
                {
                fShowMessage = fFalse;
                FGetSetRegKey(kszGreaterThan8bppMsgValue, &fShowMessage,
                    size(fShowMessage), fregSetKey); // ignore failure
                }
            }
        }
    */
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
    return fTrue;
}

/***************************************************************************
    Dialog proc for Resolution Switch dialog.  Return fTrue (bring down
    the dialog) if kdtsMaxResSwitchDlg has passed since the dialog was
    created.
***************************************************************************/
bool _FDlgResSwitch(PDialog pdlg, long *pidit, void *pv)
{
    AssertPo(pdlg, 0);
    AssertVarMem(pidit);
    AssertPvCb(pv, size(long));

    long tsResize = *(long *)pv;
    long tsCur;

    if (*pidit != ivNil)
    {
        return fTrue; // Cancel or OK pressed
    }
    else
    {
        tsCur = TsCurrent();
        if (tsCur - tsResize >= kdtsMaxResSwitchDlg)
            return fTrue;
    }

    return fFalse;
}

/***************************************************************************
    Ensure that the screen is at the user's preferred resolution for 3DMM.
    If user has no registry preference, we offer to switch, try to switch,
    and save user's	preference.  Registry failures are non-fatal, but
    failing to create the main window causes this function to fail.

    When this function returns, _fRunInWindow is set correctly and the
    main app window *might* be created.
***************************************************************************/
bool Application::_FEnsureDisplayResolution(void)
{
    AssertBaseThis(0);
#ifdef KAUAI_SDL3
    // The SDL host scales Studio's original 640x480 canvas. It does not need
    // to change the desktop display mode or overwrite the native preference.
    _fRunInWindow = fTrue;
    return fTrue;
#else

    PDialog pdlg;
    long idit;
    bool fSwitchRes;
    bool fNoValue;
    long tsResize;

    if (_FDisplayIs640480())
    {
        // System is already 640x480, so ignore registry and run fullscreen
        _fRunInWindow = fFalse;
        return fTrue;
    }

    if (!_FDisplaySwitchSupported())
    {
        // System can't switch res, so ignore registry and run in a window
        _fRunInWindow = fTrue;
        return fTrue;
    }

    // See if there's a res switch preference in the registry
    if (!FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, size(fSwitchRes), fregBinary, &fNoValue))
    {
        // Registry error...just run in a window
        _fRunInWindow = fTrue;
        return fTrue;
    }

    if (!fNoValue)
    {
        // User has a preference
        if (!fSwitchRes)
        {
            _fRunInWindow = fTrue;
            return fTrue;
        }
        else // try to switch res
        {
            _fRunInWindow = fFalse;
            if (!_FInitOS())
            {
                // we're screwed
                return fFalse;
            }
            if (!_FSwitch640480(fTrue))
            {
                _fRunInWindow = fTrue;
                _RebuildMainWindow();
                goto LSwitchFailed;
            }
            return fTrue;
        }
    }

    // User doesn't have a preference yet.  Do the interactive thing.
#ifdef RES_SWITCH_DIALOGS
    pdlg = Dialog::PdlgNew(dlidDesktopResizing, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;
    idit = pdlg->IditDo();
    ReleasePpo(&pdlg);
#else              //! RES_SWITCH_DIALOGS
    idit = 2; // OK
#endif             //! RES_SWITCH_DIALOGS
    if (idit == 1) // cancel
    {
        _fRunInWindow = fTrue;
        // try to set pref to fFalse
        fSwitchRes = fFalse;
        goto LWriteReg;
    }
    _fRunInWindow = fFalse;
    if (!_FInitOS())
        return fFalse;
    if (!_FSwitch640480(fTrue))
    {
        _fRunInWindow = fTrue;
        _RebuildMainWindow();
        goto LSwitchFailed;
    }

    tsResize = TsCurrent();
#ifdef RES_SWITCH_DIALOGS
    pdlg = Dialog::PdlgNew(dlidDesktopResized, _FDlgResSwitch, &tsResize);
    idit = ivNil; // if dialog fails to come up, treat like a cancel
    if (pvNil != pdlg)
        idit = pdlg->IditDo();
    ReleasePpo(&pdlg);
#else                               //! RES_SWITCH_DIALOGS
    idit = 2; // OK
#endif                              //! RES_SWITCH_DIALOGS
    if (idit == 1 || idit == ivNil) // cancel or timeout or dialog failure
    {
        _fSwitchedResolution = fFalse;
        _fRunInWindow = fTrue;
        _RebuildMainWindow();
        if (!_FSwitch640480(fFalse)) // restore desktop resolution
            return fFalse;
        goto LSwitchFailed;
    }
    // try to set pref to fTrue
    fSwitchRes = fTrue;
    goto LWriteReg;

LSwitchFailed:
    pdlg = Dialog::PdlgNew(dlidDesktopWontResize, pvNil, pvNil);
    if (pvNil != pdlg)
        pdlg->IditDo();
    ReleasePpo(&pdlg);
    // try to set pref to fFalse
    fSwitchRes = fFalse;
LWriteReg:
    FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, size(fSwitchRes), fregSetKey | fregBinary);
    return fTrue;
#endif
}

/***************************************************************************
    Return whether display is currently 640x480
***************************************************************************/
bool Application::_FDisplayIs640480(void)
{
#ifdef WIN
    return (GetSystemMetrics(SM_CXSCREEN) == 640 && GetSystemMetrics(SM_CYSCREEN) == 480);
#elif defined(KAUAI_SDL3)
    // A fixed SDL canvas does not impose the native desktop-resolution restriction.
    return fFalse;
#endif // WIN
#ifdef MAC
    RawRtn();
    return fFalse;
#endif // MAC
}

/***************************************************************************
    Do OS specific initialization.
***************************************************************************/
bool Application::_FInitOS(void)
{
    AssertBaseThis(0);

#ifdef KAUAI_SDL3
    if (_fMainWindowCreated)
        return fTrue;
    String stnWindowTitle;
    if (!FGetStnApp(idsWindowTitle, &stnWindowTitle))
        return fFalse;
    stnWindowTitle.FAppendSz(PszLit(" [SDL3]"));
    char title[kcchMaxStz];
    stnWindowTitle.GetSzs(title);
    if (!_FInitSDLWindow(title, 640, 480))
    {
        fprintf(stderr, "Cannot initialize the Studio SDL3 host: %s\n", KauaiSDL::Main().Error());
        return fFalse;
    }
#ifdef WIN
    SendMessage(vwig.hwndApp, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(vwig.hinst, MAKEINTRESOURCE(IDI_APP)));
#endif
    _fMainWindowCreated = fTrue;
    return fTrue;
#elif defined(WIN)
    long dxpWindow;
    long dypWindow;
    long xpWindow;
    long ypWindow;
    DWORD dwStyle = 0;
    String stnWindowTitle;

    if (_fMainWindowCreated) // If someone else called _FInitOS already,
        return fTrue;        // we can leave

    if (!FGetStnApp(idsWindowTitle, &stnWindowTitle))
        return fFalse;


    // register the window classes
    if (vwig.hinstPrev == hNil)
    {
        WNDCLASS wcs;

        wcs.style = CS_BYTEALIGNCLIENT | CS_OWNDC;
        wcs.lpfnWndProc = _LuWndProc;
        wcs.cbClsExtra = 0;
        wcs.cbWndExtra = 0;
        wcs.hInstance = vwig.hinst;
        wcs.hIcon = LoadIcon(vwig.hinst, MAKEINTRESOURCE(IDI_APP));
        wcs.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcs.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wcs.lpszMenuName = 0;
        wcs.lpszClassName = kpszAppWndCls;
        if (!RegisterClass(&wcs))
            return fFalse;

        wcs.lpfnWndProc = _LuMdiWndProc;
        wcs.lpszClassName = PszLit("MDI");
        if (!RegisterClass(&wcs))
            return fFalse;
    }

    _GetWindowProps(&xpWindow, &ypWindow, &dxpWindow, &dypWindow, &dwStyle);

    if ((vwig.hwndApp = CreateWindow(kpszAppWndCls, stnWindowTitle.Psz(), dwStyle, xpWindow, ypWindow, dxpWindow,
                                     dypWindow, hNil, hNil, vwig.hinst, pvNil)) == hNil)
    {
        return fFalse;
    }

    if (hNil == (vwig.hdcApp = GetDC(vwig.hwndApp)))
        return fFalse;

    // set a timer, so we can idle regularly.
    if (SetTimer(vwig.hwndApp, 0, 1, pvNil) == 0)
        return fFalse;

    _haccel = LoadAccelerators(vwig.hinst, MIR(acidMain));
    _haccelGlobal = LoadAccelerators(vwig.hinst, MIR(acidGlobal));
    vwig.haccel = _haccel;

    ShowWindow(vwig.hwndApp, vwig.wShow);
    _fMainWindowCreated = fTrue;
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
    return fTrue;
}

/***************************************************************************
    Determine window size, position, and window style.  Note that you
    should pass in the current dwStyle if the window already exists.  If
    the window does not already exist, pass in 0.
***************************************************************************/
#ifdef WIN
void Application::_GetWindowProps(long *pxp, long *pyp, long *pdxp, long *pdyp, DWORD *pdwStyle)
{
    AssertBaseThis(0);
    AssertVarMem(pxp);
    AssertVarMem(pyp);
    AssertVarMem(pdxp);
    AssertVarMem(pdyp);
    AssertVarMem(pdwStyle);

    if (!_fRunInWindow)
    {
        *pdwStyle |= (WS_POPUP | WS_CLIPCHILDREN);
        *pdwStyle &= ~(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
        *pxp = 0;
        *pyp = 0;
        *pdxp = GetSystemMetrics(SM_CXSCREEN);
        *pdyp = GetSystemMetrics(SM_CYSCREEN);
    }
    else
    {
        SystemRectangle rcs;
        *pdwStyle |= (WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
        *pdwStyle &= ~WS_POPUP;
        rcs.left = 0;
        rcs.top = 0;
        rcs.right = 640;
        rcs.bottom = 480;
        AdjustWindowRect(&rcs, *pdwStyle, fFalse);
        *pdxp = rcs.right - rcs.left;
        *pdyp = rcs.bottom - rcs.top;
        // Center window on screen
        // REVIEW *****: how do you adjust window appropriately for taskbar?
        *pxp = LwMax((GetSystemMetrics(SM_CXSCREEN) - *pdxp) / 2, 0);
        *pyp = LwMax((GetSystemMetrics(SM_CYSCREEN) - *pdyp) / 2, 0);
    }
}

#endif

/***************************************************************************
    Change main window properties based on whether we're running in a
    window or fullscreen.
***************************************************************************/
void Application::_RebuildMainWindow(void)
{
    AssertBaseThis(0);
    Assert(_fMainWindowCreated, 0);

#ifdef KAUAI_SDL3
    if (!KauaiSDL::Main().SetFullscreen(!_fRunInWindow))
        fprintf(stderr, "Cannot change Studio SDL3 window mode: %s\n", KauaiSDL::Main().Error());
    return;
#endif

#ifdef WIN
    long dxpWindow;
    long dypWindow;
    long xpWindow;
    long ypWindow;
    DWORD dwStyle;

    dwStyle = GetWindowLong(vwig.hwndApp, GWL_STYLE);
    _GetWindowProps(&xpWindow, &ypWindow, &dxpWindow, &dypWindow, &dwStyle);
    SetWindowLong(vwig.hwndApp, GWL_STYLE, dwStyle);
    SetWindowPos(vwig.hwndApp, HWND_TOP, xpWindow, ypWindow, dxpWindow, dypWindow, 0);
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
}

/***************************************************************************
    Open "3D Movie Maker.chk" or "3DMovie.chk"
***************************************************************************/
bool Application::_FOpenResourceFile(void)
{
    AssertBaseThis(0);
    AssertPo(&_fniProductDir, ffniDir);

    Filename fni;
    String stn;

    fni = _fniProductDir;
    if (!fni.FSetLeaf(&_stnProductLong, kftgChunky) || !fni.FResolveCase())
    {
        fni = _fniProductDir;
        if (!fni.FSetLeaf(&_stnProductShort, kftgChunky) || !fni.FResolveCase())
        {
            fni.GetStnPath(&stn);
            _FCantFindFileDialog(&stn); // ignore failure
            return fFalse;
        }
    }
    _pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
    if (pvNil == _pcfl)
        return fFalse;
    return fTrue;
}

/***************************************************************************
    Report that 3DMM can't find a file named pstnFile
***************************************************************************/
bool Application::_FCantFindFileDialog(PString pstnFile)
{
    AssertBaseThis(0);
    AssertPo(pstnFile, 0);
    if (BatchPlaybackTest())
    {
        char path[kcchMaxStz];
        pstnFile->GetSzs(path);
        std::fprintf(stderr, "BATCH_MISSING_FILE %s\n", path);
        BatchPlaybackExit(4);
    }

    PDialog pdlg;

    // Mirror to crash log so the dialog text is recoverable from disk.
    {
        char szPath[kcchMaxStz];
        pstnFile->GetSzs(szPath);
        AppendCrashLog("CANT_FIND_FILE", szPath);
    }

    pdlg = Dialog::PdlgNew(dlidCantFindFile, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;

    if (!pdlg->FPutStn(1, pstnFile))
    {
        ReleasePpo(&pdlg);
        return fFalse;
    }
    pdlg->IditDo();
    ReleasePpo(&pdlg);
    _fDontReportInitFailure = fTrue;
    return fTrue;
}

/***************************************************************************
    Report that 3DMM ran into a generic error
***************************************************************************/
bool Application::_FGenericError(Filename *path)
{
    String stn;
    path->GetStnPath(&stn);
    return _FGenericError(&stn);
}

/***************************************************************************
    Report that 3DMM ran into a generic error
***************************************************************************/
bool Application::_FGenericError(PSTZ message)
{
    String stn(message);
    return _FGenericError(&stn);
}
/***************************************************************************
    Report that 3DMM ran into a generic error
***************************************************************************/
bool Application::_FGenericError(PString message)
{
    AssertBaseThis(0);
    if (BatchPlaybackTest())
    {
        char detail[kcchMaxStz];
        message->GetSzs(detail);
        std::fprintf(stderr, "BATCH_ERROR %s\n", detail);
        BatchPlaybackExit(3);
    }

    PDialog pdlg;

    // Mirror to crash log so the dialog text is recoverable from disk.
    {
        char szBody[kcchMaxStz];
        message->GetSzs(szBody);
        AppendCrashLog("GENERIC_ERROR", szBody);
    }

    pdlg = Dialog::PdlgNew(dlidGenericErrorBox, pvNil, pvNil);
    if (pvNil == pdlg)
        return fFalse;

    if (!pdlg->FPutStn(1, message))
    {
        ReleasePpo(&pdlg);
        return fFalse;
    }
    pdlg->IditDo();
    ReleasePpo(&pdlg);
    _fDontReportInitFailure = fTrue;
    return fTrue;
}

/******************************************************************************
    Finds _fniUsersDir, _fniMelanieDir, and _fniUserDir.  _fniUserDir is
    set from the registry.  This function also determines if this is a first
    time user.
************************************************************ PETED ***********/
bool Application::_FGetUserDirectories(void)
{
    AssertBaseThis(0);
    AssertPo(&_fniMsKidsDir, ffniDir);
    Assert(_stnUser.Cch() > 0, "need valid stnUser!");

    ZString szDir;
    String stn;
    String stnT;
    String stnUsers;
    bool fFirstTimeUser;

    // Diagnostic helper: log which substep failed before returning. Each
    // entry lands in %TEMP%\3dmmforever-crash.txt as a STEP_FAIL category
    // so the failure path is recoverable from disk after the bare
    // _FGetUserDirectories generic-error dialog.
#define LOG_STEP_FAIL(msg) AppendCrashLog("STEP_FAIL", "_FGetUserDirectories: " msg)

    // First, find the Users directory
    _fniUsersDir = _fniMsKidsDir;
    if (!FGetStnApp(idsUsersDir, &stnUsers))
    {
        LOG_STEP_FAIL("FGetStnApp(idsUsersDir) failed");
        return fFalse;
    }
    if (!_fniUsersDir.FDownDir(&stnUsers, ffniMoveToDir))
    {
        _fniUsersDir.GetStnPath(&stn);
        if (!stn.FAppendStn(&stnUsers))
        {
            LOG_STEP_FAIL("FDownDir+FAppendStn for Users failed");
            return fFalse;
        }
        LOG_STEP_FAIL("FDownDir to Users failed");
        _FCantFindFileDialog(&stn); // ignore failure
        return fFalse;
    }
    AssertPo(&_fniUsersDir, ffniDir);

    // Find Melanie's dir
    _fniMelanieDir = _fniUsersDir;
    if (!FGetStnApp(idsMelanie, &stn))
    {
        LOG_STEP_FAIL("FGetStnApp(idsMelanie) failed");
        return fFalse;
    }
    if (!_fniMelanieDir.FDownDir(&stn, ffniMoveToDir))
    {
        _fniMelanieDir.GetStnPath(&stnT);
        if (!stnT.FAppendStn(&stn))
        {
            LOG_STEP_FAIL("FDownDir+FAppendStn for Melanie failed");
            return fFalse;
        }
        LOG_STEP_FAIL("FDownDir to Melanie failed");
        _FCantFindFileDialog(&stnT); // ignore failure
        return fFalse;
    }
    AssertPo(&_fniMelanieDir, ffniDir);

    fFirstTimeUser = fFalse;
    szDir[0] = chNil;
    if (!FGetSetRegKey(kszHomeDirValue, szDir, size(szDir), fregSetDefault | fregString))
    {
        LOG_STEP_FAIL("FGetSetRegKey(HomeDir, get-or-default) failed");
        return fFalse;
    }
    stn.SetSz(szDir);
    if (stn.Cch() == 0 || !_fniUserDir.FBuildFromPath(&stn, kftgDir) || tYes != _fniUserDir.TExists())
    {
        // Need to (find or create) and go to user's directory
        fFirstTimeUser = fTrue;
        _fniUserDir = _fniUsersDir;

        // Ensure that user's root directory exists.
        bool fHaveUserDir = _fniUserDir.FDownDir(&_stnUser, ffniMoveToDir);
        if (!fHaveUserDir)
            fHaveUserDir = _fniUserDir.FDownDir(&_stnUser, ffniCreateDir | ffniMoveToDir);
        // Preserve the installed profile first; fall back to the platform's
        // writable user-data root if that location cannot be used.
        if (!fHaveUserDir)
        {
            ZString szLocal;
            if (KauaiUsers::DataRoot(szLocal, kcchTotSz) && KauaiFiles::MakeDirectories(szLocal))
            {
                String stnLocal;
                stnLocal.SetSz(szLocal);
                if (_fniUserDir.FBuildFromPath(&stnLocal, kftgDir))
                {
                    fHaveUserDir = _fniUserDir.FDownDir(&_stnUser, ffniMoveToDir);
                    if (!fHaveUserDir)
                        fHaveUserDir = _fniUserDir.FDownDir(&_stnUser, ffniCreateDir | ffniMoveToDir);
                }
            }
        }

        if (!fHaveUserDir)
        {
            char szDetail[768];
            char szUserDir[kcchTotSz];
            char szUser[kcchTotSz];
            String stnDir;
            _fniUsersDir.GetStnPath(&stnDir);
            stnDir.GetSzs(szUserDir);
            _stnUser.GetSzs(szUser);
            std::snprintf(szDetail, sizeof(szDetail),
                          "_FGetUserDirectories: could not find or create per-user directory \"%s\" under \"%s\" (or "
                          "writable user-data fallback)",
                          szUser, szUserDir);
            AppendCrashLog("STEP_FAIL", szDetail);
            return fFalse;
        }

        // Try to write path to user dir to the registry
        _fniUserDir.GetStnPath(&stn);
        stn.GetSz(szDir);
        FGetSetRegKey(kszHomeDirValue, szDir, (CchSz(szDir) + kcchExtraSz) * sizeof(achar),
                      fregSetKey | fregString); // ignore failure
    }
    std::uint32_t directoryError = 0;
    if (!KauaiFiles::ChangeDirectory(szDir, &directoryError))
    {
        char szDetail[512], szPath[kcchTotSz];
        stn.SetSz(szDir);
        stn.GetSzs(szPath);
        std::snprintf(szDetail, sizeof(szDetail), "_FGetUserDirectories: ChangeDirectory(%s) failed, error=%u", szPath,
                      static_cast<unsigned>(directoryError));
        AppendCrashLog("STEP_FAIL", szDetail);
        return fFalse;
    }
    AssertPo(&_fniUserDir, ffniDir);

    if (!FSetProp(kpridFirstTimeUser, fFirstTimeUser))
    {
        LOG_STEP_FAIL("FSetProp(kpridFirstTimeUser) failed");
        return fFalse;
    }
    return fTrue;

#undef LOG_STEP_FAIL
}

/******************************************************************************
    _FGetUserName
        Attempts to get the current user name.  May return a default value
        if we get a "non-serious" error (as defined in the spec).

    Arguments:
        None

    Returns:  fTrue if _stnUser has something usable in it on return

************************************************************ PETED ***********/
bool Application::_FGetUserName(void)
{
    AssertBaseThis(0);

    bool fRet = fTrue;
    const auto user = KauaiUsers::QueryName();
    switch (user.status)
    {
    case KauaiUsers::NameStatus::ProviderError: {
        String stnFormat, stnMessage;
        if (FGetStnApp(idsWNetError, &stnFormat))
        {
            stnMessage.FFormat(&stnFormat, const_cast<achar *>(user.provider.c_str()),
                               static_cast<LegacyUlong>(user.error), const_cast<achar *>(user.message.c_str()));
            TGiveAlertSz(stnMessage.Psz(), bkOk, cokExclamation);
        }
        _stnUser.SetNil();
        fRet = fFalse;
        break;
    }
    case KauaiUsers::NameStatus::ProviderErrorUnavailable:
        Bug("Call to retrieve user provider error failed; this should never happen");
        _stnUser.SetNil();
        fRet = fFalse;
        break;
    case KauaiUsers::NameStatus::Found:
        _stnUser.SetRgch(const_cast<achar *>(user.name.data()), static_cast<long>(user.name.length()));
        if (_stnUser.Cch() > 0)
            break;
        // Empty names use the localized default, as before.
    default:
        if (!FGetStnApp(idsDefaultUser, &_stnUser))
            fRet = fFalse;
        break;
    }
    Assert(!fRet || _stnUser.Cch() > 0, "Bug in _FGetUserName");
    return fRet;
}

/******************************
    The user-data structure
*******************************/
struct UserData
{
    LegacyLong rglw[kcpridUserData];
};
static_assert(sizeof(UserData) == 4 * kcpridUserData, "user preferences must retain their 32-bit layout");

/***************************************************************************
    Reads the "user data" from the registry and SetProp's the data onto
    the app
***************************************************************************/
bool Application::_FReadUserData(void)
{
    AssertBaseThis(0);

    UserData UserData;
    long iprid;

    ClearPb(&UserData, size(UserData));

    if (!FGetSetRegKey(kszUserDataValue, &UserData, size(UserData), fregSetDefault | fregBinary))
    {
        return fFalse;
    }
    for (iprid = 0; iprid < kcpridUserData; iprid++)
    {
        if (!FSetProp(kpridUserDataBase + iprid, UserData.rglw[iprid]))
            return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Writes the "user data" from the app props to the registry
***************************************************************************/
bool Application::_FWriteUserData(void)
{
    AssertBaseThis(0);

    UserData UserData;
    long iprid;

    for (iprid = 0; iprid < kcpridUserData; iprid++)
    {
        long value;
        if (!FGetProp(kpridUserDataBase + iprid, &value) || value < INT32_MIN || value > INT32_MAX)
            return fFalse;
        UserData.rglw[iprid] = static_cast<LegacyLong>(value);
    }
    if (!FGetSetRegKey(kszUserDataValue, &UserData, size(UserData), fregSetKey | fregBinary))
    {
        return fFalse;
    }

    return fTrue;
}

/******************************************************************************
    FGetSetRegKey
        Given a reg key (and option sub-key), attempts to either get or set
        the current value of the key.  If the reg key is created on a Get
        (fSetKey == fFalse), the data in pvData will be used to set the
        default value for the key.


    Arguments:
        PZString pszValueName  --  The value name
        void *pvData      --  pointer to buffer to read or write key into or from
        long cbData       --  size of the buffer
        ulong grfreg      --  flags describing what we should do
        bool *pfNoValue  --  optional parameter, takes whether a real registry
                              error occurred or not

    Returns:  fTrue if all actions necessary could be performed

************************************************************ PETED ***********/
bool Application::FGetSetRegKey(PZString pszValueName, void *pvData, long cbData, ulong grfreg, bool *pfNoValue)
{
    AssertBaseThis(0);
    AssertSz(pszValueName);
    AssertPvCb(pvData, cbData);
    AssertNilOrVarMem(pfNoValue);

    KauaiSettings::Request request;
    request.write = (grfreg & fregSetKey) != 0;
    request.setDefault = (grfreg & fregSetDefault) != 0;
    request.machine = (grfreg & fregMachine) != 0;
    request.type = (grfreg & fregBinary)   ? KauaiSettings::Type::Binary
                   : (grfreg & fregString) ? KauaiSettings::Type::String
                                           : KauaiSettings::Type::Number;
    if (cbData < 0 || static_cast<std::uint64_t>(cbData) > UINT32_MAX)
        return fFalse;
    if (request.write && request.type == KauaiSettings::Type::String)
        Assert(CchSz((PZString)pvData) < cbData, "Invalid string for reg key");

#ifdef KAUAI_SDL3
    const auto &backend = StudioSettings::IniBackend();
#elif defined(_WIN32)
    const auto &backend = KauaiSettings::WindowsBackend();
#else
    const auto &backend = KauaiSettings::PosixBackend();
#endif
    bool wrongType = false;
    const bool result = KauaiSettings::Access(backend, kszSocratesKey, pszValueName, pvData,
                                              static_cast<std::uint32_t>(cbData), request, pfNoValue, &wrongType);
    Assert(!wrongType, "Invalid key type");
    return result;
}

/***************************************************************************
    Set the palette and bring up the Microsoft Home Logo
***************************************************************************/
bool Application::_FDisplayHomeLogo(bool fSkipSplashScreen)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    DataBlock blck;
    PDynamicArray pglclr;
    PMaskedBitmapMBMP pmbmp;
    short bo;
    short osk;

    if (!_pcfl->FFind(kctgColorTable, kcnoGlcrInit, &blck))
        return fFalse;
    pglclr = DynamicArray::PglRead(&blck, &bo, &osk);
    if (pvNil == pglclr)
        return fFalse;
    GraphicsPort::SetActiveColors(pglclr, fpalIdentity);
    ReleasePpo(&pglclr);

    if (!fSkipSplashScreen) {
        if (!_pcfl->FFind(kctgMbmp, kcnoMbmpHomeLogo, &blck))
            return fFalse;
        pmbmp = MaskedBitmapMBMP::PmbmpRead(&blck);
        if (pvNil == pmbmp)
            return fFalse;
        _pkwa->SetMbmp(pmbmp);
        ReleasePpo(&pmbmp);
        UpdateMarked();
    }

    return fTrue;
}

/***************************************************************************
    Initialize tag manager
***************************************************************************/
bool Application::_FInitProductNames(void)
{
    AssertBaseThis(0);

    PStringTable_GST pgst;
    DataBlock blck;
#ifdef KAUAI_SDL3
    try
    {
        String root;
        _fniMsKidsDir.GetStnPath(&root);
        StudioPacks::Initialize(root.Psz());
        StudioV3dmm::Initialize();
    }
    catch (const std::exception &error)
    {
        _FGenericError(const_cast<char *>(error.what()));
        return fFalse;
    }
#endif

    // Use kcbCacheTagm of cache per source, don't cache on CD

    vptagm = TagManager::PtagmNew(&_fniMsKidsDir, Application::FInsertCD, kcbCacheTagm);
    if (vptagm == pvNil)
    {
        _FGenericError(PszLit("_FInitProductNames: TagManager::PtagmNew"));
        return fFalse;
    }
#ifdef KAUAI_SDL3
    vptagm->SetSourceResolver(StudioPacks::ResolveSource);
    vptagm->SetArchiveResolver(StudioV3dmm::Archive);
#endif

#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    if (BatchPlaybackTest())
        vptagm->SetMissingTagTestCallback(BatchMissingTag);
#endif
    if (!_FReadProductTitles(&pgst))
    {
        _FGenericError(PszLit("_FInitProductNames: _FReadProductTitles"));
        goto LFail;
    }

    if (!vptagm->FMergeGstSource(pgst, kboCur, koskCur))
    {
        _FGenericError(PszLit("_FInitProductNames: FMergeGstSource"));
        goto LFail;
    }

    if (!_FFindProductDir(pgst))
    {
        _FGenericError(PszLit("_FInitProductNames: _FFindProductDir"));
        goto LFail;
    }

    if (!vptagm->FGetSid(&_stnProductLong, &_sidProduct))
    {
        _FGenericError(PszLit("_FInitProductNames: FGetSid"));
        goto LFail;
    }

    ReleasePpo(&pgst);
    return fTrue;

LFail:
    ReleasePpo(&pgst);
    return fFalse;
}

/***************************************************************************
    Read the sids and titles of installed 3DMovie products.
***************************************************************************/
bool Application::_FReadProductTitles(PStringTable_GST *ppgst)
{
    AssertBaseThis(0);
    AssertVarMem(ppgst);
    *ppgst = pvNil;
    String root;
    _fniMsKidsDir.GetStnPath(&root);
    StudioContent::Products products;
#ifdef KAUAI_SDL3
    try
    {
        products = StudioPacks::Products();
    }
    catch (const std::exception &error)
    {
        _FGenericError(const_cast<char *>(error.what()));
        return fFalse;
    }
#else
    products = StudioContent::ReadProducts(root.Psz());
#endif
    if (products.status != StudioContent::Status::Ready)
    {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "Cannot read installed product titles (status %u, native error %u)",
                      static_cast<unsigned>(products.status), static_cast<unsigned>(products.error));
        _FGenericError(detail);
        return fFalse;
    }
    PStringTable_GST pgst = StringTable_GST::PgstNew(size(LegacyLong));
    if (pgst == pvNil)
        return fFalse;
    for (const auto &product : products.entries)
    {
        String stnSid, stnTitle;
        LegacyLong sid;
        stnSid.SetSz(const_cast<achar *>(product.sourceId.c_str()));
        if (!stnSid.FGetLw(&sid))
        {
            Warn("Invalid installed product source id");
            continue;
        }
        stnTitle.SetSz(const_cast<achar *>(product.title.c_str()));
        if (!pgst->FAddStn(&stnTitle, &sid))
            goto LFail;
    }
    if (pgst->IvMac() == 0)
    {
        achar fallbackTitle[] = PszLit("3D Movie Maker/3DMovie");
        String stnTitle(fallbackTitle);
        LegacyLong sid = 1;
        if (!pgst->FAddStn(&stnTitle, &sid))
            goto LFail;
    }
    *ppgst = pgst;
    return fTrue;
LFail:
    ReleasePpo(&pgst);
    return fFalse;
}

/***************************************************************************
    Initialize 3-D Text
***************************************************************************/
bool Application::_FInitTdt(void)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    PStringTable_GST pgst;

    // set ThreeDText action names
    pgst = _PgstRead(kcnoGstAction);
    if (pvNil == pgst)
        return fFalse;
    Assert(pgst->CbExtra() == size(LegacyLong), "bad Action string table");
    if (!ThreeDText::FSetActionNames(pgst))
    {
        ReleasePpo(&pgst);
        return fFalse;
    }
    ReleasePpo(&pgst);
    return fTrue;
}

/***************************************************************************
    Read and byte-swap a StringTable_GST from _pcfl.  Assumes that extra data, if any,
    is a long.
***************************************************************************/
PStringTable_GST Application::_PgstRead(ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    DataBlock blck;

    if (!_pcfl->FFind(kctgGst, cno, &blck))
        return pvNil;
    return StudioResources::ReadStringTable(&blck);
}

/***************************************************************************
    Read various string tables from _pcfl
***************************************************************************/
bool Application::_FReadStringTables(void)
{
    AssertBaseThis(0);
    AssertPo(_pcfl, 0);

    // read studio filename list
    _pgstStudioFiles = _PgstRead(kcnoGstStudioFiles);
    if (pvNil == _pgstStudioFiles)
        return fFalse;

    // read building filename list
    _pgstBuildingFiles = _PgstRead(kcnoGstBuildingFiles);
    if (pvNil == _pgstBuildingFiles)
        return fFalse;

    // read shared filename list
    _pgstSharedFiles = _PgstRead(kcnoGstSharedFiles);
    if (pvNil == _pgstSharedFiles)
        return fFalse;

    // read misc app strings
    _pgstApp = _PgstRead(kcnoGstApp);
    if (pvNil == _pgstApp)
        return fFalse;

    return fTrue;
}

/***************************************************************************
    Initialize KidWorld (app kidworld)
***************************************************************************/
bool Application::_FInitKidworld(void)
{
    AssertBaseThis(0);

    RC rcRel(0, 0, krelOne, krelOne);
    GraphicsObjectBlock gcb(CommandHandler::HidUnique(), GraphicsObject::PgobScreen(), fgobNil, kginMark, pvNil, &rcRel);

    _pkwa = NewObj KidWorld(&gcb);
    if (pvNil == _pkwa)
        return fFalse;

    return fTrue;
}

/***************************************************************************
    Show the app splash screen
***************************************************************************/
bool Application::_FShowSplashScreen(void)
{
    AssertBaseThis(0);

    DataBlock blck;
    PMaskedBitmapMBMP pmbmp;

    if (!_pcfl->FFind(kctgMbmp, kcnoMbmpSplash, &blck))
        return fFalse;
    pmbmp = MaskedBitmapMBMP::PmbmpRead(&blck);
    if (pvNil == pmbmp)
        return fFalse;
    _pkwa->SetMbmp(pmbmp);
    ReleasePpo(&pmbmp);
    UpdateMarked();
    return fTrue;
}

/***************************************************************************
    Play the app splash sound
***************************************************************************/
bool Application::_FPlaySplashSound(void)
{
    AssertBaseThis(0);

    PChunkyResourceFile pcrf;

    pcrf = ChunkyResourceFile::PcrfNew(_pcfl, 0);
    if (pvNil == pcrf)
        return fFalse;
    vpsndm->SiiPlay(pcrf, kctgMidi, kcnoMidiSplash, ksqnNone, kvlmFull, 1, 0, 0, sclNonLoop);
    ReleasePpo(&pcrf);

    return fTrue;
}

/***************************************************************************
    Read the chunky files specified by _pgstSharedFiles, _pgstBuildingFiles
    and _pgstStudioFiles and create the global ChunkyResourceManager; indices to the Building
    and Studio CRFs are stored in _pglicrfBuilding and _pglicrfStudio.
***************************************************************************/
bool Application::_FInitCrm(void)
{
    AssertBaseThis(0);
    AssertPo(_pkwa, 0);
    AssertPo(_pgstSharedFiles, 0);
    AssertPo(_pgstBuildingFiles, 0);
    AssertPo(_pgstStudioFiles, 0);

    PGraphicsObjectInterpreter psceg = pvNil;
    PScript pscpt = pvNil;

    _pcrmAll = ChunkyResourceManager::PcrmNew(_pgstSharedFiles->IvMac() + _pgstBuildingFiles->IvMac() + _pgstStudioFiles->IvMac());
    if (pvNil == _pcrmAll)
        goto LFail;

    _pglicrfBuilding = DynamicArray::PglNew(size(LegacyLong), _pgstBuildingFiles->IvMac());
    if (pvNil == _pglicrfBuilding)
        goto LFail;

    _pglicrfStudio = DynamicArray::PglNew(size(LegacyLong), _pgstStudioFiles->IvMac());
    if (pvNil == _pglicrfStudio)
        goto LFail;

    if (!_FAddToCrm(_pgstSharedFiles, _pcrmAll, pvNil))
        goto LFail;

    if (!_FAddToCrm(_pgstBuildingFiles, _pcrmAll, _pglicrfBuilding))
        goto LFail;

    if (!_FAddToCrm(_pgstStudioFiles, _pcrmAll, _pglicrfStudio))
        goto LFail;

    // Initialize the shared util gob.
    psceg = _pkwa->PscegNew(_pcrmAll, _pkwa);
    if (pvNil == psceg)
        goto LFail;

    pscpt = (PScript)_pcrmAll->PbacoFetch(kctgScript, kcnoInitShared, Script::FReadScript);
    if (pvNil == pscpt)
        goto LFail;

    if (!psceg->FRunScript(pscpt))
        goto LFail;

    ReleasePpo(&psceg);
    ReleasePpo(&pscpt);
    return fTrue;
LFail:
    ReleasePpo(&psceg);
    ReleasePpo(&pscpt);
    return fFalse;
}

/***************************************************************************
    Helper function for _FInitCrm.  Adds the list of chunky files specified
    in pgstFiles to the ChunkyResourceManager pointed to by pcrm.  If pglFiles is not pvNil,
    it is filled in with the positions in the ChunkyResourceManager of each of the loaded
    crfs.
***************************************************************************/
bool Application::_FAddToCrm(PStringTable_GST pgstFiles, PChunkyResourceManager pcrm, PDynamicArray pglFiles)
{
    AssertBaseThis(0);
    AssertPo(&_fniProductDir, ffniDir);
    AssertPo(pgstFiles, 0);
    AssertPo(pcrm, 0);
    AssertNilOrPo(pglFiles, 0);

    bool fRet = fFalse;
    Filename fni;
    String stn;
    long istn;
    LegacyLong cbCache;
    PChunkyFile pcfl = pvNil;
    LegacyLong icfl;

    for (istn = 0; istn < pgstFiles->IvMac(); istn++)
    {
        pgstFiles->GetStn(istn, &stn);
        pgstFiles->GetExtra(istn, &cbCache);
#ifdef DEBUG
        {
            bool fAskForCDSav = Pkwa()->FAskForCD();
            bool fFoundFile;

            // In debug, we look for "buildingd.chk", "sharedd.chk", etc. and
            // use them instead of the normal files if they exist.  If they
            // don't exist, just use the normal files.
            String stnT = stn;
            stnT.FAppendCh(ChLit('d'));
            stnT.FAppendSz(PszLit(".chk")); // REVIEW *****
            Pkwa()->SetCDPrompt(fFalse);
            fFoundFile = Pkwa()->FFindFile(&stnT, &fni);
            Pkwa()->SetCDPrompt(fAskForCDSav);
            if (fFoundFile)
            {
                pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
            }
            else
            {
#endif                                         // DEBUG
                stn.FAppendSz(PszLit(".chk")); // REVIEW *****
                if (Pkwa()->FFindFile(&stn, &fni))
                    pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
#ifdef DEBUG
            }
        }
#endif // DEBUG
        if (pvNil == pcfl)
        {
            if (fni.Ftg() != ftgNil)
                fni.GetStnPath(&stn);
            if (!_fDontReportInitFailure)
                _FCantFindFileDialog(&stn); // ignore failure
            goto LFail;
        }
        if (!pcrm->FAddCfl(pcfl, cbCache, &icfl))
            goto LFail;
        ReleasePpo(&pcfl);
        if (pglFiles != pvNil && !pglFiles->FAdd(&icfl))
            goto LFail;
    }

    fRet = fTrue;
LFail:
    if (!fRet)
        ReleasePpo(&pcfl);
    return fRet;
}

/***************************************************************************
    Initialize and start the building script
***************************************************************************/
bool Application::_FInitBuilding(void)
{
    AssertBaseThis(0);

    bool fRet = fFalse;
    long i;
    LegacyLong cbCache;
    LegacyLong iv;
    PChunkyResourceFile pcrfT;
    PGraphicsObjectInterpreter psceg = pvNil;
    PScript pscpt = pvNil;

    BeginLongOp();

    psceg = _pkwa->PscegNew(_pcrmAll, _pkwa);
    if (pvNil == psceg)
        goto LFail;

    pscpt = (PScript)_pcrmAll->PbacoFetch(kctgScript, kcnoStartApp, Script::FReadScript);
    if (pvNil == pscpt)
        goto LFail;

    if (!psceg->FRunScript(pscpt))
        goto LFail;

    // Up the cache limits of the building crfs in the global crm.
    // Assumption: since the files were added to the crm in the order they
    // appear in _pgstBuilding, I can get the cache amounts from there.
    for (i = 0; i < _pglicrfBuilding->IvMac(); i++)
    {
        _pgstBuildingFiles->GetExtra(i, &cbCache);
        _pglicrfBuilding->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main ChunkyResourceManager is corrupt.");
        pcrfT->SetCbMax(cbCache);
    }

    // Zero the cache for the studio crfs in the global crm.
    for (i = 0; i < _pglicrfStudio->IvMac(); i++)
    {
        _pglicrfStudio->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main ChunkyResourceManager is corrupt.");
        pcrfT->SetCbMax(0);
    }

    fRet = fTrue;
LFail:
    if (!fRet)
        EndLongOp();
    ReleasePpo(&psceg);
    ReleasePpo(&pscpt);
    return fRet;
}

/***************************************************************************
    Initialize and start the studio script
***************************************************************************/
bool Application::_FInitStudio(PFilename pfniUserDoc, bool fFailIfDocOpenFailed)
{
    AssertBaseThis(0);

    long i;
    LegacyLong cbCache;
    LegacyLong iv;
    PChunkyResourceFile pcrfT;
    bool fRet = fFalse;

    _pstdio = Studio::PstdioNew(khidStudio, _pcrmAll, (pfniUserDoc->Ftg() == ftgNil ? pvNil : pfniUserDoc),
                               fFailIfDocOpenFailed);
    if (_pstdio == pvNil)
    {
        goto LFail;
    }

    // Up the cache limits of the studio crfs in the global crm.
    for (i = 0; i < _pglicrfStudio->IvMac(); i++)
    {
        _pgstStudioFiles->GetExtra(i, &cbCache);
        _pglicrfStudio->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main ChunkyResourceManager is corrupt.");
        pcrfT->SetCbMax(cbCache);
    }

    // Zero the cache for the building crfs in the global crm.
    for (i = 0; i < _pglicrfBuilding->IvMac(); i++)
    {
        _pglicrfBuilding->Get(i, &iv);
        pcrfT = _pcrmAll->PcrfGet(iv);
        Assert(pcrfT != pvNil, "Main ChunkyResourceManager is corrupt.");
        pcrfT->SetCbMax(0);
    }

    fRet = fTrue;

LFail:
    if (!fRet)
        PushErc(ercSocCantInitStudio);
    return fRet;
}

/***************************************************************************
    Sets _fSlowCPU if this is a slow CPU.  It tests the graphics,
    fixed-point math, and memory copying speed, and if any of them are
    slower than a threshold, _fSlowCPU is set.
***************************************************************************/
bool Application::_FDetermineIfSlowCPU(void)
{
    AssertBaseThis(0);

    bool fSlowCPU;

    // If user has a saved preference, read and use that
    if (FGetSetRegKey(kszBetterSpeedValue, &fSlowCPU, size(bool), fregBinary))
    {
        _fSlowCPU = fSlowCPU;
        return fTrue;
    }

    _fSlowCPU = fFalse;

#ifndef PERF_TEST

    return fTrue;

#else // PERF_TEST

    PGraphicsPort pgptWnd = pvNil;
    PGraphicsPort pgptOff = pvNil;
    RC rc1;
    RC rc2;
    ulong ts;
    ulong dts1;
    ulong dts2;
    ulong dts3;
    long i;
    char *pch1 = pvNil;
    char *pch2 = pvNil;
    BRS r1;
    BRS r2;
    BRS r3;

    // Test 1: Graphics.  Copy some graphics to an offscreen buffer,
    // then blit it back to the window 100 times.
    rc1.Set(0, 0, 300, 300);
    rc2.Set(0, 0, 300, 300);
    pgptWnd = GraphicsPort::PgptNewHwnd(vwig.hwndApp);
    if (pvNil == pgptWnd)
        goto LFail;
    pgptOff = GraphicsPort::PgptNewOffscreen(&rc1, 8); // World RGB buffer is 8-bit
    if (pvNil == pgptOff)
    {
        goto LFail;
    }
    // BLOCK
    {
        GraphicsEnvironment gnvWnd(pgptWnd);
        GraphicsEnvironment gnvOff(pgptOff);
        gnvOff.CopyPixels(&gnvWnd, &rc1, &rc1);
        ts = TsCurrent();
        for (i = 0; i < 100; i++)
            gnvWnd.CopyPixels(&gnvOff, &rc1, &rc2);
        dts1 = TsCurrent() - ts;
    }
    ReleasePpo(&pgptWnd);
    ReleasePpo(&pgptOff);

    // Test 2: Math.  Do 200,000 fixed-point adds and multiplies
    r1 = BR_SCALAR(1.12);    // arbitrary number
    r2 = BR_SCALAR(3.14159); // arbitrary number
    r3 = rZero;
    ts = TsCurrent();
    for (i = 0; i < 200000; i++)
    {
        r2 = BrsMul(r1, r2);
        r3 = BrsAdd(r2, r3);
    }
    dts2 = TsCurrent() - ts;

    // Test 3: Copying.  Copy a 50,000 byte block 200 times.
    if (!FAllocPv((void **)&pch1, 50000, mprNormal, fmemClear))
        goto LFail;
    if (!FAllocPv((void **)&pch2, 50000, mprNormal, fmemClear))
        goto LFail;
    ts = TsCurrent();
    for (i = 0; i < 200; i++)
        CopyPb(pch1, pch2, 50000);
    dts3 = TsCurrent() - ts;
    FreePpv((void **)&pch1);
    FreePpv((void **)&pch2);

    if (dts1 > 700 || dts2 > 200 || dts3 > 500)
        _fSlowCPU = fTrue;

    return fTrue;
LFail:
    ReleasePpo(&pgptWnd);
    ReleasePpo(&pgptOff);
    FreePpv((void **)&pch1);
    FreePpv((void **)&pch2);
    return fFalse;

#endif // PERF_TEST
}

/***************************************************************************
    Reads command line.  Also sets _fniExe to path to this executable
    and _fniPortfolioDoc to file specified on command line (if any).
    Also initializes _stnProductLong and _stnProductShort.

    -f: fast mode
    -s: slow mode
    -p"longname": long productname
    -t"shortname": short productname
    -m: don't minimize window on deactivate

***************************************************************************/
void Application::_ParseCommandLine(void)
{
    AssertBaseThis(0);

#if defined(WIN) || defined(KAUAI_SDL3)
    ZString sz;
    String stn;
    achar *pch;
    achar *pchT;
    Filename fniT;

    // Get path to current directory
    if (!KauaiFiles::CurrentDirectory(sz, kcchMaxSz))
    {
        Warn("Cannot resolve current directory");
        return;
    }
    stn.SetSz(sz);
    if (!_fniCurrentDir.FBuildFromPath(&stn, kftgDir))
        Bug("Bad current directory?");

    // Get path to exe
    if (!KauaiFiles::ExecutablePath(sz, kcchMaxSz))
    {
        Warn("Cannot resolve executable path");
        return;
    }
    stn.SetSz(sz);
    if (!_fniExe.FBuildFromPath(&stn))
        Bug("Bad module filename?");

    StudioStartup::Options<achar> options;
    const bool parsed = KauaiLaunch::HasArguments() ? StudioStartup::ParseArguments(KauaiLaunch::ArgumentCount(),
                                                                                    KauaiLaunch::Arguments(), options)
                                                    : StudioStartup::Parse(KauaiLaunch::CommandLine(), options);
    if (!parsed)
    {
        Warn("Malformed startup arguments");
        return;
    }
    if (options.dontMinimize)
        _fDontMinimize = fTrue;
    if (options.hasSpeed)
        _fSlowCPU = options.slow;
    if (options.hasLongName)
        _stnProductLong.SetRgch(const_cast<achar *>(options.longName.data()),
                                static_cast<long>(options.longName.length()));
    if (options.hasShortName)
        _stnProductShort.SetRgch(const_cast<achar *>(options.shortName.data()),
                                 static_cast<long>(options.shortName.length()));
    if (options.unknownSwitches)
        Warn("Bad command-line switch");
#ifdef KAUAI_SDL3
    StudioContent::SetISORequest(StudioContent::Text(options.isoPath.begin(), options.isoPath.end()));
#endif
    if (options.document)
    {
        if (options.documentIsLiteral)
            stn.SetSz(const_cast<achar *>(options.document));
        else
        {
            pch = const_cast<achar *>(options.document);
            // get to end of string
            pchT = pch + CchSz(pch);

            // skip quotes since FBuildFromPath can't deal
            if (*pch == ChLit('"'))
                pch++;

            // move pchT to begginning of file name itself
            while ((*pchT != '\\') && (pchT != pch))
                pchT--;
            // pchT now points to last'\', move it forward one (only if not pch)
            if (*pchT == '\\')
                pchT++;

            // set String to the path, which is the string up to the last '\'
            stn.SetRgch(pch, pchT - pch);

            // try to map to long file name if we can
            if (KauaiFiles::LookupLeafName(pch, sz, kcchMaxSz))
            {
                // append the longfile name returned...
                stn.FAppendSz(sz);
            }
            else
                // just use what was passed orginally...
                stn.SetSz(pch);

            // remove ending quotes since FBuildFromPath can't deal
            if (stn.Cch() && stn.Psz()[stn.Cch() - 1] == ChLit('\"'))
                stn.Delete(stn.Cch() - 1, 1);
        }
        if (fniT.FBuildFromPath(&stn))
        {
            SetPortfolioDoc(&fniT);
        }
    }
#endif // WIN or SDL
#if defined(MAC) && !defined(KAUAI_SDL3)
    RawRtn();
#endif // MAC
}

/***************************************************************************
    Make sure that _stnProductLong and _stnProductShort are initialized to
    non-empty strings.  If _stnProductLong wasn't initialized from the
    command line, it and _stnProductShort are read from the app resource
    file.  If _stnProductLong is valid and _stnProductShort is not,
    _stnProductShort is just set to _stnProductLong.
***************************************************************************/
bool Application::_FEnsureProductNames(void)
{
    AssertBaseThis(0);

    if (_stnProductLong.Cch() == 0)
    {
#ifdef KAUAI_SDL3
        _stnProductLong = STUDIO_NAME_LITERAL(STUDIO_PRODUCT_LONG_NAME);
        _stnProductShort = STUDIO_NAME_LITERAL(STUDIO_PRODUCT_SHORT_NAME);
#elif defined(WIN)
        ZString sz;

        if (0 == LoadString(vwig.hinst, stid3DMovieNameLong, sz, kcchMaxSz))
            return fFalse;
        _stnProductLong.SetSz(sz);

        if (0 == LoadString(vwig.hinst, stid3DMovieNameShort, sz, kcchMaxSz))
            return fFalse;
        _stnProductShort.SetSz(sz);
#else  // MAC
        RawRtn();
#endif // MAC
    }

    if (_stnProductShort.Cch() == 0)
        _stnProductShort = _stnProductLong;

    return fTrue;
}

/***************************************************************************
    Find _fniMsKidsDir
***************************************************************************/
bool Application::_FFindMsKidsDir(void)
{
    AssertBaseThis(0);
    Assert(_stnProductLong.Cch() > 0 && _stnProductShort.Cch() > 0, "_stnProductLong and _stnProductShort must exist");

    String stn;

#ifdef KAUAI_SDL3
    if (StudioContent::HasISORequest())
    {
        const auto iso = StudioContent::PrepareISO(false);
        if (iso.status != StudioContent::Status::Ready || iso.path.empty())
        {
            _FGenericError(PszLit("Cannot prepare ISO content; see the startup log for details"));
            return fFalse;
        }
        stn.SetSz(const_cast<achar *>(iso.path.c_str()));
        return _fniMsKidsDir.FBuildFromPath(&stn, kftgDir);
    }
    const auto configured = StudioContent::ConfiguredRoot();
    if (configured.status != StudioContent::Status::Ready)
    {
        _FGenericError(PszLit("Invalid THREEDMM_CONTENT_ROOT: expected an absolute content root of at most 255 characters"));
        return fFalse;
    }
    if (!configured.path.empty())
    {
        stn.SetSz(const_cast<achar *>(configured.path.c_str()));
        Filename candidate;
        if (!candidate.FBuildFromPath(&stn, kftgDir) || !_FMsKidsDirIsComplete(&candidate))
        {
            _FGenericError(PszLit("THREEDMM_CONTENT_ROOT does not contain the selected product's BLDGDATA.CHK"));
            return fFalse;
        }
        _fniMsKidsDir = candidate;
        return fTrue;
    }
    // SDL development launches supply the product directory as cwd, and an
    // installed executable may live there too. Recognize that tree before
    // searching unrelated system installs. Filename keeps this filesystem-only.
    for (int source = 0; source < 2; ++source)
    {
        Filename candidate = source == 0 ? _fniCurrentDir : _fniExe;
        if (source == 1 && !candidate.FSetLeaf(pvNil, kftgDir))
            continue;
        for (int parent = 0; parent < 3; ++parent)
        {
            if (_FMsKidsDirIsComplete(&candidate))
            {
                _fniMsKidsDir = candidate;
                return fTrue;
            }
            // CMake installs the executable beside the Microsoft Kids tree.
            // This also supports relocatable Mac/Linux installs without an env var.
            Filename installed = candidate;
            String kids(PszLit("Microsoft Kids"));
            if (installed.FDownDir(&kids, ffniMoveToDir) && _FMsKidsDirIsComplete(&installed))
            {
                _fniMsKidsDir = installed;
                return fTrue;
            }
            if (!candidate.FUpDir(pvNil, ffniMoveToDir))
                break;
        }
    }
    const auto iso = StudioContent::PrepareISO(true);
    if (iso.status != StudioContent::Status::Ready)
    {
        _FGenericError(PszLit("Cannot prepare ISO content; see the startup log for details"));
        return fFalse;
    }
    if (!iso.path.empty())
    {
        stn.SetSz(const_cast<achar *>(iso.path.c_str()));
        return _fniMsKidsDir.FBuildFromPath(&stn, kftgDir);
    }
#endif

    // Candidate kids-dir roots, in priority order. We descend each through
    // _FFindMsKidsDirAt (Microsoft Kids/MSKIDS) and prefer the first one whose
    // 3D Movie Maker subdir contains BLDGDATA.CHK -- that file ships only with
    // a real install, so it cleanly distinguishes a complete tree from a dev
    // dist/ that only has chomped artifacts. If no candidate is complete, we
    // still fall back to the first candidate that at least had a Microsoft Kids
    // subdir, so the original error-reporting path is preserved.
    Filename rgfniCand[4];
    long cfniCand = 0;

    rgfniCand[cfniCand] = _fniExe;
    if (rgfniCand[cfniCand].FSetLeaf(pvNil, kftgDir))
        cfniCand++;

    rgfniCand[cfniCand] = _fniCurrentDir;
    if (rgfniCand[cfniCand].FSetLeaf(pvNil, kftgDir))
        cfniCand++;

#ifdef WIN
    stn = PszLit("C:\\Program Files (x86)");
    if (rgfniCand[cfniCand].FBuildFromPath(&stn, kftgDir) && tYes == rgfniCand[cfniCand].TExists())
        cfniCand++;

    stn = PszLit("C:\\Program Files");
    if (rgfniCand[cfniCand].FBuildFromPath(&stn, kftgDir) && tYes == rgfniCand[cfniCand].TExists())
        cfniCand++;

#endif

    Filename fniFirstHit;
    bool fHaveFirstHit = fFalse;

    for (long i = 0; i < cfniCand; i++)
    {
        Filename fni = rgfniCand[i];
        if (!_FFindMsKidsDirAt(&fni))
            continue;
        if (!fHaveFirstHit)
        {
            fniFirstHit = fni;
            fHaveFirstHit = fTrue;
        }
        if (_FMsKidsDirIsComplete(&fni))
        {
            _fniMsKidsDir = fni;
            AssertPo(&_fniMsKidsDir, ffniDir);
            return fTrue;
        }
    }

    if (fHaveFirstHit)
    {
        _fniMsKidsDir = fniFirstHit;
        AssertPo(&_fniMsKidsDir, ffniDir);
        return fTrue;
    }

    Warn("Can't find Microsoft Kids or MSKIDS.");
    stn = PszLit("Microsoft Kids");
    _FCantFindFileDialog(&stn); // ignore failure
    return fFalse;
}

/***************************************************************************
    Finds Microsoft Kids directory at a given path. Modifies the path to
    descend into the directory. Returns true if successful.
***************************************************************************/
bool Application::_FFindMsKidsDirAt(Filename *path)
{
    String stn;

    /* REVIEW ***** (peted): if you check for the MSKIDS dir first, then
        you don't have to reset the dir string before presenting the error
        to the user */
    stn = PszLit("Microsoft Kids"); // REVIEW *****
    if (!path->FDownDir(&stn, ffniMoveToDir))
    {
        stn = PszLit("MSKIDS"); // REVIEW *****
        if (!path->FDownDir(&stn, ffniMoveToDir))
        {
            return fFalse;
        }
    }

    AssertPo(path, ffniDir);
    return fTrue;
}

/***************************************************************************
    Returns true if the given Microsoft Kids dir contains a complete 3DMM
    install. We probe for "3D Movie Maker/BLDGDATA.CHK" because BLDGDATA.CHK
    is original-release content that is never produced by our build, so its
    presence reliably distinguishes a full install from a freshly-installed
    dist/ tree containing only chomped artifacts.
***************************************************************************/
bool Application::_FMsKidsDirIsComplete(Filename *path)
{
    Filename fni = *path;
    String stn;

    stn = _stnProductLong;
    if (!fni.FDownDir(&stn, ffniMoveToDir))
    {
        stn = _stnProductShort;
        if (!fni.FDownDir(&stn, ffniMoveToDir))
            return fFalse;
    }
    stn = PszLit("BLDGDATA.CHK");
    if (!fni.FSetLeaf(&stn))
        return fFalse;
    return fni.FResolveCase();
}

/***************************************************************************
    Find _fniProductDir
    At this point, _stnProduct* is either the command line parameter or
    3D Movie Maker (in the absence of a command line parameter)
    _FFindProductDir() locates the .chk files by checking:
        first, _stnProduct* directories, or
        second, the registry of installed products.
    This routine updates _stnProductLong and _stnProductShort on return.
***************************************************************************/
bool Application::_FFindProductDir(PStringTable_GST pgst)
{
    AssertBaseThis(0);
    AssertVarMem(pgst);

    String stnLong;
    String stnShort;
    String stn;
    Filename fni;
    long istn;

    if (_FQueryProductExists(&_stnProductLong, &_stnProductShort, &_fniProductDir))
        return fTrue;

    for (istn = 0; istn < pgst->IstnMac(); istn++)
    {
        pgst->GetStn(istn, &stn);
        vptagm->SplitString(&stn, &stnLong, &stnShort);
        if (_FQueryProductExists(&stnLong, &stnShort, &fni))
        {
            _stnProductLong = stnLong;
            _stnProductShort = stnShort;
            _fniProductDir = fni;
            return fTrue;
        }
    }
    return fFalse;
}

/***************************************************************************
    See if the product exists.
    Method:  See if the directory and chunk file exist.
***************************************************************************/
bool Application::_FQueryProductExists(String *pstnLong, String *pstnShort, Filename *pfni)
{
    AssertBaseThis(0);
    AssertVarMem(pfni);
    AssertPo(pstnLong, 0);
    AssertPo(pstnShort, 0);

    Filename fni;
    String stn;

    *pfni = _fniMsKidsDir;
    if (!pfni->FDownDir(pstnLong, ffniMoveToDir) && !pfni->FDownDir(pstnShort, ffniMoveToDir))
    {
        pfni->GetStnPath(&stn);
        if (!stn.FAppendStn(&_stnProductLong))
            goto LFail;
        _FCantFindFileDialog(&stn); // ignore failure
        goto LFail;
    }

    fni = *pfni;
    if (fni.FSetLeaf(pstnLong, kftgChunky) && fni.FResolveCase())
        return fTrue;
    fni = *pfni;
    if (fni.FSetLeaf(pstnShort, kftgChunky) && fni.FResolveCase())
        return fTrue;
LFail:
    pfni->SetNil();
    return fFalse;
}

/***************************************************************************
    Socrates window was activated or deactivated.
***************************************************************************/
void Application::_Activate(bool fActive)
{
    AssertBaseThis(0);

    Application_PAR::_Activate(fActive);
    if (BackgroundPlayback())
        return;
#ifdef KAUAI_SDL3
    return; // SDL owns focus and window state on every platform.
#endif

#ifdef WIN
    bool fIsIconic;

    fIsIconic = IsIconic(vwig.hwndApp);

    if (!fActive) // app was just deactivated
    {
        if (_FDisplayIs640480() && !_fDontMinimize && !fIsIconic)
        {
            // Note: using SW_MINIMIZE causes a bug where alt-tabbing
            // from this app to a fullscreen DOS window reactivates
            // this app.  So use SW_SHOWMINNOACTIVE instead.
            ShowWindow(vwig.hwndApp, SW_SHOWMINNOACTIVE); // minimize app
            _fMinimized = fTrue;

            // Note that we examine _fMinimized during the WM_DISPLAYCHANGE message
            // received as a result of the following res change call. Therefore the
            // minimize operation MUST precede the res switch.
            if (_fSwitchedResolution)
                _FSwitch640480(fFalse);

            // When the portfolio is displayed, the main app is automatically disabled.
            // This means all keyboard/mouse input directed at the main app window will
            // be ignored until the portfolio is finished with. If the app is minimized
            // here while the portfolio is displayed, then we will be left with a disabled
            // app window on the win95 task bar. As a result, the app will not appear
            // only the task window invoked by an Alt-tab, nor is it resized when
            // the user clicks on it in the taskbar, (even though win95 tries to
            // activate it). We could do the following...
            // (1) Do not auto-minimize the app window while the portfolio is displayed.
            //		This is what happens on NT.
            // (2) Drop the portfolio here, so the app window is enabled on the taskbar.
            // (3) Make sure the app window is enabled now, by doing this...
            EnableWindow(vwig.hwndApp, TRUE);

            // The concern with doing this, is that when the app is later restored,
            // it is then enabled when it shouldn't be, as the portfolio is still
            // up in front of it. As it happens, this doesn't matter because the
            // portfolio is full screen. This means that the user can't direct any
            // mouse input to the main app window, and the portfolio will eat up any
            // keyboard input.
        }
    }

#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC

    /* Don't do this stuff unless we've got the CommandExecutionManager set up */
    if (vpcex != pvNil)
    {

        if (!fActive)
        {
            if (!vpcex->FCidIn(cidDeactivate))
            {
                vpcex->EnqueueCid(cidDeactivate);
                _fDown = fTrue;
                _cactToggle = 0;
            }
        }
        else if (_pcex != pvNil)
        {
            //
            // End the modal wait
            //
            Assert(CactModal() > 0, "AAAAAAAAAhhhhh! - P.Floyd (Encore performance)");

            // If there is no cidEndModal currently waiting to be processed,
            // enqueue one now. This ensures we don't get multiple cidEndModal's
            // processed. Note that we don't want to set _pcex null here as it
            // is later examined in the wndproc before we ultimately return
            // from FModalTopic.
            if (!_pcex->FCidIn(cidEndModal))
                _pcex->EnqueueCid(cidEndModal);
        }
        else
        {
            vpcex->FlushCid(cidDeactivate);
        }
    }
}

/***************************************************************************
    Deactivate the app
***************************************************************************/
bool Application::FCmdDeactivate(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (BackgroundPlayback())
        return fTrue;

    GraphicsObjectBlock gcb;
    PWorldOfKidspace pwoksModal;
    GraphicsObjectTreeEnumerator gte;
    PGraphicsObject pgob;
    ulong grfgte;
    long lwRet;
    CMD_MOUSE cmd;
    PT pt;
    bool fDoQuit;

    if (_pcex != pvNil)
    {
        Assert(CactModal() > 0, "AAAAAAAAAhhhhh! - P.Floyd");
        return (fTrue);
    }

    pgob = vpcex->PgobTracking();
    if ((pgob != pvNil) && (_cactToggle < 500))
    {
        //
        // Toggle the mouse button
        //
        TrackMouse(pgob, &pt);

        ClearPb(&cmd, size(CMD_MOUSE));

        cmd.pcmh = pgob;
        cmd.cid = cidTrackMouse;
        cmd.xp = pt.xp;
        cmd.yp = pt.yp;
        cmd.grfcust = GrfcustCur();
        if (_fDown)
        {
            cmd.grfcust |= fcustMouse;
        }
        else
        {
            cmd.grfcust &= ~fcustMouse;
        }
        vpcex->EnqueueCmd((PCommand)&cmd);
        vpcex->EnqueueCid(cidDeactivate);
        _fDown = !_fDown;
        _cactToggle++;
        return fTrue;
    }

    gte.Init(Pkwa(), fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || !pgob->FIs(kclsKidspaceGraphicObject))
            continue;

        ((PKidspaceGraphicObject)pgob)->Suspend();
    }

    if (FPushModal())
    {
        gcb.Set(CommandHandler::HidUnique(), Pkwa(), fgobNil, kginMark);
        gcb._rcRel.Set(0, 0, krelOne, krelOne);

        _pcex = vpcex;

        if (pvNil != (pwoksModal = NewObj WorldOfKidspace(&gcb, Pkwa()->Pstrg())))
        {
            vpcex->SetModalGob(pwoksModal);
            FModalLoop(&lwRet); // If we cannot enter modal mode, then we just won't suspend.
            vpcex->SetModalGob(pvNil);
        }

        ReleasePpo(&pwoksModal);

        _pcex = pvNil;

        // The user may have selected Close the app system menu while the
        // app was minimized on the taskbar. Depending on how Windows sent
        // the messages to us, (ie the processing order of activate and
        // close messages are not predictable it seems), we may or may not
        // have a cidQuit message queued for the app. If PopModal destroys
        // queued messages, then we would loose any Waiting cidQuit.
        // Therefore check if we have a queued cidQuit, and if so, requeue
        // it after the call to PopModal.
        fDoQuit = vpcex->FCidIn(cidQuit);

        PopModal();

        if (fDoQuit)
            vpcex->EnqueueCid(cidQuit);
    }

    gte.Init(Pkwa(), fgteNil);
    while (gte.FNextGob(&pgob, &grfgte, fgteNil))
    {
        if (!(grfgte & fgtePre) || !pgob->FIs(kclsKidspaceGraphicObject))
            continue;

        ((PKidspaceGraphicObject)pgob)->Resume();
    }

    return (fTrue);
}

/***************************************************************************
    Copy pixels from an offscreen buffer (pgnvSrc, prcSrc) to the screen
    (pgnvDst, prcDst).  This is called to move bits from an offscreen
    buffer to the screen during a _FastUpdate cycle.  This gives us
    a chance to do a transition.
***************************************************************************/
void Application::_CopyPixels(PGraphicsEnvironment pgnvSrc, RC *prcSrc, PGraphicsEnvironment pgnvDst, RC *prcDst)
{
    AssertBaseThis(0);
    AssertPo(pgnvSrc, 0);
    AssertVarMem(prcSrc);
    AssertPo(pgnvDst, 0);
    AssertVarMem(prcDst);

    PMovie pmvie = _Pmvie(); // Get the current movie, if any
    PGraphicsObject pgob;
    RC rcDst, rcSrc, rcWorkspace;

    if (pmvie == pvNil || pmvie->Trans() == transNil)
    {
        Application_PAR::_CopyPixels(pgnvSrc, prcSrc, pgnvDst, prcDst);
        return;
    }

#ifdef KAUAI_SDL3
    // Preserve the original palette-based transition presentation.
    struct TransitionGuard
    {
        bool &active;
        TransitionGuard(bool &value) : active(value)
        {
            active = true;
        }
        ~TransitionGuard()
        {
            active = false;
        }
    } transitionGuard(_fMovieTransition);
    KauaiSDL::Main().SetPresentationPixels(nullptr);
#endif

    Assert(prcSrc->Dyp() == prcDst->Dyp() && prcSrc->Dxp() == prcDst->Dxp(), "rc's are scaled");

    // Need to do a transition, but if it's a slow transition (not a cut),
    // we want to do a regular copy on all the areas around the workspace,
    // then the slow transition on just the workspace.

    pgob = Pkwa()->PgobFromHid(kidWorkspace);

    if (pgob == pvNil || pmvie->Trans() == transCut)
    {
        pmvie->DoTrans(pgnvDst, pgnvSrc, prcDst, prcSrc);
        return;
    }

    // NOTE: This code assumes that the following will base rcWorkspace
    // in the same coordinate system as prcDst, which is always true
    // according to ShonK.
    pgob->GetRc(&rcWorkspace, cooHwnd);

    // Do the areas around the workspace without the transition
    if (prcDst->ypTop < rcWorkspace.ypTop)
    {
        rcSrc = *prcSrc;
        rcSrc.ypBottom = rcWorkspace.ypTop + (rcSrc.ypTop - prcDst->ypTop);
        rcDst = *prcDst;
        rcDst.ypBottom = rcWorkspace.ypTop;
        Application_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }

    if (prcDst->ypBottom > rcWorkspace.ypBottom)
    {
        rcSrc = *prcSrc;
        rcSrc.ypTop = rcWorkspace.ypBottom + (rcSrc.ypTop - prcDst->ypTop);
        rcDst = *prcDst;
        rcDst.ypTop = rcWorkspace.ypBottom;
        Application_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }
    if (prcDst->xpLeft < rcWorkspace.xpLeft)
    {
        rcSrc.ypTop = rcWorkspace.ypTop + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.ypBottom = rcWorkspace.ypBottom + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.xpLeft = prcSrc->xpLeft;
        rcSrc.xpRight = rcWorkspace.xpLeft + (prcSrc->xpLeft - prcDst->xpLeft);
        rcDst = *prcDst;
        rcDst.xpRight = rcWorkspace.xpLeft;
        rcDst.ypTop = rcWorkspace.ypTop;
        rcDst.ypBottom = rcWorkspace.ypBottom;
        Application_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }

    if (prcDst->xpRight > rcWorkspace.xpRight)
    {
        rcSrc.ypTop = rcWorkspace.ypTop + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.ypBottom = rcWorkspace.ypBottom + (prcSrc->ypTop - prcDst->ypTop);
        rcSrc.xpLeft = rcWorkspace.xpRight + (prcSrc->xpLeft - prcDst->xpLeft);
        rcSrc.xpRight = prcSrc->xpRight;
        rcDst = *prcDst;
        rcDst.xpLeft = rcWorkspace.xpRight;
        rcDst.ypTop = rcWorkspace.ypTop;
        rcDst.ypBottom = rcWorkspace.ypBottom;
        Application_PAR::_CopyPixels(pgnvSrc, &rcSrc, pgnvDst, &rcDst);
    }

    //
    // Now do the workspace copy, with the transition
    //
    if (rcWorkspace.FIntersect(prcDst))
    {
        rcSrc = rcWorkspace;
        rcSrc.Offset(prcSrc->xpLeft - prcDst->xpLeft, prcSrc->ypTop - prcDst->ypTop);
        pmvie->DoTrans(pgnvDst, pgnvSrc, &rcWorkspace, &rcSrc);
    }
}

/***************************************************************************
    Load the Studio
***************************************************************************/
bool Application::FCmdLoadStudio(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    Filename fniUserDoc;
    ChildChunkID chidProject;
    long kidBuilding;
    PGraphicsObject pgob;
    PKidspaceGraphicObject pgokBackground;

    kidBuilding = pcmd->rglw[0];
    chidProject = pcmd->rglw[1];

    GetPortfolioDoc(&fniUserDoc); // might be ftgNil

    if (_FInitStudio(&fniUserDoc))
    {
        // Nuke the building gob
        pgob = Pkwa()->PgobFromHid(kidBuilding);
        if (pvNil != pgob)
            pgob->Release();

        // Start a project, if requested
        if (chidProject != chidNil)
        {
            pgokBackground = (PKidspaceGraphicObject)Pkwa()->PgobFromHid(kidBackground);
            if ((pgokBackground != pvNil) && pgokBackground->FIs(kclsKidspaceGraphicObject))
            {
                AssertPo(pgokBackground, 0);
                // REVIEW *****: if this fails, what happens?
                pgokBackground->FRunScript((kstDefault << 16) | chidProject);
            }
        }
    }
    else
    {
        Assert(_pstdio == pvNil, "_FInitStudio didn't clean up");
        vpcex->EnqueueCid(cidLoadStudioFailed);
    }

    return fTrue;
}

/***************************************************************************
    Load the Building
***************************************************************************/
bool Application::FCmdLoadBuilding(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if ((_pstdio != pvNil) && !_pstdio->FShutdown())
    {
        long kgobReturn;

        // This attempt may have destroyed the contents of kpridBuildingGob;
        // Try to reset it from kpridBuildingGobT.
        if (FGetProp(kpridBuildingGobT, &kgobReturn))
            FSetProp(kpridBuildingGob, kgobReturn);
        if (FGetProp(kpridBuildingStateT, &kgobReturn))
            FSetProp(kpridBuildingState, kgobReturn);
        Warn("Shutting down Studio failed");
        return fTrue;
    }

    if (_FInitBuilding())
        ReleasePpo(&_pstdio);
    return fTrue;
}

/***************************************************************************
    Exit the studio
***************************************************************************/
bool Application::FCmdExitStudio(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    tribool tRet;
    String stnBackup;

    // Now query the user to find whether they want to go to the
    // building or to exit the app completely.

    if (!FGetStnApp(idsExitStudio, &stnBackup))
        return fTrue;

    tRet = TModal(vpapp->PcrmAll(), ktpcQueryExitStudio, &stnBackup, bkYesNoCancel);

    // Take more action if the user did not hit cancel.
    if (tRet != tMaybe)
    {
        // Save the current movie if necessary.
        if ((_pstdio != pvNil) && !_pstdio->FShutdown(tRet == tYes))
            return fTrue;

        // Either go to the building, or leave the app.
        if (tRet == tYes)
        {
            // Go to the building.
            if (_FInitBuilding())
                ReleasePpo(&_pstdio);
        }
        else if (tRet == tNo)
        {
            // User wants to quit. We have have any dirty doc already by now.
            // Note that Application::Quit() doesn't release _pstdio when we quit so
            // we don't need to do it here either.
            _fQuit = fTrue;
        }
    }

    return fTrue;
}

/***************************************************************************
    Load the Theater
***************************************************************************/
bool Application::FCmdTheaterOpen(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    long kidParent;

    kidParent = pcmd->rglw[0];

    if (pvNil != _ptatr)
    {
        Bug("You forgot to close the last Theater!");
        AssertPo(_ptatr, 0);
        return fTrue;
    }
    _ptatr = Theater::PtatrNew(kidParent);
    // Let the script know whether the open succeeded or failed
    vpcex->EnqueueCid(cidTheaterOpenCompleted, pvNil, pvNil, (pvNil != _ptatr));

    return fTrue;
}

/***************************************************************************
    Close the Theater
***************************************************************************/
bool Application::FCmdTheaterClose(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    AssertPo(_ptatr, 0);
    ReleasePpo(&_ptatr);

    return fTrue;
}

/***************************************************************************
    Clear the portfolio doc
***************************************************************************/
bool Application::FCmdPortfolioClear(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    _fniPortfolioDoc.SetNil();

    return fTrue;
}

/***************************************************************************
    Display the customized open file common dlg.
***************************************************************************/
bool Application::FCmdPortfolioOpen(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    Filename fni;
    bool fOKed;
    long idsTitle, idsFilterLabel, idsFilterExt;
    Filename fniUsersDir;
    PFilename pfni;
    ulong grfPrevType;
    ChunkNumber cnoWave = cnoNil;

    // Set up strings specific to this use of the portfolio.

    switch (pcmd->rglw[0])
    {
    case kpfPortOpenMovie:
        idsTitle = idsPortfOpenMovieTitle;
        idsFilterLabel = idsPortfMovieFilterLabel;
        idsFilterExt = idsPortfMovieFilterExt;

        grfPrevType = fpfPortPrevMovie;
        cnoWave = kwavPortOpenMovie;

        break;

    case kpfPortOpenSound:
        // Only display extensions appropriate to the type of sound being imported.
        idsTitle = idsPortfOpenSoundTitle;
        idsFilterLabel = idsPortfSoundFilterLabel;
        if (pcmd->rglw[1] == kidMidiGlass)
            idsFilterExt = idsPortfSoundMidiFilterExt;
        else
            idsFilterExt = idsPortfSoundWaveFilterExt;

        grfPrevType = fpfPortPrevMovie | fpfPortPrevSound;
        cnoWave = kwavPortOpenSound;

        break;

    case kpfPortOpenTexture:
        idsTitle = idsPortfOpenTextureTitle;
        idsFilterLabel = idsPortfTextureFilterLabel;
        idsFilterExt = idsPortfTextureFilterExt;

        grfPrevType = fpfPortPrevTexture;
        // Currently no audio for open texture, because we don't open texture yet.

        break;

    default:
        Bug("Unrecognized portfolio open type.");
        return fFalse;
    }

    // Prepare to set the initial directory for the portfolio if necessary.

    switch (pcmd->rglw[2])
    {
    case kpfPortDirUsers:

        // Initial directory will be the 'Users' directory.

        vapp.GetFniUsers(&fniUsersDir);
        pfni = &fniUsersDir;

        break;

    default:

        // Initial directory will be current directory.

        pfni = pvNil;

        break;
    }

    // Now display the open dlg. Script is informed of the outcome
    // of the portfolio from beneath FPortDisplayWithIds.
    fOKed = FPortDisplayWithIds(&fni, fTrue, idsFilterLabel, idsFilterExt, idsTitle, pvNil, pvNil, pfni, grfPrevType,
                                cnoWave);
    if (fOKed)
    {
        // User selected a file, so store fni for later use.
        SetPortfolioDoc(&fni);
    }

    return fTrue;
}

/******************************************************************************
    OnnDefVariable
        Retrieves the default onn for this app.  Gets the name from the app's
        string table.
************************************************************ PETED ***********/
long Application::OnnDefVariable(void)
{
    AssertBaseThis(0);

    if (_onnDefVariable == onnNil)
    {
        String stn;

        if (!FGetStnApp(idsDefaultFont, &stn) || !FGetOnn(&stn, &_onnDefVariable))
        {
            _onnDefVariable = Application_PAR::OnnDefVariable();
        }
    }
    return _onnDefVariable;
}

/******************************************************************************
    FGetOnn
        Application version of FGetOnn.  Mainly used so that we can easily report
        failure to the user if the font isn't around.  And no, I don't care
        that we'll call FGetOnn twice on failure...it's a failure case, and
        can stand to be slow.  :)  Maps the font on failure so that there's
        still a usable onn if the user doesn't want to mess with alternate
        solutions.

    Arguments:
        PString pstn   --  The font name
        long *ponn  --  takes the result

    Returns:  fTrue if the original font could be found.

************************************************************ PETED ***********/
bool Application::FGetOnn(PString pstn, long *ponn)
{
    AssertBaseThis(0);

    if (!vntl.FGetOnn(pstn, ponn))
    {
        if (!_fFontError)
        {
            PushErc(ercSocNoDefaultFont);
            _fFontError = fTrue;
        }
        *ponn = vntl.OnnMapStn(pstn);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Return the size of memory
***************************************************************************/
void Application::MemStat(long *pdwTotalPhys, long *pdwAvailPhys)
{
#ifdef WIN
    MEMORYSTATUS ms;
    ms.dwLength = size(MEMORYSTATUS);
    GlobalMemoryStatus(&ms);
    if (pvNil != pdwTotalPhys)
        *pdwTotalPhys = ms.dwTotalPhys;
    if (pvNil != pdwAvailPhys)
        *pdwAvailPhys = ms.dwAvailPhys;
#elif defined(KAUAI_SDL3)
    const auto total = KauaiSDL::SystemMemoryBytes();
    if (pdwTotalPhys) *pdwTotalPhys = static_cast<long>(total > klwMax ? klwMax : total);
    // SDL reports installed RAM, not currently available RAM. Zero makes the
    // old browser-cache heuristic conservative without inventing a value.
    if (pdwAvailPhys) *pdwAvailPhys = 0;
#else
    RawRtn();
#endif //! WIN
}

/******************************************************************************
    DypTextDef
        Retrieves the default dypFont for this app.  Gets the font size as
        a string from the app's string table, then converts it to the number
        value.
************************************************************ PETED ***********/
long Application::DypTextDef(void)
{
    AssertBaseThis(0);

    if (_dypTextDef == 0)
    {
        String stn;

        if (pvNil == _pgstApp || !FGetStnApp(idsDefaultDypFont, &stn) || !stn.FGetLw(&_dypTextDef) || _dypTextDef <= 0)
        {
            Warn("DypTextDef failed");
            _dypTextDef = Application_PAR::DypTextDef();
        }
    }
    return _dypTextDef;
}

/***************************************************************************
    Ask the user if they want to save changes to the given doc.
***************************************************************************/
tribool Application::TQuerySaveDoc(PDocumentBase pdocb, bool fForce)
{
    AssertThis(0);
    AssertPo(pdocb, 0);

    String stnName;
    long tpc;
    String stnBackup;
    long bk;
    tribool tResult;

    pdocb->GetName(&stnName);
    tpc = fForce ? ktpcQuerySave : ktpcQuerySaveWithCancel;
    if (!FGetStnApp(idsSaveChangesBkp, &stnBackup))
        stnBackup.SetNil();
    bk = fForce ? bkYesNo : bkYesNoCancel;

    tResult = TModal(vpapp->PcrmAll(), tpc, &stnBackup, bk, kstidQuerySave, &stnName);
    vpcex->EnqueueCid(cidQuerySaveDocResult, pvNil, pvNil, tResult);

    return tResult;
}

/***************************************************************************
    Quit routine.  May or may not initiate the quit sequence (depending
    on user input).
***************************************************************************/
void Application::Quit(bool fForce)
{
    AssertThis(0);

    bool tRet;
    String stnBackup;

    // If we already know we have to quit, or a modal topic is already
    // being  displayed, then do not query the user to quit here.
    if (_fQuit || vpappb->CactModal() > (_pcex != pvNil ? 1 : 0) || FInPortfolio())
    {
        // Make sure the app is visible to the user. Otherwise if this
        // return is preventing a system shutdown and we're minimized
        // on the taskbar, then the user won't know why the shutdown failed.
        if (!_fQuit)
            EnsureInteractive();
        return;
    }

    if (fForce)
    {
        // Force quit, don't ask the user if they want to quit.  But
        // do ask if they want to save their documents.
        DocumentBase::FQueryCloseAll(fdocForceClose);
        _fQuit = fTrue;

        return;
    }

    // If we're minimized, user is closing app from the taskbar.  Quit
    // without confirmation (we'll still confirm movie save if user has
    // a dirty doc)
    // if (_fMinimized)
    // {
    //     tRet = tYes;
    // }
    // else
    // {
    //     if (!FGetStnApp(idsConfirmExitBkp, &stnBackup))
    //         stnBackup.SetNil();
    //     tRet = TModal(vpapp->PcrmAll(), ktpcQueryQuit, &stnBackup, bkYesNo);
    // }

    // if (tRet == tYes)
    // {
        // User wants to quit, so shut down studio if necessary
        if (_pstdio == pvNil || _pstdio->FShutdown(fFalse))
            _fQuit = fTrue;
    // }
}

/***************************************************************************
    Return a pointer to the current movie, if any.  The movie could be in
    the studio, theater, or splot machine.
***************************************************************************/
PMovie Application::_Pmvie(void)
{
    AssertBaseThis(0);

    PMovie pmvie = pvNil;
    PSplotMachine psplot;

    if (_pstdio != pvNil && _pstdio->Pmvie() != pvNil)
        pmvie = _pstdio->Pmvie();
    else if (_ptatr != pvNil && _ptatr->Pmvie() != pvNil)
        pmvie = _ptatr->Pmvie();
    else if (Pkwa() != pvNil)
    {
        psplot = (PSplotMachine)Pkwa()->PgobFromCls(kclsSplotMachine);
        if (psplot != pvNil)
            pmvie = psplot->Pmvie();
    }
    return pmvie;
}

/****************************************
    Info dialog items (idits)
****************************************/
enum
{
    iditOkInfo,
    iditBuildInfo,
    iditWindowModeInfo,
    iditProductNameInfo,
    iditWireframeInfo,
    iditNoTextureInfo,
    iditPoseReadoutInfo,
#ifndef KAUAI_SDL3
    iditRenderModeInfo,
#endif
#ifdef KAUAI_SDL3
    iditIntegerScalingInfo,
    iditRenderSizeInfo,
#endif
    iditLimInfo
};

namespace
{
struct OptionsEdits
{
#ifdef KAUAI_SDL3
    std::unique_ptr<StudioPacks::Options> packs;
#endif
    PMovie movie = nullptr;
    LegacyLong renderItem = ivNil;
    bool renderRequested = false;
    bool previewed = false;
    bool wireframe = World::RenderWireframe();
    bool noTexture = World::NoTexture();
    bool flatShading = World::FlatShading();
    bool flatPreference = CurrentStudioPreferences().flatShading;
    bool showPose = MovieView::FShowPoseReadout();
    LegacyLong renderScale = World::DefaultRenderScale();
    LegacyLong originalChoice = 0, currentChoice = 0;
    void (*redraw)() = nullptr;
#ifdef KAUAI_SDL3
    int windowWidth = 0, windowHeight = 0, windowX = 0, windowY = 0;
    bool windowPositionKnown = false;
    KauaiSDL::WindowStatus windowState;
#endif

    void Redraw()
    {
        if (movie)
        {
            movie->Pbwld()->MarkDirty();
            movie->InvalViews();
        }
        redraw();
    }

    bool Restore()
    {
        if (!previewed)
            return true;
        bool restored = true;
        World::SetRenderWireframe(wireframe);
        World::SetNoTexture(noTexture);
        World::SetFlatShading(flatShading);
        CurrentStudioPreferences().flatShading = flatPreference;
        MovieView::SetShowPoseReadout(showPose);
        if (renderRequested)
        {
#ifdef KAUAI_SDL3
            restored = !movie || movie->Pbwld()->FSetRenderResolution(renderScale, originalChoice == 3);
            if (restored)
                World::SetDefaultRenderScale(renderScale);
            auto &host = KauaiSDL::Main();
            if (!windowState.fullscreen && windowWidth > 0 && windowHeight > 0)
            {
                host.SetMaximized(false, true);
                host.SetWindowSize(windowWidth, windowHeight);
                if (windowPositionKnown)
                    host.SetWindowPosition(windowX, windowY);
                if (windowState.maximized)
                    host.SetMaximized(true, true);
            }
#else
            restored = !movie || movie->Pbwld()->FSetHalfMode(false, originalChoice != 0);
#endif
            if (!restored)
                vpapp->TGiveAlertSz(PszLit("There is not enough memory to restore the previous rendering size."), bkOk,
                                    cokExclamation);
        }
        Redraw();
        return restored;
    }
};
bool OptionsChanged(PDialog dialog, LegacyLong *item, void *context)
{
    auto &edits = *static_cast<OptionsEdits *>(context);
#ifdef KAUAI_SDL3
    if (edits.packs)
    {
        bool handled;
        auto close = edits.packs->Handle(dialog, *item, handled);
        if (handled)
            return close;
    }
#endif
    if (*item < 0)
    {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
        mcp::Drain(true);
#endif
        return false;
    }
    const char *url = nullptr;
    if (*item == dialog->IditFromSit(IDC_INFO_ORIGINAL_REPO))
        url = "https://github.com/microsoft/Microsoft-3D-Movie-Maker";
    else if (*item == dialog->IditFromSit(IDC_INFO_FOREVER_REPO))
        url = "https://github.com/foone/3DMMForever";
    else if (*item == dialog->IditFromSit(IDC_INFO_RESEARCH_REPO))
        url = "https://github.com/willbr/3dmm-research";
    if (url)
    {
#ifdef KAUAI_SDL3
        SDL_OpenURL(url);
#elif defined(WIN)
        // win_headers.h retains the original SDK declarations without CONST.
        ShellExecuteA(vwig.hwndApp, "open", const_cast<char *>(url), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        return false;
    }
    DialogItem control;
    dialog->GetDit(*item, &control);
    const bool resolution = *item == edits.renderItem;
    if (resolution || control.sitMin == IDC_CHECK2 || control.sitMin == IDC_CHECK3 || control.sitMin == IDC_CHECK4 ||
        control.sitMin == IDC_FLAT_SHADING)
    {
        if (!dialog->FGetValues(*item, *item + 1))
            return false;
        if (resolution)
        {
            const auto choice = dialog->LwGetRadio(*item);
#ifdef KAUAI_SDL3
            const LegacyLong scale = choice == 3 ? 1 : 1L << choice;
            const bool applied = !edits.movie || edits.movie->Pbwld()->FSetRenderResolution(scale, choice == 3);
#else
            const bool applied = !edits.movie || edits.movie->Pbwld()->FSetHalfMode(false, choice != 0);
#endif
            if (!applied)
            {
                dialog->PutRadio(*item, edits.currentChoice);
                dialog->SetValues(*item, *item + 1);
                vpapp->TGiveAlertSz(PszLit("There is not enough memory for this rendering size."), bkOk,
                                    cokExclamation);
                return false;
            }
            edits.currentChoice = choice;
#ifdef KAUAI_SDL3
            auto &host = KauaiSDL::Main();
            if (!edits.renderRequested && edits.windowState.maximized)
            {
                // Remember the normal bounds too, so cancelling a preview
                // preserves where Restore will put an originally maximized window.
                host.SetMaximized(false, true);
                host.WindowSize(edits.windowWidth, edits.windowHeight);
                edits.windowPositionKnown = host.WindowPosition(edits.windowX, edits.windowY);
            }
            World::SetDefaultRenderScale(scale);
            host.FitWindowScale(static_cast<int>(scale));
#endif
            edits.renderRequested = true;
        }
        else
        {
            const bool checked = FPure(dialog->FGetCheck(*item));
            switch (control.sitMin)
            {
            case IDC_CHECK2:
                World::SetRenderWireframe(checked);
                break;
            case IDC_CHECK3:
                World::SetNoTexture(checked);
                break;
            case IDC_CHECK4:
                MovieView::SetShowPoseReadout(checked);
                break;
            case IDC_FLAT_SHADING:
                CurrentStudioPreferences().flatShading = checked;
                World::SetFlatShading(checked);
                break;
            }
        }
        edits.previewed = true;
        edits.Redraw();
    }
    return control.ditk == ditkButton;
}
struct PreferenceBinding
{
    PZString key;
    bool StudioPreferences::*field;
    LegacyLong control;
};
const PreferenceBinding kPreferences[] = {
    {PszLit("AllowMultipleInstances"), &StudioPreferences::allowMultipleInstances, IDC_ALLOW_MULTIPLE},
    {kszSkipSplashScreenValue, &StudioPreferences::skipStartupScreens, IDC_SKIP_STARTUP},
    {PszLit("MuteStartupSound"), &StudioPreferences::muteStartupSound, IDC_MUTE_STARTUP},
    {PszLit("MuteStudioSounds"), &StudioPreferences::muteStudioSounds, IDC_MUTE_STUDIO},
    {PszLit("BackgroundPlayback"), &StudioPreferences::backgroundPlayback, IDC_BACKGROUND_PLAYBACK},
    {PszLit("CutTransitions"), &StudioPreferences::cutTransitions, IDC_CUT_TRANSITIONS},
    {PszLit("DisableAudioFades"), &StudioPreferences::disableAudioFades, IDC_DISABLE_AUDIO_FADES},
    {PszLit("FlatShading"), &StudioPreferences::flatShading, IDC_FLAT_SHADING},
    {PszLit("NormalKeyRepeat"), &StudioPreferences::normalKeyRepeat, IDC_NORMAL_KEY_REPEAT},
};
} // namespace

void Application::_LoadPreferences()
{
    auto &preferences = CurrentStudioPreferences();
    for (const auto &binding : kPreferences)
    {
        if (binding.field == &StudioPreferences::skipStartupScreens)
        {
            // Preserve the original DWORD setting and its existing installations.
            LegacyLong skip = false;
            FGetSetRegKey(binding.key, &skip, size(skip), fregSetDefault);
            preferences.*binding.field = skip != 0;
            preferences.muteStartupSound = skip != 0;
        }
        else
            FGetSetRegKey(binding.key, &(preferences.*binding.field), size(bool), fregBinary);
    }
    FGetSetRegKey(PszLit("PlaybackRate"), &preferences.playbackRate, size(preferences.playbackRate), fregBinary);
    FGetSetRegKey(PszLit("PlaybackModifier"), &preferences.playbackModifier, size(preferences.playbackModifier),
                  fregBinary);
    if (preferences.playbackRate < 0 || preferences.playbackRate > 4)
        preferences.playbackRate = 0;
    if (preferences.playbackModifier < 0 || preferences.playbackModifier > 2)
        preferences.playbackModifier = 0;
    SetBackgroundPlayback(preferences.backgroundPlayback);
}

void Application::_ApplyPreferences()
{
    const auto &preferences = CurrentStudioPreferences();
    SetBackgroundPlayback(preferences.backgroundPlayback);
    World::SetFlatShading(preferences.flatShading);
    if (preferences.normalKeyRepeat)
        MovieView::RestoreKeyboardRepeat();
    if (vpsndm)
    {
        // Keep all three movie classes, including looping WAVE sounds; UI and
        // kidspace script sounds use other classes. Asset previews use class 0.
        vpsndm->SetSoundClassRange(preferences.muteStudioSounds ? sclNonLoop : klwMin,
                                   preferences.muteStudioSounds ? sclLoopMidi : klwMax);
        if (preferences.muteStudioSounds)
        {
            vpsndm->StopAll(sqnNil, ksclUISound);
        }
    }
}

/***************************************************************************
    Put up info dialog
***************************************************************************/
bool Application::FCmdInfo(PCommand pcmd)
{
    AssertThis(0);
    PMovie pmvie = pvNil;
    PDialog pdlg;
    long idit;
    bool fRunInWindowNew;
    String stn;
    String stnT;

    pmvie = _Pmvie();

    OptionsEdits edits;
    edits.movie = pmvie;
    edits.redraw = [] {
        vpapp->UpdateMarked();
#ifdef KAUAI_SDL3
        vpapp->_DrawStudioPresentation();
        KauaiSDL::Main().Present(true);
#endif
    };
    pdlg = Dialog::PdlgNew(dlidInfo, OptionsChanged, &edits);
    if (pvNil == pdlg)
        return fTrue;
    KauaiDialogs::TabLayout tabs;
    tabs.bounds = {10, 8, 330, 223};
    tabs.pages = {
        {IDC_TAB_DISPLAY,
         u"Display",
         {IDC_RADIO4, IDC_RADIO5, IDC_CHECK2, IDC_CHECK3, IDC_CHECK4, IDC_RADIO1, IDC_RADIO2, IDC_CHECK5,
          IDC_RENDER_ORIGINAL, IDC_RENDER_2X, IDC_RENDER_4X, IDC_RENDER_REDUCED, IDC_FLAT_SHADING, IDC_DISPLAY_GROUP,
          IDC_PERFORMANCE_GROUP, IDC_RENDERING_GROUP, IDC_INFO_RENDER_LABEL}},
        {IDC_TAB_PLAYBACK,
         u"Playback & sound",
         {IDC_MUTE_STUDIO, IDC_BACKGROUND_PLAYBACK, IDC_CUT_TRANSITIONS, IDC_DISABLE_AUDIO_FADES, IDC_NORMAL_KEY_REPEAT,
          IDC_PLAYBACK_GROUP, IDC_EDITING_GROUP, IDC_PLAYBACK_ORIGINAL, IDC_PLAYBACK_6, IDC_PLAYBACK_12,
          IDC_PLAYBACK_24, IDC_PLAYBACK_UNCAPPED, IDC_SPEED_NORMAL, IDC_SPEED_98_TO_XP, IDC_SPEED_XP_TO_98,
          IDC_TIMING_GROUP}},
        {IDC_TAB_STARTUP,
         u"Startup",
         {IDC_ALLOW_MULTIPLE, IDC_SKIP_STARTUP, IDC_MUTE_STARTUP, IDC_STARTUP_GROUP, IDC_STARTUP_NOTICE}},
        {IDC_TAB_ABOUT,
         u"About",
         {IDC_EDIT4, IDC_EDIT2, IDC_INFO_COPYRIGHT, IDC_INFO_ORIGINAL_LABEL, IDC_INFO_FOREVER_LABEL,
          IDC_INFO_ORIGINAL_REPO, IDC_INFO_FOREVER_REPO, IDC_INFO_RESEARCH_LABEL, IDC_INFO_RESEARCH_REPO}},
    };
#ifdef KAUAI_SDL3
    tabs.pages.insert(
        tabs.pages.end() - 1,
        {IDC_TAB_EXPANSIONS, u"Expansions", {IDC_PACK_NICK,          IDC_PACK_DORA,         IDC_PACK_FRANKIE,
                                             IDC_PACK_STATUS_NICK,   IDC_PACK_STATUS_DORA,  IDC_PACK_STATUS_FRANKIE,
                                             IDC_PACK_INSTALL_NICK,  IDC_PACK_INSTALL_DORA, IDC_PACK_INSTALL_FRANKIE,
                                             IDC_PACK_REMOVE_NICK,   IDC_PACK_REMOVE_DORA,  IDC_PACK_REMOVE_FRANKIE,
                                             IDC_PACK_SOURCE,        IDC_PACK_FOLDER,       IDC_PACK_ISO,
                                             IDC_PACK_BASE,          IDC_PACK_BASE_BROWSE,  IDC_PACK_FIXUPS,
                                             IDC_PACK_FIXUPS_BROWSE, IDC_PACK_NOTICE,       IDC_PACK_SOURCE_LABEL,
                                             IDC_PACK_BASE_LABEL,    IDC_PACK_FIXUPS_LABEL}});
    tabs.pages.insert(tabs.pages.end() - 1, {IDC_TAB_V3DMM,
                                             u"V3DMM",
                                             {IDC_VXP_LIST, IDC_VXP_APPLIED, IDC_VXP_APPLY, IDC_VXP_UNAPPLY,
                                              IDC_VXP_APPLY_ALL, IDC_VXP_UNAPPLY_ALL, IDC_VXP_IMPORT, IDC_VXP_STATUS,
                                              IDC_VXP_INSTALLED_LABEL, IDC_VXP_APPLIED_LABEL, IDC_VXP_NOTICE}});
    try
    {
        edits.packs.reset(new StudioPacks::Options);
        edits.packs->Initialize(pdlg);
    }
    catch (const std::exception &error)
    {
        _FGenericError(const_cast<char *>(error.what()));
        ReleasePpo(&pdlg);
        return fTrue;
    }
#endif
    if (!pdlg->FSetTabs(tabs))
    {
        ReleasePpo(&pdlg);
        return fTrue;
    }
#ifndef KAUAI_SDL3
    edits.renderItem = iditRenderModeInfo;
    edits.originalChoice = _fSlowCPU ? 1 : 0;
    pdlg->PutRadio(iditRenderModeInfo, edits.originalChoice);
#else
    edits.renderItem = iditRenderSizeInfo;
    edits.originalChoice = World::DefaultRenderScale() == 4 ? 2
                           : World::DefaultRenderScale() == 2 ? 1
                           : _fSlowCPU ? 3
                                       : 0;
    auto &host = KauaiSDL::Main();
    host.WindowSize(edits.windowWidth, edits.windowHeight);
    host.WindowState(edits.windowState);
    edits.windowPositionKnown = host.WindowPosition(edits.windowX, edits.windowY);
#endif
    edits.currentChoice = edits.originalChoice;

    stn.SetSz(PszLit("3DMMForever"));
#ifdef DEBUG
    stn.FAppendSz(PszLit(" (Debug)"));
#endif // DEBUG
    // Surface the build's pointer width so users (and bug reports) can tell
    // an x64 build from an x86 build at a glance.
    achar architecture64[] = PszLit(" (64-bit)"), architecture32[] = PszLit(" (32-bit)");
    stn.FAppendSz(sizeof(void *) == 8 ? architecture64 : architecture32);
    extern const char kszBuildStamp[], kszGitCommit[]; // generated each build (cmake/WriteBuildStamp.cmake)
    stnT.FFormatSz(PszLit("\r\nVersion %d.%d.%04d  built %z"), rmj, rmm, rup, kszBuildStamp);
    stn.FAppendStn(&stnT);
    pdlg->FPutStn(iditProductNameInfo, &stn);

    stn.FFormatSz(PszLit("Git commit: %z"), kszGitCommit);
    pdlg->FPutStn(iditBuildInfo, &stn);
    pdlg->PutRadio(iditWindowModeInfo, _fRunInWindow ? 1 : 0);
    pdlg->PutCheck(iditWireframeInfo, World::RenderWireframe());
    pdlg->PutCheck(iditNoTextureInfo, World::NoTexture());
    pdlg->PutCheck(iditPoseReadoutInfo, MovieView::FShowPoseReadout());
#ifdef KAUAI_SDL3
    pdlg->PutCheck(iditIntegerScalingInfo, KauaiSDL::Main().IntegerScaling());
    pdlg->PutRadio(iditRenderSizeInfo, edits.originalChoice);
#endif

    auto &preferences = CurrentStudioPreferences();
    pdlg->PutRadio(pdlg->IditFromSit(IDC_PLAYBACK_ORIGINAL), preferences.playbackRate);
    pdlg->PutRadio(pdlg->IditFromSit(IDC_SPEED_NORMAL), preferences.playbackModifier);
    for (const auto &binding : kPreferences)
        pdlg->PutCheck(pdlg->IditFromSit(binding.control), preferences.*binding.field);
    idit = pdlg->IditDo();
    if (idit != iditOkInfo)
    {
        if (!edits.Restore())
        {
#ifdef KAUAI_SDL3
            _fSlowCPU = edits.currentChoice == 3;
#else
            _fSlowCPU = edits.currentChoice != 0;
#endif
        }
        ReleasePpo(&pdlg);
        return fTrue;
    }

#ifdef KAUAI_SDL3
    bool fIntegerScaling = FPure(pdlg->FGetCheck(iditIntegerScalingInfo));
    KauaiSDL::Main().SetIntegerScaling(fIntegerScaling);
    FGetSetRegKey(kszIntegerScalingValue, &fIntegerScaling, size(bool), fregSetKey | fregBinary);
    const LegacyLong renderChoice = pdlg->LwGetRadio(iditRenderSizeInfo);
    const bool reduced = renderChoice == 3;
    LegacyLong renderScale = reduced ? 1 : 1L << renderChoice;
    bool renderApplied = !pmvie || pmvie->Pbwld()->FSetRenderResolution(renderScale, reduced);
    if (!renderApplied)
    {
        TGiveAlertSz(PszLit("There is not enough memory for this rendering size."), bkOk, cokExclamation);
        renderScale = World::DefaultRenderScale();
    }
    else
    {
        World::SetDefaultRenderScale(renderScale);
        _fSlowCPU = reduced;
        if (pmvie)
            pmvie->InvalViews();
    }
    FGetSetRegKey(kszRenderScaleValue, &renderScale, size(renderScale), fregSetKey | fregBinary);
#else
    if (FPure(_fSlowCPU) != FPure(pdlg->LwGetRadio(iditRenderModeInfo)))
    {
        PMovie pmvie;

        _fSlowCPU = !_fSlowCPU;
        pmvie = _Pmvie();
        if (pvNil != pmvie)
        {
            pmvie->Pbwld()->FSetHalfMode(fFalse, _fSlowCPU);
            pmvie->InvalViews();
        }
    }
#endif
    bool fSlowCPU = _fSlowCPU;
    FGetSetRegKey(kszBetterSpeedValue, &fSlowCPU, size(bool), fregSetKey | fregBinary);

    {
        bool fWireframeNew = FPure(pdlg->FGetCheck(iditWireframeInfo));
        bool fNoTextureNew = FPure(pdlg->FGetCheck(iditNoTextureInfo));
        bool fPoseReadoutNew = FPure(pdlg->FGetCheck(iditPoseReadoutInfo));
        bool fRenderChanged = fFalse;
        PMovie pmvieT = _Pmvie();

        if (fWireframeNew != FPure(World::RenderWireframe()))
        {
            World::SetRenderWireframe(fWireframeNew);
            fRenderChanged = fTrue;
        }
        if (fNoTextureNew != FPure(World::NoTexture()))
        {
            World::SetNoTexture(fNoTextureNew);
            fRenderChanged = fTrue;
        }
        if (fPoseReadoutNew != FPure(MovieView::FShowPoseReadout()))
        {
            MovieView::SetShowPoseReadout(fPoseReadoutNew);
            if (pvNil != pmvieT)
                pmvieT->InvalViews();
        }
        if (fRenderChanged && pvNil != pmvieT)
        {
            pmvieT->Pbwld()->MarkDirty();
            pmvieT->InvalViews();
        }
        FGetSetRegKey(kszWireframeValue, &fWireframeNew, size(bool), fregSetKey | fregBinary);
        FGetSetRegKey(kszNoTextureValue, &fNoTextureNew, size(bool), fregSetKey | fregBinary);
        FGetSetRegKey(kszShowPoseReadoutValue, &fPoseReadoutNew, size(bool), fregSetKey | fregBinary);
    }

    fRunInWindowNew = pdlg->LwGetRadio(iditWindowModeInfo);
    if (FPure(_fRunInWindow) != FPure(fRunInWindowNew))
    {
        if (!fRunInWindowNew)
        {
            // user wants to be fullscreen
            if (_FDisplaySwitchSupported())
            {
                _fRunInWindow = fFalse;
                _RebuildMainWindow();
                if (!_FSwitch640480(fTrue))
                {
                    _fRunInWindow = fTrue;
                    _RebuildMainWindow();
                }
            }
        }
        else
        {
            // user wants to run in a window.
            // Don't allow user to run in a window at 640x480 resolution.
            if (!_FDisplayIs640480() || _fSwitchedResolution)
            {
                _fRunInWindow = fTrue;
                _RebuildMainWindow();
                if (_FSwitch640480(fFalse))
                {
                    _fSwitchedResolution = fFalse;
                }
                else
                {
                    // back to fullscreen
                    _fRunInWindow = fFalse;
                    _RebuildMainWindow();
                }
            }
        }
    }
    bool fSwitchRes = !_fRunInWindow;

    FGetSetRegKey(kszSwitchResolutionValue, &fSwitchRes, size(fSwitchRes), fregSetKey | fregBinary);

    for (const auto &binding : kPreferences)
    {
        preferences.*binding.field = FPure(pdlg->FGetCheck(pdlg->IditFromSit(binding.control)));
        if (binding.field == &StudioPreferences::skipStartupScreens)
        {
            LegacyLong skip = preferences.*binding.field;
            FGetSetRegKey(binding.key, &skip, size(skip), fregSetKey);
        }
        else
            FGetSetRegKey(binding.key, &(preferences.*binding.field), size(bool), fregSetKey | fregBinary);
    }
    preferences.playbackRate = static_cast<int>(pdlg->LwGetRadio(pdlg->IditFromSit(IDC_PLAYBACK_ORIGINAL)));
    preferences.playbackModifier = static_cast<int>(pdlg->LwGetRadio(pdlg->IditFromSit(IDC_SPEED_NORMAL)));
    FGetSetRegKey(PszLit("PlaybackRate"), &preferences.playbackRate, size(preferences.playbackRate),
                  fregSetKey | fregBinary);
    FGetSetRegKey(PszLit("PlaybackModifier"), &preferences.playbackModifier, size(preferences.playbackModifier),
                  fregSetKey | fregBinary);
    _ApplyPreferences();
    if (pmvie)
    {
        pmvie->Pbwld()->MarkDirty();
        pmvie->InvalViews();
    }
#ifdef KAUAI_SDL3
    if (edits.renderRequested && renderApplied)
        KauaiSDL::Main().FitWindowScale(static_cast<int>(renderScale));
#endif
    ReleasePpo(&pdlg);
    return fTrue;
}

#ifdef WIN
#ifdef UNICODE
typedef LONG(WINAPI *PFNCHDS)(LPDEVMODEW lpDevMode, DWORD dwFlags);
const PZString kpszChds = PszLit("ChangeDisplaySettingsW");
#else
typedef LONG(WINAPI *PFNCHDS)(LPDEVMODEA lpDevMode, DWORD dwFlags);
const PZString kpszChds = PszLit("ChangeDisplaySettingsA");
#endif // !UNICODE

#ifdef BUG1920
#ifdef UNICODE
typedef BOOL(WINAPI *PFNENUM)(LPCWSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEW lpDevMode);
const PZString kpszEnum = PszLit("EnumDisplaySettingsW");
#else
typedef BOOL(WINAPI *PFNENUM)(LPCSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEA lpDevMode);
const PZString kpszEnum = PszLit("EnumDisplaySettingsA");
#endif // !UNICODE
#endif // BUG1920
#endif // WIN

#ifndef DM_BITSPERPEL
#define DM_BITSPERPEL 0x00040000L // from wingdi.h
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#endif //! DM_BITSPERPEL

#ifndef CDS_FULLSCREEN
#define CDS_FULLSCREEN 4
#endif //! CDS_FULLSCREEN

#ifndef DISP_CHANGE_SUCCESSFUL
#define DISP_CHANGE_SUCCESSFUL 0
#endif //! DISP_CHANGE_SUCCESSFUL

/***************************************************************************
    Determine if display resolution switching is supported
***************************************************************************/
bool Application::_FDisplaySwitchSupported(void)
{
    AssertBaseThis(0);

#ifdef KAUAI_SDL3
    return fTrue; // Desktop fullscreen does not require a display-mode switch.
#endif

#ifdef WIN
    DWORD dwVersion;
    byte bVersionMajor;

    dwVersion = GetVersion();
    bVersionMajor = (byte)(dwVersion & 0x000000ff);
    // FOONE: We don't care if it's NT or not, major version 4.0 means win95 and up or NT4.0 and up.
    if (bVersionMajor >= 4)
        return fTrue;
#endif // WIN
#ifdef MAC
    RawRtn();
#endif             // MAC
    return fFalse; // OS doesn't support res-switching
}

/***************************************************************************
    Switch to/from 640x480x8bit video mode.  It uses GetProcAddress so it
    can fail gracefully on systems that don't support
    ChangeDisplaySettings().
***************************************************************************/
bool Application::_FSwitch640480(bool fTo640480)
{
    AssertBaseThis(0);

#ifdef KAUAI_SDL3
    // SDL scales the original canvas in desktop fullscreen; never change the
    // desktop's resolution or palette depth.
    return !KauaiSDL::Main().Active() || KauaiSDL::Main().SetFullscreen(fTo640480 && !_fRunInWindow);
#endif

#ifdef WIN
#ifdef BUG1920
    bool fSetMode = fFalse, fSetBbp = fTrue;
    DWORD iModeNum;
    PFNENUM pfnEnum;
#endif // BUG1920
    HINSTANCE hLibrary;
    PFNCHDS pfnChds;
    DEVMODE devmode;
    long lwResult;

    hLibrary = LoadLibrary(PszLit("USER32.DLL"));
    if (0 == hLibrary)
        goto LFail;

    pfnChds = (PFNCHDS)GetProcAddress(hLibrary, kpszChds);
    if (pvNil == pfnChds)
        goto LFail;

#ifdef BUG1920
    pfnEnum = (PFNENUM)GetProcAddress(hLibrary, kpszEnum);
    if (pvNil == pfnEnum)
        goto LFail;
#endif // BUG1920

    if (fTo640480)
    {
        // Try to switch to 640x480
#ifdef BUG1920
    LRetry:
        for (iModeNum = 0; pfnEnum(NULL, iModeNum, &devmode); iModeNum++)
        {
            if ((fSetBbp ? devmode.dmBitsPerPel != 8 : devmode.dmBitsPerPel < 8) || devmode.dmPelsWidth != 640 ||
                devmode.dmPelsHeight != 480)
            {
                continue;
            }
            lwResult = pfnChds(&devmode, CDS_FULLSCREEN);
            if (lwResult == DISP_CHANGE_SUCCESSFUL)
            {
                fSetMode = fTrue;
                break;
            }
        }
        if (!fSetMode && fSetBbp)
        {
            fSetBbp = fFalse;
            goto LRetry;
        }

        if (fSetMode && _FDisplayIs640480())
#else  // BUG1920
        devmode.dmSize = size(DEVMODE);
        devmode.dmBitsPerPel = 8;
        devmode.dmPelsWidth = 640;
        devmode.dmPelsHeight = 480;
        devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        lwResult = pfnChds(&devmode, CDS_FULLSCREEN);
        if (DISP_CHANGE_SUCCESSFUL != lwResult)
        {
            // try without setting the bpp
            devmode.dmFields &= ~DM_BITSPERPEL;
            lwResult = pfnChds(&devmode, CDS_FULLSCREEN);
        }

        if (DISP_CHANGE_SUCCESSFUL == lwResult && _FDisplayIs640480())
#endif // !BUG1920
        {
            _fSwitchedResolution = fTrue;
            SetWindowPos(vwig.hwndApp, HWND_TOP, 0, 0, 640, 480, 0);
        }
        else
        {
            goto LFail;
        }
    }
    else
    {
        // Try to restore user's previous resolution
        lwResult = pfnChds(NULL, CDS_FULLSCREEN);
        if (DISP_CHANGE_SUCCESSFUL != lwResult)
            goto LFail;
    }
    FreeLibrary(hLibrary);
    return fTrue;
LFail:
    if (0 != hLibrary)
        FreeLibrary(hLibrary);
#endif // WIN
#ifdef MAC
    RawRtn();
#endif // MAC
    return fFalse;
}

/***************************************************************************
    Clean up routine - app is shutting down
***************************************************************************/
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
/***************************************************************************
    Override of ApplicationBase::TopOfLoop so the embedded MCP server can
    drain its inbound JSON-RPC queue once per kauai event-loop tick. Tools
    run synchronously on this thread, so they can safely touch HWNDs, GDI,
    and vpcex without thread marshalling.
***************************************************************************/
void Application::TopOfLoop(void)
{
    Application_PAR::TopOfLoop();
    mcp::Drain();
}
#endif // DEBUG

/***************************************************************************
    Clean up routine - app is shutting down
***************************************************************************/
void Application::_CleanUp(void)
{
#ifdef KAUAI_SDL3
    EndMovieOnlyPlayback();
    KauaiSDL::Main().SetPresentationPixels(nullptr);
    ReleasePpo(&_pgptStudioPresentation);
    ReleasePpo(&_pgptStudioSnapshot);
#endif
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    mcp::Shutdown();
#endif
    _FWriteUserData();
    ReleasePpo(&_pstdio);
    ReleasePpo(&_ptatr);
    ReleasePpo(&_pcfl);
    ReleasePpo(&_pmvieHandoff);
    ReleasePpo(&_pcrmAll);
    ReleasePpo(&_pglicrfBuilding);
    ReleasePpo(&_pglicrfStudio);
    ReleasePpo(&_pgstBuildingFiles);
    ReleasePpo(&_pgstStudioFiles);
    ReleasePpo(&_pgstSharedFiles);
    ReleasePpo(&_pgstApp);
    ReleasePpo(&_pkwa);
    World::CloseBRender();
    Application_PAR::_CleanUp();
    if (_fSwitchedResolution)
        _FSwitch640480(fFalse); // try to restore desktop
}

/***************************************************************************
    Bring the Studio forward before opening a modal dialog.
***************************************************************************/
void Application::EnsureInteractive(void)
{
#ifdef KAUAI_SDL3
    KauaiSDL::Main().EnsureInteractive();
#elif defined(WIN)
    if (_fMinimized || GetForegroundWindow() != vwig.hwndApp)
    {
        SetForegroundWindow(vwig.hwndApp);
        ShowWindow(vwig.hwndApp, SW_RESTORE);
    }
#else
    if (_fMinimized)
        RawRtn();
#endif
}

/***************************************************************************
    Put up a modal help balloon.
***************************************************************************/
tribool Application::TModal(PResourceCache prca, long tpc, PString pstnBackup, long bkBackup, long stidSubst, PString pstnSubst)
{
    AssertThis(0);
    AssertNilOrPo(prca, 0);

    long lwSelect;
    tribool tRet;
    String stn;

    // If app is minimized, restore it so user can see the dialog
    EnsureInteractive();

    if (ivNil != stidSubst)
    {
        AssertPo(pstnSubst, 0);
        if (!Pkwa()->Pstrg()->FPut(stidSubst, pstnSubst))
            return tMaybe;
    }

    if (pvNil == prca || !Pkwa()->FModalTopic(prca, tpc, &lwSelect))
    {
        // Backup plan: use old TGiveAlertSz.
        if (pvNil != pstnSubst)
            stn.FFormat(pstnBackup, pstnSubst);
        else
            stn = *pstnBackup;
        tRet = TGiveAlertSz(stn.Psz(), bkBackup, cokExclamation);
    }
    else
    {
        // Help balloon appeared ok, so translate returned value into
        // something we can use here.  lwSelect is returned as the
        // (1 based) index of the btn clicked, out of Yes/No/Cancel.
        switch (lwSelect)
        {
        case 1:
            tRet = tYes;
            break;
        case 2:
            tRet = tNo;
            break;
        case 3:
            tRet = tMaybe;
            break;
        default:
            Bug("TModal() balloon returned unrecognized selection");
            tRet = tMaybe;
        }
    }

    if (ivNil != stidSubst)
        Pkwa()->Pstrg()->Delete(stidSubst);
    UpdateMarked(); // make sure alert is drawn over
    return tRet;
}

/***************************************************************************
    Static function to prompt the user to insert the CD named pstnTitle
***************************************************************************/
bool Application::FInsertCD(const MissingSourceRequest &request)
{
    auto pstnTitle = request.title;
    AssertPo(pstnTitle, 0);
    if (BatchPlaybackTest())
    {
        char title[kcchMaxStz];
        pstnTitle->GetSzs(title);
        std::fprintf(stderr, "BATCH_MISSING_SOURCE %s\n", title);
        BatchPlaybackExit(4);
    }

    bool tRet = false;
#ifdef KAUAI_SDL3
    const auto utf8 = [](PString text) {
        const auto bytes = text->Cch() * sizeof(achar);
#ifdef UNICODE
        const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
        const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
        const auto count = KauaiText::ToUtf16(text->Psz(), bytes, encoding, nullptr, 0);
        std::vector<std::uint16_t> units(count);
        std::string result;
        if ((!bytes || count) && (!count || KauaiText::ToUtf16(text->Psz(), bytes, encoding, units.data(), count)))
            KauaiText::EncodeUtf8(units, result);
        return result;
    };
    StudioContent::MissingContent content{utf8(request.title), utf8(request.folder), utf8(request.contentRoot),
                                          request.sid};
    if (request.disabled)
    {
        KauaiDialogs::Session disabled;
        if (disabled.InitAlert(content.source +
                                   " is disabled. Enable it in Options > Expansions, then restart 3DMMForever.",
                               "Expansion disabled", KauaiDialogs::AlertButtons::ok))
            KauaiSDL::RunDialog(disabled);
        return fFalse;
    }
    if (request.asset)
    {
        content.hasAsset = true;
        content.assetType = request.asset->ctg;
        content.assetNumber = request.asset->cno;
    }
    KauaiDialogs::Session session;
    if (session.InitAlert(StudioContent::MissingContentText(content), "Missing movie content",
                          KauaiDialogs::AlertButtons::yesNoCancel))
    {
        for (auto &control : session.resource.controls)
        {
            if (control.id == 1)
                control.title.text = u"&Retry";
            if (control.id == 3)
                control.title.text = u"&Downloads";
        }
        session.context = &session;
        session.changed = [](void *context, std::int32_t &item) {
            const auto &dialog = *static_cast<KauaiDialogs::Session *>(context);
            if (item == dialog.Item(3))
            {
                SDL_OpenURL("https://3dmm.com/showthread.php?t=17038");
                return false;
            }
            return item == dialog.Item(1) || item == dialog.Item(2);
        };
        auto &host = KauaiSDL::Main();
        host.FlushInput(true, true);
        host.SuspendFocusEvents(true);
        tRet = KauaiSDL::RunDialog(session, host.WindowID(), [](void *) { KauaiSDL::Main().Present(); }) &&
               session.AlertAnswer() == KauaiDialogs::AlertResult::yes;
        if (!KauaiSDL::CurrentDialog().active)
            host.ResumeAfterModal();
        host.Present(true);
    }
#else
    String stnBackup;

    stnBackup = PszLit("I can't find the CD '%s'  Please insert it.  Should I look again?");
    tRet = vpapp->TModal(vpapp->PcrmAll(), ktpcQueryCD, &stnBackup, bkYesNo, kstidQueryCD, pstnTitle);
#endif

    /* Don't tell the user that they told us not to try again; they know
        that already */
    if (tRet == tNo)
        vapp._fDontReportInitFailure = fTrue;

    return (tYes == tRet);
}

/******************************************************************************
    DisplayErrors
        Displays any errors that happen to be on the error stack.  Call this
        when you don't want to wait until idle time to show errors.
************************************************************ PETED ***********/
void Application::DisplayErrors(void)
{
    AssertThis(0);
    if (BatchPlaybackTest() && vpers && vpers->Cerc())
    {
        for (LegacyLong index = 0; index < vpers->Cerc(); ++index)
            std::fprintf(stderr, "BATCH_ERROR_CODE %ld\n", static_cast<long>(vpers->ErcGet(index)));
        BatchPlaybackExit(3);
    }

    LegacyLong erc;
    String stnMessage;

    if (vpers->FPop(&erc))
    {
        String stnErr;

        // Mirror the popped erc to the crash log so the on-screen "internal
        // error: NNN" dialog text is recoverable from disk later. The dialog
        // path that follows (TModal + helptopic) is its own thing -- not
        // routed through _FGenericError or AppendCrashLog otherwise.
        {
            char szDiag[64];
            std::snprintf(szDiag, sizeof(szDiag), "DisplayErrors: erc=%ld", static_cast<long>(erc));
            AppendCrashLog("DISPLAY_ERROR", szDiag);
        }

        vpers->Clear();

        //
        // Convert to help topic number
        //
        switch (erc)
        {
        case ercOomHq:
            erc = ktpcercOomHq;
            break;

        case ercOomPv:
            erc = ktpcercOomPv;
            break;

        case ercOomNew:
            erc = ktpcercOomNew;
            break;

        /* We don't have any real specific information to present the user in
            these cases, plus they generally shouldn't come up anyway (a more
            informational error should have been pushed farther up the chain
            of error reporters) */
        case ercFilePerm:
        case ercFileOpen:
        case ercFileCreate:
        case ercFileSwapNames:
        case ercFileRename:
        case ercFniGeneral:
        case ercFniDelete:
        case ercFniRename:
        case ercFniMismatch:
        case ercFniDirCreate:
        case ercFneGeneral:
        case ercCflCreate:
        case ercCflSaveCopy:
        case ercSndmCantInit:
        case ercSndmPartialInit:
        case ercGfxCantDraw:
        case ercGfxCantSetFont:
        case ercGfxNoFontList:
        case ercGfxCantSetPalette:
        case ercDlgCantGetArgs:
        case ercDlgCantFind:
        case ercRtxdTooMuchText:
        case ercRtxdReadFailed:
        case ercRtxdSaveFailed:
        case ercCantOpenVideo:
        case ercMbmpCantOpenBitmap:

        /* In theory, these are obsolete. */
        case ercSocTdtTooLong:
        case ercSocWaveInProblems:
        case ercSocCreatedUserDir:

            /* Display a generic error message, with both the symbolic name
             * and a short description of what went wrong -- the bare numeric
             * code by itself ("Internal error: 103") was unactionable. The
             * mapping is hand-maintained here against framedef.h / utilerro.h
             * because there's no runtime erc -> name table in kauai. */
            {
                const char *pszName = "(unknown)";
                const char *pszDesc = "no description available";
                switch (erc)
                {
                case ercOomHq:              pszName = "ercOomHq";              pszDesc = "out of memory (HQ allocation)"; break;
                case ercOomPv:              pszName = "ercOomPv";              pszDesc = "out of memory (PV allocation)"; break;
                case ercOomNew:             pszName = "ercOomNew";             pszDesc = "out of memory (new)"; break;
                case ercFilePerm:           pszName = "ercFilePerm";           pszDesc = "file permission denied"; break;
                case ercFileOpen:           pszName = "ercFileOpen";           pszDesc = "couldn't open file"; break;
                case ercFileCreate:         pszName = "ercFileCreate";         pszDesc = "couldn't create file"; break;
                case ercFileSwapNames:      pszName = "ercFileSwapNames";      pszDesc = "couldn't swap file names"; break;
                case ercFileRename:         pszName = "ercFileRename";         pszDesc = "couldn't rename file"; break;
                case ercFniGeneral:         pszName = "ercFniGeneral";         pszDesc = "filename error (general)"; break;
                case ercFniDelete:          pszName = "ercFniDelete";          pszDesc = "couldn't delete file"; break;
                case ercFniRename:          pszName = "ercFniRename";          pszDesc = "couldn't rename file"; break;
                case ercFniMismatch:        pszName = "ercFniMismatch";        pszDesc = "filename mismatch"; break;
                case ercFniDirCreate:       pszName = "ercFniDirCreate";       pszDesc = "couldn't create directory"; break;
                case ercFneGeneral:         pszName = "ercFneGeneral";         pszDesc = "file enumerator error"; break;
                case ercCflCreate:          pszName = "ercCflCreate";          pszDesc = "couldn't create chunky file"; break;
                case ercCflSaveCopy:        pszName = "ercCflSaveCopy";        pszDesc = "couldn't save copy of chunky file"; break;
                case ercSndmCantInit:       pszName = "ercSndmCantInit";       pszDesc = "sound manager init failed"; break;
                case ercSndmPartialInit:    pszName = "ercSndmPartialInit";    pszDesc = "sound manager partial init"; break;
                case ercGfxCantDraw:        pszName = "ercGfxCantDraw";        pszDesc = "graphics: drawing failed"; break;
                case ercGfxCantSetFont:     pszName = "ercGfxCantSetFont";     pszDesc = "graphics: couldn't set font"; break;
                case ercGfxNoFontList:      pszName = "ercGfxNoFontList";      pszDesc = "graphics: no font list"; break;
                case ercGfxCantSetPalette:  pszName = "ercGfxCantSetPalette";  pszDesc = "graphics: couldn't set palette"; break;
                case ercDlgCantGetArgs:     pszName = "ercDlgCantGetArgs";     pszDesc = "dialog: couldn't get arguments"; break;
                case ercDlgCantFind:        pszName = "ercDlgCantFind";        pszDesc = "dialog: couldn't find resource"; break;
                case ercRtxdTooMuchText:    pszName = "ercRtxdTooMuchText";    pszDesc = "rich text: too much text"; break;
                case ercRtxdReadFailed:     pszName = "ercRtxdReadFailed";     pszDesc = "rich text: read failed"; break;
                case ercRtxdSaveFailed:     pszName = "ercRtxdSaveFailed";     pszDesc = "rich text: save failed"; break;
                case ercCantOpenVideo:      pszName = "ercCantOpenVideo";      pszDesc = "couldn't open video"; break;
                case ercMbmpCantOpenBitmap: pszName = "ercMbmpCantOpenBitmap"; pszDesc = "couldn't open masked bitmap"; break;
                case ercSocTdtTooLong:      pszName = "ercSocTdtTooLong";      pszDesc = "3D text too long"; break;
                case ercSocWaveInProblems:  pszName = "ercSocWaveInProblems";  pszDesc = "wave-in capture problems"; break;
                case ercSocCreatedUserDir:  pszName = "ercSocCreatedUserDir";  pszDesc = "created user directory"; break;
                }
                stnErr.FFormatSz(PszLit("%d (%z) -- %z"), erc, pszName, pszDesc);
                // Mirror to crash log so the on-screen text is recoverable later.
                {
                    char szLog[256];
                    std::snprintf(szLog, sizeof(szLog), "DisplayErrors -> generic: erc=%ld %s -- %s", static_cast<long>(erc), pszName, pszDesc);
                    AppendCrashLog("DISPLAY_ERROR", szLog);
                }
            }
            if (!vapp.Pkwa()->Pstrg()->FPut(kstidGenericError, &stnErr))
                stnErr.SetNil();
            erc = ktpcercSocGenericError;
            break;

        case ercFileGeneral:
            erc = ktpcercFileGeneral;
            break;

        case ercCflOpen:
            erc = ktpcercCflOpen;
            break;

        case ercCflSave:
            erc = ktpcercCflSave;
            break;

        case ercCrfCantLoad:
            erc = ktpcercCrfCantLoad;
            break;

        case ercFniHidden:
            erc = ktpcercFniHidden;
            break;

        case ercOomGdi:
            erc = ktpcercOomGdi;
            break;

        case ercDlgOom:
            erc = ktpcercDlgOom;
            break;

        case ercCantSave:
            erc = ktpcercCantSave;
            break;

        case ercSocSaveFailure:
            erc = ktpcercSocSaveFailure;
            break;

        case ercSocSceneSwitch:
            erc = ktpcercSocSceneSwitch;
            break;

        case ercSocSceneChop:
            erc = ktpcercSocSceneChop;
            break;

        case ercSocBadFile:
            erc = ktpcercSocBadFile;
            break;

        case ercSocNoTboxSelected:
            erc = ktpcercSocNoTboxSelected;
            break;

        case ercSocNoActrSelected:
            erc = ktpcercSocNoActrSelected;
            break;

        case ercSocNotUndoable:
            erc = ktpcercSocNotUndoable;
            break;

        case ercSocNoScene:
            erc = ktpcercSocNoScene;
            break;

        case ercSocBadVersion:
            erc = ktpcercSocBadVersion;
            break;

        case ercSocNothingToPaste:
            erc = ktpcercSocNothingToPaste;
            break;

        case ercSocBadFrameSlider:
            erc = ktpcercSocBadFrameSlider;
            break;

        case ercSocGotoFrameFailure:
            erc = ktpcercSocGotoFrameFailure;
            break;

        case ercSocDeleteBackFailure:
            erc = ktpcercSocDeleteBackFailure;
            break;

        case ercSocActionNotApplicable:
            erc = ktpcercSocActionNotApplicable;
            break;

        case ercSocCannotPasteThatHere:
            erc = ktpcercSocCannotPasteThatHere;
            break;

        case ercSocNoModlForChar:
            erc = ktpcercSocNoModlForChar;
            break;

        case ercSocNameTooLong:
            erc = ktpcercSocNameTooLong;
            break;

        case ercSocTboxTooSmall:
            erc = ktpcercSocTboxTooSmall;
            break;

        case ercSocNoThumbnails:
            erc = ktpcercSocNoThumbnails;
            break;

        case ercSocBadTdf:
            erc = ktpcercSocBadTdf;
            break;

        case ercSocNoActrMidi:
            erc = ktpcercSocNoActrMidi;
            break;

        case ercSocNoImportRollCall:
            erc = ktpcercSocNoImportRollCall;
            break;

        case ercSocNoNukeRollCall:
            erc = ktpcercSocNoNukeRollCall;
            break;

        case ercSocSceneSortError:
            erc = ktpcercSocSceneSortError;
            break;

        case ercSocCantInitSceneSort:
            erc = ktpcercSocCantInitSceneSort;
            break;

        case ercSocCantInitSplot:
            erc = ktpcercSocCantInitSplot;
            break;

        case ercSocNoWaveIn:
            erc = ktpcercSocNoWaveIn;
            break;

        case ercSocPortfolioFailed:
            erc = ktpcercSocPortfolioFailed;
            break;

        case ercSocCantInitStudio:
            erc = ktpcercSocCantInitStudio;
            break;

        case ercSoc3DWordCreate:
            erc = ktpcercSoc3DWordCreate;
            break;

        case ercSoc3DWordChange:
            erc = ktpcercSoc3DWordChange;
            break;

        case ercSocWaveSaveFailure:
            erc = ktpcercSocWaveSaveFailure;
            break;

        case ercSocNoSoundName:
            erc = ktpcercSocNoSoundName;
            break;

        case ercSndamWaveDeviceBusy:
            erc = ktpcercSndamWaveDeviceBusy;
            break;

        case ercSocNoKidSndsInMovie:
            erc = ktpcercSocNoKidSndsInMovie;
            break;

        case ercSocMissingMelanieDoc:
            erc = ktpcercSocMissingMelanieDoc;
            break;

        case ercSocCantLoadMelanieDoc:
            erc = ktpcercSocCantLoadMelanieDoc;
            break;

        case ercSocBadSoundFile:
            erc = ktpcercSocBadSoundFile;
            break;

        case ercSocBadSceneSound:
            erc = ktpcercSocBadSceneSound;
            break;

        case ercSocNoDefaultFont:
            erc = ktpcercSocNoDefaultFont;
            break;

        case ercSocCantCacheTag:
            erc = ktpcercSocCantCacheTag;
            break;

        case ercSocInvalidFilename:
            erc = ktpcercSocInvalidFilename;
            break;

        case ercSndMidiDeviceBusy:
            erc = ktpcercSndMidiDeviceBusy;
            break;

        case ercSocCantCopyMsnd:
            erc = ktpcercSocCantCopyMsnd;
            break;

        default:
            return;
        }

        FGetStnApp(idsOOM, &stnMessage);
        TModal(PcrmAll(), erc, &stnMessage, bkOk);
        if (stnErr.Cch() != 0)
            vapp.Pkwa()->Pstrg()->Delete(kstidGenericError);
    }
}

/***************************************************************************
    Idle routine.  Do ApplicationBase idle stuff, then report any runtime errors.
***************************************************************************/
bool Application::FCmdIdle(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fFileError = fFalse;
    PChunkyFile pcfl;

    Application_PAR::FCmdIdle(pcmd);

    /* Check all open chunky files for errors */
    for (pcfl = ChunkyFile::PcflFirst(); pcfl != pvNil; pcfl = (PChunkyFile)pcfl->PbllNext())
    {
        if (pcfl->ElError() != elNil)
        {
            fFileError = fTrue;
            pcfl->ResetEl();
        }
    }
    /* Ensure that we report *something* if there was a file error */
    if (fFileError && vpers->Cerc() == 0)
        PushErc(ercFileGeneral);

    DisplayErrors();

    return fTrue;
}

#ifdef WIN
/***************************************************************************
    Tell another instance of the app to open a document.
***************************************************************************/
bool Application::_FSendOpenDocCmd(HWND hwnd, PFilename pfniUserDoc)
{
    AssertBaseThis(0);
    Assert(pvNil != hwnd, "bad hwnd");
    AssertPo(pfniUserDoc, ffniFile);

    String stnUserDoc;
    String stn;
    Filename fniTemp;
    PFileObject pfil = pvNil;
    DataBlock blck;
    DWORD dwProcId;

    // Write filename to 3DMMOpen.tmp in the temp dir
    pfniUserDoc->GetStnPath(&stnUserDoc);
    if (!fniTemp.FGetTemp())
        goto LFail;
    stn = kpszOpenFile;
    if (!fniTemp.FSetLeaf(&stn, kftgTemp))
        goto LFail;
    if (tYes == fniTemp.TExists())
    {
        // replace any existing open-doc request
        if (!fniTemp.FDelete())
            goto LFail;
    }
    pfil = FileObject::PfilCreate(&fniTemp);
    if (pvNil == pfil)
        goto LFail;
    if (!pfil->FSetFpMac(stnUserDoc.CbData()))
        goto LFail;
    blck.Set(pfil, 0, stnUserDoc.CbData());
    if (!stnUserDoc.FWrite(&blck, 0))
        goto LFail;
    blck.Free(); // so it doesn't reference pfil anymore
    ReleasePpo(&pfil);
#ifdef WIN
    dwProcId = GetWindowThreadProcessId(hwnd, NULL);
    PostThreadMessage(dwProcId, WM_USER, klwOpenDoc, 0);
#else
    RawRtn();
#endif //! WIN
    return fTrue;
LFail:
    blck.Free(); // so it doesn't reference pfil anymore
    ReleasePpo(&pfil);
    return fFalse;
}
#endif // WIN

#ifdef WIN
/***************************************************************************
    Process a request (from another instance of the app) to open a document
***************************************************************************/
bool Application::_FProcessOpenDocCmd(void)
{
    AssertBaseThis(0);

    String stnUserDoc;
    String stn;
    Filename fniTemp;
    PFileObject pfil = pvNil;
    Filename fniUserDoc;
    DataBlock blck;

    // Find the temp file
    if (!fniTemp.FGetTemp())
        return fFalse;
    stn = kpszOpenFile;
    if (!fniTemp.FSetLeaf(&stn, kftgTemp))
        return fFalse;
    if (tYes != fniTemp.TExists())
    {
        Bug("Got a ProcessOpenDocCmd but there's no temp file!");
        return fFalse;
    }

    // See if we can accept open document commands now: If accelerators
    // are enabled, then ctrl-o is enabled, so open-document commands are
    // acceptable.
    if (_cactDisable > 0 || CactModal() > 0)
        goto LFail;

    // Read the document filename from temp file
    pfil = FileObject::PfilOpen(&fniTemp);
    if (pvNil == pfil)
        goto LFail;
    blck.Set(pfil, 0, pfil->FpMac());
    if (!stnUserDoc.FRead(&blck, 0))
        goto LFail;
    blck.Free(); // so it doesn't reference pfil anymore
    ReleasePpo(&pfil);

    if (!fniUserDoc.FBuildFromPath(&stnUserDoc))
        goto LFail;
    if (pvNil == _pstdio)
    {
        SetPortfolioDoc(&fniUserDoc);
        vpcex->EnqueueCid(cidLoadStudioDoc); // this will load the portfolio doc
    }
    else
    {
        // ignore failure
        _pstdio->FLoadMovie(&fniUserDoc);
    }

    fniTemp.FDelete(); // ignore failure
    return fTrue;
LFail:
    blck.Free(); // so it doesn't reference pfil anymore
    ReleasePpo(&pfil);
    fniTemp.FDelete(); // ignore failure
    return fFalse;
}
#endif // WIN

#if defined(WIN) || defined(KAUAI_SDL3)
/***************************************************************************
    Override standard _FGetNextEvt to catch WM_USER event.  Otherwise
    the event will get thrown away, because the event's hwnd is nil.
***************************************************************************/
#ifdef KAUAI_SDL3
bool Application::FBeginMovieOnlyPlayback()
{
    auto movie = _pstdio ? _pstdio->Pmvie() : nullptr;
    auto view = movie ? static_cast<PMovieView>(movie->PddgGet(0)) : nullptr;
    auto &host = KauaiSDL::Main();
    if (_fMovieOnly || !view || !movie->Pscen())
        return false;
    view->GetRc(&_rcMovieOnly, cooHwnd);
    const auto scale = movie->Pbwld()->RenderScale();
    if (scale > 1 && !view->PgptSuperSizeFrame(true, true))
    {
        return false;
    }
    KauaiPixels::DamageRect rectangle{_rcMovieOnly.xpLeft, _rcMovieOnly.ypTop, _rcMovieOnly.xpRight,
                                      _rcMovieOnly.ypBottom};
    host.SetPresentationPixels(nullptr);
    if (!host.SetPresentationSource(&rectangle))
    {
        return false;
    }
    _fMovieOnly = true;
    HideCurs();
    return true;
}
void Application::EndMovieOnlyPlayback()
{
    if (!_fMovieOnly)
        return;
    _fMovieOnly = false;
    auto &host = KauaiSDL::Main();
    host.SetPresentationSource(nullptr);
    ShowCurs();
    host.Invalidate();
}
void Application::DrawHostOverlay(PGraphicsEnvironment pgnv)
{
    if (_fDrawingStudioPresentation)
        return;
    if (!_fMovieOnly)
    {
        _DrawStudioPresentation();
        return;
    }
    auto movie = _pstdio ? _pstdio->Pmvie() : nullptr;
    if (!_fMovieOnly || !movie)
        return;
    auto &host = KauaiSDL::Main();
    if (movie->FUserPaused())
        DrawMoviePause(pgnv);
    auto view = static_cast<PMovieView>(movie->PddgGet(0));
    const bool indexed = !movie->FUserPaused();
    auto frame = view ? view->PgptSuperSizeFrame(false, indexed) : nullptr;
    if (frame && !_fMovieTransition)
    {
        const auto scale = movie->Pbwld()->RenderScale();
        RC target(0, 0, _rcMovieOnly.Dxp() * scale, _rcMovieOnly.Dyp() * scale);
        if (movie->FUserPaused())
        {
            GraphicsEnvironment high(frame);
            high.SetRcSrc(&_rcMovieOnly);
            high.SetRcDst(&target);
            DrawMoviePause(&high);
        }
        auto pixels = frame->PrgbLockPixels();
        if (indexed)
        {
            Color palette[256];
            if (frame->FGetIndexedPalette(palette))
                host.SetIndexedPresentationPixels(pixels, target.Dxp(), target.Dyp(), frame->CbRow(), palette, false,
                                                  view->SuperSizeFrameSerial());
        }
        else
            host.SetPresentationPixels(pixels, target.Dxp(), target.Dyp(), frame->CbRow());
        frame->Unlock();
    }
    else
    {
        host.SetPresentationPixels(nullptr);
    }
}
void Application::_DrawStudioPresentation()
{
    auto &host = KauaiSDL::Main();
    auto movie = _pstdio ? _pstdio->Pmvie() : nullptr;
    auto view = movie ? static_cast<PMovieView>(movie->PddgGet(0)) : nullptr;
    auto root = GraphicsObject::PgobFromHwnd(static_cast<CoreNative::WindowHandle>(host.WindowToken()));
    const auto scale = movie ? movie->Pbwld()->RenderScale() : 1;
    if (!view || !root || scale == 1 || _fMovieTransition)
    {
        host.SetPresentationPixels(nullptr);
        if (scale == 1)
        {
            ReleasePpo(&_pgptStudioPresentation);
            ReleasePpo(&_pgptStudioSnapshot);
        }
        return;
    }
    RC native;
    root->GetRc(&native, cooLocal);
    RC high(0, 0, native.Dxp() * scale, native.Dyp() * scale);
    bool redraw = !_pgptStudioPresentation || high != _rcStudioPresentation;
    if (redraw)
    {
        auto port = GraphicsPort::PgptNewOffscreen(&high, 32);
        auto snapshot = GraphicsPort::PgptNewOffscreen(&native, 32);
        if (!port || !snapshot)
        {
            ReleasePpo(&port);
            ReleasePpo(&snapshot);
            host.SetPresentationPixels(nullptr);
            PushErc(ercGfxCantDraw);
            return;
        }
        host.SetPresentationPixels(nullptr);
        ReleasePpo(&_pgptStudioPresentation);
        _pgptStudioPresentation = port;
        ReleasePpo(&_pgptStudioSnapshot);
        _pgptStudioSnapshot = snapshot;
        _rcStudioPresentation = high;
    }
    RC viewport;
    view->GetRc(&viewport, cooHwnd);
    viewport.FIntersect(&native);
    // Keep the completed Studio image, repainting only changed areas. Splitting
    // the controls around the movie avoids expanding a frame-counter change
    // into a repaint of the whole window. DrawTree retains normal draw order.
    RC bands[5] = {RC(native.xpLeft, native.ypTop, native.xpRight, viewport.ypTop),
                   RC(native.xpLeft, viewport.ypBottom, native.xpRight, native.ypBottom),
                   RC(native.xpLeft, viewport.ypTop, viewport.xpLeft, viewport.ypBottom),
                   RC(viewport.xpRight, viewport.ypTop, native.xpRight, viewport.ypBottom), viewport};
    RC dirty[5];
    for (auto &rectangle : dirty)
        rectangle.Zero();
    redraw = redraw || _studioViewGrid != view->Grid();
    const auto canvas = root->Pgpt()->PrgbLockPixels();
    auto snapshot = _pgptStudioSnapshot->PrgbLockPixels();
    const auto bytes = native.Dxp() * 4;
    for (long y = 0; y < native.Dyp(); ++y)
    {
        const auto source = canvas + y * root->Pgpt()->CbRow();
        auto previous = snapshot + y * _pgptStudioSnapshot->CbRow();
        if (redraw || std::memcmp(source, previous, bytes) != 0)
        {
            long first = 0, last = native.Dxp();
            if (!redraw)
            {
                while (first < last && std::memcmp(source + first * 4, previous + first * 4, 4) == 0)
                    ++first;
                while (last > first && std::memcmp(source + (last - 1) * 4, previous + (last - 1) * 4, 4) == 0)
                    --last;
            }
            RC row(first, y, last, y + 1);
            for (int i = 0; i < 5; ++i)
            {
                RC clip = row;
                if (clip.FIntersect(&bands[i]))
                    dirty[i].Union(&clip);
            }
            std::memcpy(previous, source, bytes);
        }
    }
    _pgptStudioSnapshot->Unlock();
    root->Pgpt()->Unlock();
    // A scaled font's ink can extend past the changed pixels of its 1x
    // counterpart. Repaint complete counter cells rather than clipping larger
    // digits to the small canvas's ink bounds.
    for (auto hid : {kidFrameText, kidSceneText})
        if (auto counter = _pkwa->PgobFromHid(hid))
        {
            RC bounds;
            counter->GetRc(&bounds, cooHwnd);
            for (int i = 0; i < 4; ++i)
            {
                RC changed = dirty[i];
                if (changed.FIntersect(&bounds))
                {
                    RC repaint = bounds;
                    if (repaint.FIntersect(&bands[i]))
                        dirty[i].Union(&repaint);
                }
            }
        }
    if (!dirty[4].FEmpty() && _studioMovieSerial == view->SuperSizeFrameSerial())
        view->PgptSuperSizeFrame(true, true); // Direct editing/caret changes.
    if (_studioMovieSerial != view->SuperSizeFrameSerial())
        dirty[4] = viewport;
    bool changed = false;
    _fDrawingStudioPresentation = true;
    for (auto &rectangle : dirty)
        if (!rectangle.FEmpty())
        {
            root->DrawTree(_pgptStudioPresentation, &high, &rectangle, fgobUseVis | fgobUseCache);
            changed = true;
        }
    _fDrawingStudioPresentation = false;
    _studioMovieSerial = view->SuperSizeFrameSerial();
    _studioViewGrid = view->Grid();
    if (changed && ++_studioPresentationSerial == 0)
        ++_studioPresentationSerial;
    auto pixels = _pgptStudioPresentation->PrgbLockPixels();
    host.SetPresentationPixels(pixels, high.Dxp(), high.Dyp(), _pgptStudioPresentation->CbRow(), true,
                               _studioPresentationSerial);
    _pgptStudioPresentation->Unlock();
}
void Application::DrawMoviePause(PGraphicsEnvironment pgnv)
{
    const long centerX = (_rcMovieOnly.xpLeft + _rcMovieOnly.xpRight) / 2;
    const long centerY = (_rcMovieOnly.ypTop + _rcMovieOnly.ypBottom) / 2;
    RC box(_rcMovieOnly.xpLeft + 24, centerY - 55, _rcMovieOnly.xpRight - 24, centerY + 55);
    pgnv->FillRc(&box, kacrBlack);
    String title(PszLit("Paused")), prompt(PszLit("press space to resume"));
    pgnv->SetFont(OnnDefVariable(), fontBold, 32, tahCenter, tavCenter);
    pgnv->DrawStn(&title, centerX, centerY - 20, kacrWhite);
    pgnv->SetFont(OnnDefVariable(), fontNil, 22, tahCenter, tavCenter);
    pgnv->DrawStn(&prompt, centerX, centerY + 23, kacrWhite);
}
void Application::_DispatchEvt(PEVT event)
{
    if (_fMovieOnly && (event->kind == KauaiInput::Kind::buttonDown || event->kind == KauaiInput::Kind::buttonUp))
    {
        auto movie = _pstdio ? _pstdio->Pmvie() : nullptr;
        if (!movie || movie->FUserPaused() || !_rcMovieOnly.FPtIn(long(event->x), long(event->y)))
            return;
    }
    Application_PAR::_DispatchEvt(event);
}
bool Application::_FDispatchWindowShortcut(const EVT &event)
{
    if (_fMovieOnly)
    {
        auto movie = _pstdio ? _pstdio->Pmvie() : nullptr;
        if (event.kind == KauaiInput::Kind::character)
            return true;
        if (event.kind == KauaiInput::Kind::keyDown && event.modifiers == 0 && (event.key == 32 || event.key == 27))
        {
            if (!event.repeat && movie)
            {
                if (event.key == 32)
                    movie->SetUserPaused(!movie->FUserPaused());
                else if (movie->FPlaying())
                    movie->Play();
                else
                    EndMovieOnlyPlayback();
            }
            return true;
        }
        // Keep hidden editor commands out of movie-only playback.
        if (event.kind == KauaiInput::Kind::keyDown && !(event.key == 13 && event.modifiers == KauaiSDL::alt))
            return true;
    }
    if (event.kind != KauaiInput::Kind::keyDown || event.key != 13 || event.modifiers != KauaiSDL::alt)
        return false;
    // Consume repeats so holding Alt+Enter cannot repeatedly switch modes.
    if (!event.repeat)
    {
        KauaiSDL::WindowStatus state;
        auto &host = KauaiSDL::Main();
        if (host.WindowState(state) && host.SetFullscreen(!state.fullscreen))
            _fRunInWindow = state.fullscreen;
        else
            fprintf(stderr, "Cannot toggle Studio SDL3 fullscreen: %s\n", host.Error());
    }
    return true;
}
bool Application::_FDispatchShortcut(const EVT &event)
{
    if (event.kind != KauaiInput::Kind::keyDown) return false;
    const auto command = StudioInput::ShortcutCommand(event.key, event.modifiers, _cactDisable > 0);
    if (!command) return false;
    if (vpcex) vpcex->EnqueueCid(command);
    return true;
}
#endif
bool Application::_FGetNextEvt(PEVT pevt)
{
    AssertThis(0);
    AssertVarMem(pevt);

    if (!Application_PAR::_FGetNextEvt(pevt))
        return fFalse;
#ifdef KAUAI_SDL3
    return fTrue;
#else
    if (pevt->message != WM_USER || pevt->wParam != klwOpenDoc)
        return fTrue;
    _FProcessOpenDocCmd(); // ignore failure
    return fFalse;         // we've handled the WM_USER event
#endif
}
#endif // WIN

/***************************************************************************
    Override default _FastUpdate to optionally skip offscreen buffer
***************************************************************************/
void Application::_FastUpdate(PGraphicsObject pgob, PRegion pregnClip, ulong grfapp, PGraphicsPort pgpt)
{
    AssertBaseThis(0);

    PMovie pmvie;

    pmvie = _Pmvie();

    if (_fOnscreenDrawing && pvNil != pmvie && pmvie->FPlaying() &&
        pmvie->Pscen()->Nfrm() != pmvie->Pscen()->NfrmFirst())
    {
        Application_PAR::_FastUpdate(pgob, pregnClip, grfapp | fappOnscreen, pgpt);
    }
    else
    {
        Application_PAR::_FastUpdate(pgob, pregnClip, grfapp, pgpt);
    }
}

#if defined(WIN) || defined(KAUAI_SDL3)
/***************************************************************************
    Override default UpdateHwnd to optionally skip offscreen buffer
***************************************************************************/
void Application::UpdateHwnd(CoreNative::WindowHandle hwnd, RC *prc, ulong grfapp)
{
    AssertBaseThis(0); // Application may not be completely valid

    PMovie pmvie;

    pmvie = _Pmvie();

    if (_fOnscreenDrawing && pvNil != pmvie && pmvie->FPlaying() &&
        pmvie->Pscen()->Nfrm() != pmvie->Pscen()->NfrmFirst())
    {
        Application_PAR::UpdateHwnd(hwnd, prc, grfapp | fappOnscreen);
    }
    else
    {
        Application_PAR::UpdateHwnd(hwnd, prc, grfapp);
    }
}
#endif // WIN

#ifdef WIN

#ifndef WM_DISPLAYCHANGE
#define WM_DISPLAYCHANGE 0x007E // from winuser.h
#endif                          // !WM_DISPLAYCHANGE

/***************************************************************************
    Handle Windows messages for the main app window. Return true iff the
    default window proc should _NOT_ be called.
***************************************************************************/
bool Application::_FFrameWndProc(HWND hwnd, uint wm, WPARAM wParam, LPARAM lw, LRESULT *plwRet)
{
    AssertBaseThis(0);
    AssertVarMem(plwRet);

#ifdef KAUAI_SDL3
    if (KauaiSDL::Main().IsWindow(hwnd))
    {
        if (wm == WM_SIZE)
        {
            _fMinimized = wParam == SIZE_MINIMIZED;
            return Application_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
        }
        if (wm == WM_DISPLAYCHANGE)
        {
            *plwRet = 0;
            return fTrue; // SDL handles display changes and renderer resizing.
        }
    }
#endif

    switch (wm)
    {
    case WM_ERASEBKGND:
        // Tell windows that we handled the Erase so it doesn't do one.
        // In general we don't want to erase our background ahead of time.
        // This prevents AVIs from flashing.
        *plwRet = fTrue;
        return fTrue;

    case WM_SIZE: {
        bool fRet;
        long lwStyle;

        fRet = Application_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
        lwStyle = GetWindowLong(hwnd, GWL_STYLE);
        lwStyle &= ~WS_MAXIMIZEBOX;
        if (wParam == SIZE_MINIMIZED)
        {
            _fMinimized = fTrue;
            if (vpcex != pvNil)
                lwStyle |= WS_SYSMENU;
            else
                lwStyle &= ~WS_SYSMENU;
        }
        else if (!_fRunInWindow)
            lwStyle &= ~WS_SYSMENU;
        SetWindowLong(hwnd, GWL_STYLE, lwStyle);
        if (wParam == SIZE_RESTORED)
        {
            if (_fMainWindowCreated)
                _RebuildMainWindow();
            if (_fSwitchedResolution && _fMinimized)
            {
                if (!_FDisplayIs640480())
                    _FSwitch640480(fTrue);
            }
            ShowWindow(vwig.hwndApp, SW_RESTORE); // restore app window
            _fMinimized = fFalse;
        }
        return fRet;
    }
    case WM_DISPLAYCHANGE:
        // Note that we don't need to do any of this if we're closing down
        if (_fQuit)
            break;

        if (_FDisplayIs640480())
        {
            _fRunInWindow = fFalse;
            _RebuildMainWindow();
        }
        else
        {
            // We're not running at 640x480 resolution now. Current design is that
            // if we switch from 640x480 to higher while the app is minimized, the
            // app it to still be full screen when restored. Therefore we don't
            // need to change _fRunInWindow here, as that we remain the same as
            // before the res switch, (as will the Windows properties for the app).
            // All we need to is make a note that we're no longer running in the
            // current windows settings resolution if we're running in full screen.

            if (!_fRunInWindow)
            {
                _fSwitchedResolution = fTrue;

                // If we're not minimized then we must switch to 640x480 resolution.
                // Don't switch res unless we're the active app window

                if (!_fMinimized && GetForegroundWindow() == vwig.hwndApp)
                {
                    if (!_FSwitch640480(fTrue))
                        _fSwitchedResolution = fFalse;
                }
            }

            // Call rebuild now to make sure the app window gets positioned
            // at the centre of the screen. Note that none of the other
            // window attributes will change beneath _RebuildMainWindow.
            if (!_fMinimized)
                _RebuildMainWindow();
        }
        return fTrue;

    case WM_INITMENUPOPUP: {
        // Disable the Close menu item if we are displaying a modal topic.
        // The user can't exit until a modal topic is dismissed.

        bool fDisableClose = (vpappb->CactModal() > (_pcex != pvNil ? 1 : 0));

        EnableMenuItem((HMENU)wParam, SC_CLOSE,
                       MF_BYCOMMAND | (fDisableClose ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));

        break;
    }
    }

    if ((_pstdio == pvNil) || (_pstdio->Pmvie() == pvNil))
    {
        return Application_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
    }

    switch (wm)
    {
    case WM_QUERY_EXISTS:
        *plwRet = _pstdio->Pmvie()->LwQueryExists(wParam, lw);
        return (fTrue);

    case WM_QUERY_LOCATION:
        *plwRet = _pstdio->Pmvie()->LwQueryLocation(wParam, lw);
        return (fTrue);

    case WM_SET_MOVIE_POS:
        *plwRet = _pstdio->Pmvie()->LwSetMoviePos(wParam, lw);
        return (fTrue);

    default:
        return Application_PAR::_FFrameWndProc(hwnd, wm, wParam, lw, plwRet);
    }
}
#endif // WIN

/***************************************************************************
 *
 * Returns whether or not screen savers should be allowed.
 *
 * Parameters:
 *  None
 *
 * Returns:
 *  fTrue  - Screen savers should be allowed
 *  fFalse - Screen savers should be blocked
 *
 **************************************************************************/
bool Application::FAllowScreenSaver(void)
{
    AssertBaseThis(0);

    // Disable the screen saver if...
    // 1. We're going to autominimize if a screen saver starts. Otherwise
    //    the user would be confused when they get back to the machine.
    // 2. We've switched resolutions, (ie we're full screen in a > 640x480 mode).
    //    Otherwise the screen save only acts on a portion of the screen.

    return !_FDisplayIs640480() && !_fSwitchedResolution;
}

/***************************************************************************
    Disable the application accelerators
***************************************************************************/
void Application::DisableAccel(void)
{
    AssertBaseThis(0); // Gets called from destructors

#ifndef KAUAI_SDL3
    if (_cactDisable == 0)
    {
#ifdef WIN
        _haccel = vwig.haccel;
        vwig.haccel = _haccelGlobal;
#else
        RawRtn();
#endif
    }
#endif

    _cactDisable++;
}

/***************************************************************************
    Enable the application accelerators
***************************************************************************/
void Application::EnableAccel(void)
{
    AssertBaseThis(0); // Gets called from destructors
    Assert(_cactDisable > 0, "Enable called w/o a disable");

    _cactDisable--;

#ifndef KAUAI_SDL3
    if (_cactDisable == 0)
    {
#ifdef WIN
        vwig.haccel = _haccel;
#else
        RawRtn();
#endif
    }
#endif
}

/***************************************************************************
 *
 * Handle disable accelerator command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Application::FCmdDisableAccel(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    DisableAccel();
    return fTrue;
}

/***************************************************************************
 *
 * Handle enable accelerator command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Application::FCmdEnableAccel(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    EnableAccel();
    return fTrue;
}

/******************************************************************************
    FCmdInvokeSplot
        Invokes the splot machine.

    Arguments:
        PCommand pcmd
            rglw[0]  --  contains the GraphicsObject id of the parent of the Splot machine
            rglw[1]  --  contains the GraphicsObject id of the Splot machine itself

    Returns: fTrue always

************************************************************ PETED ***********/
bool Application::FCmdInvokeSplot(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PSplotMachine psplot;

    psplot = SplotMachine::PsplotNew(pcmd->rglw[0], pcmd->rglw[1], _pcrmAll);
    if (psplot == pvNil)
        PushErc(ercSocCantInitSplot);

    return fTrue;
}

/***************************************************************************
    Handoff a movie to the app so it can pass it on to the studio
***************************************************************************/
void Application::HandoffMovie(PMovie pmvie)
{
    AssertThis(0);
    AssertPo(pmvie, 0);

    ReleasePpo(&_pmvieHandoff);
    _pmvieHandoff = pmvie;
    _pmvieHandoff->AddRef();
}

/***************************************************************************
    Grab the Application movie
***************************************************************************/
PMovie Application::PmvieRetrieve(void)
{
    AssertThis(0);

    PMovie pmvie = _pmvieHandoff;

    _pmvieHandoff = pvNil; //  Caller now owns this pointer
    return pmvie;
}

#ifdef BUG1085
/******************************************************************************
    HideCurs
    ShowCurs
    PushCurs
    PopCurs

        Some simple cursor restoration functionality, for use when a modal
        topic comes up.  Assumes that you won't try to mess with the cursor
        state while you've got the old cursor state pushed, and that you won't
        try to push the cursor state while you've already got the old cursor
        state pushed.

************************************************************ PETED ***********/
void Application::HideCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide != ivNil, "Can't hide cursor in Push/PopCurs pair");
    _cactCursHide++;
    Application_PAR::HideCurs();
}

void Application::ShowCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide > 0, "Unbalanced ShowCurs call");
    _cactCursHide--;
    Application_PAR::ShowCurs();
}

void Application::PushCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide != ivNil, "Can't nest cursor restoration");
    _cactCursSav = _cactCursHide;
    while (_cactCursHide)
        ShowCurs();
    _cactCursHide = ivNil;
}

void Application::PopCurs(void)
{
    AssertThis(0);

    Assert(_cactCursHide == ivNil, "Unbalanced cursor restoration");
    _cactCursHide = 0;
    while (_cactCursHide < _cactCursSav)
        HideCurs();
}
#endif // BUG1085

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the Application
***************************************************************************/
void Application::AssertValid(LegacyUlong grf)
{
    Application_PAR::AssertValid(0);
    AssertNilOrPo(_pstdio, 0);
    AssertNilOrPo(_ptatr, 0);
    AssertNilOrPo(_pmvieHandoff, 0);
    AssertPo(_pcfl, 0);
    AssertPo(_pgstStudioFiles, 0);
    AssertPo(_pgstBuildingFiles, 0);
    AssertPo(_pgstSharedFiles, 0);
    AssertPo(_pgstApp, 0);
    AssertPo(_pkwa, 0);
    AssertPo(_pgstApp, 0);
    AssertPo(_pcrmAll, 0);
    AssertPo(_pglicrfBuilding, 0);
    AssertPo(_pglicrfStudio, 0);
    AssertNilOrPo(_pcex, 0);
}

/***************************************************************************
    Mark memory used by the Application
***************************************************************************/
void Application::MarkMem(void)
{
    AssertThis(0);
    Application_PAR::MarkMem();
#ifdef KAUAI_SDL3
    MarkMemObj(_pgptStudioPresentation);
    MarkMemObj(_pgptStudioSnapshot);
#endif
    MarkMemObj(vptagm);
    Material_MTRL::MarkShadeTable();
    ThreeDText::MarkActionNames();
    MarkMemObj(_pstdio);
    MarkMemObj(_ptatr);
    MarkMemObj(_pmvieHandoff);
    MarkMemObj(_pcfl);
    MarkMemObj(_pgstStudioFiles);
    MarkMemObj(_pgstBuildingFiles);
    MarkMemObj(_pgstSharedFiles);
    MarkMemObj(_pgstApp);
    MarkMemObj(_pkwa);
    MarkMemObj(_pgstApp);
    MarkMemObj(_pcrmAll);
    MarkMemObj(_pglicrfBuilding);
    MarkMemObj(_pglicrfStudio);
    MarkMemObj(_pcex);
}
#endif // DEBUG

//
//
//
//  KidWorld (KidWorld for App) stuff begins here
//
//
//

/***************************************************************************
    KidWorld destructor
***************************************************************************/
KidWorld::~KidWorld(void)
{
    ReleasePpo(&_pmbmp);
}

/***************************************************************************
    Set the KidWorld's MaskedBitmapMBMP (for splash screen)
***************************************************************************/
void KidWorld::SetMbmp(PMaskedBitmapMBMP pmbmp)
{
    AssertThis(0);
    AssertNilOrPo(pmbmp, 0);

    RC rc;

    if (pvNil != pmbmp)
        pmbmp->AddRef();
    ReleasePpo(&_pmbmp);
    _pmbmp = pmbmp;
    GetRcVis(&rc, cooLocal);
    vpappb->MarkRc(&rc, this);
}

/***************************************************************************
    Draw the KidWorld's MaskedBitmapMBMP, if any (for splash screen)
***************************************************************************/
void KidWorld::Draw(PGraphicsEnvironment pgnv, RC *prcClip)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prcClip);

    if (pvNil != _pmbmp)
        pgnv->DrawMbmp(_pmbmp, 0, 0);
}

/***************************************************************************
    Find a file given a string.
***************************************************************************/
bool KidWorld::FFindFile(PString pstnSrc, PFilename pfni)
{
    AssertThis(0);
    AssertPo(pstnSrc, 0);
    AssertPo(pfni, 0);

    return vptagm->FFindFile(vapp.SidProduct(), pstnSrc, pfni, FAskForCD());
}

/***************************************************************************
    Do a modal help topic.
***************************************************************************/
bool KidWorld::FModalTopic(PResourceCache prca, ChunkNumber cnoTopic, long *plwRet)
{
    AssertThis(0);
    AssertPo(prca, 0);
    AssertVarMem(plwRet);

    bool fRet;

    // Take any special action here if necessary, before the
    // modal help topic is displayed. (Eg, disable help keys).

    // Now take the default action.
#ifdef BUG1085
    vapp.PushCurs();
    fRet = KidWorld_PAR::FModalTopic(prca, cnoTopic, plwRet);
    vapp.PopCurs();
#else
    fRet = KidWorld_PAR::FModalTopic(prca, cnoTopic, plwRet);
#endif // !BUG1085

    // Let script know that the modal topic has been dismissed.
    // This is required for the projects.
    vpcex->EnqueueCid(cidModalTopicClosed, 0, 0, *plwRet);

    return fRet;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the KidWorld
***************************************************************************/
void KidWorld::AssertValid(LegacyUlong grf)
{
    KidWorld_PAR::AssertValid(0);
    AssertNilOrPo(_pmbmp, 0);
}

/***************************************************************************
    Mark memory used by the KidWorld
***************************************************************************/
void KidWorld::MarkMem(void)
{
    AssertThis(0);
    KidWorld_PAR::MarkMem();
    MarkMemObj(_pmbmp);
}
#endif // DEBUG
