/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_MIDI_WORKER_H
#define KAUAI_MIDI_WORKER_H
#include "mm/types.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMMidiWorker MMMidiWorker;
typedef MMU32 (*MMMidiWork)(void *context);
/* The worker starts idle. Work returns the next wait in milliseconds, with
 * UINT32_MAX meaning indefinite. Callbacks must return normally. Signals
 * coalesce, including those sent during work. Stop/Destroy belong to the
 * owner thread. Stop joins any in-flight callback. Signal and Running may
 * race with Stop. Stop all callers before Destroy. */
MMMidiWorker *mm_midi_worker_start(MMMidiWork, void *context);
void mm_midi_worker_signal(MMMidiWorker *);
int mm_midi_worker_running(MMMidiWorker *);
void mm_midi_worker_stop(MMMidiWorker *);
void mm_midi_worker_destroy(MMMidiWorker *);
#ifdef __cplusplus
}
#endif
#endif
