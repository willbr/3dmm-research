/* Codec registration converted from Kauai codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CODEC_REGISTRY_H
#define THREE_MM_ENGINE_CODEC_REGISTRY_H
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMCodecServices
    {
        void *context;
        void (*retain)(void *, void *codec);
        void (*release)(void *, void *codec);
        int (*can_do)(void *, void *codec, int encode, ThreeMMI32 format);
    } ThreeMMCodecServices;
    typedef struct ThreeMMCodecRegistry
    {
        ThreeMMCollectionStorage storage;
        ThreeMMCodecServices services;
        ThreeMMU32 count;
        void *default_codec;
        ThreeMMI32 default_format;
    } ThreeMMCodecRegistry;
    /* Owning references: default first, registered in reverse order on disposal.
     * Default codec wins lookup, then latest registration wins; duplicates each own
     * a reference. Failed add retains none. Callbacks may not mutate/re-enter this
     * registry. find/at return borrowed tokens; initialize/dispose exactly as owners. */
    int three_mm_codec_registry_init(ThreeMMCodecRegistry *, const ThreeMMCodecServices *,
                                     const ThreeMMStorageAllocator *, void *default_codec, ThreeMMI32 default_format);
    int three_mm_codec_registry_dispose(ThreeMMCodecRegistry *);
    int three_mm_codec_registry_add(ThreeMMCodecRegistry *, void *codec);
    int three_mm_codec_registry_default(ThreeMMCodecRegistry *, ThreeMMI32 format);
    void *three_mm_codec_registry_at(const ThreeMMCodecRegistry *, ThreeMMU32 position);
    void *three_mm_codec_registry_find(const ThreeMMCodecRegistry *, int encode, ThreeMMI32 format);
#ifdef __cplusplus
}
#endif
#endif
