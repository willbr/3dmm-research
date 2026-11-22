#ifndef MARCUS_PLAYBACK_AUDIO_H
#define MARCUS_PLAYBACK_AUDIO_H

#include "mm/movie_audio_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusPlaybackAudio MarcusPlaybackAudio;
typedef struct MarcusPlaybackSound
{
    MMMovieAudioKey key;
    MMI32 actor, style, volume;
    int actor_sound, loop, queue, silent;
} MarcusPlaybackSound;

enum { MARCUS_PLAYBACK_AUDIO_RATE = 48000 };
/* The fallback bank path is borrowed for the lifetime of the audio owner. */
MarcusPlaybackAudio *marcus_playback_audio_create(const char *fallback_bank);
void marcus_playback_audio_destroy(MarcusPlaybackAudio *);

/* Call on the player thread. The device callback shares only the locked mixer. */
void marcus_playback_audio_clear(MarcusPlaybackAudio *);
void marcus_playback_audio_cut(MarcusPlaybackAudio *);
void marcus_playback_audio_suspend(MarcusPlaybackAudio *, int suspended);
void marcus_playback_audio_stop(MarcusPlaybackAudio *);
void marcus_playback_audio_update(MarcusPlaybackAudio *, int playing);
int marcus_playback_audio_pending(MarcusPlaybackAudio *);
int marcus_playback_audio_auditioning(MarcusPlaybackAudio *);

int marcus_playback_audio_set_mix(MarcusPlaybackAudio *, MMAudioCounter lane, int muted, int solo);
void marcus_playback_audio_track_mix(MarcusPlaybackAudio *, MMAudioCounter lane, int *muted, int *solo, int *audible);
void marcus_playback_audio_reset_mix(MarcusPlaybackAudio *);
int marcus_playback_audio_mix_active(MarcusPlaybackAudio *);
void marcus_playback_audio_mute(MarcusPlaybackAudio *, int muted);
void marcus_playback_audio_stats(MarcusPlaybackAudio *, MMMovieAudioStats *);

/* Instrument bytes are loaded lazily and borrowed until audio destruction. */
const void *marcus_playback_audio_bank(MarcusPlaybackAudio *, size_t *bytes);

enum { MARCUS_PLAYBACK_AUDIO_DEVICE = MM_MOVIE_AUDIO_CACHE_LIMIT + 1 };
/* Returns a cache status or DEVICE with a diagnostic. Audition is one-shot
 * and begins after the caller stops the previous presentation. Decoder calls
 * are synchronous and happen outside the mixer lock. */
int marcus_playback_audio_play(MarcusPlaybackAudio *, const MarcusPlaybackSound *, MMAudioCounter offset,
                               const MMMovieAudioDecode *, int audition, char *error, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
