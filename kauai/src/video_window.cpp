/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <new>
#include "frame.h"
#ifdef WIN
#include "mciavi.h"
#endif
ASSERTNAME
RTCLASS(VideoWindow)

struct VideoWindow::WindowHost
{
    PGraphicsObject base = nullptr;

#ifdef WIN
    HWND window = nullptr;
    MCIDEVICEID device = 0;

    static WindowHost &From(void *context)
    {
        return *static_cast<WindowHost *>(context);
    }

    bool Query(DWORD item, DWORD_PTR &result, DWORD track = 1) const
    {
        MCI_STATUS_PARMS status = {};
        status.dwItem = item;
        status.dwTrack = track;
        if (mciSendCommand(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status))
            return false;
        result = status.dwReturn;
        return true;
    }

    static int Open(void *context, void *source)
    {
        auto &host = From(context);
        MCI_ANIM_OPEN_PARMS open = {};
        open.lpstrDeviceType = PszLit("avivideo");
        open.lpstrElementName = static_cast<achar *>(source);
        open.dwStyle = WS_CHILD | WS_CLIPSIBLINGS | WS_DISABLED;
        open.hWndParent = host.base->HwndContainer();
        if (mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_ANIM_OPEN_PARENT | MCI_ANIM_OPEN_WS,
                           (DWORD_PTR)&open))
            return false;
        host.device = open.wDeviceID;
        return true;
    }

    static void Close(void *context)
    {
        MCI_GENERIC_PARMS close = {};
        mciSendCommand(From(context).device, MCI_CLOSE, MCI_WAIT, (DWORD_PTR)&close);
    }

    static void SuspendSound(void *, int suspend)
    {
        if (vpsndm)
            if (auto device = vpsndm->PsndvFromCtg(kctgWave))
                device->Suspend(suspend != 0);
    }

    static int Info(void *context, MMI32 *frames, MMPixelRect *source)
    {
        auto &host = From(context);
        DWORD_PTR result;
        MCI_ANIM_RECT_PARMS rect = {};
        // AVI's driver returns the HWND for 0x4001, not MCI_ANIM_STATUS_HWND.
        if (!host.Query(0x00004001, result, 0))
            return false;
        host.window = (HWND)result;
        if (!host.Query(MCI_STATUS_LENGTH, result))
            return false;
        *frames = static_cast<MMI32>(result);
        if (mciSendCommand(host.device, MCI_WHERE, MCI_ANIM_WHERE_SOURCE, (DWORD_PTR)&rect))
            return false;
        *source = {rect.rc.left, rect.rc.top, rect.rc.right, rect.rc.bottom};
        return true;
    }

    static void Origin(void *context, MMI32 *x, MMI32 *y)
    {
        RC base;
        From(context).base->GetRc(&base, cooHwnd);
        *x = base.xpLeft;
        *y = base.ypTop;
    }

    static void Move(void *context, const MMPixelRect *rect)
    {
        MoveWindow(From(context).window, rect->left, rect->top, rect->right - rect->left,
                   rect->bottom - rect->top, TRUE);
    }

    static void Show(void *context)
    {
        MCI_ANIM_WINDOW_PARMS show = {};
        show.nCmdShow = SW_SHOW;
        mciSendCommand(From(context).device, MCI_WINDOW, MCI_ANIM_WINDOW_STATE, (DWORD_PTR)&show);
    }

    static MMU32 PaletteGeneration(void *)
    {
        return static_cast<MMU32>(vcactRealize);
    }

    static void RealizePalette(void *context)
    {
        mciSendCommand(From(context).device, MCI_REALIZE, MCI_ANIM_REALIZE_BKGD, 0);
    }

    static int Start(void *context)
    {
        MCI_ANIM_PLAY_PARMS play = {};
        return !mciSendCommand(From(context).device, MCI_PLAY, MCI_MCIAVI_PLAY_WINDOW, (DWORD_PTR)&play);
    }

    static void Pause(void *context)
    {
        MCI_GENERIC_PARMS pause = {};
        mciSendCommand(From(context).device, MCI_PAUSE, 0, (DWORD_PTR)&pause);
    }

    static int Stopped(void *context)
    {
        DWORD_PTR mode;
        return From(context).Query(MCI_STATUS_MODE, mode) && (mode == MCI_MODE_STOP || mode == MCI_MODE_PAUSE);
    }

    MMVideoWindowServices Services()
    {
        return {this, Open, Close, SuspendSound, Info, Origin, Move, Show, PaletteGeneration,
                RealizePalette, Start, Pause, Stopped};
    }
