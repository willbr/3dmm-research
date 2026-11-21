/* Movie WAVE import converted from engine/msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/wave_import.h"
#include <string.h>

static int valid_input(const MMReadStream *source)
{
    return source && source->read_at && source->length >= 12 && source->length <= 0x7fffffffUL;
}

int mm_wave_file_import(const MMReadStream *source, MMI32 style, const MMChunkNameValue *name, MMU32 *number,
                        const MMWaveFileServices *s, const MMArchiveWriter *writer)
{
    MMWaveBuffer storage;
    MMMemoryStream payload;
    MMU32 prepared_bytes;
    const void *prepared;
    int ok = 0;
    if (!valid_input(source))
    {
        s->bad_sound(s->context);
        return 0;
    }
    if (!mm_wave_buffer_init(&storage, s->allocator))
        return 0;
    if (!mm_wave_buffer_reserve(&storage, source->length))
        goto done;
    if (!mm_read_exact(source, 0, storage.data, source->length) ||
        !s->prepare(s->context, storage.data, source->length, &prepared, &prepared_bytes) ||
        !mm_memory_stream_init(&payload, prepared, prepared_bytes))
    {
        s->bad_sound(s->context);
        goto done;
    }
    ok = mm_sound_write(&payload.stream, 1, style, name, number, writer);
done:
    mm_wave_buffer_dispose(&storage);
    return ok;
}
static void write_word(unsigned char *b, MMU32 n)
{
    b[0] = (unsigned char)n;
    b[1] = (unsigned char)(n >> 8);
    b[2] = (unsigned char)(n >> 16);
    b[3] = (unsigned char)(n >> 24);
}

typedef struct NativeWavePayload
{
    unsigned char header[20], fact_and_data[20];
    const unsigned char *parts[4];
    MMU32 lengths[4];
} NativeWavePayload;

static int read_native_payload(void *p, MMU32 offset, void *out, size_t count)
{
    const NativeWavePayload *payload = (const NativeWavePayload *)p;
    unsigned char *bytes = (unsigned char *)out;
    unsigned i;
    for (i = 0; i < 4 && count; ++i)
    {
        size_t copy;
        if (offset >= payload->lengths[i])
        {
            offset -= payload->lengths[i];
            continue;
        }
        copy = payload->lengths[i] - offset;
        if (copy > count)
            copy = count;
        memcpy(bytes, payload->parts[i] + offset, copy);
        bytes += copy;
        count -= copy;
        offset = 0;
    }
    return count == 0;
}

int mm_wave_native_import(const MMReadStream *source, MMI32 style, const MMChunkNameValue *name, MMU32 *number,
                          const MMNativeWaveServices *s, const MMArchiveWriter *writer)
{
    MMNativeWaveFormat format;
    MMWaveBuffer input;
    MMReadStream encoded;
    NativeWavePayload payload;
    MMU32 samples, source_bytes, format_bytes, data_bytes, compressed;
    const void *codec_format, *data;
    int ok = 0, result;
    if (!mm_wave_buffer_init(&input, s->allocator))
        return 0;
    if (!valid_input(source))
        goto bad;
    if (!s->open_file(s->context, &format))
    {
        if (!s->open_memory)
            goto bad;
        if (!mm_wave_buffer_reserve(&input, source->length))
            goto done;
        if (!mm_read_exact(source, 0, input.data, source->length) ||
            !s->open_memory(s->context, input.data, source->length, &format))
            goto bad;
    }
    if (format.rate != 11025 || format.bits != 8 || format.channels != 1)
    {
        if (!s->convert(s->context))
            goto bad;
        format.rate = 11025;
        format.bits = 8;
        format.channels = 1;
    }
    samples = s->samples(s->context);
    if (samples == 0xffffffffUL || !format.channels || !format.bits || format.bits % 8 ||
        format.channels > 0x7fffffffUL / (format.bits / 8) ||
        samples > 0x7fffffffUL / (format.channels * (format.bits / 8)))
        goto bad;
    source_bytes = samples * (format.bits / 8) * format.channels;
    if (!s->activate(s->context))
        goto bad;
    if (!s->allocate_samples(s->context, source_bytes))
        goto done;
    if (!s->read_samples(s->context, &samples))
        goto bad;
    result = s->compress(s->context, source_bytes, &codec_format, &format_bytes, &data, &data_bytes);
    if (result < 0)
        goto bad;
    if (!result)
        goto done;
    if (format_bytes > 0x7fffffffUL - 40 || data_bytes > 0x7fffffffUL - 40 - format_bytes)
        goto bad;
    /* Preserve the classic comparison using the RIFF payload length, which
     * excludes the eight-byte RIFF header, rather than the physical length. */
    compressed = 32 + format_bytes + data_bytes;
    if (compressed <= source->length)
    {
        memcpy(payload.header, "RIFF", 4);
        write_word(payload.header + 4, compressed);
        memcpy(payload.header + 8, "WAVEfmt ", 8);
        write_word(payload.header + 16, format_bytes);
        memcpy(payload.fact_and_data, "fact", 4);
        write_word(payload.fact_and_data + 4, 4);
        write_word(payload.fact_and_data + 8, samples);
        memcpy(payload.fact_and_data + 12, "data", 4);
        write_word(payload.fact_and_data + 16, data_bytes);
        payload.parts[0] = payload.header; payload.lengths[0] = sizeof(payload.header);
        payload.parts[1] = (const unsigned char *)codec_format; payload.lengths[1] = format_bytes;
        payload.parts[2] = payload.fact_and_data; payload.lengths[2] = sizeof(payload.fact_and_data);
        payload.parts[3] = (const unsigned char *)data; payload.lengths[3] = data_bytes;
        encoded.context = &payload;
        encoded.length = compressed + 8;
        encoded.read_at = read_native_payload;
        source = &encoded;
    }
    ok = mm_sound_write(source, 1, style, name, number, writer);
    goto done;
bad:
    s->bad_sound(s->context);
done:
    s->cleanup(s->context);
    mm_wave_buffer_dispose(&input);
    return ok;
}
