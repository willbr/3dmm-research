/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_RECORDING_H
#define KAUAI_SDL_RECORDING_H
#include "mm/audio_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMSdlRecorder MMSdlRecorder;
typedef struct MMSdlRecordingStats
{
    MMAudioCounter opened, started, completed, previewed, captured_frames;
} MMSdlRecordingStats;

/* Main-thread controls. The slot starts null. Open replaces it on success,
 * preserving the previous take on failure. SDL serializes the capture callback
 * with the stream lock. Destroy stops capture and preview. */
int mm_sdl_recorder_open(MMSdlRecorder **, MMU32 rate, unsigned channels, unsigned bits, MMU32 max_ms);
void mm_sdl_recorder_destroy(MMSdlRecorder *);
int mm_sdl_recorder_start(MMSdlRecorder *);
int mm_sdl_recorder_stop(MMSdlRecorder *);
int mm_sdl_recorder_recording(MMSdlRecorder *);
int mm_sdl_recorder_play(MMSdlRecorder *, float volume);
void mm_sdl_recorder_suspend_preview(MMSdlRecorder *, int suspended);
int mm_sdl_recorder_playing(MMSdlRecorder *);
/* Borrow until a new take, successful reopen, or destruction. Null means no take. */
const unsigned char *mm_sdl_recorder_wave(const MMSdlRecorder *, MMU32 *bytes);
void mm_sdl_recorder_stats(MMSdlRecordingStats *);

#ifdef __cplusplus
}
#endif
#endif
