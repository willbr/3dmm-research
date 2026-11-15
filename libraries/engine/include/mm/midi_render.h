/* MIDI render policy converted from Kauai midi_render.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_MIDI_RENDER_H
#define MM_MIDI_RENDER_H
#include "audio_mixer.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMidiStreamEvent
    {
        MMU32 deltaTicks, streamId, eventWord;
    } MMMidiStreamEvent;
    typedef struct MMMidiRenderServices
    {
        void *context;
        int (*ready)(void *);
        void (*message)(void *, MMU32);
        void (*render)(void *, float *, size_t);
        void (*silence)(void *);
        void (*reset)(void *);
    } MMMidiRenderServices;
    typedef struct MMMidiRenderBuffer MMMidiRenderBuffer;
    typedef struct MMMidiRenderer
    {
        MMMidiRenderBuffer *head, *tail, *returned, *returned_tail;
        MMAudioCounter wait_frames, fraction;
        MMU32 tempo, rate;
        float volume;
        MMAudioCounter queued, completed, messages, notes, frames, audible_frames;
        MMAudioAllocator allocator;
        MMMidiRenderServices services;
    } MMMidiRenderer;
    /* Host serializes calls. Queue borrows aligned immutable events until PopReturn
     * or disposal; render moves nodes without allocating/freeing or notifying clients. */
    void mm_midi_render_init(MMMidiRenderer *, const MMMidiRenderServices *,
                                   const MMAudioAllocator *);
    void mm_midi_render_dispose(MMMidiRenderer *);
    void mm_midi_render_clock_reset(MMMidiRenderer *, MMU32 rate);
    int mm_midi_render_queue(MMMidiRenderer *, MMMidiStreamEvent *, MMI32 bytes, MMI32 offset,
                                   MMI32 repeats, size_t tag);
    int mm_midi_render(MMMidiRenderer *, float *, size_t);
    void mm_midi_render_stop(MMMidiRenderer *);
    int mm_midi_render_pop(MMMidiRenderer *, void **data, size_t *tag);
    int mm_midi_render_reset(MMMidiRenderer *);
    void mm_midi_render_idle(MMMidiRenderer *);
    void mm_midi_render_volume(MMMidiRenderer *, float);
#ifdef __cplusplus
}
#endif
#endif
