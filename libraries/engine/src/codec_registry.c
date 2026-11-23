/* Codec registration converted from Kauai codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/codec_registry.h"
#include "mm/codec.h"
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
    registry->use_builtin = 0;
    if (codec)
        services->retain(services->context, codec);
    return MM_STORAGE_OK;
}
int mm_codec_registry_init_builtin(MMCodecRegistry *registry, const MMCodecServices *services,
                                   const MMStorageAllocator *allocator, MMI32 format)
{
    int result = mm_codec_registry_init(registry, services, allocator, NULL, format);
    if (result == MM_STORAGE_OK)
        registry->use_builtin = 1;
    return result;
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

static int has_builtin(const MMCodecRegistry *registry, MMI32 format)
{
    return (!registry || registry->use_builtin) && (format == MM_CODEC_KCDC || format == MM_CODEC_KCD2);
}

static void *find_registered(const MMCodecRegistry *registry, int encode, MMI32 format)
{
    return registry && registry->services.convert ? mm_codec_registry_find(registry, encode, format) : NULL;
}

int mm_codec_registry_can_code(const MMCodecRegistry *registry, int encode, MMI32 format)
{
    return has_builtin(registry, format) || find_registered(registry, encode, format) != NULL;
}

typedef struct MMCodecCall
{
    const MMCodecRegistry *registry;
    void *registered;
    int encode;
    int builtin_status;
} MMCodecCall;

static void *find_packed_codec(void *context, MMI32 format)
{
    MMCodecCall *call = (MMCodecCall *)context;
    if (has_builtin(call->registry, format))
        return call;
    call->registered = find_registered(call->registry, call->encode, format);
    return call->registered ? call : NULL;
}

static int convert_packed_codec(void *context, MMI32 format, const void *input, MMU32 bytes,
                                void *output, MMU32 capacity, MMI32 *written)
{
    MMCodecCall *call = (MMCodecCall *)context;
    const MMCodecRegistry *registry = call->registry;
    const MMStorageAllocator *allocator = registry ? &registry->storage.allocator : NULL;
    if (call->registered)
        return registry->services.convert(registry->services.context, call->registered, call->encode,
                                          format, input, bytes, output, capacity, written);
    call->builtin_status = call->encode ?
        mm_codec_encode(format, input, bytes, output, capacity, written, allocator) :
        mm_codec_decode(format, input, bytes, output, capacity, written);
    return call->builtin_status == MM_DATA_OK;
}

int mm_codec_registry_code(const MMCodecRegistry *registry, MMI32 format, const void *input, MMU32 bytes,
                           void *output, MMU32 capacity, MMI32 *written, int *builtin_status)
{
    MMCodecCall call;
    MMPackedDecoder codec;
    int result;
    call.registry = registry;
    call.registered = NULL;
    call.encode = format != 0;
    call.builtin_status = MM_DATA_OK;
    codec.context = &call;
    codec.find = find_packed_codec;
    codec.convert = convert_packed_codec;
    result = call.encode ? mm_packed_encode(&codec, format, input, bytes, output, capacity, written) :
                           mm_packed_decode(&codec, input, bytes, output, capacity, written);
    if (builtin_status)
        *builtin_status = call.builtin_status;
    return result;
}
