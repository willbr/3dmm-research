/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    msnd.h: Movie sound class

    Primary Authors: *****, *****
    Status:  Reviewed

    Movie playback stops mixer classes for one-shot, looping wave and
    looping MIDI sounds. Queue numbers also distinguish actor sounds
    from scene sounds and separate their sound styles.

***************************************************************************/
#ifndef MSND_H
#define MSND_H

#include <cstdint>

#include "sound_queue.h"
#include "movie_sound.h"

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
inline const MMSoundResourceHost &Host() { return *mm_kauai_movie_sound_host(); }
constexpr MMResourceBlockReader Read = mm_kauai_movie_sound_read;
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

MMFrameSoundOutput MovieSoundOutput(PCommandHandler alarmTarget);

#endif // MSND_H
