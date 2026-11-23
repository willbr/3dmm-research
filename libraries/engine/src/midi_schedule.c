/* Scheduled MIDI playback converted from Kauai midi_stream_win.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/midi_schedule.h"
#include "mm/clock.h"
#include "mm/wide.h"
#include <string.h>

typedef struct ScheduledBuffer
{
    MMMidiStreamEvent *events;
    MMU32 count, first;
    MMI32 repeats;
    size_t tag;
} ScheduledBuffer;

int mm_midi_schedule_init(MMMidiSchedule *schedule, const MMStorageAllocator *allocator,
                         const MMMidiScheduleServices *services, int trash)
{
    memset(schedule, 0, sizeof(*schedule));
    if (mm_storage_init(&schedule->storage, allocator) != MM_STORAGE_OK)
        return 0;
    schedule->storage.min_grow[0] = sizeof(ScheduledBuffer);
    schedule->buffers.storage = &schedule->storage;
    schedule->buffers.width = sizeof(ScheduledBuffer);
    schedule->buffers.trash_unused = trash;
    schedule->services = *services;
    return 1;
}

void mm_midi_schedule_dispose(MMMidiSchedule *schedule)
{
    if (schedule->buffers.storage)
        mm_storage_dispose(&schedule->storage);
    memset(schedule, 0, sizeof(*schedule));
}

void mm_midi_schedule_activate(MMMidiSchedule *schedule)
{
    schedule->changed = schedule->stopping = 0;
}

static void notify_worker(MMMidiSchedule *schedule)
{
    schedule->changed = 1;
    schedule->services.wake(schedule->services.context);
}

int mm_midi_schedule_queue(MMMidiSchedule *schedule, MMMidiStreamEvent *events, MMI32 bytes, MMI32 offset,
                          MMI32 repeats, size_t tag)
{
    ScheduledBuffer buffer;
    if (!events || bytes <= 0 || offset < 0 || offset >= bytes || bytes % (MMI32)sizeof(*events) ||
        offset % (MMI32)sizeof(*events))
        return 0;
    buffer.events = events;
    buffer.count = (MMU32)bytes / sizeof(*events);
    buffer.first = (MMU32)offset / sizeof(*events);
    buffer.repeats = repeats;
    buffer.tag = tag;
    if (mm_array_insert(&schedule->buffers, schedule->buffers.count, &buffer, 1) != MM_STORAGE_OK)
        return 0;
    if (schedule->buffers.count == 1)
        notify_worker(schedule);
    return 1;
}

void mm_midi_schedule_stop(MMMidiSchedule *schedule)
{
    schedule->stopping = 1;
    notify_worker(schedule);
}

static ScheduledBuffer *front(MMMidiSchedule *schedule)
{
    return (ScheduledBuffer *)schedule->storage.buffers[0].data;
}

static void return_buffers(MMMidiSchedule *schedule, MMU32 count)
{
    const MMMidiScheduleServices *services = &schedule->services;
    ScheduledBuffer buffer;
    if (count == schedule->buffers.count)
        services->reset(services->context);
    while (count)
    {
        buffer = *front(schedule);
        mm_array_delete(&schedule->buffers, 0, 1);
        --count;
        services->leave(services->context);
        services->complete(services->context, buffer.events, buffer.tag);
        services->enter(services->context);
    }
}

static MMU32 next_wait(MMI32 delay)
{
    /* Preserve the fallback's signed clock and its original infinite sentinel. */
    if (delay == 0x7fffffffL)
        return MM_MIDI_SCHEDULE_IDLE;
    return delay > 0 ? (MMU32)delay : 0;
}

MMU32 mm_midi_schedule_step(MMMidiSchedule *schedule, int signaled)
{
    const MMMidiScheduleServices *services = &schedule->services;
    ScheduledBuffer *buffer;
    MMU32 word, now, ticks;
    MMI32 delay;
    /* The host's timed wait may expire just before a producer signals. */
    if (schedule->changed && !signaled)
        return MM_MIDI_SCHEDULE_IDLE;
    schedule->changed = 0;
    if (!signaled && schedule->buffers.count)
    {
        buffer = front(schedule);
        if (schedule->cursor < buffer->count)
        {
            word = buffer->events[schedule->cursor++].eventWord;
            if (!(word >> 24))
                services->message(services->context, word & 0xffffffUL);
            if (schedule->cursor == buffer->count)
                return 0;
            now = services->clock(services->context);
            schedule->deadline += buffer->events[schedule->cursor].deltaTicks;
            delay = mm_signed_word(schedule->deadline - now);
            if (delay < -(MM_CLOCK_MILLISECONDS_PER_SECOND / 30))
            {
                schedule->deadline = now;
                return 0;
            }
            return next_wait(delay);
        }
        if (buffer->repeats == 1)
            return_buffers(schedule, 1);
        else
        {
            if (buffer->repeats > 0)
                --buffer->repeats;
            buffer->first = 0;
        }
    }
    else if (signaled && schedule->stopping)
    {
        schedule->stopping = 0;
        return_buffers(schedule, schedule->buffers.count);
    }
    if (!schedule->buffers.count)
        return MM_MIDI_SCHEDULE_IDLE;
    buffer = front(schedule);
    schedule->cursor = buffer->first;
    ticks = buffer->events[buffer->first].deltaTicks;
    schedule->deadline = services->clock(services->context) + ticks;
    return next_wait(mm_signed_word(ticks));
}
