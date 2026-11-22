/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    msnd.h: Movie sound class

    Primary Authors: *****, *****
    Status:  Reviewed

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

#include <cstdint>

#include "sound_queue.h"

const long kSndSamplesPerSec = 22050;
const long kSndBitsPerSample = 8;
const long kSndBlockAlign = 1;
const long kSndChannels = 1;

/****************************************

    Movie Sound on file

****************************************/
struct MovieSoundFile
{
    int16_t bo;
    int16_t osk;
    int32_t sty;        // sound type
    int32_t vlmDefault; // default volume
    bool fInvalid;      // Invalid flag
};
static_assert(sizeof(MovieSoundFile) == 16, "MovieSoundFile on-disk layout drift");
const ByteOrderMask kbomMsndf = 0x5FC00000;

const ChildChunkID kchidSnd = 0; // Movie Sound sound/music

void StopAllMovieSounds(void);

/****************************************

    The Movie Sound class

****************************************/
using PMovieSoundMSND = MMSoundOwner *;

namespace EngineSound
{
const MMSoundResourceHost &Host();
int Read(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *);
PMovieSoundMSND Fetch(PTAG);
PMovieSoundMSND Fetch(MMResourceCache *, ChunkTagOrType, ChunkNumber);
bool Info(PChunkyFile, ChunkTagOrType, ChunkNumber, bool *invalid = nullptr, long *style = nullptr,
          long *volume = nullptr);
bool CopyMidi(PFileObject, PChunkyFile, ChunkNumber *, PString name = nullptr);
bool CopyMidi(PFileObject, const MMArchiveWriter &, ChunkNumber *, PString);
bool WriteMidi(PChunkyFile, PMidiStream, PString, ChunkNumber *);
bool CopyWave(PFileObject, PChunkyFile, long style, ChunkNumber *, PString name = nullptr);
bool CopyWave(PFileObject, const MMArchiveWriter &, long style, ChunkNumber *, PString);
bool WriteWave(PFileObject, PChunkyFile, long style, PString, ChunkNumber *);

inline const MMSoundState &State(PMovieSoundMSND sound) { return *mm_sound_owner_state(sound); }
inline bool Valid(PMovieSoundMSND sound) { return !State(sound).invalid; }
inline tribool NoSound(PMovieSoundMSND sound) { return static_cast<tribool>(State(sound).silent); }
inline long Style(PMovieSoundMSND sound) { return State(sound).resource.style; }
inline long Volume(PMovieSoundMSND sound) { return State(sound).resource.volume; }

void GetName(PMovieSoundMSND, PString);
bool Invalidate(PMovieSoundMSND);
long Priority(PMovieSoundMSND, long tool);
void Play(PMovieSoundMSND, long object, bool loop, bool queue, long volume, long priority,
          bool actor = false, ulong start = 0);
}

/****************************************
    Movie Sound Queue  (MovieSoundQueue)
    Sounds to be played at one time.
    These are of all types, queues &
    classes

****************************************/
typedef class MovieSoundQueue *PMovieSoundQueue;
#define MovieSoundQueue_PAR CommandHandler
#define kclsMovieSoundQueue 'MSQ'

class MovieSoundQueue : public MovieSoundQueue_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(MovieSoundQueue)

  protected:
    MMFrameSoundQueue _queue;
    MMFrameSoundOutput _Output();

  public:
    MovieSoundQueue(long hid) : MovieSoundQueue_PAR(hid)
    {
        mm_frame_sound_queue_prepare(&_queue);
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
        _queue.duration = kdtimOffMsq;
    }
    void SndOnShort(void)
    {
        AssertThis(0);
        _queue.duration = kdtim2Msq;
    }
    void SndOnLong(void)
    {
        AssertThis(0);
        _queue.duration = kdtimLongMsq;
    }
    void StopAll(void);
    bool FPlaying(bool fLoop);

    // Save/Restore snd-on duration times
    long DtimSnd(void)
    {
        AssertThis(0);
        return _queue.duration;
    }
    void SndOnDtim(long dtim)
    {
        AssertThis(0);
        _queue.duration = dtim;
    }
};

#endif MSND_H
