/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "compressed_wave_decode.h"
#include <gsm.h>
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int decode_gsm(const MMWaveView *wave, MMWaveBuffer *pcm, size_t *count)
{
    gsm state;
    gsm_byte block[65];
    gsm_signal *samples;
    int enabled = 1, ok = 0;
    size_t at, sample;
    *count = (size_t)(wave->data_bytes / 65) * 320;
    if (!mm_wave_buffer_reserve(pcm, (MMU32)(*count * sizeof(*samples))))
        return 0;
    state = gsm_create();
    if (!state)
        return 0;
    if (gsm_option(state, GSM_OPT_WAV49, &enabled) < 0)
        goto done;
    samples = (gsm_signal *)pcm->data;
    for (at = 0, sample = 0; at < wave->data_bytes; at += sizeof(block), sample += 320)
    {
        memcpy(block, wave->data + at, sizeof(block));
        if (gsm_decode(state, block, samples + sample) || gsm_decode(state, block + 33, samples + sample + 160))
            goto done;
    }
    ok = 1;
done:
    gsm_destroy(state);
    return ok;
}

static int decode_mp3(const MMWaveView *wave, MMI32 channels, MMI32 rate, MMWaveBuffer *pcm, size_t *count)
{
    drmp3 decoder;
    drmp3_int16 samples[4096 * 2];
    size_t batch, bytes, capacity;
    int ok = 0;
    if (!drmp3_init_memory(&decoder, wave->data, wave->data_bytes, NULL))
        return 0;
    if (decoder.channels != (unsigned)channels || decoder.sampleRate != (unsigned)rate)
        goto done;
    for (;;)
    {
        batch = (size_t)drmp3_read_pcm_frames_s16(&decoder, 4096, samples) * (unsigned)channels;
        if (!batch)
            break;
        if (batch > INT_MAX / sizeof(*samples) - *count)
            goto done;
        bytes = (*count + batch) * sizeof(*samples);
        if (bytes > pcm->capacity)
        {
            capacity = pcm->capacity <= INT_MAX / 2 ? 2 * pcm->capacity : INT_MAX;
            if (!mm_wave_buffer_reserve(pcm, (MMU32)(capacity < bytes ? bytes : capacity)))
                goto done;
        }
        memcpy(pcm->data + *count * sizeof(*samples), samples, batch * sizeof(*samples));
        *count += batch;
    }
    ok = *count != 0;
done:
    drmp3_uninit(&decoder);
    return ok;
}

int mm_compressed_wave_decode(const void *data, size_t bytes, MMDecodedWave *out)
{
    MMWaveView wave;
    MMWaveBuffer pcm;
    MMDecodedWave result = {0};
    int encoding = mm_wave_compressed(data, bytes, &wave, &result.rate, &result.channels);
    int ok;
    if (encoding <= 0)
        return encoding;
    mm_wave_buffer_init(&pcm, NULL);
    ok = encoding == MM_WAVE_GSM ? decode_gsm(&wave, &pcm, &result.count)
                                : decode_mp3(&wave, result.channels, result.rate, &pcm, &result.count);
    if (!ok)
    {
        mm_wave_buffer_dispose(&pcm);
        return MM_WAVE_INVALID;
    }
    result.count = (size_t)mm_wave_trim_frames(encoding, wave.fact, (MMU32)(result.count / result.channels)) *
                   result.channels;
    result.samples = (short *)pcm.data;
    *out = result;
    return encoding;
}

void mm_compressed_wave_dispose(MMDecodedWave *wave)
{
    free(wave->samples);
    memset(wave, 0, sizeof(*wave));
}
