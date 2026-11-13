/* WAVE policy converted from Kauai wave_codec.cpp and compressed_wave.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/wave.h"
#include "three_mm/collection_storage.h"
#include <string.h>
#include <stdlib.h>
static const ThreeMMI32 coefficients[7][2] = {{256, 0}, {512, -256}, {0, 0},     {192, 64},
                                              {240, 0}, {460, -208}, {392, -232}};
static const ThreeMMI32 adaptation[16] = {230, 230, 230, 230, 307, 409, 512, 614,
                                          768, 614, 512, 409, 307, 230, 230, 230};
static unsigned u16(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static ThreeMMU32 u32(const unsigned char *p)
{
    return u16(p) | ((ThreeMMU32)u16(p + 2) << 16);
}
static void word(unsigned char **p, ThreeMMU32 n)
{
    *(*p)++ = (unsigned char)n;
    *(*p)++ = (unsigned char)(n >> 8);
}
static void dword(unsigned char **p, ThreeMMU32 n)
{
    word(p, n);
    word(p, n >> 16);
}
static void tag(unsigned char **p, const char *s)
{
    memcpy(*p, s, 4);
    *p += 4;
}
static int riff(const unsigned char *p, size_t bytes, ThreeMMU32 *end)
{
    ThreeMMU32 n;
    if (!p || bytes < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        return 0;
    n = u32(p + 4);
    if (n < 4 || n > 0xfffffff7UL || n > bytes - 8)
        return 0;
    *end = n + 8;
    return 1;
}
int three_mm_wave_parse(const void *data, size_t bytes, ThreeMMWaveView *out)
{
    const unsigned char *p = (const unsigned char *)data;
    ThreeMMWaveView v;
    ThreeMMU32 end, at, n;
    memset(&v, 0, sizeof(v));
    if (!out || !riff(p, bytes, &end))
        return 0;
    for (at = 12; at < end;)
    {
        if (end - at < 8)
            return 0;
        n = u32(p + at + 4);
        if (n > end - at - 8)
            return 0;
        if (!memcmp(p + at, "fmt ", 4) && !v.format)
        {
            v.format = p + at + 8;
            v.format_bytes = n;
        }
        else if (!memcmp(p + at, "data", 4) && !v.data)
        {
            v.data = p + at + 8;
            v.data_bytes = n;
        }
        else if (!memcmp(p + at, "fact", 4) && n >= 4)
            v.fact = u32(p + at + 8);
        at += 8 + n;
        if ((n & 1) && at < end)
            ++at;
    }
    if (!v.format || v.format_bytes < 16 || !v.data || !v.data_bytes)
        return 0;
    *out = v;
    return 1;
}
int three_mm_wave_compressed(const void *data, size_t bytes, ThreeMMWaveView *out, ThreeMMI32 *rate,
                             ThreeMMI32 *channels)
{
    ThreeMMWaveView v;
    ThreeMMU32 hz;
    unsigned code, ch;
    if (!three_mm_wave_parse(data, bytes, &v))
        return THREE_MM_WAVE_NOT_HANDLED;
    code = u16(v.format);
    if (code != THREE_MM_WAVE_GSM && code != THREE_MM_WAVE_MP3)
        return THREE_MM_WAVE_NOT_HANDLED;
    hz = u32(v.format + 4);
    ch = u16(v.format + 2);
    if (!hz || hz > 0x7fffffffUL || !ch || ch > 2)
        return THREE_MM_WAVE_INVALID;
    if (code == THREE_MM_WAVE_GSM && (ch != 1 || v.format_bytes < 20 || u16(v.format + 12) != 65 ||
                                      u16(v.format + 14) != 0 || u16(v.format + 16) < 2 || u16(v.format + 18) != 320 ||
                                      v.data_bytes % 65 || v.data_bytes / 65 > (0x7fffffffUL / 2) / 320))
        return THREE_MM_WAVE_INVALID;
    *out = v;
    *rate = (ThreeMMI32)hz;
    *channels = (ThreeMMI32)ch;
    return (int)code;
}
ThreeMMU32 three_mm_wave_trim_frames(int encoding, ThreeMMU32 fact, ThreeMMU32 frames)
{
    return encoding == THREE_MM_WAVE_GSM && fact && fact < frames ? fact : frames;
}
int three_mm_wave_legacy_format(const void *data, size_t bytes)
{
    const unsigned char *p = (const unsigned char *)data, *f;
    ThreeMMU32 end, at, n;
    unsigned channels, bits, i, j;
    if (!riff(p, bytes, &end))
        return 0;
    for (at = 12; end - at >= 8;)
    {
        n = u32(p + at + 4);
        if (n > end - at - 8)
            return 0;
        if (!memcmp(p + at, "fmt ", 4))
        {
            if (n < 16)
                return 0;
            f = p + at + 8;
            channels = u16(f + 2);
            bits = u16(f + 14);
            if (!channels || channels > 2 || !u32(f + 4) || u32(f + 4) > 48000)
                return 0;
            if (u16(f) == 1)
                return (bits == 8 || bits == 16) && u16(f + 12) == channels * (bits / 8);
            if (u16(f) != 2 || channels != 1 || u32(f + 4) != 11025 || u16(f + 12) != 256 || bits != 4 || n < 50 ||
                u16(f + 16) != 32 || u16(f + 18) != 500 || u16(f + 20) != 7)
                return 0;
            for (i = 0; i < 7; ++i)
                for (j = 0; j < 2; ++j)
                    if (u16(f + 22 + i * 4 + j * 2) != ((unsigned)coefficients[i][j] & 65535U))
                        return 0;
            return 1;
        }
        at += 8 + n;
        if (n & 1)
        {
            if (at == end)
                break;
            ++at;
        }
    }
    return 0;
}
int three_mm_wave_fact_frames(const void *data, size_t bytes, ThreeMMU32 *frames)
{
    const unsigned char *p = (const unsigned char *)data;
    ThreeMMU32 end, at, n, count = 0;
    unsigned format = 0;
    if (!frames || !riff(p, bytes, &end))
        return 0;
    for (at = 12; end - at >= 8;)
    {
        n = u32(p + at + 4);
        if (n > end - at - 8)
            return 0;
        if (!memcmp(p + at, "fmt ", 4) && n >= 16)
            format = u16(p + at + 8);
        if (!memcmp(p + at, "fact", 4) && n >= 4)
            count = u32(p + at + 8);
        at += 8 + n;
        if (n & 1)
        {
            if (at == end)
                break;
            ++at;
        }
    }
    if (!count || (format != 2 && format != 0x11))
        return 0;
    *frames = count;
    return 1;
}
/* Unsigned two-word error sum: a 16-bit sample difference squared fits one
 * word, but the sum over a block does not. No C99 or floating-point rounding. */
