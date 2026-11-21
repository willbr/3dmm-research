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

static int over_memory_limit(MMU32 bytes, MMU32 overhead, MMI32 limit)
{
    return limit < 0 || overhead > (MMU32)limit || bytes > (MMU32)limit - overhead;
}

int mm_wave_stream_prepare(MMOwnedBlock *source, MMOwnedBlock *dest, int removable,
                            MMU32 stream_bytes, MMI32 memory_limit, MMBlockTransform decode,
                            const MMStorageAllocator *allocator)
{
    MMBlockFile file;
    MMBlockIO from, to;
    if (source == dest || mm_owned_block_check(dest) != MM_DATA_OK || dest->view.kind != MM_BLOCK_EMPTY ||
        mm_owned_block_io(source, &from) != MM_DATA_OK || from.view.kind != MM_BLOCK_FILE)
        return 0;
    file.file = source->file;
    file.offset = from.view.start;
    file.length = from.view.length;
    if (!from.view.packed && removable)
    {
        if (mm_owned_block_set_temp(dest, file.length, over_memory_limit(file.length, stream_bytes, memory_limit)) ==
                MM_DATA_OK &&
            mm_owned_block_io(dest, &to) == MM_DATA_OK && mm_block_copy(&from, &to, 0, allocator) == MM_DATA_OK)
            return 1;
        goto fail;
    }
    if (mm_owned_block_set_file(dest, &file, from.view.packed) != MM_DATA_OK)
        return 0;
    if (!from.view.packed)
        return 1;
    if (mm_owned_block_code(dest, 0, NULL, decode, NULL) != MM_DATA_OK)
        goto fail;
    if (over_memory_limit(dest->view.capacity, stream_bytes, memory_limit) &&
        mm_owned_block_materialize(dest, 0, &file) == MM_DATA_OK && file.file)
    {
        mm_owned_block_set_file(dest, &file, 0);
        dest->files->release(dest->files->context, file.file);
    }
    return 1;
fail:
    mm_owned_block_clear(dest);
    return 0;
}

int mm_wave_stream_fetch(MMOwnedBlock *block, void *object_out, void *size_out, const MMSoundPayloadServices *s)
{
    void *object = NULL, *stream = NULL;
    MMI32 bytes = s->object_bytes;
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    memcpy(object_out, &object, sizeof(object));
    if (mm_owned_block_check(block) != MM_DATA_OK || block->view.kind != MM_BLOCK_FILE)
        return 0;
    stream = s->open_stream(s->context, block);
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
