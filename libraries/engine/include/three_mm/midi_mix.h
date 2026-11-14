/* Stream arbitration converted from Kauai mididev2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_MIDI_MIX_H
#define THREE_MM_MIDI_MIX_H
#include "midi_render.h"
#include "sound_queue.h"
#include "midi.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMidiQueueServices
    {
        ThreeMMSoundQueueServices entries;
        void *context;
        ThreeMMU32 (*clock)(void *), (*start)(void *);
        void (*set_start)(void *, ThreeMMU32);
        int (*play)(void *, const ThreeMMSoundEntry *); /* NULL stops this queue. */
    } ThreeMMMidiQueueServices;
    void three_mm_midi_queue_run(const ThreeMMMidiQueueServices *, ThreeMMI32 first);
    void three_mm_midi_queue_pause(const ThreeMMMidiQueueServices *, ThreeMMI32 first);
    void three_mm_midi_queue_notify(const ThreeMMMidiQueueServices *, void *sound);
    typedef struct ThreeMMMidiMixVoice
    {
        void *queue, *sound;
        ThreeMMI32 id, priority, plays;
        ThreeMMU32 duration, offset;
        ThreeMMI32 volume;
        ThreeMMU32 start;
    } ThreeMMMidiMixVoice;
    typedef struct ThreeMMMidiMixState
    {
        int playing, waiting;
        ThreeMMI32 outstanding, base_volume, sound_volume;
    } ThreeMMMidiMixState;
    typedef struct ThreeMMMidiMixServices
    {
        void *context;
        ThreeMMI32 (*count)(void *);
        void (*get)(void *, ThreeMMI32, ThreeMMMidiMixVoice *);
        void (*put)(void *, ThreeMMI32, const ThreeMMMidiMixVoice *);
        int (*insert)(void *, ThreeMMI32, const ThreeMMMidiMixVoice *);
        void (*erase)(void *, ThreeMMI32);
        void (*rotate)(void *, ThreeMMI32 skipped);
        ThreeMMU32 (*clock)(void *), (*duration)(void *, void *sound);
        void (*enter)(void *), (*leave)(void *), (*stop_device)(void *), (*wait_return)(void *),
            (*signal_return)(void *);
        void (*signal_worker)(void *);
        int (*active)(void *), (*activate)(void *, int);
        void (*busy)(void *), (*volume)(void *, ThreeMMI32);
        int (*seek)(void *, void *sound, ThreeMMU32 time, ThreeMMI32 *skip);
        void *(*lock_key)(void *, ThreeMMI32 *bytes);
        void (*unlock_key)(void *);
        void *(*lock_sound)(void *, void *sound, ThreeMMI32 *bytes);
        void (*unlock_sound)(void *, void *sound);
        int (*submit)(void *, void *data, ThreeMMI32 bytes, ThreeMMI32 skip, ThreeMMI32 repeats, void *sound);
        void (*notify)(void *, void *queue, void *sound);
    } ThreeMMMidiMixServices;
    /* Caller holds its recursive mutex; notify temporarily leaves it. Device Stop
     * returns all submitted buffers, potentially from another thread. */
    void three_mm_midi_mix_stop(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *);
    void three_mm_midi_mix_volume(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *, ThreeMMI32);
    void three_mm_midi_mix_suspend(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *, int);
    int three_mm_midi_mix_play(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *, const ThreeMMMidiMixVoice *);
    void three_mm_midi_mix_restart(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *, int fresh);
    void three_mm_midi_mix_submit(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *, ThreeMMU32 now);
    void three_mm_midi_mix_return(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *, void *sound);
    ThreeMMU32 three_mm_midi_mix_expire(ThreeMMMidiMixState *, const ThreeMMMidiMixServices *);
    /* Appends groups of at most 100 events. No sample/codec/device dependency. */
    typedef struct ThreeMMMidiStreamBuildServices
    {
        void *context;
        int (*next)(void *, ThreeMMMidiEvent *);
        int (*append)(void *, const ThreeMMMidiStreamEvent *, ThreeMMI32 count);
    } ThreeMMMidiStreamBuildServices;
    int three_mm_midi_stream_build(const ThreeMMMidiStreamBuildServices *, ThreeMMU32 *duration);
    int three_mm_midi_stream_seek(const ThreeMMMidiStreamEvent *, ThreeMMI32 count, ThreeMMU32 time, void *context,
                                  int (*prepend)(void *, const ThreeMMMidiStreamEvent *, ThreeMMI32),
                                  ThreeMMI32 *skip_bytes);
#ifdef __cplusplus
}
#endif
#endif