typedef struct Error
{
    ThreeMMU32 high, low;
} Error;
static ThreeMMI32 clamp(ThreeMMI32 n, ThreeMMI32 lo, ThreeMMI32 hi)
{
    return n < lo ? lo : n > hi ? hi : n;
}
static Error block(const ThreeMMI32 *samples, unsigned valid, int predictor, unsigned char *bytes)
{
    ThreeMMI32 older = samples[0], last = samples[1], delta, difference, prediction, residual, nibble, decoded;
    unsigned i, at;
    ThreeMMU32 square, old;
    Error error;
    error.high = error.low = 0;
    delta = last - older;
    if (delta < 0)
        delta = -delta;
    delta = clamp(delta, 16, 32767);
    bytes[0] = (unsigned char)predictor;
    bytes[1] = (unsigned char)delta;
    bytes[2] = (unsigned char)(delta >> 8);
    bytes[3] = (unsigned char)last;
    bytes[4] = (unsigned char)(((unsigned)last & 65535U) >> 8);
    bytes[5] = (unsigned char)older;
    bytes[6] = (unsigned char)(((unsigned)older & 65535U) >> 8);
    for (i = 2; i < 500; ++i)
    {
        prediction = (last * coefficients[predictor][0] + older * coefficients[predictor][1]) / 256;
        residual = samples[i] - prediction;
        nibble = clamp((residual + (residual < 0 ? -delta / 2 : delta / 2)) / delta, -8, 7);
        while (delta * adaptation[nibble & 15] / 256 > 32767)
            nibble += nibble < 0 ? 1 : -1;
        decoded = clamp(prediction + nibble * delta, -32768, 32767);
        difference = samples[i] - decoded;
        if (difference < 0)
            difference = -difference;
        if (i < valid)
        {
            square = (ThreeMMU32)difference * (ThreeMMU32)difference;
            old = error.low;
            error.low += square;
            if (error.low < old)
                ++error.high;
        }
        older = last;
        last = decoded;
        delta = delta * adaptation[nibble & 15] / 256;
        if (delta < 16)
            delta = 16;
        at = 7 + (i - 2) / 2;
        if (!(i & 1))
            bytes[at] = (unsigned char)((nibble & 15) << 4);
        else
            bytes[at] |= (unsigned char)(nibble & 15);
    }
    return error;
}
int three_mm_wave_encode(const unsigned char *samples, ThreeMMU32 count, ThreeMMU32 rate, unsigned channels,
                         unsigned bits, int adpcm, unsigned char *output, ThreeMMU32 capacity, ThreeMMU32 *size)
{
    ThreeMMU32 blocks = 0, payload, total, frames, at, k, valid, sample, average;
    unsigned i, j;
    unsigned char *p = output, best[256], candidate[256];
    ThreeMMI32 input[500];
    int predictor;
    Error minimum, error;
    if (!size || !samples || !rate || rate > 0x7fffffffUL || !count || count > 0x7fffffffUL)
        return 0;
    if (adpcm)
    {
        if (channels != 1 || bits != 8)
            return 0;
        blocks = (count + 499) / 500;
        if (blocks > (0x7fffffffUL - 90) / 256 || rate / 500 > 0xffffffffUL / 256)
            return 0;
        average = (rate / 500) * 256;
        k = (rate % 500) * 256 / 500;
        if (average > 0xffffffffUL - k)
            return 0;
        average += k;
        payload = blocks * 256;
        frames = count;
        total = 90 + payload;
    }
    else
    {
        if (!channels || channels > 2 || (bits != 8 && bits != 16) || count > 0x7fffffffUL - 45 ||
            count % (channels * (bits / 8)) || rate > 0xffffffffUL / (channels * (bits / 8)))
            return 0;
        payload = count;
        frames = count / (channels * (bits / 8));
        total = 44 + payload + (payload & 1);
        average = rate * channels * (bits / 8);
    }
    if (output && capacity < total)
        return 0;
    *size = total;
    if (!output)
        return 1;
    tag(&p, "RIFF");
    dword(&p, total - 8);
    tag(&p, "WAVE");
    tag(&p, "fmt ");
    dword(&p, adpcm ? 50 : 16);
    word(&p, adpcm ? 2 : 1);
    word(&p, channels);
    dword(&p, rate);
    dword(&p, average);
    word(&p, adpcm ? 256 : channels * (bits / 8));
    word(&p, adpcm ? 4 : bits);
    if (adpcm)
    {
        word(&p, 32);
        word(&p, 500);
        word(&p, 7);
        for (i = 0; i < 7; ++i)
            for (j = 0; j < 2; ++j)
                word(&p, (ThreeMMU32)coefficients[i][j]);
        tag(&p, "fact");
        dword(&p, 4);
        dword(&p, frames);
    }
    tag(&p, "data");
    dword(&p, payload);
    if (!adpcm)
    {
        memcpy(p, samples, count);
        if (count & 1)
            p[count] = 0;
        return 1;
    }
    for (at = 0; at < blocks; ++at)
    {
        valid = count - at * 500;
        if (valid > 500)
            valid = 500;
        for (i = 0; i < 500; ++i)
        {
            sample = at * 500 + i;
            if (sample >= count)
                sample = count - 1;
            input[i] = ((ThreeMMI32)samples[sample] - 128) * 256;
        }
        minimum.high = minimum.low = 0xffffffffUL;
        for (predictor = 0; predictor < 7; ++predictor)
        {
            error = block(input, (unsigned)valid, predictor, candidate);
            if (error.high < minimum.high || (error.high == minimum.high && error.low < minimum.low))
            {
                minimum = error;
                memcpy(best, candidate, 256);
            }
        }
        memcpy(p, best, 256);
        p += 256;
    }
    return 1;
}
int three_mm_wave_import_adpcm(ThreeMMU32 samples)
{
    return samples > 1500;
}
int three_mm_wave_import_keep(int legacy, ThreeMMU32 original, ThreeMMU32 encoded)
{
    return legacy && original <= encoded;
}

