/* Codec registration converted from Kauai codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CODEC_REGISTRY_H
#define MM_ENGINE_CODEC_REGISTRY_H
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMCodecServices
    {
        void *context;
        void (*retain)(void *, void *codec);
        void (*release)(void *, void *codec);
        int (*can_do)(void *, void *codec, int encode, MMI32 format);
    } MMCodecServices;
    typedef struct MMCodecRegistry
    {
        MMCollectionStorage storage;
        MMCodecServices services;
        MMU32 count;
        void *default_codec;
        MMI32 default_format;
    } MMCodecRegistry;
    /* Owning references: default first, registered in reverse order on disposal.
     * Default codec wins lookup, then latest registration wins; duplicates each own
     * a reference. Failed add retains none. Callbacks may not mutate/re-enter this
     * registry. find/at return borrowed tokens; initialize/dispose exactly as owners. */
    int mm_codec_registry_init(MMCodecRegistry *, const MMCodecServices *,
                                     const MMStorageAllocator *, void *default_codec, MMI32 default_format);
    int mm_codec_registry_dispose(MMCodecRegistry *);
    int mm_codec_registry_add(MMCodecRegistry *, void *codec);
    int mm_codec_registry_default(MMCodecRegistry *, MMI32 format);
    void *mm_codec_registry_at(const MMCodecRegistry *, MMU32 position);
    void *mm_codec_registry_find(const MMCodecRegistry *, int encode, MMI32 format);
#ifdef __cplusplus
}
#endif
#endif
