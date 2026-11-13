/* Movie WAVE import converted from engine/msnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_WAVE_IMPORT_H
#define THREE_MM_ENGINE_WAVE_IMPORT_H
#include "wave.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Physical files, text and external decoding are host services. Borrowed
     * prepared data/name remain valid through write_resource. Temporary file
     * ownership transfers to this controller and is released on every path. */
    typedef struct ThreeMMWaveFileServices
    {
        void *context;
        const ThreeMMStorageAllocator *allocator; /* Optional source-buffer allocator. */
        ThreeMMI32 (*size)(void *, void *source);
        int (*read)(void *, void *source, void *buffer, ThreeMMI32 bytes);
        int (*prepare)(void *, const void *, ThreeMMI32, const void **, ThreeMMI32 *);
        const void *(*name)(void *, void *source, const void *provided);
        void *(*create_temp)(void *);
        int (*write_temp)(void *, void *temp, const void *, ThreeMMI32);
        int (*write_resource)(void *, void *temp, void *destination, ThreeMMI32 style, const void *name,
                              void *number_out);
        void (*release)(void *, void *temp);
        void (*bad_sound)(void *);
    } ThreeMMWaveFileServices;
    int three_mm_wave_file_import(void *source, void *destination, ThreeMMI32 style, const void *name, void *number_out,
                                  const ThreeMMWaveFileServices *);
    /* The classic native host keeps AudioMan/ACM as external codec services. C
     * owns conversion decisions, sample bounds, RIFF writing, source selection,
     * publication and cleanup. compress returns 1, 0 (memory), or -1 (bad sound).
     * Native format bytes borrow codec storage until cleanup. */
    typedef struct ThreeMMNativeWaveFormat
    {
        ThreeMMU32 rate;
        unsigned channels, bits;
    } ThreeMMNativeWaveFormat;
    typedef struct ThreeMMNativeWaveServices
    {
        void *context;
        int (*begin)(void *, void *source, const void *name);
        int (*original_size)(void *, ThreeMMI32 *);
        int (*open_sound)(void *, ThreeMMNativeWaveFormat *);
        int (*convert)(void *);
        ThreeMMU32 (*samples)(void *);
        int (*activate)(void *);
        int (*allocate_samples)(void *, ThreeMMU32);
        int (*read_samples)(void *, ThreeMMU32 *);
        int (*compress)(void *, ThreeMMU32, const void **format, ThreeMMU32 *format_bytes, const void **data,
                        ThreeMMU32 *data_bytes);
        int (*create_file)(void *);
        int (*write)(void *, const void *, ThreeMMU32);
        void (*finish_conversion)(void *);
        int (*open_selected)(void *, int original);
        int (*write_resource)(void *, void *destination, ThreeMMI32 style, void *number_out);
        void (*bad_sound)(void *);
        void (*cleanup)(void *);
    } ThreeMMNativeWaveServices;
    int three_mm_wave_native_import(void *source, void *destination, ThreeMMI32 style, const void *name,
                                    void *number_out, const ThreeMMNativeWaveServices *);
#ifdef __cplusplus
}
#endif
#endif
