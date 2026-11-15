/* Kauai codec converted from codkauai.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CODEC_H
#define MM_ENGINE_CODEC_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_CODEC_KCDC = 0x4b434443,
        MM_CODEC_KCD2 = 0x4b434432
    };
    /* Input/output must not overlap. Counts are published only on success. Decode
     * uses no allocation. Encode borrows an optional allocator for two scratch
     * arrays, retaining NORMAL priority and releasing both on every exit.
     * NULL allocator uses the C heap. Sizes must be positive and below storage
     * limit; encoding additionally requires input size*4 below that limit. */
    int mm_codec_encode(MMI32 format, const void *input, MMU32 bytes, void *output, MMU32 capacity,
                              MMI32 *written, const MMStorageAllocator *allocator);
    int mm_codec_decode(MMI32 format, const void *input, MMU32 bytes, void *output, MMU32 capacity,
                              MMI32 *written);
#ifdef __cplusplus
}
#endif
#endif
