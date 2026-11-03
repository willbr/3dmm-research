/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Graphical video object implementation.

***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(Video)
RTCLASS(VideoStream)

BEGIN_CMD_MAP_BASE(VideoStream)
END_CMD_MAP(&VideoStream::FCmdAll, pvNil, kgrfcmmAll)

const long kcmhlGvds = kswMin; // put videos at the head of the list

/***************************************************************************
    Create a video - either an HWND based one, or just a video stream,
    depending on fHwndBased. pgobBase is assumed to be valid for the life
    of the video.
***************************************************************************/
PVideo Video::PgvidNew(PFilename pfni, PGraphicsObject pgobBase, bool fHwndBased, long hid)
{
    AssertPo(pfni, ffniFile);
    AssertPo(pgobBase, 0);

#ifdef KAUAI_SDL3
    return VideoStream::PgvdsNew(pfni, pgobBase, hid, fHwndBased);
#else
    if (fHwndBased)
        return VideoWindow::PgvdwNew(pfni, pgobBase, hid);
    return VideoStream::PgvdsNew(pfni, pgobBase, hid);
#endif
}

/***************************************************************************
    Constructor for a generic video.
***************************************************************************/
Video::Video(long hid) : Video_PAR(hid)
{
    AssertBaseThis(0);
}

/***************************************************************************
    Constructor for video stream class.
***************************************************************************/
VideoStream::VideoStream(long hid, bool withAudio) : VideoStream_PAR(hid)
{
    AssertBaseThis(0);

#ifdef KAUAI_SDL3
    _host = KauaiVideo::NewSDLStreamHost(withAudio);
#elif defined(WIN)
    _host = KauaiVideo::NewStreamHost();
#endif // WIN
}

/***************************************************************************
    Destructor for video stream class.
***************************************************************************/
VideoStream::~VideoStream(void)
{
    AssertBaseThis(0);

    Stop();
    delete _host;
}

/***************************************************************************
    Initialize a video stream object.
***************************************************************************/
bool VideoStream::_FInit(PFilename pfni, PGraphicsObject pgobBase)
{
    AssertBaseThis(0);
    AssertPo(pfni, ffniFile);
    AssertPo(pgobBase, 0);

    _pgobBase = pgobBase;
    if (!_host || !_host->Open(pfni))
    {
        PushErc(ercCantOpenVideo);
        return fFalse;
    }
    const auto info = _host->Info();
    _dxp = info.width;
    _dyp = info.height;
    _nfrMac = info.frames;
    _nfrCur = 0;
    _nfrMarked = -1;
    SetRcPlay(pvNil);
    return fTrue;
}

/***************************************************************************
    Create a new video stream object.
***************************************************************************/
PVideoStream VideoStream::PgvdsNew(PFilename pfni, PGraphicsObject pgobBase, long hid, bool withAudio)
{
    AssertPo(pfni, ffniFile);
    PVideoStream pgvds;

    if (hid == hidNil)
        hid = CommandHandler::HidUnique();

    if (pvNil == (pgvds = NewObj VideoStream(hid, withAudio)))
        return pvNil;

    if (!pgvds->_FInit(pfni, pgobBase))
    {
        ReleasePpo(&pgvds);
        return pvNil;
    }

    return pgvds;
}

/***************************************************************************
    Return the number of frames in the video.
***************************************************************************/
long VideoStream::NfrMac(void)
{
    AssertThis(0);
    return _nfrMac;
}

/***************************************************************************
    Return the current frame of the video.
***************************************************************************/
long VideoStream::NfrCur(void)
{
    AssertThis(0);
    if (_fPlaying && _host->ControlsPlayback())
        _nfrCur = _host->Position();
    return _nfrCur;
}

/***************************************************************************
    Advance to a particular frame.  If we are playing, stop playing.  This
    only changes internal state and doesn't mark anything.
***************************************************************************/
void VideoStream::GotoNfr(long nfr)
{
    AssertThis(0);
    AssertIn(nfr, 0, _nfrMac);

    Stop();
    _nfrCur = nfr;
}

/***************************************************************************
    Return whether or not the video is playing.
***************************************************************************/
bool VideoStream::FPlaying(void)
{
    AssertThis(0);
    if (_fPlaying && _host->ControlsPlayback() && !_host->Playing())
        Stop();
    return _fPlaying;
}

/***************************************************************************
    Start playing at the current frame.  This assumes the gob is valid
    until the video is stopped or nuked.  The gob should call this video's
    Draw method in its Draw method.
***************************************************************************/
bool VideoStream::FPlay(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);

    Stop();
    if (!vpcex->FAddCmh(this, kcmhlGvds, kgrfcmmAll))
        return fFalse;
    if (!_host->Start(_nfrCur))
    {
        vpcex->RemoveCmh(this, kcmhlGvds);
        return fFalse;
    }

    SetRcPlay(prc);
    _fPlaying = fTrue;

    _tsPlay = TsCurrent() - _host->TimeForFrame(_nfrCur);

    return fTrue;
}

/***************************************************************************
    Set the rectangle to play into.
***************************************************************************/
void VideoStream::SetRcPlay(RC *prc)
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
void VideoStream::Stop(void)
{
    AssertThis(0);

    if (_host)
    {
        if (_fPlaying && _host->ControlsPlayback())
            _nfrCur = _host->Position();
        _host->Stop();
    }
    if (vpcex)
        vpcex->RemoveCmh(this, kcmhlGvds);
    _fPlaying = fFalse;
}

/***************************************************************************
    Intercepts all commands, so we get to play our movie no matter what.
***************************************************************************/
bool VideoStream::FCmdAll(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (!_fPlaying)
    {
        Stop();
        return fFalse;
    }

    // update _nfrCur
    const bool controlled = _host->ControlsPlayback();
    _nfrCur = controlled ? _host->Position() : _host->FrameForTime(TsCurrent() - _tsPlay);
    if (_nfrCur >= _nfrMac)
        _nfrCur = _nfrMac - 1;
    else if (_nfrCur < 0)
        _nfrCur = 0;

    if (_nfrCur != _nfrMarked)
    {
        _pgobBase->InvalRc(&_rcPlay, kginMark);
        _nfrMarked = _nfrCur;
    }
    if (controlled ? !_host->Playing() : _nfrCur >= _nfrMac - 1)
        Stop();

    return fFalse;
}

/***************************************************************************
    Call this to draw the current state of the video image.
***************************************************************************/
void VideoStream::Draw(PGraphicsEnvironment pgnv, RC *prc)
{
    AssertThis(0);
    AssertPo(pgnv, 0);
    AssertVarMem(prc);
    if (_host->ControlsPlayback() && _rcPlay.FEmpty())
        return;
    _host->DrawFrame(_nfrCur, pgnv, prc);
}

/***************************************************************************
    Get the normal rectangle for the movie (top-left at (0, 0)).
***************************************************************************/
void VideoStream::GetRc(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);

    prc->Set(0, 0, _dxp, _dyp);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a VideoStream.
***************************************************************************/
void VideoStream::AssertValid(LegacyUlong grf)
{
    VideoStream_PAR::AssertValid(0);
    AssertPo(_pgobBase, 0);
    // REVIEW shonk: fill in VideoStream::AssertValid
}
#endif // DEBUG
