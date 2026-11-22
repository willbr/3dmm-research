/* Movie WAVE import converted from engine/msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_WAVE_IMPORT_H
#define MM_ENGINE_WAVE_IMPORT_H
#include "sound_resource.h"
#include "wave.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Decode and prepare the supplied bytes. Prepared output may alias them,
     * and must remain valid until the sound writer returns. */
    typedef struct MMWaveFileServices
    {
        void *context;
        const MMStorageAllocator *allocator; /* Optional source-buffer allocator. */
        int (*prepare)(void *, const void *, MMU32, const void **, MMU32 *);
        void (*bad_sound)(void *);
    } MMWaveFileServices;
    int mm_wave_file_import(const MMReadStream *, MMI32 style, const MMChunkNameValue *name, MMU32 *number,
                            const MMWaveFileServices *, const MMArchiveWriter *);
    enum { MM_WAVE_BAD_AUDIO = -1 };
    /* Read, prepare and write using C-owned scratch. Return 1 on success,
     * MM_WAVE_BAD_AUDIO on read or preparation failure, or 0 on input allocation or archive write failure. */
    int mm_wave_decode_import(const MMReadStream *, MMI32 style, const MMChunkNameValue *name, MMU32 *number,
                              const MMWaveImportServices *, const MMArchiveWriter *);
    /* The classic native host keeps AudioMan/ACM as external codec services. C
     * owns input fallback, conversion decisions, sample bounds, RIFF framing, source selection,
     * publication and cleanup. compress returns 1, 0 (memory), or -1 (bad sound).
     * The decoder may borrow input bytes through cleanup. Native format and
     * sample bytes borrow codec storage through the sound write. */
    typedef struct MMNativeWaveFormat
    {
        MMU32 rate;
        unsigned channels, bits;
    } MMNativeWaveFormat;
    typedef struct MMNativeWaveServices
    {
        void *context;
        const MMStorageAllocator *allocator; /* Optional fallback input allocator. */
        int (*open_file)(void *, MMNativeWaveFormat *);
        int (*open_memory)(void *, unsigned char *, MMU32, MMNativeWaveFormat *);
        int (*convert)(void *);
        MMU32 (*samples)(void *);
        int (*activate)(void *);
        int (*allocate_samples)(void *, MMU32);
        int (*read_samples)(void *, MMU32 *);
        int (*compress)(void *, MMU32, const void **format, MMU32 *format_bytes, const void **data,
                        MMU32 *data_bytes);
        void (*bad_sound)(void *);
        void (*cleanup)(void *);
    } MMNativeWaveServices;
    int mm_wave_native_import(const MMReadStream *, MMI32 style, const MMChunkNameValue *name, MMU32 *number,
                              const MMNativeWaveServices *, const MMArchiveWriter *);
#ifdef __cplusplus
}
#endif
#endif
