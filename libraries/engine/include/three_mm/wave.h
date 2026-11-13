/* WAVE policy converted from Kauai wave_codec.cpp and compressed_wave.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_WAVE_H
#define THREE_MM_ENGINE_WAVE_H
#include "collection_storage.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Borrowed slices; callers retain the input for the lifetime of this view.
     * Parsing preserves the compressed reader's first fmt/data, last fact policy. */
    typedef struct ThreeMMWaveView
    {
        const unsigned char *format, *data;
        ThreeMMU32 format_bytes, data_bytes, fact;
    } ThreeMMWaveView;
    int three_mm_wave_parse(const void *, size_t, ThreeMMWaveView *);
    enum
    {
        THREE_MM_WAVE_INVALID = -1,
        THREE_MM_WAVE_NOT_HANDLED = 0,
        THREE_MM_WAVE_GSM = 0x31,
        THREE_MM_WAVE_MP3 = 0x55
    };
    /* Classifies the external-codec formats. Invalid RIFF/other encodings retain
     * NotHandled so the host's general WAVE decoder can apply its existing rules. */
    int three_mm_wave_compressed(const void *, size_t, ThreeMMWaveView *, ThreeMMI32 *rate, ThreeMMI32 *channels);
    ThreeMMU32 three_mm_wave_trim_frames(int encoding, ThreeMMU32 fact, ThreeMMU32 decoded_frames);
    int three_mm_wave_legacy_format(const void *, size_t);
    int three_mm_wave_fact_frames(const void *, size_t, ThreeMMU32 *);
    /* Query the required size with output == NULL. Invalid arguments/capacity leave
     * output untouched. Input/output must not overlap. PCM samples are interleaved little endian. ADPCM takes
     * unsigned mono 8-bit samples and repeats the last sample in a partial block. */
    int three_mm_wave_encode(const unsigned char *, ThreeMMU32 count, ThreeMMU32 rate, unsigned channels, unsigned bits,
                             int adpcm, unsigned char *output, ThreeMMU32 capacity, ThreeMMU32 *size);
    int three_mm_wave_import_adpcm(ThreeMMU32 samples);
    int three_mm_wave_import_keep(int legacy, ThreeMMU32 original, ThreeMMU32 encoded);
    /* Decode/resample are external codec/device services. The controller owns
     * import selection, C encoding and cleanup. Conversion may publish storage on
     * failure; release_samples is called for every non-NULL returned pointer.
     * Selected bytes borrow either input or initialized scratch and are
     * published only on success. A failed call may alter scratch. */
    typedef struct ThreeMMWaveImportServices
    {
        void *context;
        int (*load)(void *, const void *, ThreeMMU32, ThreeMMU32 *decoded_bytes);
        int (*convert_u8)(void *, unsigned char **, ThreeMMI32 *count);
        void (*release_samples)(void *, unsigned char *);
    } ThreeMMWaveImportServices;
    /* An owning audio buffer uses the supplied allocator (NULL = C heap) without
     * the collection subsystem's 128 MiB group limit. Initialize before use and
     * dispose once; never copy an owning buffer. */
    typedef struct ThreeMMWaveBuffer
    {
        unsigned char *data;
        ThreeMMU32 capacity;
        ThreeMMStorageAllocator allocator;
    } ThreeMMWaveBuffer;
    int three_mm_wave_buffer_init(ThreeMMWaveBuffer *, const ThreeMMStorageAllocator *);
    int three_mm_wave_buffer_reserve(ThreeMMWaveBuffer *, ThreeMMU32);
    void three_mm_wave_buffer_dispose(ThreeMMWaveBuffer *);
    int three_mm_wave_prepare(const void *, ThreeMMU32, const ThreeMMWaveImportServices *, ThreeMMWaveBuffer *,
                              const unsigned char **, ThreeMMU32 *);
#ifdef __cplusplus
}
#endif
#endif
