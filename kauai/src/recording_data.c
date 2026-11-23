/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "recording_data.h"
#include "mm/audio_clock.h"
#include <stdlib.h>
#include <string.h>

int mm_recording_init(MMRecordingBuffer *buffer, MMU32 rate, unsigned channels, unsigned bits, MMU32 max_ms)
{
    MMAudioCounter frames;
    unsigned char *data;
    MMU32 bytes, width;
    if (!rate || rate > 192000 || !channels || channels > 2 || (bits != 8 && bits != 16) || !max_ms)
        return 0;
    width = channels * (bits / 8);
    frames = mm_audio_count_divide(mm_audio_count_multiply(rate, max_ms), mm_audio_counter(1000, 0), NULL);
    if (frames.high || !frames.low || frames.low > (0x7fffffffUL - 45) / width)
        return 0;
    bytes = frames.low * width;
    if ((size_t)bytes != bytes)
        return 0;
    data = malloc((size_t)bytes);
    if (!data)
        return 0;
    free(buffer->data);
    buffer->data = data;
    buffer->capacity = bytes;
    buffer->used = 0;
    buffer->rate = rate;
    buffer->channels = channels;
    buffer->bits = bits;
    buffer->frame_bytes = width;
    return 1;
}

void mm_recording_dispose(MMRecordingBuffer *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

void mm_recording_reset(MMRecordingBuffer *buffer)
{
    buffer->used = 0;
}

size_t mm_recording_append(MMRecordingBuffer *buffer, const void *samples, size_t bytes)
{
    size_t available = buffer->capacity - buffer->used;
    if (!samples || !buffer->frame_bytes || bytes % buffer->frame_bytes)
        return 0;
    if (bytes > available)
        bytes = available;
    if (bytes)
        memcpy(buffer->data + buffer->used, samples, bytes);
    buffer->used += bytes;
    return bytes;
}

int mm_recording_full(const MMRecordingBuffer *buffer)
{
    return buffer->capacity && buffer->used == buffer->capacity;
}

size_t mm_recording_frames(const MMRecordingBuffer *buffer)
{
    return buffer->frame_bytes ? buffer->used / buffer->frame_bytes : 0;
}

static int sample_value(const unsigned char *pcm, size_t sample, unsigned bits)
{
    unsigned word;
    if (bits == 8)
        return (int)pcm[sample] - 128;
    word = pcm[sample * 2] | ((unsigned)pcm[sample * 2 + 1] << 8);
    return word >= 32768 ? (int)word - 65536 : (int)word;
}

static int bias_value(const MMRecordingBuffer *buffer, size_t frames)
{
    /* Each side is bounded by 96,000 samples times 32,768. */
    MMU32 positive = 0, negative = 0;
    size_t margin = buffer->rate / 4, count, i;
    int value, bias;
    if (!margin)
        margin = 1;
    count = (frames < margin ? frames : margin) * buffer->channels;
    for (i = 0; i < count; ++i)
    {
        value = sample_value(buffer->data, i, buffer->bits);
        if (value < 0)
            negative += (MMU32)-value;
        else
            positive += (MMU32)value;
    }
    bias = positive >= negative ? (int)((positive - negative) / count) : -(int)((negative - positive) / count);
    return abs(bias) < (buffer->bits == 8 ? 3 : 1000) ? 0 : bias;
}

static void center_samples(unsigned char *pcm, size_t count, unsigned bits, int bias)
{
    size_t i;
    unsigned word;
    /* AudioMan subtracts the bias with wraparound at the source width. */
    for (i = 0; i < count; ++i)
    {
        word = (unsigned)(sample_value(pcm, i, bits) - bias);
        if (bits == 8)
            pcm[i] = (unsigned char)(word + 128);
        else
        {
            pcm[2 * i] = (unsigned char)word;
            pcm[2 * i + 1] = (unsigned char)(word >> 8);
        }
    }
}

static void trim(const MMRecordingBuffer *buffer, const unsigned char *pcm, size_t frames, size_t *first, size_t *end)
{
    size_t frame, begin = frames, last = 0, margin = buffer->rate / 4;
    unsigned channel;
    int gate = buffer->bits == 8 ? 3 : 823;
    for (frame = 0; frame < frames; ++frame)
        for (channel = 0; channel < buffer->channels; ++channel)
            if (abs(sample_value(pcm, frame * buffer->channels + channel, buffer->bits)) >= gate)
            {
                if (begin == frames)
                    begin = frame;
                last = frame + 1;
                break;
            }
    *first = 0;
    *end = frames;
    /* Silence is retained. AudioMan's first active position is one-based. */
    if (begin != frames)
    {
        ++begin;
        if (begin > margin)
            begin -= margin;
        last += 2 * margin;
        if (last > frames)
            last = frames;
        if (begin < last)
        {
            *first = begin;
            *end = last;
        }
    }
}

int mm_recording_wave(const MMRecordingBuffer *buffer, MMWaveBuffer *out, MMU32 *bytes)
{
    size_t frames = mm_recording_frames(buffer), first, end;
    const unsigned char *samples;
    unsigned char *centered = NULL;
    int bias, ok;
    MMU32 count, needed;
    if (frames <= 8)
        return 0;
    frames -= 8;
    samples = buffer->data;
    bias = bias_value(buffer, frames);
    if (bias)
    {
        centered = malloc(frames * buffer->frame_bytes);
        if (!centered)
            return 0;
        memcpy(centered, samples, frames * buffer->frame_bytes);
        center_samples(centered, frames * buffer->channels, buffer->bits, bias);
        samples = centered;
    }
    trim(buffer, samples, frames, &first, &end);
    samples += first * buffer->frame_bytes;
    count = (MMU32)((end - first) * buffer->frame_bytes);
    ok = mm_wave_encode(samples, count, buffer->rate, buffer->channels, buffer->bits, 0, NULL, 0, &needed) &&
         mm_wave_buffer_reserve(out, needed) &&
         mm_wave_encode(samples, count, buffer->rate, buffer->channels, buffer->bits, 0, out->data, needed, bytes);
    free(centered);
    return ok;
}