int three_mm_wave_prepare(const void *data, ThreeMMU32 bytes, const ThreeMMWaveImportServices *s,
                          ThreeMMWaveBuffer *scratch, const unsigned char **out, ThreeMMU32 *size)
{
    ThreeMMU32 decoded, encoded;
    ThreeMMI32 count = 0;
    unsigned char *samples = NULL;
    const unsigned char *chosen;
    int legacy, ok = 0, adpcm;
    if (!data || bytes < 12 || bytes > 0x7fffffffUL || !s || !s->load || !s->convert_u8 || !s->release_samples ||
        !scratch || !out || !size)
        return 0;
    if (!s->load(s->context, data, bytes, &decoded) || !decoded || decoded > 0x7fffffffUL)
        return 0;
    legacy = three_mm_wave_legacy_format(data, bytes);
    if (legacy && bytes <= 346)
    {
        *out = (const unsigned char *)data;
        *size = bytes;
        return 1;
    }
    if (!s->convert_u8(s->context, &samples, &count) || count <= 0 || !samples)
        goto done;
    adpcm = three_mm_wave_import_adpcm((ThreeMMU32)count);
    if (!three_mm_wave_encode(samples, (ThreeMMU32)count, 11025, 1, 8, adpcm, NULL, 0, &encoded) ||
        !three_mm_wave_buffer_reserve(scratch, encoded) ||
        !three_mm_wave_encode(samples, (ThreeMMU32)count, 11025, 1, 8, adpcm, scratch->data, encoded, &encoded))
        goto done;
    chosen = scratch->data;
    if (three_mm_wave_import_keep(legacy, bytes, encoded))
    {
        chosen = (const unsigned char *)data;
        encoded = bytes;
    }
    *out = chosen;
    *size = encoded;
    ok = 1;
done:
    if (samples)
        s->release_samples(s->context, samples);
    return ok;
}

