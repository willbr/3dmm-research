/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_wave_device.h"
#include "sdl_audio_output.h"
#include "mm/reference.h"
#include <SDL3/SDL_atomic.h>
#include <stdlib.h>
#include <string.h>

struct MMSdlWaveDevice
{
    MMReference references;
    MMSdlAudioOutput *output;
    MMAudioDeviceMixer *mixer;
    MMI32 volume, full_volume;
};

/* The lock protects publication and reference changes. Never acquire the
 * mixer or SDL stream lock while holding it. */
static SDL_SpinLock registry;
static MMSdlWaveDevice *current;
static MMAudioCounter decoded;

MMSdlWaveDevice *mm_sdl_wave_device_create(MMI32 full_volume)
{
    MMSdlWaveDevice *device;
    if (full_volume <= 0)
        return NULL;
    device = malloc(sizeof(*device));
    if (!device)
        return NULL;
    device->output = mm_sdl_audio_output_create_mixer();
    if (!device->output)
    {
        free(device);
        return NULL;
    }
    device->mixer = mm_sdl_audio_output_mixer(device->output);
    device->volume = device->full_volume = full_volume;
    mm_reference_init(&device->references);
    return device;
}

int mm_sdl_wave_device_open(MMSdlWaveDevice *device)
{
    if (!mm_sdl_audio_output_open(device->output, NULL, NULL))
        return 0;
    SDL_LockSpinlock(&registry);
    current = device;
    SDL_UnlockSpinlock(&registry);
    return 1;
}

void mm_sdl_wave_device_release(MMSdlWaveDevice *device)
{
    int destroy;
    if (!device)
        return;
    SDL_LockSpinlock(&registry);
    destroy = mm_reference_drop(&device->references) == 0;
    if (destroy && current == device)
        current = NULL;
    SDL_UnlockSpinlock(&registry);
    if (destroy)
    {
        mm_sdl_audio_output_destroy(device->output);
        free(device);
    }
}

void mm_sdl_wave_device_set_volume(MMSdlWaveDevice *device, MMI32 volume)
{
    MMAudioMixer *mixer = mm_audio_device_mixer_enter(device->mixer);
    device->volume = volume < 0 ? 0 : volume;
    mm_audio_mixer_volume(mixer, device->volume / (float)device->full_volume);
    mm_audio_device_mixer_leave(device->mixer);
}

MMI32 mm_sdl_wave_device_volume(MMSdlWaveDevice *device)
{
    MMI32 volume;
    mm_audio_device_mixer_enter(device->mixer);
    volume = device->volume;
    mm_audio_device_mixer_leave(device->mixer);
    return volume;
}

void mm_sdl_wave_device_suspend(MMSdlWaveDevice *device, int suspend)
{
    mm_audio_mixer_suspend(mm_audio_device_mixer_enter(device->mixer), suspend);
    mm_audio_device_mixer_leave(device->mixer);
}

void mm_sdl_wave_device_begin(MMSdlWaveDevice *device)
{
    mm_audio_mixer_begin(mm_audio_device_mixer_enter(device->mixer));
    mm_audio_device_mixer_leave(device->mixer);
}

void mm_sdl_wave_device_end(MMSdlWaveDevice *device)
{
    mm_audio_mixer_end(mm_audio_device_mixer_enter(device->mixer));
    mm_audio_device_mixer_leave(device->mixer);
}

void mm_sdl_wave_note_decode(void)
{
    SDL_LockSpinlock(&registry);
    decoded = mm_audio_count_add(decoded, mm_audio_counter(1, 0));
    SDL_UnlockSpinlock(&registry);
}

void mm_sdl_wave_stats(MMSdlWaveStats *out)
{
    MMSdlWaveDevice *device;
    const MMAudioMixer *mixer;
    memset(out, 0, sizeof(*out));
    SDL_LockSpinlock(&registry);
    device = current;
    if (device)
    {
        mm_reference_add(&device->references);
        out->decoded = decoded;
    }
    SDL_UnlockSpinlock(&registry);
    if (!device)
        return;
    mixer = mm_audio_device_mixer_enter(device->mixer);
    out->active = 1;
    out->started = mixer->started;
    out->completed = mixer->completed;
    mm_audio_device_mixer_leave(device->mixer);
    mm_sdl_wave_device_release(device);
}

int mm_sdl_wave_queue_init(MMSdlWaveQueue *queue, MMSdlWaveDevice *device, MMSoundQueue *entries,
                           const MMAudioSample *(*sample)(void *))
{
    MMAudioCounter id = mm_audio_mixer_add(mm_audio_device_mixer_enter(device->mixer));
    mm_audio_device_mixer_leave(device->mixer);
    if (!id.low && !id.high)
        return 0;
    SDL_LockSpinlock(&registry);
    mm_reference_add(&device->references);
    SDL_UnlockSpinlock(&registry);
    queue->device = device;
    queue->entries = entries;
    queue->sample = sample;
    queue->id = id;
    return 1;
}

void mm_sdl_wave_queue_dispose(MMSdlWaveQueue *queue)
{
    MMSdlWaveDevice *device = queue->device;
    if (device)
    {
        mm_audio_mixer_remove(mm_audio_device_mixer_enter(device->mixer), queue->id);
        mm_audio_device_mixer_leave(device->mixer);
        memset(queue, 0, sizeof(*queue));
        mm_sdl_wave_device_release(device);
    }
}

void mm_sdl_wave_queue_enter(MMSdlWaveQueue *queue)
{
    const MMAudioMixer *mixer = mm_audio_device_mixer_enter(queue->device->mixer);
    MMSoundQueue *entries = queue->entries;
    while (entries->current < mm_sound_queue_count(entries))
    {
        MMSoundEntry entry = mm_sound_queue_get(entries, entries->current);
        if (!mm_audio_mixer_done(mixer, queue->id, entry.id))
            break;
        ++entries->current;
    }
}

void mm_sdl_wave_queue_leave(MMSdlWaveQueue *queue)
{
    mm_audio_device_mixer_leave(queue->device->mixer);
}

int mm_sdl_wave_queue_sync(const MMSdlWaveQueue *queue)
{
    return mm_audio_device_mixer_sync_queue(queue->device->mixer, queue->id, queue->entries,
                                            queue->device->full_volume, queue->sample);
}

void mm_sdl_wave_queue_flush(MMSdlWaveQueue *queue, const MMSoundQueueServices *services)
{
    mm_audio_device_mixer_enter(queue->device->mixer);
    mm_sound_queue_flush(services);
    mm_sdl_wave_queue_sync(queue);
    mm_audio_device_mixer_leave(queue->device->mixer);
}
