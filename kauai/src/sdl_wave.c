/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_wave.h"
#include "compressed_wave_decode.h"
#include <SDL3/SDL.h>
#include <limits.h>

typedef struct WavePCM
{
    SDL_AudioSpec source;
    unsigned char *sdl_data;
    MMU32 bytes;
    MMDecodedWave compressed;
} WavePCM;

static const unsigned char *wave_data(const WavePCM *pcm)
{
    return pcm->sdl_data ? pcm->sdl_data : (const unsigned char *)pcm->compressed.samples;
}

static void release_pcm(WavePCM *pcm)
{
    SDL_free(pcm->sdl_data);
    mm_compressed_wave_dispose(&pcm->compressed);
}

static int load_pcm(WavePCM *pcm, const void *data, size_t bytes)
{
    MMU32 frames;
    Uint32 decoded;
    int width, format;
    SDL_IOStream *input;
    format = mm_compressed_wave_decode(data, bytes, &pcm->compressed);
    if (format == MM_WAVE_INVALID)
        return SDL_SetError("Invalid compressed WAVE audio");
    if (format != MM_WAVE_NOT_HANDLED)
    {
        pcm->source.format = SDL_AUDIO_S16;
        pcm->source.channels = pcm->compressed.channels;
        pcm->source.freq = pcm->compressed.rate;
        pcm->bytes = (MMU32)(pcm->compressed.count * sizeof(short));
        return 1;
    }
    input = SDL_IOFromConstMem(data, bytes);
    if (!input || !SDL_LoadWAV_IO(input, true, &pcm->source, &pcm->sdl_data, &decoded))
        return 0;
    width = SDL_AUDIO_BYTESIZE(pcm->source.format) * pcm->source.channels;
    if (width > 0 && mm_wave_fact_frames(data, bytes, &frames) && frames < decoded / (unsigned)width)
        decoded = frames * (unsigned)width;
    pcm->bytes = decoded;
    return 1;
}

static int import_load(void *context, const void *data, MMU32 bytes, MMU32 *decoded)
{
    WavePCM *pcm = context;
    if (!load_pcm(pcm, data, bytes))
        return 0;
    *decoded = pcm->bytes;
    return 1;
}

static int import_convert(void *context, MMI32 rate, unsigned char **samples, MMI32 *count)
{
    WavePCM *pcm = context;
    SDL_AudioSpec destination = {SDL_AUDIO_U8, 1, 0};
    int bytes = 0, ok;
    destination.freq = rate;
    ok = SDL_ConvertAudioSamples(&pcm->source, wave_data(pcm), (int)pcm->bytes, &destination, samples, &bytes);
    *count = bytes;
    return ok;
}

static void free_samples(void *context, unsigned char *samples)
{
    (void)context;
    SDL_free(samples);
}

int mm_sdl_wave_import(void *context, const MMReadStream *source, MMI32 style, const MMChunkNameValue *name,
                       MMU32 *number, const MMArchiveWriter *writer)
{
    WavePCM pcm = {0};
    MMWaveImportServices services = {NULL, import_load, import_convert, free_samples};
    int result;
    (void)context;
    services.context = &pcm;
    result = mm_wave_decode_import(source, style, name, number, &services, writer);
    release_pcm(&pcm);
    return result;
}

int mm_sdl_wave_prepare(const void *data, size_t bytes, MMWaveBuffer *scratch, const unsigned char **selected, MMU32 *size)
{
    WavePCM pcm = {0};
    MMWaveImportServices services = {NULL, import_load, import_convert, free_samples};
    int result;
    if (bytes > INT_MAX)
        return 0;
    services.context = &pcm;
    result = mm_wave_prepare(data, (MMU32)bytes, &services, scratch, selected, size);
    release_pcm(&pcm);
    return result;
}

int mm_sdl_wave_seconds(const void *data, size_t bytes, double *seconds)
{
    WavePCM pcm = {0};
    const SDL_AudioSpec destination = {SDL_AUDIO_F32, 2, MM_AUDIO_MIX_RATE};
    SDL_AudioStream *stream;
    int available = -1;
    *seconds = 0;
    if (!data || bytes < 12 || bytes > INT_MAX)
        return 0;
    if (load_pcm(&pcm, data, bytes) && pcm.bytes <= INT_MAX)
    {
        stream = SDL_CreateAudioStream(&pcm.source, &destination);
        if (stream)
        {
            /* Match conversion's flushed output length without resampling. */
            if (SDL_PutAudioStreamData(stream, wave_data(&pcm), (int)pcm.bytes) && SDL_FlushAudioStream(stream))
                available = SDL_GetAudioStreamAvailable(stream);
            SDL_DestroyAudioStream(stream);
        }
    }
    release_pcm(&pcm);
    if (available < 0 || available % (2 * sizeof(float)))
        return 0;
    *seconds = (double)(available / (2 * sizeof(float))) / MM_AUDIO_MIX_RATE;
    return 1;
}

static void free_converted(void *samples)
{
    SDL_free(samples);
}

int mm_sdl_wave_decode(const void *data, size_t bytes, MMAudioSample **out)
{
    WavePCM pcm = {0};
    const SDL_AudioSpec destination = {SDL_AUDIO_F32, 2, MM_AUDIO_MIX_RATE};
    unsigned char *converted = NULL;
    MMAudioSample *sample;
    int available = 0, ok;
    if (!data || bytes < 12 || bytes > INT_MAX)
        return 0;
    ok = load_pcm(&pcm, data, bytes) && pcm.bytes <= INT_MAX &&
         SDL_ConvertAudioSamples(&pcm.source, wave_data(&pcm), (int)pcm.bytes, &destination, &converted, &available);
    release_pcm(&pcm);
    if (ok && available >= 0 && !(available % (2 * sizeof(float))))
    {
        sample = mm_audio_sample_adopt((float *)converted, available / (2 * sizeof(float)), free_converted);
        if (sample)
        {
            *out = sample;
            return 1;
        }
    }
    SDL_free(converted);
    return 0;
}
