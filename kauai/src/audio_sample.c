#include "audio_sample.h"
#include "mm/native_runtime.h"
#include "mm/reference.h"
#include <float.h>
#include <stdlib.h>

struct MMAudioSample
{
    MMNativeMutex *mutex;
    MMReference references;
    float *stereo;
    size_t frames;
    void (*free_buffer)(void *);
};

MMAudioSample *mm_audio_sample_adopt(float *stereo, size_t frames, void (*free_buffer)(void *))
{
    MMAudioSample *sample;
    size_t i;
    if ((frames && !stereo) || !free_buffer || frames > (size_t)-1 / (2 * sizeof(float)))
        return NULL;
    sample = (MMAudioSample *)malloc(sizeof(*sample));
    if (!sample)
        return NULL;
    sample->mutex = mm_native_mutex_create(NULL);
    if (!sample->mutex)
    {
        free(sample);
        return NULL;
    }
    mm_reference_init(&sample->references);
    sample->stereo = stereo;
    sample->frames = frames;
    sample->free_buffer = free_buffer;
    for (i = 0; i < frames * 2; ++i)
        if (!(stereo[i] >= -FLT_MAX && stereo[i] <= FLT_MAX))
            stereo[i] = 0;
    return sample;
}

void mm_audio_sample_retain(const void *object)
{
    MMAudioSample *sample = (MMAudioSample *)object;
    mm_native_mutex_enter(sample->mutex);
    mm_reference_add(&sample->references);
    mm_native_mutex_leave(sample->mutex);
}

void mm_audio_sample_release(const void *object)
{
    MMAudioSample *sample = (MMAudioSample *)object;
    MMI32 remaining;
    if (!sample)
        return;
    mm_native_mutex_enter(sample->mutex);
    remaining = mm_reference_drop(&sample->references);
    mm_native_mutex_leave(sample->mutex);
    if (!remaining)
    {
        mm_native_mutex_destroy(sample->mutex);
        if (sample->stereo)
            sample->free_buffer(sample->stereo);
        free(sample);
    }
}

const float *mm_audio_sample_data(const MMAudioSample *sample)
{
    return sample->stereo;
}

size_t mm_audio_sample_frames(const MMAudioSample *sample)
{
    return sample->frames;
}

MMAudioVoice mm_audio_sample_voice(const MMAudioSample *sample, MMI32 id, MMI32 plays)
{
    MMAudioVoice voice;
    voice.id = id;
    voice.plays = plays;
    voice.sample = sample;
    voice.stereo = sample ? sample->stereo : NULL;
    voice.frames = sample ? sample->frames : 0;
    voice.volume = 1;
    voice.offset_ms = 0;
    voice.paused = voice.stopped = 0;
    return voice;
}
