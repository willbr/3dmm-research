#include "mm/clock.h"
#include "mm/runtime_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "clock check failed at %d\n", __LINE__); exit(1); } } while (0)

enum { ALARM = 11, KEY, TRACK, MOVE, NON_MENU = 100, IDLE = 101 };

static const MMClockCommandIds ids = { ALARM, KEY, TRACK, MOVE, NON_MENU };

typedef struct Event
{
    MMI32 value;
    MMU32 time, current, delay;
    int direct;
} Event;

typedef struct Test
{
    MMAlarmClock *head, *clock;
    MMU32 stamp;
    int samples, sample_error, allocation_fails, allocations;
    int deliveries, reschedule, destroy;
    Event events[12];
} Test;

static int sample(void *p, MMU32 *stamp)
{
    Test *t = p;
    ++t->samples;
    *stamp = t->stamp;
    return t->sample_error ? t->sample_error : 1;
}

static int deliver(void *p, MMI32 id, const MMClockAlarm *alarm)
{
    Test *t = p;
    MMAlarmClock *clock = t->clock;
    Event *event;

    CHECK(id == clock->id && t->deliveries < (int)(sizeof(t->events) / sizeof(t->events[0])));
    event = &t->events[t->deliveries++];
    event->value = alarm->value;
    event->time = alarm->time;
    event->current = clock->current;
    event->delay = clock->alarm_delay;
    event->direct = alarm->target != NULL;
    if (t->reschedule && alarm->value == 102)
        CHECK(mm_alarm_clock_schedule(clock, 1, alarm->target, 104, 0, 0));
    if (t->destroy)
    {
        CHECK(mm_clock_unregister(&t->head, clock) && mm_alarm_clock_dispose(clock));
        t->clock = NULL;
        free(clock);
        return 0;
    }
    return 1;
}

static void initialize(Test *t, MMAlarmClock *clock, MMU32 flags, MMClockServices *services)
{
    memset(t, 0, sizeof(*t));
    t->clock = clock;
    t->stamp = 1000;
    services->context = t;
    services->now = sample;
    services->deliver = deliver;
    CHECK(mm_alarm_clock_init(clock, t, 42, flags, NULL, 1));
}

static void scalable(void)
{
    Test t;
    MMScaledClock clock;
    MMRuntimeCounter lhs, quotient;
    const MMRuntimeCounter frequency = { MM_CLOCK_NORMAL_SCALE, 0 };
    MMU32 random = 0x12436587UL;
    int i;

    memset(&t, 0, sizeof(t));
    mm_scaled_clock_init(&clock, 0xfffffff0UL);
    CHECK(mm_scaled_clock_time(&clock, 0x10) == 32);
    CHECK(mm_scaled_clock_scale(&clock, MM_CLOCK_NORMAL_SCALE, &t, sample) == 1 && !t.samples);
    t.stamp = 0x10;
    CHECK(mm_scaled_clock_scale(&clock, MM_CLOCK_NORMAL_SCALE / 2, &t, sample) == 1 && t.samples == 1);
    CHECK(mm_scaled_clock_time(&clock, 0x25) == 42);
    mm_scaled_clock_jump(&clock, 7);
    CHECK(mm_scaled_clock_time(&clock, 0x25) == 49);
    t.sample_error = -2;
    CHECK(mm_scaled_clock_scale(&clock, 0, &t, sample) == -2 && mm_scaled_clock_time(&clock, 0x25) == 49);
    t.sample_error = 0;
    t.stamp = 0x25;
    CHECK(mm_scaled_clock_scale(&clock, 0, &t, sample) == 1 && mm_scaled_clock_time(&clock, 0x12345678) == 49);

    for (i = 0; i < 4000; ++i)
    {
        random = random * 1664525UL + 1013904223UL;
        clock.scale = random;
        random = random * 1664525UL + 1013904223UL;
        lhs.low = random;
        lhs.high = 0;
        CHECK(mm_runtime_counter_scale(lhs, clock.scale, frequency, &quotient));
        CHECK(mm_scaled_clock_time(&clock, clock.system_base + random) == clock.application_base + quotient.low);
    }
}

static void dispatch(void)
{
    Test t;
    MMAlarmClock clock;
    MMClockServices services;
    int i;
    static const MMI32 order[] = {102, 101, 104, 103, 105};
    static const MMU32 times[] = {5, 5, 6, 8, 9};
    static const MMU32 lateness[] = {7, 7, 6, 6, 3};

    initialize(&t, &clock, MM_CLOCK_NO_SLIP, &services);
    CHECK(mm_alarm_clock_start(&clock, 0, &services) == 1);
    CHECK(mm_alarm_clock_schedule(&clock, 5, &services, 101, 0, 0));
    CHECK(mm_alarm_clock_schedule(&clock, 5, &services, 102, 0, 0));
    CHECK(mm_alarm_clock_schedule(&clock, 8, NULL, 103, 0, 0));
    CHECK(mm_alarm_clock_schedule(&clock, 9, &t, 105, 0, 0));
    t.reschedule = 1;
    t.stamp = 1200;
    CHECK(!mm_alarm_clock_dispatch(&clock, ALARM, &ids, &services) && t.samples == 1 && !clock.current);
    CHECK(!mm_alarm_clock_dispatch(&clock, IDLE, &ids, &services) && t.deliveries == 5 && clock.current == 12);
    for (i = 0; i < 5; ++i)
    {
        CHECK(t.events[i].value == order[i] && t.events[i].time == times[i] && t.events[i].current == times[i]);
        CHECK(t.events[i].delay == lateness[i] && t.events[i].direct == (i != 3));
    }
    CHECK(!clock.count && clock.next_time == (MMU32)-1 && clock.alarm_delay == 3);
    t.sample_error = -2;
    CHECK(mm_alarm_clock_dispatch(&clock, IDLE, &ids, &services) == -2 && !clock.alarm_delay && clock.current == 12);
    CHECK(mm_alarm_clock_dispose(&clock));
}

