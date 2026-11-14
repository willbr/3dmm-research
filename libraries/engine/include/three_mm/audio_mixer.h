/* Mixer policy converted from Kauai audio_mixer.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_AUDIO_MIXER_H
#define THREE_MM_AUDIO_MIXER_H
#include "audio_clock.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMAudioVoice
    {
        ThreeMMI32 id, plays;
        const void *sample;
        const float *stereo;
        size_t frames;
        float volume;
        ThreeMMU32 offset_ms;
        int paused, stopped;
    } ThreeMMAudioVoice;
    typedef struct ThreeMMAudioAllocator
    {
        void *context;
        void *(*allocate)(void *, size_t);
        void (*release)(void *, void *);
    } ThreeMMAudioAllocator;
    typedef struct ThreeMMAudioQueue ThreeMMAudioQueue;
    typedef struct ThreeMMAudioMixer
    {
        ThreeMMAudioQueue *queues;
        ThreeMMAudioCounter next, started, completed, rendered_frames;
        ThreeMMAudioAllocator allocator;
        float volume;
        unsigned groups;
        int suspended;
    } ThreeMMAudioMixer;
    /* Serialized by the host. Sample identity and PCM storage are borrowed until
     * replacement/removal. Sync is transactional. Render allocates nothing. */
    void three_mm_audio_mixer_init(ThreeMMAudioMixer *, const ThreeMMAudioAllocator *);
    void three_mm_audio_mixer_dispose(ThreeMMAudioMixer *);
    ThreeMMAudioCounter three_mm_audio_mixer_add(ThreeMMAudioMixer *);
    void three_mm_audio_mixer_remove(ThreeMMAudioMixer *, ThreeMMAudioCounter);
    int three_mm_audio_mixer_sync(ThreeMMAudioMixer *, ThreeMMAudioCounter, const ThreeMMAudioVoice *, size_t);
    int three_mm_audio_mixer_done(const ThreeMMAudioMixer *, ThreeMMAudioCounter, ThreeMMI32);
    void three_mm_audio_mixer_volume(ThreeMMAudioMixer *, float);
    void three_mm_audio_mixer_suspend(ThreeMMAudioMixer *, int);
    void three_mm_audio_mixer_begin(ThreeMMAudioMixer *);
    void three_mm_audio_mixer_end(ThreeMMAudioMixer *);
    void three_mm_audio_mixer_render(ThreeMMAudioMixer *, float *, size_t);
#ifdef __cplusplus
}
#endif
#endif
