#ifndef MARCUS_PLAYBACK_AUDIO_H
#define MARCUS_PLAYBACK_AUDIO_H

#include "mm/audio_mixer.h"
#include "mm/movie_audio_cache.h"
#include "mm/preview_media.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusPlaybackAudio MarcusPlaybackAudio;

enum { MARCUS_PLAYBACK_AUDIO_RATE = MM_AUDIO_MIX_RATE };
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

typedef int (*MarcusPlaybackAudioRead)(void *, MMPreviewOwnedAudio *);
enum { MARCUS_PLAYBACK_AUDIO_DEVICE = MM_MOVIE_AUDIO_CACHE_LIMIT + 1 };
/* Returns a cache status or DEVICE. Local decode failures report a diagnostic.
 * Audition is one-shot. The source callback uses the owned Preview receiver
 * to fill a zeroed output outside the mixer lock and returns nonzero on
 * success. The caller stops the previous presentation before audition. */
int marcus_playback_audio_play(MarcusPlaybackAudio *, const MMPreviewCue *, MMU32 active_scene, MMAudioCounter offset,
                               void *context, MarcusPlaybackAudioRead, int audition, char *error, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