static void slip_and_reset(void)
{
    Test t;
    MMAlarmClock clock;
    MMClockServices services;

    initialize(&t, &clock, MM_CLOCK_RESET_ON_INPUT, &services);
    CHECK(mm_alarm_clock_start(&clock, 0, &services) == 1);
    CHECK(mm_alarm_clock_schedule(&clock, 0, &t, 1, 0, 0));
    CHECK(mm_alarm_clock_schedule(&clock, 4, &t, 2, 0, 0));
    t.stamp = 1200;
    CHECK(!mm_alarm_clock_dispatch(&clock, IDLE, &ids, &services) && clock.current == 1 && t.deliveries == 1);
    CHECK(clock.base == 1 && clock.stamp == 1200 && clock.next_time == 4 && !clock.alarm_delay);
    t.stamp = 1300;
    CHECK(!mm_alarm_clock_dispatch(&clock, MOVE, &ids, &services) && !clock.current && t.deliveries == 1);
    t.stamp = 1350;
    CHECK(mm_alarm_clock_time(&clock, 1, t.stamp) == 3 && mm_alarm_clock_time(&clock, 0, 0) == 0);
    CHECK(mm_alarm_clock_start(&clock, 3, &services) == 1 && clock.count == 1 && clock.next_time == 4);
    t.stamp = 1370;
    CHECK(!mm_alarm_clock_dispatch(&clock, IDLE, &ids, &services) && t.deliveries == 2 && clock.current == 4);
    t.stamp = 0xfffffff0UL;
    CHECK(mm_alarm_clock_start(&clock, 0, &services) == 1);
    CHECK(mm_alarm_clock_schedule(&clock, 2, &t, 3, 1, 0x10) && clock.next_time == 3);
    CHECK(mm_alarm_clock_dispose(&clock));
}

static void registry_and_destruction(void)
{
    Test t;
    MMAlarmClock *clock = malloc(sizeof(*clock)), other;
    MMClockServices services;

    CHECK(clock != NULL);
    initialize(&t, clock, 0, &services);
    CHECK(mm_alarm_clock_init(&other, &t, 42, 0, NULL, 0));
    mm_clock_register(&t.head, &other);
    mm_clock_register(&t.head, clock);
    CHECK(mm_clock_find(t.head, 42) == clock);
    CHECK(mm_alarm_clock_start(clock, 0, &services) == 1);
    CHECK(mm_alarm_clock_schedule(clock, 1, &other, 1, 0, 0));
    CHECK(mm_alarm_clock_schedule(clock, 2, &t, 2, 0, 0));
    CHECK(mm_alarm_clock_schedule(&other, 3, &other, 3, 0, 0));
    mm_clock_bury_target(t.head, &other);
    CHECK(!other.count && clock->count == 1 && clock->next_time == 1);
    t.stamp = 1017;
    CHECK(!mm_alarm_clock_dispatch(clock, IDLE, &ids, &services) && !t.deliveries && clock->next_time == 2);
    t.destroy = 1;
    t.stamp = 1034;
    CHECK(!mm_alarm_clock_dispatch(clock, IDLE, &ids, &services) && !t.clock && t.head == &other && t.deliveries == 1);
    CHECK(mm_clock_find(t.head, 42) == &other && mm_clock_unregister(&t.head, &other) && !t.head);
    CHECK(mm_alarm_clock_dispose(&other));
}

static void *allocate(void *p, void *memory, MMU32 old_bytes, MMU32 new_bytes, int priority)
{
    Test *t = p;
    (void)old_bytes;
    (void)priority;
    ++t->allocations;
    return t->allocation_fails ? NULL : realloc(memory, new_bytes);
}

static void release(void *p, void *memory, MMU32 bytes)
{
    (void)p;
    (void)bytes;
    free(memory);
}

static void failed_insert(void)
{
    Test t;
    MMAlarmClock clock;
    MMStorageAllocator allocator = {0};

    memset(&t, 0, sizeof(t));
    allocator.context = &t;
    allocator.resize = allocate;
    allocator.release = release;
    CHECK(mm_alarm_clock_init(&clock, &t, 42, 0, &allocator, 0));
    t.allocation_fails = 1;
    CHECK(!mm_alarm_clock_schedule(&clock, 1, &t, 0, 0, 0) && t.allocations && !clock.count &&
          clock.next_time == (MMU32)-1);
    t.allocation_fails = 0;
    CHECK(mm_alarm_clock_schedule(&clock, 1, &t, 0, 0, 0) && clock.count == 1 && clock.next_time == 1);
    CHECK(mm_alarm_clock_dispose(&clock));
}

int main(void)
{
    scalable();
    dispatch();
    slip_and_reset();
    registry_and_destruction();
    failed_insert();
    puts("Scalable application clocks, alarms and callback lifetime passed");
    return 0;
}
