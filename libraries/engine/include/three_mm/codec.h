/* Kauai codec converted from codkauai.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CODEC_H
#define THREE_MM_ENGINE_CODEC_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_CODEC_KCDC = 0x4b434443,
        THREE_MM_CODEC_KCD2 = 0x4b434432
    };
    /* Input/output must not overlap. Counts are published only on success. Decode
     * uses no allocation. Encode borrows an optional allocator for two scratch
     * arrays, retaining NORMAL priority and releasing both on every exit.
     * NULL allocator uses the C heap. Sizes must be positive and below storage
     * limit; encoding additionally requires input size*4 below that limit. */
    int three_mm_codec_encode(ThreeMMI32 format, const void *input, ThreeMMU32 bytes, void *output, ThreeMMU32 capacity,
                              ThreeMMI32 *written, const ThreeMMStorageAllocator *allocator);
    int three_mm_codec_decode(ThreeMMI32 format, const void *input, ThreeMMU32 bytes, void *output, ThreeMMU32 capacity,
                              ThreeMMI32 *written);
#ifdef __cplusplus
}
#endif
#endif
