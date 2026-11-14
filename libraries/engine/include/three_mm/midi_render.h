/* MIDI render policy converted from Kauai midi_render.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_MIDI_RENDER_H
#define THREE_MM_MIDI_RENDER_H
#include "audio_mixer.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMidiStreamEvent
    {
        ThreeMMU32 deltaTicks, streamId, eventWord;
    } ThreeMMMidiStreamEvent;
    typedef struct ThreeMMMidiRenderServices
    {
        void *context;
        int (*ready)(void *);
        void (*message)(void *, ThreeMMU32);
        void (*render)(void *, float *, size_t);
        void (*silence)(void *);
        void (*reset)(void *);
    } ThreeMMMidiRenderServices;
    typedef struct ThreeMMMidiRenderBuffer ThreeMMMidiRenderBuffer;
    typedef struct ThreeMMMidiRenderer
    {
        ThreeMMMidiRenderBuffer *head, *tail, *returned, *returned_tail;
        ThreeMMAudioCounter wait_frames, fraction;
        ThreeMMU32 tempo, rate;
        float volume;
        ThreeMMAudioCounter queued, completed, messages, notes, frames, audible_frames;
        ThreeMMAudioAllocator allocator;
        ThreeMMMidiRenderServices services;
    } ThreeMMMidiRenderer;
    /* Host serializes calls. Queue borrows aligned immutable events until PopReturn
     * or disposal; render moves nodes without allocating/freeing or notifying clients. */
    void three_mm_midi_render_init(ThreeMMMidiRenderer *, const ThreeMMMidiRenderServices *,
                                   const ThreeMMAudioAllocator *);
    void three_mm_midi_render_dispose(ThreeMMMidiRenderer *);
    void three_mm_midi_render_clock_reset(ThreeMMMidiRenderer *, ThreeMMU32 rate);
    int three_mm_midi_render_queue(ThreeMMMidiRenderer *, ThreeMMMidiStreamEvent *, ThreeMMI32 bytes, ThreeMMI32 offset,
                                   ThreeMMI32 repeats, size_t tag);
    int three_mm_midi_render(ThreeMMMidiRenderer *, float *, size_t);
    void three_mm_midi_render_stop(ThreeMMMidiRenderer *);
    int three_mm_midi_render_pop(ThreeMMMidiRenderer *, void **data, size_t *tag);
    int three_mm_midi_render_reset(ThreeMMMidiRenderer *);
    void three_mm_midi_render_idle(ThreeMMMidiRenderer *);
    void three_mm_midi_render_volume(ThreeMMMidiRenderer *, float);
#ifdef __cplusplus
}
#endif
#endif
