/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    msnd.h: Movie sound class

    Primary Authors: *****, *****
    Status:  Reviewed

    BASE ---> BaseCacheableObject ---> MovieSoundMSND
    BASE ---> CommandHandler  ---> MovieSoundQueue

    NOTE: when the MovieSoundQueue stops sounds, it does it based on sound class (scl)
    and not sound queue (sqn).  This is slightly less efficient, because the
    SoundManager must search all open sound queues for the given scl's when we stop
    sounds; however, the code is made much simpler, because the sqn is
    generated on the fly based on whether the sound is for an actor or
    background, the sty of the sound, and (in the case of actor sounds) the
    arid of the source of the sound.  If we had to enumerate all sounds
    based on that information, we'd wind up calling into the SoundManager a minimum
    of three times, plus three times for each actor; not only is the
    enumeration on this side inefficient (the MovieSoundQueue would have to call into the
    Movie to enumerate all the known actors), but the number of calls to SoundManager
    gets to be huge!  On top of all that, we'd probably wind up finding some
    bugs where a sound is still playing for an actor that's been deleted, and
    possibly fail to stop the sound properly (Murphy reigning strong in any
    software project).

***************************************************************************/
#ifndef MSND_H
#define MSND_H

// Sound types
enum
{
    styNil = 0,
    styUnused, // Retain.  Existing content depends on subsequent values
    stySfx,
    stySpeech,
    styMidi,
    styLim
};

// Sound-class-number constants
const long sclNonLoop = 0;
const long sclLoopWav = 1;
const long sclLoopMidi = 2;

#define vlmNil (-1)

// Sound-queue-number constants
enum
{
    sqnActr = 0x10000000,
    sqnBkgd = 0x20000000,
    sqnLim
};
#define ksqnStyShift 16; // Shift for the sqnsty

// Sound Queue Delta times
// Any sound times less than ksqdtimLong will be clocked & stopped
const long kdtimOffMsq = 0;
const long kdtimLongMsq = klwMax;
const long kdtim2Msq = ((kdtimSecond * 2) * 10) / 12; // adjustment -> 2 seconds
const long kSndSamplesPerSec = 22050;
const long kSndBitsPerSample = 8;
const long kSndBlockAlign = 1;
const long kSndChannels = 1;

/****************************************

    Movie Sound on file

****************************************/
struct MovieSoundFile
{
    short bo;
    short osk;
    long sty;        // sound type
    long vlmDefault; // default volume
    bool fInvalid;   // Invalid flag
};
const ByteOrderMask kbomMsndf = 0x5FC00000;

const ChildChunkID kchidSnd = 0; // Movie Sound sound/music

// Function to stop all movie sounds.
inline void StopAllMovieSounds(void)
{
    vpsndm->StopAll(sqnNil, sclNonLoop);
    vpsndm->StopAll(sqnNil, sclLoopWav);
    vpsndm->StopAll(sqnNil, sclLoopMidi);
}

