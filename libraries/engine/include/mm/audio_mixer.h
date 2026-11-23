/* Mixer policy converted from Kauai audio_mixer.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_AUDIO_MIXER_H
#define MM_AUDIO_MIXER_H
#include "audio_clock.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum { MM_AUDIO_MIX_RATE = 48000 };
    typedef struct MMAudioVoice
    {
        MMI32 id, plays; /* Zero plays loops indefinitely. */
        const void *sample;
        const float *stereo;
        size_t frames;
        float volume;
        MMU32 offset_ms;
        int paused, stopped;
    } MMAudioVoice;
    typedef int (*MMAudioVoiceReader)(void *, size_t index, MMAudioVoice *);
    typedef struct MMAudioAllocator
    {
        void *context;
        void *(*allocate)(void *, size_t);
        void (*release)(void *, void *);
    } MMAudioAllocator;
    typedef struct MMAudioSampleServices
    {
        void (*retain)(const void *sample);
        void (*release)(const void *sample);
    } MMAudioSampleServices;
    typedef struct MMAudioQueue MMAudioQueue;
    typedef struct MMAudioMixer
    {
        MMAudioQueue *queues;
        MMAudioCounter next, started, completed, rendered_frames;
        MMAudioAllocator allocator;
        MMAudioSampleServices samples;
        float volume;
        unsigned groups;
        int suspended;
    } MMAudioMixer;
    /* Serialized by the host. Without sample services PCM is borrowed until
     * replacement/removal. With both callbacks the mixer owns a reference for
     * each published voice. Callbacks cannot fail or reenter the mixer. Sync is
     * transactional. Render neither allocates nor changes sample references. */
    void mm_audio_mixer_init(MMAudioMixer *, const MMAudioAllocator *);
    void mm_audio_mixer_init_samples(MMAudioMixer *, const MMAudioAllocator *, const MMAudioSampleServices *);
    void mm_audio_mixer_dispose(MMAudioMixer *);
    MMAudioCounter mm_audio_mixer_add(MMAudioMixer *);
    void mm_audio_mixer_remove(MMAudioMixer *, MMAudioCounter);
    int mm_audio_mixer_sync(MMAudioMixer *, MMAudioCounter, const MMAudioVoice *, size_t);
    /* Read each candidate once while the old voices and their samples are live.
     * Readers must not reenter the mixer. Failure leaves the queue untouched. */
    int mm_audio_mixer_sync_read(MMAudioMixer *, MMAudioCounter, size_t, void *, MMAudioVoiceReader);
    int mm_audio_mixer_done(const MMAudioMixer *, MMAudioCounter, MMI32);
    void mm_audio_mixer_volume(MMAudioMixer *, float);
    void mm_audio_mixer_suspend(MMAudioMixer *, int);
    void mm_audio_mixer_begin(MMAudioMixer *);
    void mm_audio_mixer_end(MMAudioMixer *);
    void mm_audio_mixer_render(MMAudioMixer *, float *, size_t);
    void mm_audio_mix_stereo(float *output, const float *input, size_t frames, float volume);
    void mm_audio_clip_stereo(float *output, size_t frames);
#ifdef __cplusplus
}
#endif
#endif
