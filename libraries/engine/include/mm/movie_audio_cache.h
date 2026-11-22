#ifndef MM_MOVIE_AUDIO_CACHE_H
#define MM_MOVIE_AUDIO_CACHE_H

#include "movie_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieAudioCache MMMovieAudioCache;
typedef struct MMMovieAudioKey
{
    MMI32 source;
    MMU32 type, number, scene, child;
} MMMovieAudioKey;

typedef struct MMMovieAudioMedia
{
    MMMovieAudioClip clip;
    void *owner;
    void (*release)(void *);
    MMMovieAudioEvent (*event)(void *, size_t);
} MMMovieAudioMedia;

typedef struct MMMovieAudioSynth
{
    void *owner;
    void (*release)(void *);
    void (*message)(void *, MMU32);
    void (*render)(void *, float *, size_t);
    void (*reset)(void *);
} MMMovieAudioSynth;

typedef struct MMMovieAudioDecode
{
    void *context;
    int (*load)(void *, MMMovieAudioMedia *);
    int (*synth)(void *, MMMovieAudioSynth *);
} MMMovieAudioDecode;

enum { MM_MOVIE_AUDIO_CACHE_OK, MM_MOVIE_AUDIO_CACHE_SOURCE, MM_MOVIE_AUDIO_CACHE_MEMORY,
       MM_MOVIE_AUDIO_CACHE_LIMIT };

MMMovieAudioCache *mm_movie_audio_cache_create(size_t budget);
void mm_movie_audio_cache_clear(MMMovieAudioCache *);
void mm_movie_audio_cache_destroy(MMMovieAudioCache *);

/* Authored keys use scene and child for local sounds. External sounds ignore
 * scene. A null key represents silence. Success publishes a prepared voice
 * or null to stop the lane. A successful host callback transfers one owner
 * to C. The cache and voice lifecycle calls require external serialization.
 * Decoded media is immutable and survives eviction while used by a voice.
 * Each MIDI voice gets an independent synth. Callbacks must not throw or
 * reenter. On failure the voice output is unchanged. */
int mm_movie_audio_cache_voice(MMMovieAudioCache *, const MMMovieAudioKey *, const MMMovieAudioDecode *,
                               MMAudioCounter offset, MMI32 volume, int loop, MMMovieAudioVoice **);

#ifdef __cplusplus
}
#endif
#endif
