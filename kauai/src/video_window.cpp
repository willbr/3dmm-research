/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <new>
#include "frame.h"
#ifdef WIN
#include "mciavi.h"
#endif
ASSERTNAME
RTCLASS(VideoWindow)
namespace KauaiVideo
{
struct WindowState
{
#ifdef WIN
    HWND window = nullptr;
    long device = 0;
#endif
};
} // namespace KauaiVideo
/***************************************************************************
    Create a new video window.
***************************************************************************/
PVideoWindow VideoWindow::PgvdwNew(PFilename pfni, PGraphicsObject pgobBase, long hid)
{
    AssertPo(pfni, ffniFile);
    PVideoWindow pgvdw;

    if (hid == hidNil)
        hid = CommandHandler::HidUnique();

    if (pvNil == (pgvdw = NewObj VideoWindow(hid)))
        return pvNil;

    if (!pgvdw->_FInit(pfni, pgobBase))
    {
        ReleasePpo(&pgvdw);
        return pvNil;
    }

    return pgvdw;
}

/***************************************************************************
    Constructor for a video window.
***************************************************************************/
VideoWindow::VideoWindow(long hid) : VideoWindow_PAR(hid)
{
    AssertBaseThis(0);
    _native = new (std::nothrow) KauaiVideo::WindowState;
}

/***************************************************************************
    Destructor for a video window.
***************************************************************************/
VideoWindow::~VideoWindow(void)
{
    AssertBaseThis(0);

#ifdef WIN
    if (_native && _fDeviceOpen)
    {
        MCI_GENERIC_PARMS mci;
        PSoundDevice psndv;

        mciSendCommand(_native->device, MCI_CLOSE, MCI_WAIT, (DWORD_PTR)&mci);
        if (pvNil != vpsndm && pvNil != (psndv = vpsndm->PsndvFromCtg(kctgWave)))
        {
            psndv->Suspend(fFalse);
        }
    }
#endif // WIN
    delete _native;
}

/***************************************************************************
    Initialize the VideoWindow.
***************************************************************************/
bool VideoWindow::_FInit(PFilename pfni, PGraphicsObject pgobBase)
{
    AssertPo(pfni, ffniFile);
    AssertPo(pgobBase, 0);

    _pgobBase = pgobBase;
    if (!_native)
    {
        PushErc(ercCantOpenVideo);
        return fFalse;
    }

#ifdef WIN
    MCI_ANIM_OPEN_PARMS mciOpen;
    MCI_STATUS_PARMS mciStatus;
    MCI_ANIM_RECT_PARMS mciRect;
    String stn;
    PSoundDevice psndv;

    pfni->GetStnPath(&stn);

    ClearPb(&mciOpen, size(mciOpen));
    mciOpen.lpstrDeviceType = PszLit("avivideo");
    mciOpen.lpstrElementName = stn.Psz();
    mciOpen.dwStyle = WS_CHILD | WS_CLIPSIBLINGS | WS_DISABLED;
    mciOpen.hWndParent = _pgobBase->HwndContainer();
    if (0 != mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_ANIM_OPEN_PARENT | MCI_ANIM_OPEN_WS,
                            (DWORD_PTR)&mciOpen))
    {
        goto LFail;
    }
    _native->device = mciOpen.wDeviceID;
    _fDeviceOpen = fTrue;
    if (pvNil != vpsndm && pvNil != (psndv = vpsndm->PsndvFromCtg(kctgWave)))
    {
        psndv->Suspend(fTrue);
    }

    // get the hwnd
    ClearPb(&mciStatus, size(mciStatus));
    // REVIEW shonk: mmsystem.h defines MCI_ANIM_STATUS_HWND as 0x00004003,
    // which doesn't give us the hwnd. 4001 does!
    mciStatus.dwItem = 0x00004001;
    if (0 != mciSendCommand(_native->device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&mciStatus))
    {
        goto LFail;
    }
    _native->window = (HWND)mciStatus.dwReturn;

    // get the length
    ClearPb(&mciStatus, size(mciStatus));
    mciStatus.dwItem = MCI_STATUS_LENGTH;
    mciStatus.dwTrack = 1;
    if (0 != mciSendCommand(_native->device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&mciStatus))
    {
        goto LFail;
    }
    _nfrMac = mciStatus.dwReturn;

    // get the rectangle
    if (0 != mciSendCommand(_native->device, MCI_WHERE, MCI_ANIM_WHERE_SOURCE, (DWORD_PTR)&mciRect))
    {
        goto LFail;
    }
    _rcPlay = (RC)mciRect.rc;
    _dxp = _rcPlay.Dxp();
    _dyp = _rcPlay.Dyp();

    mciSendCommand(_native->device, MCI_REALIZE, MCI_ANIM_REALIZE_BKGD, 0);
    _cactPal = vcactRealize;

    return fTrue;

LFail:
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::_FInit
#endif        // MAC

    PushErc(ercCantOpenVideo);
    return fFalse;
}

/***************************************************************************
    Return the number of frames in the video.
***************************************************************************/
long VideoWindow::NfrMac(void)
{
    AssertThis(0);

    return _nfrMac;
}

/***************************************************************************
    Return the current frame of the video.
***************************************************************************/
long VideoWindow::NfrCur(void)
{
    AssertThis(0);

#ifdef WIN
    MCI_STATUS_PARMS mciStatus;

    // get the position
    ClearPb(&mciStatus, size(mciStatus));
    mciStatus.dwItem = MCI_STATUS_POSITION;
    mciStatus.dwTrack = 1;
    if (0 != mciSendCommand(_native->device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&mciStatus))
    {
        Warn("getting position failed");
        return 0;
    }
    return mciStatus.dwReturn;
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::NfrCur
    return 0;
#endif // MAC
#if !defined(WIN) && !defined(MAC)
    return 0; // Native movie controls are absent; Studio uses VideoStreamSDL.
#endif
}

