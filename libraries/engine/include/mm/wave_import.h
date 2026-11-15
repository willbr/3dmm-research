/* Movie WAVE import converted from engine/msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_WAVE_IMPORT_H
#define MM_ENGINE_WAVE_IMPORT_H
#include "wave.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Physical files, text and external decoding are host services. Borrowed
     * prepared data/name remain valid through write_resource. Temporary file
     * ownership transfers to this controller and is released on every path. */
    typedef struct MMWaveFileServices
    {
        void *context;
        const MMStorageAllocator *allocator; /* Optional source-buffer allocator. */
        MMI32 (*size)(void *, void *source);
        int (*read)(void *, void *source, void *buffer, MMI32 bytes);
        int (*prepare)(void *, const void *, MMI32, const void **, MMI32 *);
        const void *(*name)(void *, void *source, const void *provided);
        void *(*create_temp)(void *);
        int (*write_temp)(void *, void *temp, const void *, MMI32);
        int (*write_resource)(void *, void *temp, void *destination, MMI32 style, const void *name,
                              void *number_out);
        void (*release)(void *, void *temp);
        void (*bad_sound)(void *);
    } MMWaveFileServices;
    int mm_wave_file_import(void *source, void *destination, MMI32 style, const void *name, void *number_out,
                                  const MMWaveFileServices *);
    /* The classic native host keeps AudioMan/ACM as external codec services. C
     * owns conversion decisions, sample bounds, RIFF writing, source selection,
     * publication and cleanup. compress returns 1, 0 (memory), or -1 (bad sound).
     * Native format bytes borrow codec storage until cleanup. */
    typedef struct MMNativeWaveFormat
    {
        MMU32 rate;
        unsigned channels, bits;
    } MMNativeWaveFormat;
    typedef struct MMNativeWaveServices
    {
        void *context;
        int (*begin)(void *, void *source, const void *name);
        int (*original_size)(void *, MMI32 *);
        int (*open_sound)(void *, MMNativeWaveFormat *);
        int (*convert)(void *);
        MMU32 (*samples)(void *);
        int (*activate)(void *);
        int (*allocate_samples)(void *, MMU32);
        int (*read_samples)(void *, MMU32 *);
        int (*compress)(void *, MMU32, const void **format, MMU32 *format_bytes, const void **data,
                        MMU32 *data_bytes);
        int (*create_file)(void *);
        int (*write)(void *, const void *, MMU32);
        void (*finish_conversion)(void *);
        int (*open_selected)(void *, int original);
        int (*write_resource)(void *, void *destination, MMI32 style, void *number_out);
        void (*bad_sound)(void *);
        void (*cleanup)(void *);
    } MMNativeWaveServices;
    int mm_wave_native_import(void *source, void *destination, MMI32 style, const void *name,
                                    void *number_out, const MMNativeWaveServices *);
#ifdef __cplusplus
}
#endif
#endif
