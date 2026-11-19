/* Mixer policy converted from Kauai audio_mixer.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_AUDIO_MIXER_H
#define MM_AUDIO_MIXER_H
#include "audio_clock.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMAudioVoice
    {
        MMI32 id, plays;
        const void *sample;
        const float *stereo;
        size_t frames;
        float volume;
        MMU32 offset_ms;
        int paused, stopped;
    } MMAudioVoice;
    typedef struct MMAudioAllocator
    {
        void *context;
        void *(*allocate)(void *, size_t);
        void (*release)(void *, void *);
    } MMAudioAllocator;
    typedef struct MMAudioQueue MMAudioQueue;
    typedef struct MMAudioMixer
    {
        MMAudioQueue *queues;
        MMAudioCounter next, started, completed, rendered_frames;
        MMAudioAllocator allocator;
        float volume;
        unsigned groups;
        int suspended;
    } MMAudioMixer;
    /* Serialized by the host. Sample identity and PCM storage are borrowed until
     * replacement/removal. Sync is transactional. Render allocates nothing. */
    void mm_audio_mixer_init(MMAudioMixer *, const MMAudioAllocator *);
    void mm_audio_mixer_dispose(MMAudioMixer *);
    MMAudioCounter mm_audio_mixer_add(MMAudioMixer *);
    void mm_audio_mixer_remove(MMAudioMixer *, MMAudioCounter);
    int mm_audio_mixer_sync(MMAudioMixer *, MMAudioCounter, const MMAudioVoice *, size_t);
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
