/* Codec registration converted from Kauai codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/codec_registry.h"
#include <string.h>
int mm_codec_registry_init(MMCodecRegistry *registry, const MMCodecServices *services,
                                 const MMStorageAllocator *allocator, void *codec, MMI32 format)
{
    if (!registry || !services || !services->retain || !services->release || !services->can_do || !format)
        return MM_STORAGE_ARGUMENT;
    if (mm_storage_init(&registry->storage, allocator) != MM_STORAGE_OK)
        return MM_STORAGE_ARGUMENT;
    registry->storage.min_grow[0] = 128;
    registry->services = *services;
    registry->count = 0;
    registry->default_codec = codec;
    registry->default_format = format;
    if (codec)
        services->retain(services->context, codec);
    return MM_STORAGE_OK;
}
void *mm_codec_registry_at(const MMCodecRegistry *registry, MMU32 position)
{
    void *codec;
    if (!registry || position >= registry->count || !registry->storage.buffers[0].data ||
        registry->count > registry->storage.buffers[0].capacity / sizeof(codec))
        return NULL;
    memcpy(&codec, registry->storage.buffers[0].data + position * sizeof(codec), sizeof(codec));
    return codec;
}
int mm_codec_registry_dispose(MMCodecRegistry *registry)
{
    void *codec;
    if (!registry || registry->storage.buffers[0].locks || registry->storage.buffers[1].locks)
        return MM_STORAGE_ARGUMENT;
    codec = registry->default_codec;
    registry->default_codec = NULL;
    if (codec)
        registry->services.release(registry->services.context, codec);
    while (registry->count)
    {
        codec = mm_codec_registry_at(registry, registry->count - 1);
        --registry->count;
        registry->services.release(registry->services.context, codec);
    }
    return mm_storage_dispose(&registry->storage);
}
int mm_codec_registry_add(MMCodecRegistry *registry, void *codec)
{
    MMArray array;
    int result;
    if (!registry || !codec)
        return MM_STORAGE_ARGUMENT;
    memset(&array, 0, sizeof(array));
    array.storage = &registry->storage;
    array.width = sizeof(codec);
    array.count = registry->count;
    array.free_count = -1;
    result = mm_array_add(&array, &codec, NULL);
    if (result != MM_STORAGE_OK)
        return result;
    registry->count = array.count;
    registry->services.retain(registry->services.context, codec);
    return MM_STORAGE_OK;
}
int mm_codec_registry_default(MMCodecRegistry *registry, MMI32 format)
{
    if (!registry || !format)
        return MM_STORAGE_ARGUMENT;
    registry->default_format = format;
    return MM_STORAGE_OK;
}
void *mm_codec_registry_find(const MMCodecRegistry *registry, int encode, MMI32 format)
{
    MMU32 i;
    void *codec;
    if (!registry || !format)
        return NULL;
    codec = registry->default_codec;
    if (codec && registry->services.can_do(registry->services.context, codec, encode, format))
        return codec;
    for (i = registry->count; i-- > 0;)
    {
        codec = mm_codec_registry_at(registry, i);
        if (registry->services.can_do(registry->services.context, codec, encode, format))
            return codec;
    }
    return NULL;
}
