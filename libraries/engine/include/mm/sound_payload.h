/* Sound payload factories converted from Kauai midi.cpp, sdl_sound.cpp and sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SOUND_PAYLOAD_H
#define MM_ENGINE_SOUND_PAYLOAD_H
#include "resource_cache.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Owned blocks and native object/device services are borrowed for the call.
     * Output pointers are pointer-sized native slots; byte estimates are signed
     * 32-bit slots. Callbacks must not re-enter the controller. */
    typedef struct MMSoundPayloadServices
    {
        void *context;
        MMI32 object_bytes;
        MMBlockTransform decode_block;
        void *(*create)(void *);
        void (*release)(void *, void *);
        /* Take ownership of the new stream handle, which may be NULL. */
        void (*adopt_midi)(void *, void *object, void *handle);
        void (*invalidate)(void *, void *, size_t);
        /* Encoded WAVE bytes are borrowed until decode returns. */
        int (*decode)(void *, void *object, const void *encoded, MMU32 bytes);
        int (*allocation_size)(void *, void *object, MMI32 *);
        void (*decoded)(void *);
        /* AudioMan prepares its stream from a borrowed file-backed block. */
        void *(*open_stream)(void *, const MMOwnedBlock *);
        void (*release_stream)(void *, void *stream);
        MMI32 (*stream_size)(void *, void *stream);
        int (*decode_stream)(void *, void *object, void *stream);
        void (*adopt_stream)(void *, void *object, void *stream);
    } MMSoundPayloadServices;
    void *mm_midi_payload_read(MMOwnedBlock *, const MMSoundPayloadServices *);
    int mm_midi_payload_fetch(MMOwnedBlock *, void *object_out, void *size_out, const MMSoundPayloadServices *);
    int mm_wave_payload_fetch(MMOwnedBlock *, void *object_out, void *size_out, const MMSoundPayloadServices *);
    int mm_wave_stream_fetch(MMResourceCache *, MMU32 type, MMU32 number, void *object_out, void *size_out,
                                   const MMSoundPayloadServices *);
#ifdef __cplusplus
}
#endif
#endif
