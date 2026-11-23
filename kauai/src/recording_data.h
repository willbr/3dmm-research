/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_RECORDING_DATA_H
#define KAUAI_RECORDING_DATA_H
#include "mm/wave.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMRecordingBuffer
{
    unsigned char *data;
    size_t capacity, used;
    MMU32 rate;
    unsigned channels, bits, frame_bytes;
} MMRecordingBuffer;

/* Initialize a zeroed or existing capture. Failure preserves it. Callers
 * serialize capture and control access. Append is allocation-free. */
int mm_recording_init(MMRecordingBuffer *, MMU32 rate, unsigned channels, unsigned bits, MMU32 max_ms);
void mm_recording_dispose(MMRecordingBuffer *);
void mm_recording_reset(MMRecordingBuffer *);
size_t mm_recording_append(MMRecordingBuffer *, const void *, size_t bytes);
int mm_recording_full(const MMRecordingBuffer *);
size_t mm_recording_frames(const MMRecordingBuffer *);
/* Render a take without mutating its PCM. A failed call leaves the published
 * WAVE bytes and length intact. Output is an initialized wave buffer. */
int mm_recording_wave(const MMRecordingBuffer *, MMWaveBuffer *out, MMU32 *bytes);

#ifdef __cplusplus
}
#endif
#endif
