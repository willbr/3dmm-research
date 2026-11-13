/* Movie WAVE import converted from engine/msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/wave_import.h"
#include "three_mm/collection_storage.h"
int three_mm_wave_file_import(void *source, void *destination, ThreeMMI32 style, const void *name, void *number_out,
                              const ThreeMMWaveFileServices *s)
{
    ThreeMMWaveBuffer storage;
    ThreeMMI32 bytes = s->size(s->context, source), prepared_bytes;
    const void *prepared, *selected_name;
    void *temp = NULL;
    int ok = 0;
    if (bytes < 12)
    {
        s->bad_sound(s->context);
        return 0;
    }
    if (!three_mm_wave_buffer_init(&storage, s->allocator))
        return 0;
    if (!three_mm_wave_buffer_reserve(&storage, (ThreeMMU32)bytes))
        goto done;
    if (!s->read(s->context, source, storage.data, bytes) ||
        !s->prepare(s->context, storage.data, bytes, &prepared, &prepared_bytes))
    {
        s->bad_sound(s->context);
        goto done;
    }
    selected_name = s->name(s->context, source, name);
    temp = s->create_temp(s->context);
    if (!temp)
        goto done;
    ok = s->write_temp(s->context, temp, prepared, prepared_bytes) &&
         s->write_resource(s->context, temp, destination, style, selected_name, number_out);
done:
    if (temp)
        s->release(s->context, temp);
    three_mm_wave_buffer_dispose(&storage);
    return ok;
}
static int write_word(const ThreeMMNativeWaveServices *s, ThreeMMU32 n)
{
    unsigned char b[4];
    b[0] = (unsigned char)n;
    b[1] = (unsigned char)(n >> 8);
    b[2] = (unsigned char)(n >> 16);
    b[3] = (unsigned char)(n >> 24);
    return s->write(s->context, b, 4);
}
int three_mm_wave_native_import(void *source, void *destination, ThreeMMI32 style, const void *name, void *number_out,
                                const ThreeMMNativeWaveServices *s)
{
    ThreeMMNativeWaveFormat format;
    ThreeMMI32 original;
    ThreeMMU32 samples, source_bytes, format_bytes, data_bytes, compressed;
    const void *codec_format, *data;
    int ok = 0, result;
    if (!s->begin(s->context, source, name))
        goto done;
    if (!s->original_size(s->context, &original) || !s->open_sound(s->context, &format))
        goto bad;
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
    if (!s->create_file(s->context))
        goto done;
    if (!s->write(s->context, "RIFF", 4) || !write_word(s, compressed) || !s->write(s->context, "WAVEfmt ", 8) ||
        !write_word(s, format_bytes) || !s->write(s->context, codec_format, format_bytes) ||
        !s->write(s->context, "fact", 4) || !write_word(s, 4) || !write_word(s, samples) ||
        !s->write(s->context, "data", 4) || !write_word(s, data_bytes) || !s->write(s->context, data, data_bytes))
        goto done;
    s->finish_conversion(s->context);
    if (!s->open_selected(s->context, original < 0 || compressed > (ThreeMMU32)original))
        goto done;
    ok = s->write_resource(s->context, destination, style, number_out);
    goto done;
bad:
    s->bad_sound(s->context);
done:
    s->cleanup(s->context);
    return ok;
}
