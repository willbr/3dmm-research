/* Codec registration converted from Kauai codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CODEC_REGISTRY_H
#define MM_ENGINE_CODEC_REGISTRY_H
#include "collections.h"
#include "data_stream.h"
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
        int (*convert)(void *, void *codec, int encode, MMI32 format, const void *input, MMU32 bytes,
                       void *output, MMU32 capacity, MMI32 *written);
    } MMCodecServices;
    typedef struct MMCodecRegistry
    {
        MMCollectionStorage storage;
        MMCodecServices services;
        MMU32 count;
        void *default_codec;
        MMI32 default_format;
        int use_builtin;
    } MMCodecRegistry;
    /* Owning references: default first, registered in reverse order on disposal.
     * Default codec wins lookup, then latest registration wins; duplicates each own
     * a reference. Failed add retains none. Callbacks may not mutate/re-enter this
     * registry. find/at return borrowed tokens; initialize/dispose exactly as owners. */
    int mm_codec_registry_init(MMCodecRegistry *, const MMCodecServices *,
                                     const MMStorageAllocator *, void *default_codec, MMI32 default_format);
    /* KCDC and KCD2 take precedence over registered codecs. */
    int mm_codec_registry_init_builtin(MMCodecRegistry *, const MMCodecServices *,
                                       const MMStorageAllocator *, MMI32 default_format);
    int mm_codec_registry_dispose(MMCodecRegistry *);
    int mm_codec_registry_add(MMCodecRegistry *, void *codec);
    int mm_codec_registry_default(MMCodecRegistry *, MMI32 format);
    void *mm_codec_registry_at(const MMCodecRegistry *, MMU32 position);
    void *mm_codec_registry_find(const MMCodecRegistry *, int encode, MMI32 format);
    /* A NULL registry uses only the built-in codecs with C heap scratch storage.
     * Format zero requests decompression. Nonzero formats request compression.
     * These operations use convert for registered tokens. The optional
     * builtin_status receives the raw built-in result for host diagnostics. */
    int mm_codec_registry_can_code(const MMCodecRegistry *, int encode, MMI32 format);
    int mm_codec_registry_code(const MMCodecRegistry *, MMI32 format, const void *input, MMU32 bytes,
                               void *output, MMU32 capacity, MMI32 *written, int *builtin_status);
#ifdef __cplusplus
}
#endif
#endif
