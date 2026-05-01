/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Video playback.

***************************************************************************/
#ifndef VIDEO_H
#define VIDEO_H

/***************************************************************************
    Generic video class. This is an interface that supports the VideoStream
    (video stream) and VideoWindow (video window) classes.
***************************************************************************/
typedef class Video *PVideo;
#define Video_PAR CommandHandler
#define kclsVideo 'GVID'
class Video : public Video_PAR
{
    RTCLASS_DEC

  protected:
    Video(long hid);
    ~Video(void)
    {
    }

  public:
    static PVideo PgvidNew(PFilename pfni, PGraphicsObject pgobBase, bool fHwndBased = fFalse, long hid = hidNil);

    virtual long NfrMac(void) = 0;
    virtual long NfrCur(void) = 0;
    virtual void GotoNfr(long nfr) = 0;

    virtual bool FPlaying(void) = 0;
    virtual bool FPlay(RC *prc = pvNil) = 0;
    virtual void Stop(void) = 0;

    virtual void Draw(PGraphicsEnvironment pgnv, RC *prc) = 0;
    virtual void GetRc(RC *prc) = 0;
    virtual void SetRcPlay(RC *prc) = 0;
};

/****************************************
    Video stream class.
****************************************/
typedef class VideoStream *PVideoStream;
#define VideoStream_PAR Video
#define kclsVideoStream 'GVDS'
class VideoStream : public VideoStream_PAR
{
    RTCLASS_DEC
    CMD_MAP_DEC(VideoStream)
    ASSERT

  protected:
    long _nfrMac;
    long _nfrCur;
    long _nfrMarked;
    long _dxp;
    long _dyp;

    PGraphicsObject _pgobBase;
    RC _rcPlay;
    ulong _tsPlay;

    bool _fPlaying;

#ifdef WIN
    long _dnfr;
    PAVIFILE _pavif;
    PAVISTREAM _pavis;
    PGETFRAME _pavig;
    HDRAWDIB _hdd;
#endif // WIN

    VideoStream(long hid);
    ~VideoStream(void);

    virtual bool _FInit(PFilename pfni, PGraphicsObject pgobBase);

  public:
    static PVideoStream PgvdsNew(PFilename pfni, PGraphicsObject pgobBase, long hid = hidNil);

    virtual long NfrMac(void);
    virtual long NfrCur(void);
    virtual void GotoNfr(long nfr);

    virtual bool FPlaying(void);
    virtual bool FPlay(RC *prc = pvNil);
    virtual void Stop(void);

    virtual void Draw(PGraphicsEnvironment pgnv, RC *prc);
    virtual void GetRc(RC *prc);
    virtual void SetRcPlay(RC *prc);

    virtual bool FCmdAll(PCommand pcmd);
};

/****************************************
    Video in a window class.
****************************************/
typedef class VideoWindow *PVideoWindow;
#define VideoWindow_PAR Video
#define kclsVideoWindow 'GVDW'
class VideoWindow : public VideoWindow_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    HWND _hwndMovie;
    long _lwDevice;
    long _dxp;
    long _dyp;
    RC _rc;
    RC _rcPlay;
    long _nfrMac;
    PGraphicsObject _pgobBase;
    long _cactPal;

    bool _fDeviceOpen : 1;
    bool _fPlaying : 1;
    bool _fVisible : 1;

    VideoWindow(long hid);
    ~VideoWindow(void);

    virtual bool _FInit(PFilename pfni, PGraphicsObject pgobBase);
    virtual void _SetRc(void);

  public:
    static PVideoWindow PgvdwNew(PFilename pfni, PGraphicsObject pgobBase, long hid = hidNil);

    virtual long NfrMac(void);
    virtual long NfrCur(void);
    virtual void GotoNfr(long nfr);

    virtual bool FPlaying(void);
    virtual bool FPlay(RC *prc = pvNil);
    virtual void Stop(void);

    virtual void Draw(PGraphicsEnvironment pgnv, RC *prc);
    virtual void GetRc(RC *prc);
    virtual void SetRcPlay(RC *prc);
};

#endif //! VIDEO_H
