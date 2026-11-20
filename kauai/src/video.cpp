/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Graphical video object implementation.

***************************************************************************/
#include <exception>
#include "frame.h"
ASSERTNAME

RTCLASS(Video)
RTCLASS(VideoStream)

BEGIN_CMD_MAP_BASE(VideoStream)
END_CMD_MAP(&VideoStream::FCmdAll, pvNil, kgrfcmmAll)

const long kcmhlGvds = kswMin; // put videos at the head of the list

struct VideoStream::PlaybackHost
{
    VideoStream *video;
    std::exception_ptr error;

    explicit PlaybackHost(VideoStream *owner) : video(owner) {}

    template <class Action> int Guard(Action action)
    {
        try
        {
            return action();
        }
        catch (...)
        {
            if (!error)
                error = std::current_exception();
            return -1;
        }
    }

    void Rethrow()
    {
        if (error)
            std::rethrow_exception(error);
    }

    MMVideoStreamServices Services()
    {
        return {
            this,
            [](void *context, int add) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] {
                    if (add)
                        return vpcex && vpcex->FAddCmh(host.video, kcmhlGvds, kgrfcmmAll) ? 1 : 0;
                    if (vpcex)
                        vpcex->RemoveCmh(host.video, kcmhlGvds);
                    return 1;
                });
            },
            [](void *context, MMI32 frame) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] { return host.video->_host->Start(frame); });
            },
            video->_host ? StopDevice : nullptr,
            [](void *context) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] { return host.video->_host->ControlsPlayback(); });
            },
            [](void *context) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] { return host.video->_host->Playing(); });
            },
            [](void *context, MMI32 *frame) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] {
                    *frame = host.video->_host->Position();
                    return 1;
                });
            },
            [](void *context, MMU32 time, MMI32 *frame) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] {
                    *frame = host.video->_host->FrameForTime(time);
                    return 1;
                });
            },
            [](void *context, MMI32 frame, MMI32 *time) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] {
                    *time = host.video->_host->TimeForFrame(frame);
                    return 1;
                });
            },
            [](void *context, MMU32 *time) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] {
                    *time = TsCurrent();
                    return 1;
                });
            },
            [](void *context, const MMPixelRect *bounds) {
                auto &host = *static_cast<PlaybackHost *>(context);
                return host.Guard([&] {
                    RC rectangle(bounds->left, bounds->top, bounds->right, bounds->bottom);
                    host.video->_pgobBase->InvalRc(&rectangle, kginMark);
                    return 1;
                });
            }
        };
    }

    static int StopDevice(void *context)
    {
        auto &host = *static_cast<PlaybackHost *>(context);
        return host.Guard([&] {
            host.video->_host->Stop();
            return 1;
        });
    }
};

static MMPixelRect VideoBounds(const RC &rectangle)
{
    return {rectangle.xpLeft, rectangle.ypTop, rectangle.xpRight, rectangle.ypBottom};
}

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
    mm_video_stream_init(&_playback, info.width, info.height, info.frames);
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
    return _playback.frames;
}

/***************************************************************************
    Return the current frame of the video.
***************************************************************************/
long VideoStream::NfrCur(void)
{
    AssertThis(0);
    PlaybackHost host(this);
    const auto services = host.Services();
    mm_video_stream_position(&_playback, &services);
    host.Rethrow();
    return _playback.frame;
}

/***************************************************************************
    Advance to a particular frame.  If we are playing, stop playing.  This
    only changes internal state and doesn't mark anything.
***************************************************************************/
void VideoStream::GotoNfr(long nfr)
{
    AssertThis(0);
    AssertIn(nfr, 0, _playback.frames);
    PlaybackHost host(this);
    const auto services = host.Services();
    mm_video_stream_seek(&_playback, &services, nfr);
    host.Rethrow();
}

/***************************************************************************
    Return whether or not the video is playing.
***************************************************************************/
bool VideoStream::FPlaying(void)
{
    AssertThis(0);
    PlaybackHost host(this);
    const auto services = host.Services();
    const int playing = mm_video_stream_playing(&_playback, &services);
    host.Rethrow();
    return playing > 0;
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
    PlaybackHost host(this);
    const auto services = host.Services();
    const auto bounds = prc ? VideoBounds(*prc) : MMPixelRect{};
    const int started = mm_video_stream_start(&_playback, &services, prc ? &bounds : nullptr);
    host.Rethrow();
    return started > 0;
}

/***************************************************************************
    Set the rectangle to play into.
***************************************************************************/
void VideoStream::SetRcPlay(RC *prc)
{
    AssertThis(0);
    AssertNilOrVarMem(prc);
    const auto bounds = prc ? VideoBounds(*prc) : MMPixelRect{};
    mm_video_stream_bounds(&_playback, prc ? &bounds : nullptr);
}

/***************************************************************************
    Stop playing.
***************************************************************************/
void VideoStream::Stop(void)
{
    AssertThis(0);
    PlaybackHost host(this);
    const auto services = host.Services();
    mm_video_stream_stop(&_playback, &services);
    host.Rethrow();
}

/***************************************************************************
    Intercepts all commands, so we get to play our movie no matter what.
***************************************************************************/
bool VideoStream::FCmdAll(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    PlaybackHost host(this);
    const auto services = host.Services();
    mm_video_stream_tick(&_playback, &services);
    host.Rethrow();
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
    if (mm_video_stream_drawable(&_playback, _host->ControlsPlayback()))
        _host->DrawFrame(_playback.frame, pgnv, prc);
}

/***************************************************************************
    Get the normal rectangle for the movie (top-left at (0, 0)).
***************************************************************************/
void VideoStream::GetRc(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);
    prc->Set(0, 0, _playback.width, _playback.height);
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
