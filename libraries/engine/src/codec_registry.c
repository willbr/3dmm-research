/* Codec registration converted from Kauai codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/codec_registry.h"
#include <string.h>
int three_mm_codec_registry_init(ThreeMMCodecRegistry *registry, const ThreeMMCodecServices *services,
                                 const ThreeMMStorageAllocator *allocator, void *codec, ThreeMMI32 format)
{
    if (!registry || !services || !services->retain || !services->release || !services->can_do || !format)
        return THREE_MM_STORAGE_ARGUMENT;
    if (three_mm_storage_init(&registry->storage, allocator) != THREE_MM_STORAGE_OK)
        return THREE_MM_STORAGE_ARGUMENT;
    registry->storage.min_grow[0] = 128;
    registry->services = *services;
    registry->count = 0;
    registry->default_codec = codec;
    registry->default_format = format;
    if (codec)
        services->retain(services->context, codec);
    return THREE_MM_STORAGE_OK;
}
void *three_mm_codec_registry_at(const ThreeMMCodecRegistry *registry, ThreeMMU32 position)
{
    void *codec;
    if (!registry || position >= registry->count || !registry->storage.buffers[0].data ||
        registry->count > registry->storage.buffers[0].capacity / sizeof(codec))
        return NULL;
    memcpy(&codec, registry->storage.buffers[0].data + position * sizeof(codec), sizeof(codec));
    return codec;
}
int three_mm_codec_registry_dispose(ThreeMMCodecRegistry *registry)
{
    void *codec;
    if (!registry || registry->storage.buffers[0].locks || registry->storage.buffers[1].locks)
        return THREE_MM_STORAGE_ARGUMENT;
    codec = registry->default_codec;
    registry->default_codec = NULL;
    if (codec)
        registry->services.release(registry->services.context, codec);
    while (registry->count)
    {
        codec = three_mm_codec_registry_at(registry, registry->count - 1);
        --registry->count;
        registry->services.release(registry->services.context, codec);
    }
    return three_mm_storage_dispose(&registry->storage);
}
int three_mm_codec_registry_add(ThreeMMCodecRegistry *registry, void *codec)
{
    ThreeMMArray array;
    int result;
    if (!registry || !codec)
        return THREE_MM_STORAGE_ARGUMENT;
    memset(&array, 0, sizeof(array));
    array.storage = &registry->storage;
    array.width = sizeof(codec);
    array.count = registry->count;
    array.free_count = -1;
    result = three_mm_array_add(&array, &codec, NULL);
    if (result != THREE_MM_STORAGE_OK)
        return result;
    registry->count = array.count;
    registry->services.retain(registry->services.context, codec);
    return THREE_MM_STORAGE_OK;
}
int three_mm_codec_registry_default(ThreeMMCodecRegistry *registry, ThreeMMI32 format)
{
    if (!registry || !format)
        return THREE_MM_STORAGE_ARGUMENT;
    registry->default_format = format;
    return THREE_MM_STORAGE_OK;
}
void *three_mm_codec_registry_find(const ThreeMMCodecRegistry *registry, int encode, ThreeMMI32 format)
{
    ThreeMMU32 i;
    void *codec;
    if (!registry || !format)
        return NULL;
    codec = registry->default_codec;
    if (codec && registry->services.can_do(registry->services.context, codec, encode, format))
        return codec;
    for (i = registry->count; i-- > 0;)
    {
        codec = three_mm_codec_registry_at(registry, i);
        if (registry->services.can_do(registry->services.context, codec, encode, format))
            return codec;
    }
    return NULL;
}