/***************************************************************************
    Advance to a particular frame.  If we are playing, stop playing.  This
    only changes internal state and doesn't mark anything.
***************************************************************************/
void VideoWindow::GotoNfr(long nfr)
{
    AssertThis(0);
    AssertIn(nfr, 0, _nfrMac);

#ifdef WIN
    MCI_SEEK_PARMS mciSeek;

    ClearPb(&mciSeek, size(mciSeek));
    mciSeek.dwTo = nfr;
    if (0 != mciSendCommand(_native->device, MCI_SEEK, MCI_TO, (DWORD_PTR)&mciSeek))
    {
        Warn("seeking failed");
    }
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::GotoNfr
#endif        // MAC
}

/***************************************************************************
    Return whether or not the video is playing.
***************************************************************************/
bool VideoWindow::FPlaying(void)
{
    AssertThis(0);

    if (!_fPlaying)
        return fFalse;

#ifdef WIN
    MCI_STATUS_PARMS mciStatus;

    // get the mode
    ClearPb(&mciStatus, size(mciStatus));
    mciStatus.dwItem = MCI_STATUS_MODE;
    mciStatus.dwTrack = 1;
    if (0 == mciSendCommand(_native->device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&mciStatus) &&
        (MCI_MODE_STOP == mciStatus.dwReturn || MCI_MODE_PAUSE == mciStatus.dwReturn))
    {
        _fPlaying = fFalse;
    }
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::NfrCur
#endif        // MAC

    return _fPlaying;
}

/***************************************************************************
    Start playing at the current frame.  This assumes the gob is valid
    until the video is stopped or nuked.  The gob should call this video's
    Draw method in its Draw method.
***************************************************************************/
bool VideoWindow::FPlay(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);

    Stop();

#ifdef WIN
    MCI_ANIM_PLAY_PARMS mciPlay;

    // get the play rectangle
    SetRcPlay(prc);

    // position the hwnd
    _SetRc();

    // start the movie playing
    ClearPb(&mciPlay, size(mciPlay));
    if (0 != mciSendCommand(_native->device, MCI_PLAY, MCI_MCIAVI_PLAY_WINDOW, (DWORD_PTR)&mciPlay))
    {
        return fFalse;
    }
    _fPlaying = fTrue;

    return fTrue;
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::NfrCur
    return fFalse;
#endif // MAC
#if !defined(WIN) && !defined(MAC)
    return false; // Native controls are absent; Studio uses VideoStreamSDL.
#endif
}

/***************************************************************************
    Set the rectangle to play into.
***************************************************************************/
void VideoWindow::SetRcPlay(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);

    if (pvNil == prc)
        _rcPlay.Set(0, 0, _dxp, _dyp);
    else
        _rcPlay = *prc;
}

/***************************************************************************
    Stop playing.
***************************************************************************/
void VideoWindow::Stop(void)
{
    AssertThis(0);

    if (!_fPlaying)
        return;

#ifdef WIN
    MCI_GENERIC_PARMS mciPause;

    ClearPb(&mciPause, size(mciPause));
    mciSendCommand(_native->device, MCI_PAUSE, 0, (DWORD_PTR)&mciPause);
#endif // WIN

#ifdef MAC
    RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::Stop
#endif        // MAC
    _fPlaying = fFalse;
}

/***************************************************************************
    Call this to draw the current state of the video image.
***************************************************************************/
void VideoWindow::Draw(PGraphicsEnvironment pgnv, RC *prc)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prc);

    _SetRc();
}

/***************************************************************************
    Position the hwnd associated with the video to match the GraphicsObject's position.
***************************************************************************/
void VideoWindow::_SetRc(void)
{
    AssertThis(0);
    RC rcGob, rc;

    _pgobBase->GetRc(&rcGob, cooHwnd);
    rc = _rcPlay;
    rc.Offset(rcGob.xpLeft, rcGob.ypTop);
    if (_rc != rc || !_fVisible)
    {
#ifdef WIN
        MoveWindow(_native->window, rc.xpLeft, rc.ypTop, rc.Dxp(), rc.Dyp(), fTrue);
        if (!_fVisible)
        {
            MCI_ANIM_WINDOW_PARMS mciWindow;

            // show the playback window
            ClearPb(&mciWindow, size(mciWindow));
            mciWindow.nCmdShow = SW_SHOW;
            mciSendCommand(_native->device, MCI_WINDOW, MCI_ANIM_WINDOW_STATE, (DWORD_PTR)&mciWindow);
            _fVisible = fTrue;
        }
#endif // WIN

#ifdef MAC
        RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::_SetRc
#endif            // MAC
        _rc = rc;
    }

    if (_cactPal != vcactRealize)
    {
#ifdef WIN
        mciSendCommand(_native->device, MCI_REALIZE, MCI_ANIM_REALIZE_BKGD, 0);
#endif // WIN
#ifdef MAC
        RawRtn(); // REVIEW shonk: Mac: implement VideoWindow::_SetRc
#endif            // MAC
        _cactPal = vcactRealize;
    }
}

/***************************************************************************
    Get the normal rectangle for the movie (top-left at (0, 0)).
***************************************************************************/
void VideoWindow::GetRc(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);

    prc->Set(0, 0, _dxp, _dyp);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a VideoWindow.
***************************************************************************/
void VideoWindow::AssertValid(LegacyUlong grf)
{
    VideoWindow_PAR::AssertValid(0);
    Assert(_native, 0);
#ifdef WIN
    Assert(_native->window != hNil, 0);
#endif
    AssertPo(_pgobBase, 0);
}
#endif // DEBUG
