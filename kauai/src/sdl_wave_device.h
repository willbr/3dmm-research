/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_WAVE_DEVICE_H
#define KAUAI_SDL_WAVE_DEVICE_H
#include "audio_mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMSdlWaveDevice MMSdlWaveDevice;
typedef struct MMSdlWaveQueue
{
    MMSdlWaveDevice *device;
    MMSoundQueue *entries;
    const MMAudioSample *(*sample)(void *);
    MMAudioCounter id;
} MMSdlWaveQueue;
typedef struct MMSdlWaveStats
{
    int active;
    MMAudioCounter decoded, started, completed;
} MMSdlWaveStats;

MMSdlWaveDevice *mm_sdl_wave_device_create(MMI32 full_volume);
/* Open on the control thread and publish the device for diagnostics. */
int mm_sdl_wave_device_open(MMSdlWaveDevice *);
/* Each live queue keeps the device alive after its creator releases it. */
void mm_sdl_wave_device_release(MMSdlWaveDevice *);
void mm_sdl_wave_device_set_volume(MMSdlWaveDevice *, MMI32);
MMI32 mm_sdl_wave_device_volume(MMSdlWaveDevice *);
void mm_sdl_wave_device_suspend(MMSdlWaveDevice *, int);
void mm_sdl_wave_device_begin(MMSdlWaveDevice *);
void mm_sdl_wave_device_end(MMSdlWaveDevice *);
void mm_sdl_wave_note_decode(void);
void mm_sdl_wave_stats(MMSdlWaveStats *);

/* Bind a fresh queue before exposing its entries to callers. The entries
 * and sample reader are borrowed until dispose. The mixer lock protects
 * the entries, cursor and projection and is recursive. */
int mm_sdl_wave_queue_init(MMSdlWaveQueue *, MMSdlWaveDevice *, MMSoundQueue *, const MMAudioSample *(*)(void *));
void mm_sdl_wave_queue_dispose(MMSdlWaveQueue *);
void mm_sdl_wave_queue_enter(MMSdlWaveQueue *);
void mm_sdl_wave_queue_leave(MMSdlWaveQueue *);
int mm_sdl_wave_queue_sync(const MMSdlWaveQueue *);
void mm_sdl_wave_queue_flush(MMSdlWaveQueue *, const MMSoundQueueServices *);

#ifdef __cplusplus
}
#endif
#endif
