#include "playback_audio.h"
#include "sdl_audio_output.h"
#include <SDL3/SDL.h>

struct MarcusPlaybackAudio
{
    MMMovieAudio *mixer;
    MMMovieAudioCache *clips;
    SDL_Mutex *mutex;
    MMSdlAudioOutput *output;
    const char *fallback_bank;
    void *bank;
    size_t bank_bytes;
    int auditioning;
};

static void render_output(void *p, float *buffer, size_t frames)
{
    MarcusPlaybackAudio *audio = p;
    SDL_LockMutex(audio->mutex);
    mm_movie_audio_render(audio->mixer, buffer, frames);
    SDL_UnlockMutex(audio->mutex);
}

typedef struct AudioDeviceError
{
    char *message;
    size_t capacity;
} AudioDeviceError;

static void report_device_error(void *p, int initializing, const char *detail)
{
    AudioDeviceError *error = p;
    SDL_snprintf(error->message, error->capacity, initializing ? "Cannot initialize movie audio: %s"
                                                                : "Cannot open movie audio output: %s", detail);
}
static int open_device(MarcusPlaybackAudio *audio, char *error, size_t capacity)
{
    AudioDeviceError report;
    report.message = error;
    report.capacity = capacity;
    return mm_sdl_audio_output_open(audio->output, &report, report_device_error);
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
    audio->output = mm_sdl_audio_output_create(audio, render_output, MM_MOVIE_AUDIO_BLOCK_FRAMES);
    if (!audio->mutex || !audio->mixer || !audio->clips || !audio->output)
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
    mm_sdl_audio_output_destroy(audio->output);
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
    mm_sdl_audio_output_clear(audio->output);
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

int marcus_playback_audio_play(MarcusPlaybackAudio *audio, const MMPreviewCue *sound, MMU32 active_scene,
                               MMAudioCounter offset,
                               const MMMovieAudioDecode *decode, int audition, char *error, size_t capacity)
{
    MMMovieAudioKey key;
    MMMovieAudioVoice *voice;
    int submitted, status;
    key.source = sound->source;
    key.type = sound->type;
    key.number = sound->number;
    key.scene = sound->source == 0 ? (sound->scene_type ? sound->scene_number : active_scene) : 0;
    key.child = sound->child;
    status = mm_movie_audio_cache_voice(audio->clips, sound->silent ? NULL : &key, decode, offset, sound->volume,
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
