/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_AUDIO_MIXER_H
#define KAUAI_AUDIO_MIXER_H
#include "audio_sample.h"
#include "mm/sound_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMAudioDeviceMixer MMAudioDeviceMixer;

MMAudioDeviceMixer *mm_audio_device_mixer_create(void);
/* Stop render callbacks and other users before destroying the mixer. */
void mm_audio_device_mixer_destroy(MMAudioDeviceMixer *);
/* The lock is recursive. The returned core is borrowed until leave. */
MMAudioMixer *mm_audio_device_mixer_enter(MMAudioDeviceMixer *);
void mm_audio_device_mixer_leave(MMAudioDeviceMixer *);
/* The sound-queue host must use this mixer's lock for its entries and cursor.
 * The sample reader borrows PCM and leaves the queue and mixer untouched. */
int mm_audio_device_mixer_sync_queue(MMAudioDeviceMixer *, MMAudioCounter, const MMSoundQueue *,
                                     MMI32 full_volume, const MMAudioSample *(*sample)(void *));

#ifdef __cplusplus
}
#endif
#endif
