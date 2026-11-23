/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "audio_mixer.h"
#include "mm/native_runtime.h"
#include <stdlib.h>

struct MMAudioDeviceMixer
{
    MMNativeMutex *mutex;
    MMAudioMixer core;
};

MMAudioDeviceMixer *mm_audio_device_mixer_create(void)
{
    static const MMAudioSampleServices samples = {mm_audio_sample_retain, mm_audio_sample_release};
    MMAudioDeviceMixer *mixer = (MMAudioDeviceMixer *)malloc(sizeof(*mixer));
    if (!mixer)
        return NULL;
    mixer->mutex = mm_native_mutex_create(NULL);
    if (!mixer->mutex)
    {
        free(mixer);
        return NULL;
    }
    mm_audio_mixer_init_samples(&mixer->core, NULL, &samples);
    return mixer;
}

void mm_audio_device_mixer_destroy(MMAudioDeviceMixer *mixer)
{
    if (!mixer)
        return;
    mm_audio_mixer_dispose(&mixer->core);
    mm_native_mutex_destroy(mixer->mutex);
    free(mixer);
}

MMAudioMixer *mm_audio_device_mixer_enter(MMAudioDeviceMixer *mixer)
{
    if (mm_native_mutex_enter(mixer->mutex) != 0)
        abort();
    return &mixer->core;
}

void mm_audio_device_mixer_leave(MMAudioDeviceMixer *mixer)
{
    if (mm_native_mutex_leave(mixer->mutex) != 0)
        abort();
}

typedef struct SoundVoices
{
    const MMSoundQueue *queue;
    const MMAudioSample *(*sample)(void *);
    MMI32 full_volume;
} SoundVoices;

static int read_sound_voice(void *context, size_t index, MMAudioVoice *voice)
{
    const SoundVoices *input = (const SoundVoices *)context;
    const MMSoundEntry entry = mm_sound_queue_get(input->queue, input->queue->current + (MMI32)index);
    const MMAudioSample *sample = input->sample(entry.object);
    if (!sample)
        return 0;
    *voice = mm_audio_sample_voice(sample, entry.id, entry.plays > 0 ? entry.plays : 0);
    voice->volume = (float)(entry.volume > 0 ? entry.volume : 0) / (float)input->full_volume;
    voice->offset_ms = entry.offset;
    voice->paused = entry.pause > 0;
    voice->stopped = entry.pause < 0;
    return 1;
}

int mm_audio_device_mixer_sync_queue(MMAudioDeviceMixer *mixer, MMAudioCounter id, const MMSoundQueue *queue,
                                     MMI32 full_volume, const MMAudioSample *(*sample)(void *))
{
    SoundVoices input;
    MMAudioMixer *core;
    MMI32 count;
    int result;
    if (!sample || full_volume <= 0)
        return 0;
    core = mm_audio_device_mixer_enter(mixer);
    count = mm_sound_queue_count(queue);
    input.queue = queue;
    input.sample = sample;
    input.full_volume = full_volume;
    result = mm_audio_mixer_sync_read(core, id, count > queue->current ? (size_t)(count - queue->current) : 0,
                                     &input, read_sound_voice);
    mm_audio_device_mixer_leave(mixer);
    return result;
}
