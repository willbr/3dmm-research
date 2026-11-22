#include "playback_audio.h"
#include <SDL3/SDL.h>

struct MarcusPlaybackAudio
{
    MMMovieAudio *mixer;
    MMMovieAudioCache *clips;
    SDL_Mutex *mutex;
    SDL_AudioStream *stream;
    const char *fallback_bank;
    void *bank;
    size_t bank_bytes;
    int initialized, auditioning;
};

static void SDLCALL feed(void *p, SDL_AudioStream *stream, int additional, int total)
{
    MarcusPlaybackAudio *audio = p;
    float output[MM_MOVIE_AUDIO_BLOCK_FRAMES * 2];
    const size_t frame_bytes = 2 * sizeof(*output);
    size_t frames;
    int bytes;
    (void)total;
    while (additional > 0)
    {
        frames = ((size_t)additional + frame_bytes - 1) / frame_bytes;
        if (frames > MM_MOVIE_AUDIO_BLOCK_FRAMES)
            frames = MM_MOVIE_AUDIO_BLOCK_FRAMES;
        SDL_LockMutex(audio->mutex);
        mm_movie_audio_render(audio->mixer, output, frames);
        SDL_UnlockMutex(audio->mutex);
        bytes = (int)(frames * frame_bytes);
        if (!SDL_PutAudioStreamData(stream, output, bytes))
            break;
        additional -= bytes;
    }
}

static int open_device(MarcusPlaybackAudio *audio, char *error, size_t capacity)
{
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, MARCUS_PLAYBACK_AUDIO_RATE};
    SDL_AudioStream *stream;
    if (audio->stream)
        return 1;
    if (!audio->initialized)
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            SDL_snprintf(error, capacity, "Cannot initialize movie audio: %s", SDL_GetError());
            return 0;
        }
        audio->initialized = 1;
    }
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, audio);
    if (!stream || !SDL_ResumeAudioStreamDevice(stream))
    {
        SDL_snprintf(error, capacity, "Cannot open movie audio output: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        return 0;
    }
    audio->stream = stream;
    return 1;
}

MarcusPlaybackAudio *marcus_playback_audio_create(const char *fallback_bank)
{
    MarcusPlaybackAudio *audio = SDL_calloc(1, sizeof(*audio));
    if (!audio)
        return NULL;
    audio->fallback_bank = fallback_bank;
    audio->mutex = SDL_CreateMutex();
    audio->mixer = mm_movie_audio_create();
    audio->clips = mm_movie_audio_cache_create(256UL * 1024UL * 1024UL);
    if (!audio->mutex || !audio->mixer || !audio->clips)
    {
        marcus_playback_audio_destroy(audio);
        return NULL;
    }
    return audio;
}

void marcus_playback_audio_destroy(MarcusPlaybackAudio *audio)
{
    if (!audio)
        return;
    SDL_DestroyAudioStream(audio->stream);
    if (audio->initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    mm_movie_audio_destroy(audio->mixer);
    mm_movie_audio_cache_destroy(audio->clips);
    SDL_free(audio->bank);
    SDL_DestroyMutex(audio->mutex);
    SDL_free(audio);
}

void marcus_playback_audio_clear(MarcusPlaybackAudio *audio)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_clear(audio->mixer);
    SDL_UnlockMutex(audio->mutex);
    /* SDL can hold the stream lock while calling feed. */
    if (audio->stream)
        SDL_ClearAudioStream(audio->stream);
    mm_movie_audio_cache_clear(audio->clips);
}

void marcus_playback_audio_cut(MarcusPlaybackAudio *audio)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_cut(audio->mixer);
    SDL_UnlockMutex(audio->mutex);
}

void marcus_playback_audio_suspend(MarcusPlaybackAudio *audio, int suspended)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_suspend(audio->mixer, suspended);
    SDL_UnlockMutex(audio->mutex);
}

void marcus_playback_audio_stop(MarcusPlaybackAudio *audio)
{
    audio->auditioning = 0;
    marcus_playback_audio_suspend(audio, 1);
    marcus_playback_audio_clear(audio);
}

void marcus_playback_audio_update(MarcusPlaybackAudio *audio, int playing)
{
    if (playing && audio->auditioning)
    {
        marcus_playback_audio_clear(audio);
        audio->auditioning = 0;
    }
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_collect(audio->mixer);
    if (audio->auditioning && !mm_movie_audio_pending(audio->mixer))
        audio->auditioning = 0;
    mm_movie_audio_suspend(audio->mixer, !playing && !audio->auditioning);
    SDL_UnlockMutex(audio->mutex);
}

