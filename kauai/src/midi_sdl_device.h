/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_SDL_DEVICE_H
#define KAUAI_MIDI_SDL_DEVICE_H
#include "mm/audio_mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMSdlMidiDevice MMSdlMidiDevice;
typedef void (*MMSdlMidiComplete)(void *context, void *buffer, size_t tag);
typedef struct MMSdlMidiStats
{
    int active;
    MMAudioCounter queued, completed, messages, notes, frames, audible_frames;
} MMSdlMidiStats;

enum { MM_SDL_MIDI_FULL_VOLUME = 0x10000 };

/* A null or empty path selects KAUAI_SOUNDFONT or the installed bank. Create,
 * activate and destroy on the host thread. The callback context is borrowed
 * through destroy. Buffers are borrowed until returned on the worker. */
MMSdlMidiDevice *mm_sdl_midi_create(MMSdlMidiComplete, void *context, const char *bank_path);
/* Return queued buffers and stop control callers before destroying the device. */
void mm_sdl_midi_destroy(MMSdlMidiDevice *);
int mm_sdl_midi_activate(MMSdlMidiDevice *, int active);
int mm_sdl_midi_active(MMSdlMidiDevice *);
void mm_sdl_midi_set_volume(MMSdlMidiDevice *, MMI32 volume);
MMI32 mm_sdl_midi_volume(MMSdlMidiDevice *);
int mm_sdl_midi_queue(MMSdlMidiDevice *, void *data, MMI32 bytes, MMI32 offset, MMI32 repeats, size_t tag);
void mm_sdl_midi_stop(MMSdlMidiDevice *);
void mm_sdl_midi_stats(MMSdlMidiDevice *, MMSdlMidiStats *);

#ifdef __cplusplus
}
#endif
#endif
