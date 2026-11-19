#ifndef MM_MOVIE_AUDIO_H
#define MM_MOVIE_AUDIO_H
#include "audio_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieAudio MMMovieAudio;
typedef struct MMMovieAudioVoice MMMovieAudioVoice;
enum { MM_MOVIE_AUDIO_BLOCK_FRAMES = 512 };
typedef struct MMMovieAudioEvent
{
    MMU32 milliseconds, message;
} MMMovieAudioEvent;
typedef struct MMMovieAudioClip
{
    MMI32 source;
    MMU32 type, number;
    const float *stereo;
    size_t sample_frames, midi_events;
} MMMovieAudioClip;
typedef struct MMMovieAudioServices
{
    void *owner;
    void (*release)(void *);
    MMMovieAudioEvent (*event)(void *, size_t);
    void (*message)(void *, MMU32);
    void (*render)(void *, float *, size_t);
    void (*reset)(void *);
} MMMovieAudioServices;
typedef struct MMMovieAudioStats
{
    unsigned long wave_sounds, midi_sounds, rendered_frames, audible_frames;
} MMMovieAudioStats;

/* Host serializes mixer calls. The initial state is suspended. Clear drops
 * voices. Track settings, global controls and counters retain their state. */
MMMovieAudio *mm_movie_audio_create(void);
void mm_movie_audio_destroy(MMMovieAudio *);
void mm_movie_audio_clear(MMMovieAudio *);
void mm_movie_audio_collect(MMMovieAudio *);
void mm_movie_audio_cut(MMMovieAudio *);
int mm_movie_audio_cut_stops(int loop, int midi);
void mm_movie_audio_suspend(MMMovieAudio *, int);
void mm_movie_audio_mute(MMMovieAudio *, int);
int mm_movie_audio_pending(const MMMovieAudio *);
void mm_movie_audio_stats(const MMMovieAudio *, MMMovieAudioStats *);

MMAudioCounter mm_movie_audio_lane(int actor_sound, MMI32 actor, MMI32 style);
int mm_movie_audio_set_mix(MMMovieAudio *, MMAudioCounter lane, int muted, int solo);
void mm_movie_audio_track_mix(const MMMovieAudio *, MMAudioCounter lane, int *muted, int *solo, int *audible);
void mm_movie_audio_reset_mix(MMMovieAudio *);
int mm_movie_audio_mix_active(const MMMovieAudio *);

/* The prepared voice retains the owner and borrows its immutable PCM/events.
 * A successful prepare takes the owner reference. Release is required for
 * every voice. MIDI voices also require the four MIDI service callbacks.
 * Prepare can preroll MIDI independently of the mixer lock. Events use
 * ascending absolute milliseconds. Service calls do not throw or reenter.
 * Submit consumes the voice on all paths. A null voice stops the lane. */
MMMovieAudioVoice *mm_movie_audio_prepare(const MMMovieAudioClip *, const MMMovieAudioServices *,
                                         MMAudioCounter offset, MMI32 volume, int loop);
void mm_movie_audio_voice_destroy(MMMovieAudioVoice *);
int mm_movie_audio_submit(MMMovieAudio *, MMAudioCounter lane, int queued, MMMovieAudioVoice *);
/* Writes interleaved 48 kHz stereo and makes no allocations of its own.
 * Host supplies at most MM_MOVIE_AUDIO_BLOCK_FRAMES frames per call. */
void mm_movie_audio_render(MMMovieAudio *, float *output, size_t frames);

#ifdef __cplusplus
}
#endif
#endif
