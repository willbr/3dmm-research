/* Sound payload factories converted from Kauai midi.cpp, sdl_sound.cpp and sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/sound_payload.h"
#include <string.h>
void *three_mm_midi_payload_read(void *block, const ThreeMMSoundPayloadServices *s)
{
    void *object = s->create(s->context);
    if (!object)
        return NULL;
    if (!s->unpack(s->context, block) || !s->read_midi(s->context, object, block))
    {
        s->release(s->context, object);
        return NULL;
    }
    return object;
}
int three_mm_midi_payload_fetch(void *block, void *object_out, void *size_out, const ThreeMMSoundPayloadServices *s)
{
    void *object;
    ThreeMMI32 bytes = s->block_size(s->context, block, 1);
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    if (!s->unpack(s->context, block))
        goto fail;
    bytes = s->block_size(s->context, block, 0);
    memcpy(size_out, &bytes, 4);
    object = three_mm_midi_payload_read(block, s);
    if (!object)
        goto fail;
    memcpy(object_out, &object, sizeof(object));
    return 1;
fail:
    s->invalidate(s->context, object_out, sizeof(object));
    s->invalidate(s->context, size_out, 4);
    return 0;
}
int three_mm_wave_payload_fetch(void *block, void *object_out, void *size_out, const ThreeMMSoundPayloadServices *s)
{
    void *object = NULL;
    ThreeMMI32 bytes = s->object_bytes;
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    memcpy(object_out, &object, sizeof(object));
    if (!s->unpack(s->context, block))
        return 0;
    bytes = s->block_size(s->context, block, 0);
    if (bytes < 12 || !s->read_encoded(s->context, block, bytes))
        return 0;
    object = s->create(s->context);
    if (!object)
        return 0;
    if (!s->decode(s->context, object) || !s->allocation_size(s->context, object, &bytes))
    {
        s->release(s->context, object);
        return 0;
    }
    memcpy(size_out, &bytes, 4);
    memcpy(object_out, &object, sizeof(object));
    if (s->decoded)
        s->decoded(s->context);
    return 1;
}
int three_mm_wave_stream_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *object_out, void *size_out,
                               const ThreeMMSoundPayloadServices *s)
{
    void *object = NULL, *stream;
    ThreeMMI32 bytes = s->object_bytes;
    memcpy(size_out, &bytes, 4);
    if (!object_out)
        return 1;
    memcpy(object_out, &object, sizeof(object));
    stream = s->open_stream(s->context, cache, type, number);
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
        s->release(s->context, stream);
    memcpy(object_out, &object, sizeof(object));
    return object != NULL;
}
