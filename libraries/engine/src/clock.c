#include "mm/clock.h"
#include "mm/collections.h"
#include <string.h>

void mm_scaled_clock_init(MMScaledClock *clock, MMU32 system_time)
{
    clock->system_base = system_time;
    clock->application_base = 0;
    clock->scale = MM_CLOCK_NORMAL_SCALE;
}

MMU32 mm_scaled_clock_time(const MMScaledClock *clock, MMU32 system_time)
{
    MMU32 elapsed = system_time - clock->system_base;
    MMU32 scale = clock->scale;
    if (scale != MM_CLOCK_NORMAL_SCALE)
    {
        MMU32 fraction = elapsed & 0xffffU;
        /* Take the middle 32 bits of the 16.16 product using 32-bit partials. */
        elapsed = (elapsed >> 16) * scale + fraction * (scale >> 16) +
                  ((fraction * (scale & 0xffffU)) >> 16);
    }
    return clock->application_base + elapsed;
}

int mm_scaled_clock_scale(MMScaledClock *clock, MMU32 scale, void *context, MMClockSample now)
{
    MMU32 stamp;
    int result;

    if (clock->scale == scale)
        return 1;
    result = now(context, &stamp);
    if (result <= 0)
        return result;
    clock->application_base = mm_scaled_clock_time(clock, stamp);
    clock->system_base = stamp;
    clock->scale = scale;
    return 1;
}

void mm_scaled_clock_jump(MMScaledClock *clock, MMU32 delta)
{
    clock->application_base += delta;
}

int mm_alarm_clock_init(MMAlarmClock *clock, void *owner, MMI32 id, MMU32 flags,
                        const MMStorageAllocator *allocator, int trash)
{
    MMAlarmClock fresh = {0};
    if (mm_storage_init(&fresh.alarms, allocator) != MM_STORAGE_OK)
        return 0;
    fresh.owner = owner;
    fresh.id = id;
    fresh.flags = flags;
    fresh.next_time = (MMU32)-1;
    fresh.alarms.min_grow[0] = 128;
    fresh.alarms.min_grow[1] = 16;
    fresh.trash = trash;
    *clock = fresh;
    return 1;
}

int mm_alarm_clock_dispose(MMAlarmClock *clock)
{
    if (mm_storage_dispose(&clock->alarms) != MM_STORAGE_OK)
        return 0;
    clock->count = 0;
    return 1;
}

void mm_clock_register(MMAlarmClock **head, MMAlarmClock *clock)
{
    clock->next = *head;
    *head = clock;
}

int mm_clock_unregister(MMAlarmClock **head, MMAlarmClock *clock)
{
    while (*head && *head != clock)
        head = &(*head)->next;
    if (!*head)
        return 0;
    *head = clock->next;
    clock->next = 0;
    return 1;
}

MMAlarmClock *mm_clock_find(MMAlarmClock *head, MMI32 id)
{
    while (head && head->id != id)
        head = head->next;
    return head;
}

static MMArray alarm_array(MMAlarmClock *clock)
{
    MMArray array = {0};
    array.storage = &clock->alarms;
    array.width = sizeof(MMClockAlarm);
    array.count = clock->count;
    array.free_count = -1;
    array.trash_unused = clock->trash;
    return array;
}

int mm_alarm_clock_at(const MMAlarmClock *clock, MMU32 index, MMClockAlarm *alarm)
{
    if (index >= clock->count)
        return 0;
    memcpy(alarm, clock->alarms.buffers[0].data + index * sizeof(*alarm), sizeof(*alarm));
    return 1;
}

static void remove_alarm(MMAlarmClock *clock, MMU32 index)
{
    MMArray array = alarm_array(clock);
    mm_array_delete(&array, index, 1);
    clock->count = array.count;
}

