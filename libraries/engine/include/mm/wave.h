/* WAVE policy converted from Kauai wave_codec.cpp and compressed_wave.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_WAVE_H
#define MM_ENGINE_WAVE_H
#include "collection_storage.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Borrowed slices; callers retain the input for the lifetime of this view.
     * Parsing preserves the compressed reader's first fmt/data, last fact policy. */
    typedef struct MMWaveView
    {
        const unsigned char *format, *data;
        MMU32 format_bytes, data_bytes, fact;
    } MMWaveView;
    int mm_wave_parse(const void *, size_t, MMWaveView *);
    enum
    {
        MM_WAVE_INVALID = -1,
        MM_WAVE_NOT_HANDLED = 0,
        MM_WAVE_GSM = 0x31,
        MM_WAVE_MP3 = 0x55
    };
    /* Classifies the external-codec formats. Invalid RIFF/other encodings retain
     * NotHandled so the host's general WAVE decoder can apply its existing rules. */
    int mm_wave_compressed(const void *, size_t, MMWaveView *, MMI32 *rate, MMI32 *channels);
    MMU32 mm_wave_trim_frames(int encoding, MMU32 fact, MMU32 decoded_frames);
    int mm_wave_legacy_format(const void *, size_t);
    int mm_wave_fact_frames(const void *, size_t, MMU32 *);
    /* Query the required size with output == NULL. Invalid arguments/capacity leave
     * output untouched. Input/output must not overlap. PCM samples are interleaved little endian. ADPCM takes
     * unsigned mono 8-bit samples and repeats the last sample in a partial block. */
    int mm_wave_encode(const unsigned char *, MMU32 count, MMU32 rate, unsigned channels, unsigned bits,
                             int adpcm, unsigned char *output, MMU32 capacity, MMU32 *size);
    int mm_wave_import_adpcm(MMU32 samples);
    int mm_wave_import_keep(int legacy, MMU32 original, MMU32 encoded);
    /* Decode/resample are external codec/device services. The controller owns
     * import selection, C encoding and cleanup. Conversion may publish storage on
     * failure; release_samples is called for every non-NULL returned pointer.
     * Selected bytes borrow either input or initialized scratch and are
     * published only on success. A failed call may alter scratch. */
    typedef struct MMWaveImportServices
    {
        void *context;
        int (*load)(void *, const void *, MMU32, MMU32 *decoded_bytes);
        int (*convert_u8)(void *, unsigned char **, MMI32 *count);
        void (*release_samples)(void *, unsigned char *);
    } MMWaveImportServices;
    /* An owning audio buffer uses the supplied allocator (NULL = C heap) without
     * the collection subsystem's 128 MiB group limit. Initialize before use and
     * dispose once; never copy an owning buffer. */
    typedef struct MMWaveBuffer
    {
        unsigned char *data;
        MMU32 capacity;
        MMStorageAllocator allocator;
    } MMWaveBuffer;
    int mm_wave_buffer_init(MMWaveBuffer *, const MMStorageAllocator *);
    int mm_wave_buffer_reserve(MMWaveBuffer *, MMU32);
    void mm_wave_buffer_dispose(MMWaveBuffer *);
    int mm_wave_prepare(const void *, MMU32, const MMWaveImportServices *, MMWaveBuffer *,
                              const unsigned char **, MMU32 *);
#ifdef __cplusplus
}
#endif
#endif