static void *wave_resize(void *ctx, void *p, ThreeMMU32 old, ThreeMMU32 n, int priority)
{
    (void)ctx;
    (void)old;
    (void)priority;
    return realloc(p, (size_t)n);
}
static void wave_release(void *ctx, void *p, ThreeMMU32 n)
{
    (void)ctx;
    (void)n;
    free(p);
}
int three_mm_wave_buffer_init(ThreeMMWaveBuffer *b, const ThreeMMStorageAllocator *a)
{
    if (!b || (a && (!a->resize || !a->release)))
        return 0;
    memset(b, 0, sizeof(*b));
    if (a)
        b->allocator = *a;
    else
    {
        b->allocator.resize = wave_resize;
        b->allocator.release = wave_release;
    }
    return 1;
}
int three_mm_wave_buffer_reserve(ThreeMMWaveBuffer *b, ThreeMMU32 n)
{
    void *p;
    if (!b || !b->allocator.resize || n > 0x7fffffffUL || (size_t)n != n)
        return 0;
    if (n <= b->capacity)
        return 1;
    p = b->allocator.resize(b->allocator.context, b->data, b->capacity, n, THREE_MM_STORAGE_NORMAL);
    if (!p)
        return 0;
    b->data = (unsigned char *)p;
    b->capacity = n;
    return 1;
}
void three_mm_wave_buffer_dispose(ThreeMMWaveBuffer *b)
{
    if (b->data)
        b->allocator.release(b->allocator.context, b->data, b->capacity);
    b->data = NULL;
    b->capacity = 0;
}
