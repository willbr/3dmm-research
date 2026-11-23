/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_recording.h"
#include "recording_data.h"
#include "sdl_audio_output.h"
#include "sdl_wave.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <stdlib.h>

struct MMSdlRecorder
{
    MMRecordingBuffer buffer;
    MMWaveBuffer wave;
    MMU32 wave_bytes;
    SDL_AudioStream *capture;
    MMSdlAudioOutput *output;
    MMAudioCounter queue;
    int initialized, recording, playing, failed;
};

static SDL_SpinLock stats_lock;
static MMSdlRecordingStats stats;

static void count_stat(MMAudioCounter *stat, size_t amount)
{
    SDL_LockSpinlock(&stats_lock);
    *stat = mm_audio_count_add(*stat, mm_audio_count_size(amount));
    SDL_UnlockSpinlock(&stats_lock);
}

void mm_sdl_recorder_stats(MMSdlRecordingStats *out)
{
    SDL_LockSpinlock(&stats_lock);
    *out = stats;
    SDL_UnlockSpinlock(&stats_lock);
}

static void drain_capture(MMSdlRecorder *recorder, SDL_AudioStream *stream)
{
    unsigned char bytes[4096];
    int count;
    while ((count = SDL_GetAudioStreamData(stream, bytes, sizeof(bytes))) > 0)
        if (recorder->recording)
            mm_recording_append(&recorder->buffer, bytes, (size_t)count);
    if (count < 0)
        recorder->failed = 1;
}

static void SDLCALL feed(void *context, SDL_AudioStream *stream, int additional, int total)
{
    (void)additional;
    (void)total;
    drain_capture(context, stream);
}

void mm_sdl_recorder_destroy(MMSdlRecorder *recorder)
{
    if (!recorder)
        return;
    SDL_DestroyAudioStream(recorder->capture);
    mm_sdl_audio_output_destroy(recorder->output);
    if (recorder->initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    mm_wave_buffer_dispose(&recorder->wave);
    mm_recording_dispose(&recorder->buffer);
    free(recorder);
}

int mm_sdl_recorder_open(MMSdlRecorder **slot, MMU32 rate, unsigned channels, unsigned bits, MMU32 max_ms)
{
    MMSdlRecorder *next = calloc(1, sizeof(*next));
    SDL_AudioSpec spec;
    if (!next)
        return SDL_OutOfMemory();
    if (!mm_recording_init(&next->buffer, rate, channels, bits, max_ms))
        goto fail;
    mm_wave_buffer_init(&next->wave, NULL);
    next->output = mm_sdl_audio_output_create_mixer();
    if (!next->output)
        goto fail;
    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        goto fail;
    next->initialized = 1;
    spec.format = bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels = (int)channels;
    spec.freq = (int)rate;
    next->capture = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, feed, next);
    if (!next->capture)
        goto fail;
    mm_sdl_recorder_destroy(*slot);
    *slot = next;
    count_stat(&stats.opened, 1);
    return 1;
fail:
    mm_sdl_recorder_destroy(next);
    return 0;
}

int mm_sdl_recorder_start(MMSdlRecorder *recorder)
{
    if (!recorder || recorder->recording)
        return 0;
    if (recorder->playing && !mm_sdl_recorder_stop(recorder))
        return 0;
    SDL_LockAudioStream(recorder->capture);
    SDL_ClearAudioStream(recorder->capture);
    mm_recording_reset(&recorder->buffer);
    recorder->wave_bytes = 0;
    recorder->failed = 0;
    recorder->recording = 1;
    SDL_UnlockAudioStream(recorder->capture);
    if (!SDL_ResumeAudioStreamDevice(recorder->capture))
    {
        SDL_LockAudioStream(recorder->capture);
        recorder->recording = 0;
        SDL_UnlockAudioStream(recorder->capture);
        return 0;
    }
    count_stat(&stats.started, 1);
    return 1;
}