#endif
};

PVideoWindow VideoWindow::PgvdwNew(PFilename pfni, PGraphicsObject pgobBase, long hid)
{
    AssertPo(pfni, ffniFile);
    if (hid == hidNil)
        hid = CommandHandler::HidUnique();

    auto video = NewObj VideoWindow(hid);
    if (video && !video->_FInit(pfni, pgobBase))
        ReleasePpo(&video);
    return video;
}

VideoWindow::VideoWindow(long hid) : VideoWindow_PAR(hid), _native(new (std::nothrow) WindowHost), _playback{}
{
    AssertBaseThis(0);
}

VideoWindow::~VideoWindow(void)
{
    AssertBaseThis(0);
#ifdef WIN
    if (_native)
    {
        auto services = _native->Services();
        mm_video_window_close(&_playback, &services);
    }
#endif
    delete _native;
}

bool VideoWindow::_FInit(PFilename pfni, PGraphicsObject pgobBase)
{
    AssertPo(pfni, ffniFile);
    AssertPo(pgobBase, 0);

    if (_native)
    {
        _native->base = pgobBase;
#ifdef WIN
        String path;
        pfni->GetStnPath(&path);
        auto services = _native->Services();
        if (mm_video_window_open(&_playback, &services, path.Psz()))
            return fTrue;
#elif defined(MAC)
        RawRtn();
#endif
    }
    PushErc(ercCantOpenVideo);
    return fFalse;
}

long VideoWindow::NfrMac(void)
{
    AssertThis(0);
    return _playback.frames;
}

long VideoWindow::NfrCur(void)
{
    AssertThis(0);
#ifdef WIN
    DWORD_PTR position;
    if (_native->Query(MCI_STATUS_POSITION, position))
        return static_cast<long>(position);
    Warn("getting position failed");
#endif
    return 0;
}

void VideoWindow::GotoNfr(long nfr)
{
    AssertThis(0);
    AssertIn(nfr, 0, _playback.frames);
#ifdef WIN
    MCI_SEEK_PARMS seek = {};
    seek.dwTo = nfr;
    if (mciSendCommand(_native->device, MCI_SEEK, MCI_TO, (DWORD_PTR)&seek))
        Warn("seeking failed");
#endif
}

bool VideoWindow::FPlaying(void)
{
    AssertThis(0);
#ifdef WIN
    auto services = _native->Services();
    return mm_video_window_playing(&_playback, &services);
#else
    return fFalse;
#endif
}

bool VideoWindow::FPlay(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);
#ifdef WIN
    MMPixelRect bounds;
    if (prc)
        bounds = {prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom};
    auto services = _native->Services();
    return mm_video_window_start(&_playback, &services, prc ? &bounds : nullptr);
#else
    return fFalse;
#endif
}

void VideoWindow::SetRcPlay(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);
    MMPixelRect bounds;
    if (prc)
        bounds = {prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom};
    mm_video_window_bounds(&_playback, prc ? &bounds : nullptr);
}

void VideoWindow::Stop(void)
{
    AssertThis(0);
#ifdef WIN
    auto services = _native->Services();
    mm_video_window_stop(&_playback, &services);
#endif
}

void VideoWindow::Draw(PGraphicsEnvironment pgnv, RC *prc)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prc);
#ifdef WIN
    auto services = _native->Services();
    mm_video_window_place(&_playback, &services);
#endif
}

void VideoWindow::GetRc(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    prc->Set(0, 0, _playback.width, _playback.height);
}

#ifdef DEBUG
void VideoWindow::AssertValid(LegacyUlong grf)
{
    VideoWindow_PAR::AssertValid(0);
    Assert(_native, 0);
#ifdef WIN
    Assert(_native->window != hNil, 0);
#endif
    AssertPo(_native->base, 0);
}
#endif
