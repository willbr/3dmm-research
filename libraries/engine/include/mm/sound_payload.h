/* Sound payload factories converted from Kauai midi.cpp, sdl_sound.cpp and sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SOUND_PAYLOAD_H
#define MM_ENGINE_SOUND_PAYLOAD_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Opaque owners and block/file/device services are borrowed for the call.
     * Output pointers are pointer-sized native slots; byte estimates are signed
     * 32-bit slots. Callbacks must not re-enter the controller. */
    typedef struct MMSoundPayloadServices
    {
        void *context;
        MMI32 object_bytes;
        int (*unpack)(void *, void *block);
        MMI32 (*block_size)(void *, void *block, int packed);
        void *(*create)(void *);
        void (*release)(void *, void *);
        int (*read_midi)(void *, void *object, void *block);
        void (*invalidate)(void *, void *, size_t);
        /* Sampled SDL reader: read_encoded precedes object allocation. Decode
         * uses external audio services; allocation size includes decoded samples. */
        int (*read_encoded)(void *, void *block, MMI32 bytes);
        int (*decode)(void *, void *object);
        int (*allocation_size)(void *, void *object, MMI32 *);
        void (*decoded)(void *);
        /* AudioMan reader: stream creation owns physical chunk/block access. */
        void *(*open_stream)(void *, void *cache, MMU32 type, MMU32 number);
        MMI32 (*stream_size)(void *, void *stream);
        int (*decode_stream)(void *, void *object, void *stream);
        void (*adopt_stream)(void *, void *object, void *stream);
    } MMSoundPayloadServices;
    void *mm_midi_payload_read(void *block, const MMSoundPayloadServices *);
    int mm_midi_payload_fetch(void *block, void *object_out, void *size_out, const MMSoundPayloadServices *);
    int mm_wave_payload_fetch(void *block, void *object_out, void *size_out, const MMSoundPayloadServices *);
    int mm_wave_stream_fetch(void *cache, MMU32 type, MMU32 number, void *object_out, void *size_out,
                                   const MMSoundPayloadServices *);
#ifdef __cplusplus
}
#endif
#endif