int mm_sdl_recorder_stop(MMSdlRecorder *recorder)
{
    MMAudioDeviceMixer *device;
    size_t frames;
    int failed;
    if (!recorder)
        return 1;
    if (recorder->playing)
    {
        device = mm_sdl_audio_output_mixer(recorder->output);
        mm_audio_mixer_sync(mm_audio_device_mixer_enter(device), recorder->queue, NULL, 0);
        mm_audio_device_mixer_leave(device);
        recorder->playing = 0;
    }
    if (!recorder->recording)
        return 1;
    if (!SDL_PauseAudioStreamDevice(recorder->capture))
        return 0;
    SDL_LockAudioStream(recorder->capture);
    SDL_FlushAudioStream(recorder->capture);
    drain_capture(recorder, recorder->capture);
    recorder->recording = 0;
    failed = recorder->failed;
    frames = mm_recording_frames(&recorder->buffer);
    SDL_UnlockAudioStream(recorder->capture);
    count_stat(&stats.captured_frames, frames);
    if (failed)
        return 0;
    if (frames > 8)
    {
        if (!mm_recording_wave(&recorder->buffer, &recorder->wave, &recorder->wave_bytes))
            return 0;
        count_stat(&stats.completed, 1);
    }
    return 1;
}

int mm_sdl_recorder_recording(MMSdlRecorder *recorder)
{
    int done;
    if (!recorder || !recorder->recording)
        return 0;
    SDL_LockAudioStream(recorder->capture);
    done = recorder->failed || mm_recording_full(&recorder->buffer);
    SDL_UnlockAudioStream(recorder->capture);
    if (done)
        mm_sdl_recorder_stop(recorder);
    return recorder->recording;
}

int mm_sdl_recorder_play(MMSdlRecorder *recorder, float volume)
{
    MMAudioSample *sample;
    MMAudioDeviceMixer *device;
    MMAudioMixer *mixer;
    MMAudioVoice voice;
    int ok;
    if (!recorder || recorder->recording || !recorder->wave_bytes || !mm_sdl_recorder_stop(recorder) ||
        !mm_sdl_audio_output_open(recorder->output, NULL, NULL) ||
        !mm_sdl_wave_decode(recorder->wave.data, recorder->wave_bytes, &sample))
        return 0;
    device = mm_sdl_audio_output_mixer(recorder->output);
    mixer = mm_audio_device_mixer_enter(device);
    if (!recorder->queue.low && !recorder->queue.high)
        recorder->queue = mm_audio_mixer_add(mixer);
    voice = mm_audio_sample_voice(sample, 1, 1);
    mm_audio_mixer_volume(mixer, volume);
    ok = (recorder->queue.low || recorder->queue.high) && mm_audio_mixer_sync(mixer, recorder->queue, &voice, 1);
    mm_audio_device_mixer_leave(device);
    mm_audio_sample_release(sample);
    if (ok)
    {
        recorder->playing = 1;
        count_stat(&stats.previewed, 1);
    }
    return ok;
}

void mm_sdl_recorder_suspend_preview(MMSdlRecorder *recorder, int suspended)
{
    MMAudioDeviceMixer *device = mm_sdl_audio_output_mixer(recorder->output);
    mm_audio_mixer_suspend(mm_audio_device_mixer_enter(device), suspended);
    mm_audio_device_mixer_leave(device);
}

int mm_sdl_recorder_playing(MMSdlRecorder *recorder)
{
    MMAudioDeviceMixer *device;
    int done;
    if (!recorder || !recorder->playing)
        return 0;
    device = mm_sdl_audio_output_mixer(recorder->output);
    done = mm_audio_mixer_done(mm_audio_device_mixer_enter(device), recorder->queue, 1);
    mm_audio_device_mixer_leave(device);
    if (done)
        mm_sdl_recorder_stop(recorder);
    return recorder->playing;
}

const unsigned char *mm_sdl_recorder_wave(const MMSdlRecorder *recorder, MMU32 *bytes)
{
    MMU32 count = recorder ? recorder->wave_bytes : 0;
    if (bytes)
        *bytes = count;
    return count ? recorder->wave.data : NULL;
}
