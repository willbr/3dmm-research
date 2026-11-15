/* Stream arbitration converted from Kauai mididev2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_MIDI_MIX_H
#define MM_MIDI_MIX_H
#include "midi_render.h"
#include "sound_queue.h"
#include "midi.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMidiQueueServices
    {
        MMSoundQueueServices entries;
        void *context;
        MMU32 (*clock)(void *), (*start)(void *);
        void (*set_start)(void *, MMU32);
        int (*play)(void *, const MMSoundEntry *); /* NULL stops this queue. */
    } MMMidiQueueServices;
    void mm_midi_queue_run(const MMMidiQueueServices *, MMI32 first);
    void mm_midi_queue_pause(const MMMidiQueueServices *, MMI32 first);
    void mm_midi_queue_notify(const MMMidiQueueServices *, void *sound);
    typedef struct MMMidiMixVoice
    {
        void *queue, *sound;
        MMI32 id, priority, plays;
        MMU32 duration, offset;
        MMI32 volume;
        MMU32 start;
    } MMMidiMixVoice;
    typedef struct MMMidiMixState
    {
        int playing, waiting;
        MMI32 outstanding, base_volume, sound_volume;
    } MMMidiMixState;
    typedef struct MMMidiMixServices
    {
        void *context;
        MMI32 (*count)(void *);
        void (*get)(void *, MMI32, MMMidiMixVoice *);
        void (*put)(void *, MMI32, const MMMidiMixVoice *);
        int (*insert)(void *, MMI32, const MMMidiMixVoice *);
        void (*erase)(void *, MMI32);
        void (*rotate)(void *, MMI32 skipped);
        MMU32 (*clock)(void *), (*duration)(void *, void *sound);
        void (*enter)(void *), (*leave)(void *), (*stop_device)(void *), (*wait_return)(void *),
            (*signal_return)(void *);
        void (*signal_worker)(void *);
        int (*active)(void *), (*activate)(void *, int);
        void (*busy)(void *), (*volume)(void *, MMI32);
        int (*seek)(void *, void *sound, MMU32 time, MMI32 *skip);
        void *(*lock_key)(void *, MMI32 *bytes);
        void (*unlock_key)(void *);
        void *(*lock_sound)(void *, void *sound, MMI32 *bytes);
        void (*unlock_sound)(void *, void *sound);
        int (*submit)(void *, void *data, MMI32 bytes, MMI32 skip, MMI32 repeats, void *sound);
        void (*notify)(void *, void *queue, void *sound);
    } MMMidiMixServices;
    /* Caller holds its recursive mutex; notify temporarily leaves it. Device Stop
     * returns all submitted buffers, potentially from another thread. */
    void mm_midi_mix_stop(MMMidiMixState *, const MMMidiMixServices *);
    void mm_midi_mix_volume(MMMidiMixState *, const MMMidiMixServices *, MMI32);
    void mm_midi_mix_suspend(MMMidiMixState *, const MMMidiMixServices *, int);
    int mm_midi_mix_play(MMMidiMixState *, const MMMidiMixServices *, const MMMidiMixVoice *);
    void mm_midi_mix_restart(MMMidiMixState *, const MMMidiMixServices *, int fresh);
    void mm_midi_mix_submit(MMMidiMixState *, const MMMidiMixServices *, MMU32 now);
    void mm_midi_mix_return(MMMidiMixState *, const MMMidiMixServices *, void *sound);
    MMU32 mm_midi_mix_expire(MMMidiMixState *, const MMMidiMixServices *);
    /* Appends groups of at most 100 events. No sample/codec/device dependency. */
    typedef struct MMMidiStreamBuildServices
    {
        void *context;
        int (*next)(void *, MMMidiEvent *);
        int (*append)(void *, const MMMidiStreamEvent *, MMI32 count);
    } MMMidiStreamBuildServices;
    int mm_midi_stream_build(const MMMidiStreamBuildServices *, MMU32 *duration);
    int mm_midi_stream_seek(const MMMidiStreamEvent *, MMI32 count, MMU32 time, void *context,
                                  int (*prepend)(void *, const MMMidiStreamEvent *, MMI32),
                                  MMI32 *skip_bytes);
#ifdef __cplusplus
}
#endif
#endif
