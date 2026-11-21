/* Sound payload factories converted from Kauai midi.cpp, sdl_sound.cpp and sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_payload.h"
#include "mm/wave.h"
#include <string.h>

static int unpack(MMOwnedBlock *block, const MMSoundPayloadServices *s)
{
    return mm_owned_block_code(block, 0, s->context, s->decode_block, NULL) == MM_DATA_OK;
}

void *mm_midi_payload_read(MMOwnedBlock *block, const MMSoundPayloadServices *s)
{
    MMBlockIO io;
    void *handle;
    void *object = s->create(s->context);
    if (!object)
        return NULL;
    if (!unpack(block, s) || mm_owned_block_io(block, &io) != MM_DATA_OK ||
        mm_block_read_handle(&io, 0, io.view.length, 0, &handle) != MM_DATA_OK)
    {
        s->release(s->context, object);
        return NULL;
    }
    s->adopt_midi(s->context, object, handle);
    return object;
}

int mm_midi_payload_fetch(MMOwnedBlock *block, void *object_out, void *size_out, const MMSoundPayloadServices *s)
{
    void *object;
    MMI32 bytes = (MMI32)block->view.length;
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    if (!unpack(block, s))
        goto fail;
    bytes = (MMI32)block->view.length;
    memcpy(size_out, &bytes, 4);
    object = mm_midi_payload_read(block, s);
    if (!object)
        goto fail;
    memcpy(object_out, &object, sizeof(object));
    return 1;
fail:
    s->invalidate(s->context, object_out, sizeof(object));
    s->invalidate(s->context, size_out, 4);
    return 0;
}

int mm_wave_payload_fetch(MMOwnedBlock *block, void *object_out, void *size_out, const MMSoundPayloadServices *s)
{
    void *object = NULL;
    MMI32 bytes = s->object_bytes;
    MMBlockIO io;
    MMWaveBuffer encoded;
    int decoded = 0;
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    memcpy(object_out, &object, sizeof(object));
    if (!unpack(block, s) || mm_owned_block_io(block, &io) != MM_DATA_OK ||
        io.view.length < 12 || !mm_wave_buffer_init(&encoded, NULL))
        return 0;
    if (mm_wave_buffer_reserve(&encoded, io.view.length) &&
        mm_block_read(&io, 0, encoded.data, io.view.length, 0) == MM_DATA_OK)
    {
        object = s->create(s->context);
        decoded = object && s->decode(s->context, object, encoded.data, io.view.length);
    }
    mm_wave_buffer_dispose(&encoded);
    if (!decoded || !s->allocation_size(s->context, object, &bytes))
    {
        if (object)
            s->release(s->context, object);
        return 0;
    }
    memcpy(size_out, &bytes, 4);
    memcpy(object_out, &object, sizeof(object));
    if (s->decoded)
        s->decoded(s->context);
    return 1;
}

int mm_wave_stream_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, void *object_out, void *size_out,
                         const MMSoundPayloadServices *s)
{
    void *object = NULL, *stream = NULL;
    MMI32 bytes = s->object_bytes;
    MMOwnedBlock block;
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    memcpy(object_out, &object, sizeof(object));
    if (mm_owned_block_init(&block, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return 0;
    if (mm_resource_cache_read_block(cache, type, number, &block) == MM_CHUNK_OK)
        stream = s->open_stream(s->context, &block);
    mm_owned_block_clear(&block);
    if (!stream)
        return 0;
    bytes = s->stream_size(s->context, stream);
    memcpy(size_out, &bytes, 4);
    object = s->create(s->context);
    if (object && !s->decode_stream(s->context, object, stream))
    {
        s->release(s->context, object);
        object = NULL;
    }
    if (object)
        s->adopt_stream(s->context, object, stream);
    else
        s->release_stream(s->context, stream);
    memcpy(object_out, &object, sizeof(object));
    return object != NULL;
}
