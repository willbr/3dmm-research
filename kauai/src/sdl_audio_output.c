/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio_output.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <stdlib.h>

enum { OUTPUT_BLOCK_FRAMES = 512 };
struct MMSdlAudioOutput
{
    MMAudioDeviceMixer *mixer;
    SDL_AudioStream *stream;
    MMSdlAudioRender render;
    void *render_context;
    size_t frame_limit;
    int initialized;
};

static void SDLCALL feed(void *context, SDL_AudioStream *stream, int additional, int total)
{
    MMSdlAudioOutput *output = (MMSdlAudioOutput *)context;
    float buffer[OUTPUT_BLOCK_FRAMES * 2];
    const size_t frame_bytes = 2 * sizeof(float);
    (void)total;
    while (additional > 0)
    {
        size_t frames = ((size_t)additional + frame_bytes - 1) / frame_bytes;
        int bytes;
        if (frames > output->frame_limit)
            frames = output->frame_limit;
        output->render(output->render_context, buffer, frames);
        bytes = (int)(frames * frame_bytes);
        if (!SDL_PutAudioStreamData(stream, buffer, bytes))
            break;
        additional -= bytes;
    }
}

MMSdlAudioOutput *mm_sdl_audio_output_create(void *context, MMSdlAudioRender render, size_t frame_limit)
{
    MMSdlAudioOutput *output;
    if (!render || !frame_limit || frame_limit > OUTPUT_BLOCK_FRAMES)
        return NULL;
    output = (MMSdlAudioOutput *)calloc(1, sizeof(*output));
    if (!output)
        return NULL;
    output->render_context = context;
    output->render = render;
    output->frame_limit = frame_limit;
    return output;
}

static void render_mixer(void *context, float *buffer, size_t frames)
{
    MMAudioDeviceMixer *device = (MMAudioDeviceMixer *)context;
    MMAudioMixer *mixer = mm_audio_device_mixer_enter(device);
    mm_audio_mixer_render(mixer, buffer, frames);
    mm_audio_device_mixer_leave(device);
}

MMSdlAudioOutput *mm_sdl_audio_output_create_mixer(void)
{
    MMSdlAudioOutput *output = mm_sdl_audio_output_create(NULL, render_mixer, OUTPUT_BLOCK_FRAMES);
    if (!output)
        return NULL;
    output->mixer = mm_audio_device_mixer_create();
    if (!output->mixer)
    {
        free(output);
        return NULL;
    }
    output->render_context = output->mixer;
    return output;
}

void mm_sdl_audio_output_destroy(MMSdlAudioOutput *output)
{
    if (!output)
        return;
    SDL_DestroyAudioStream(output->stream);
    if (output->initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    mm_audio_device_mixer_destroy(output->mixer);
    free(output);
}

int mm_sdl_audio_output_open(MMSdlAudioOutput *output, void *error_context, MMSdlAudioOutputError error)
{
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, MM_AUDIO_MIX_RATE};
    if (output->stream)
        return 1;
    if (!output->initialized)
    {
        if (output->mixer)
            SDL_SetMainReady();
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            if (error)
                error(error_context, 1, SDL_GetError());
            return 0;
        }
        output->initialized = 1;
    }
    output->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, output);
    if (output->stream && SDL_ResumeAudioStreamDevice(output->stream))
        return 1;
    if (error)
        error(error_context, 0, SDL_GetError());
    SDL_DestroyAudioStream(output->stream);
    output->stream = NULL;
    return 0;
}

void mm_sdl_audio_output_clear(MMSdlAudioOutput *output)
{
    if (output->stream)
        SDL_ClearAudioStream(output->stream);
}

MMAudioDeviceMixer *mm_sdl_audio_output_mixer(MMSdlAudioOutput *output)
{
    return output->mixer;
}