int marcus_playback_audio_pending(MarcusPlaybackAudio *audio)
{
    int result;
    SDL_LockMutex(audio->mutex);
    result = mm_movie_audio_pending(audio->mixer);
    SDL_UnlockMutex(audio->mutex);
    return result;
}

int marcus_playback_audio_auditioning(MarcusPlaybackAudio *audio)
{
    return audio->auditioning && marcus_playback_audio_pending(audio);
}

int marcus_playback_audio_set_mix(MarcusPlaybackAudio *audio, MMAudioCounter lane, int muted, int solo)
{
    int result;
    SDL_LockMutex(audio->mutex);
    result = mm_movie_audio_set_mix(audio->mixer, lane, muted, solo);
    SDL_UnlockMutex(audio->mutex);
    return result;
}

void marcus_playback_audio_track_mix(MarcusPlaybackAudio *audio, MMAudioCounter lane, int *muted, int *solo, int *audible)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_track_mix(audio->mixer, lane, muted, solo, audible);
    SDL_UnlockMutex(audio->mutex);
}

void marcus_playback_audio_reset_mix(MarcusPlaybackAudio *audio)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_reset_mix(audio->mixer);
    SDL_UnlockMutex(audio->mutex);
}

int marcus_playback_audio_mix_active(MarcusPlaybackAudio *audio)
{
    int result;
    SDL_LockMutex(audio->mutex);
    result = mm_movie_audio_mix_active(audio->mixer);
    SDL_UnlockMutex(audio->mutex);
    return result;
}

void marcus_playback_audio_mute(MarcusPlaybackAudio *audio, int muted)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_mute(audio->mixer, muted);
    SDL_UnlockMutex(audio->mutex);
}

void marcus_playback_audio_stats(MarcusPlaybackAudio *audio, MMMovieAudioStats *stats)
{
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_stats(audio->mixer, stats);
    SDL_UnlockMutex(audio->mutex);
}

const void *marcus_playback_audio_bank(MarcusPlaybackAudio *audio, size_t *bytes)
{
    const char *base;
    char *path;
    if (!audio->bank)
    {
        base = SDL_GetBasePath();
        if (SDL_asprintf(&path, "%sassets/GeneralUserGS.sf2", base ? base : "") >= 0)
        {
            audio->bank = SDL_LoadFile(path, &audio->bank_bytes);
            SDL_free(path);
        }
        if (!audio->bank && audio->fallback_bank)
            audio->bank = SDL_LoadFile(audio->fallback_bank, &audio->bank_bytes);
        if (!audio->bank_bytes)
        {
            SDL_free(audio->bank);
            audio->bank = NULL;
        }
    }
    *bytes = audio->bank_bytes;
    return audio->bank;
}

int marcus_playback_audio_play(MarcusPlaybackAudio *audio, const MarcusPlaybackSound *sound, MMAudioCounter offset,
                               const MMMovieAudioDecode *decode, int audition, char *error, size_t capacity)
{
    const MMMovieAudioKey *key = sound->silent ? NULL : &sound->key;
    MMMovieAudioVoice *voice;
    int submitted;
    int status = mm_movie_audio_cache_voice(audio->clips, key, decode, offset, sound->volume,
                                            sound->loop && !audition, &voice);
    if (status != MM_MOVIE_AUDIO_CACHE_OK)
        return status;
    if (voice && !open_device(audio, error, capacity))
    {
        mm_movie_audio_voice_destroy(voice);
        return MARCUS_PLAYBACK_AUDIO_DEVICE;
    }
    SDL_LockMutex(audio->mutex);
    submitted = mm_movie_audio_submit(audio->mixer, mm_movie_audio_lane(sound->actor_sound, sound->actor, sound->style),
                                      sound->queue && !audition, voice);
    if (submitted && audition)
    {
        audio->auditioning = 1;
        mm_movie_audio_suspend(audio->mixer, 0);
    }
    SDL_UnlockMutex(audio->mutex);
    return submitted ? MM_MOVIE_AUDIO_CACHE_OK : MM_MOVIE_AUDIO_CACHE_MEMORY;
}
