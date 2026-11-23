/* Scheduled MIDI playback converted from Kauai midi_stream_win.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_MIDI_SCHEDULE_H
#define MM_MIDI_SCHEDULE_H
#include "collections.h"
#include "midi_render.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MM_MIDI_SCHEDULE_IDLE 0xffffffffUL

typedef struct MMMidiScheduleServices
{
    void *context;
    MMU32 (*clock)(void *);
    void (*message)(void *, MMU32);
    void (*reset)(void *);
    void (*enter)(void *), (*leave)(void *), (*wake)(void *);
    void (*complete)(void *, void *data, size_t tag);
} MMMidiScheduleServices;

typedef struct MMMidiSchedule
{
    MMCollectionStorage storage;
    MMArray buffers;
    MMMidiScheduleServices services;
    MMU32 cursor, deadline;
    int changed, stopping;
} MMMidiSchedule;

/* Initialize in place and dispose once worker and callers have stopped.
 * Hold the device lock exactly once for activate, queue, stop and step.
 * Step drops it around completion callbacks. Queued events are borrowed
 * until their callback. Activate and dispose with an empty queue. */
int mm_midi_schedule_init(MMMidiSchedule *, const MMStorageAllocator *, const MMMidiScheduleServices *, int trash);
void mm_midi_schedule_dispose(MMMidiSchedule *);
void mm_midi_schedule_activate(MMMidiSchedule *);
int mm_midi_schedule_queue(MMMidiSchedule *, MMMidiStreamEvent *, MMI32 bytes, MMI32 offset, MMI32 repeats, size_t tag);
void mm_midi_schedule_stop(MMMidiSchedule *);
/* Signaled means the host consumed the wake latch. Returns the next wait in
 * milliseconds. A zero wait steps again with signaled clear. */
MMU32 mm_midi_schedule_step(MMMidiSchedule *, int signaled);

#ifdef __cplusplus
}
#endif
#endif