void mm_clock_remove_target(MMAlarmClock *clock, void *target)
{
    MMClockAlarm alarm;
    MMU32 i = clock->count;
    while (i)
    {
        --i;
        mm_alarm_clock_at(clock, i, &alarm);
        if (alarm.target == target)
            remove_alarm(clock, i);
    }
}

void mm_clock_bury_target(MMAlarmClock *head, void *target)
{
    for (; head; head = head->next)
        mm_clock_remove_target(head, target);
}

int mm_alarm_clock_start(MMAlarmClock *clock, MMU32 time, const MMClockServices *s)
{
    MMU32 stamp;
    int result;

    clock->base = clock->current = time;
    clock->alarm_delay = 0;
    result = s->now(s->context, &stamp);
    if (result > 0)
        clock->stamp = stamp;
    return result;
}

MMU32 mm_alarm_clock_time(const MMAlarmClock *clock, int adjusted, MMU32 timestamp)
{
    MMU32 elapsed;
    if (!adjusted)
        return clock->current;
    elapsed = timestamp - clock->stamp;
    return clock->base + (elapsed / MM_CLOCK_MILLISECONDS_PER_SECOND) * MM_CLOCK_TICKS_PER_SECOND +
           ((elapsed % MM_CLOCK_MILLISECONDS_PER_SECOND) * MM_CLOCK_TICKS_PER_SECOND) /
               MM_CLOCK_MILLISECONDS_PER_SECOND;
}

int mm_alarm_clock_schedule(MMAlarmClock *clock, MMI32 delay, void *target, MMI32 value,
                            int adjusted, MMU32 timestamp)
{
    MMClockAlarm alarm = {0}, existing;
    MMArray array = alarm_array(clock);
    MMU32 low = 0, high = clock->count, middle;

    alarm.target = target;
    alarm.time = mm_alarm_clock_time(clock, adjusted, timestamp) + (MMU32)(delay > 1 ? delay : 1);
    alarm.value = value;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        mm_alarm_clock_at(clock, middle, &existing);
        if (existing.time < alarm.time)
            high = middle;
        else
            low = middle + 1;
    }
    if (mm_array_insert(&array, low, &alarm, 1) != MM_STORAGE_OK)
        return 0;
    clock->count = array.count;
    if (clock->next_time > alarm.time)
        clock->next_time = alarm.time;
    return 1;
}

int mm_alarm_clock_dispatch(MMAlarmClock *clock, MMI32 command, const MMClockCommandIds *ids,
                           const MMClockServices *s)
{
    MMClockAlarm alarm;
    MMU32 stamp, time;
    int result;

    clock->alarm_delay = 0;
    if (command == ids->alarm)
        return 0;
    result = s->now(s->context, &stamp);
    if (result <= 0)
        return result;
    time = mm_alarm_clock_time(clock, 1, stamp);
    if ((clock->flags & MM_CLOCK_RESET_ON_INPUT) &&
        (command == ids->key || command == ids->track_mouse || command == ids->mouse_move ||
         command < ids->first_non_menu))
    {
        clock->stamp = stamp;
        clock->base = time = 0;
    }
    if (time >= clock->next_time)
    {
        for (;;)
        {
            if (!clock->count)
            {
                clock->next_time = (MMU32)-1;
                break;
            }
            mm_alarm_clock_at(clock, clock->count - 1, &alarm);
            if (alarm.time > time)
            {
                clock->next_time = alarm.time;
                break;
            }
            remove_alarm(clock, clock->count - 1);
            clock->current = alarm.time;
            clock->next_time = (MMU32)-1;
            if (time > alarm.time && !(clock->flags & MM_CLOCK_NO_SLIP))
            {
                clock->base = time = alarm.time;
                clock->stamp = stamp;
            }
            if (alarm.target)
                clock->alarm_delay = time - clock->current;
            result = s->deliver(s->context, clock->id, &alarm);
            if (result <= 0)
                return result;
        }
    }
    clock->current = time;
    return 0;
}