/****************************************

    The Movie Sound class

****************************************/
typedef class MovieSoundMSND *PMovieSoundMSND;
#define MovieSoundMSND_PAR BaseCacheableObject
#define kclsMovieSoundMSND 'MSND'
class MovieSoundMSND : public MovieSoundMSND_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    // these are inherent to the msnd
    ChunkTagOrType _ctgSnd;       // ChunkTagOrType of the WAV or MIDI chunk
    ChunkNumber _cnoSnd;       // ChunkNumber of the WAV or MIDI chunk
    PResourceCache _prca;        // file that the WAV/MIDI lives in
    long _sty;         // MIDI, speech, or sfx
    long _vlm;         // Volume of the sound
    tribool _fNoSound; // Set if silent sound
    String _stn;          // Sound name
    bool _fInvalid;    // Invalid flag

  protected:
    bool _FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);

  public:
    static bool FReadMsnd(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
    static bool FGetMsndInfo(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfInvalid = pvNil, long *psty = pvNil,
                             long *pvlm = pvNil);
    static bool FCopyMidi(PFileObject pfilSrc, PChunkyFile pcflDest, ChunkNumber *pcno, PString pstn = pvNil);
    static bool FWriteMidi(PChunkyFile pcflDest, PMIDS pmids, String *pstnName, ChunkNumber *pcno);
    static bool FCopyWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, ChunkNumber *pcno, PString pstn = pvNil);
    static bool FWriteWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, String *pstnName, ChunkNumber *pcno);
    ~MovieSoundMSND(void);

    static long SqnActr(long sty, long objID);
    static long SqnBkgd(long sty, long objID);
    long Scl(bool fLoop)
    {
        return (fLoop ? ((_sty == styMidi) ? sclLoopMidi : sclLoopWav) : sclNonLoop);
    }
    long SqnActr(long objID)
    {
        AssertThis(0);
        return SqnActr(_sty, objID);
    }
    long SqnBkgd(long objID)
    {
        AssertThis(0);
        return SqnBkgd(_sty, objID);
    }

    bool FInvalidate(void);
    bool FValid(void)
    {
        AssertBaseThis(0);
        return FPure(!_fInvalid);
    }
    PString Pstn(void)
    {
        AssertThis(0);
        return &_stn;
    }
    long Sty(void)
    {
        AssertThis(0);
        return _sty;
    }
    long Vlm(void)
    {
        AssertThis(0);
        return _vlm;
    }
    long Spr(long tool); // Return Priority
    tribool FNoSound(void)
    {
        AssertThis(0);
        return _fNoSound;
    }

    void Play(long objID, bool fLoop, bool fQueue, long vlm, long spr, bool fActr = fFalse, ulong dtsStart = 0);
};

/****************************************

    Movie Sound Queue  (MovieSoundQueue)
    Sounds to be played at one time.
    These are of all types, queues &
    classes

****************************************/
typedef class MovieSoundQueue *PMovieSoundQueue;
#define MovieSoundQueue_PAR CommandHandler
#define kclsMovieSoundQueue 'MSQ'

const long kcsqeGrow = 10; // quantum growth for sqe

// Movie sound queue entry
struct SoundQueryEntry
{
    long objID;     // Unique identifier (actor id, eg)
    bool fLoop;     // Looping sound flag
    bool fQueue;    // Queued sound
    long vlmMod;    // Volume modification
    long spr;       // Priority
    bool fActr;     // Actor vs Scene (to generate unique class)
    PMovieSoundMSND pmsnd;    // PMovieSoundMSND
    ulong dtsStart; // How far into the sound to start playing
};

class MovieSoundQueue : public MovieSoundQueue_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(MovieSoundQueue)

  protected:
    PDynamicArray _pglsqe; // Sound queue entries
    long _dtim;  // Time sound allowed to play
    PClock _pclok;

  public:
    MovieSoundQueue(long hid) : MovieSoundQueue_PAR(hid)
    {
    }
    ~MovieSoundQueue(void);

    static PMovieSoundQueue PmsqNew(void);

    bool FEnqueue(PMovieSoundMSND pmsnd, long objID, bool fLoop, bool fQueue, long vlm, long spr, bool fActr = fFalse,
                  ulong dtsStart = 0, bool fLowPri = fFalse);
    void PlayMsq(void);  // Destroys queue as it plays
    void FlushMsq(void); // Without playing the sounds
    bool FCmdAlarm(PCommand pcmd);

    // Sound on/off & duration control
    void SndOff(void)
    {
        AssertThis(0);
        _dtim = kdtimOffMsq;
    }
    void SndOnShort(void)
    {
        AssertThis(0);
        _dtim = kdtim2Msq;
    }
    void SndOnLong(void)
    {
        AssertThis(0);
        _dtim = kdtimLongMsq;
    }
    void StopAll(void)
    {
        if (pvNil != _pclok)
            _pclok->Stop();

        StopAllMovieSounds();
    }
    bool FPlaying(bool fLoop)
    {
        AssertThis(0);
        return (fLoop ? (vpsndm->FPlayingAll(sqnNil, sclLoopMidi) || vpsndm->FPlayingAll(sqnNil, sclLoopWav))
                      : vpsndm->FPlayingAll(sqnNil, sclNonLoop));
    }

    // Save/Restore snd-on duration times
    long DtimSnd(void)
    {
        AssertThis(0);
        return _dtim;
    }
    void SndOnDtim(long dtim)
    {
        AssertThis(0);
        _dtim = dtim;
    }
};

#endif MSND_H
