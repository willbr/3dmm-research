#ifndef MM_ENGINE_CLOCK_H
#define MM_ENGINE_CLOCK_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MM_CLOCK_NORMAL_SCALE 0x10000UL
enum { MM_CLOCK_MILLISECONDS_PER_SECOND = 1000, MM_CLOCK_TICKS_PER_SECOND = 60 };
enum { MM_CLOCK_RESET_ON_INPUT = 1, MM_CLOCK_NO_SLIP = 2 };

typedef int (*MMClockSample)(void *, MMU32 *milliseconds);
typedef struct MMScaledClock
{
    MMU32 system_base, application_base, scale;
} MMScaledClock;

void mm_scaled_clock_init(MMScaledClock *, MMU32 system_time);
MMU32 mm_scaled_clock_time(const MMScaledClock *, MMU32 system_time);
int mm_scaled_clock_scale(MMScaledClock *, MMU32 scale, void *, MMClockSample);
void mm_scaled_clock_jump(MMScaledClock *, MMU32 delta);

typedef struct MMClockAlarm
{
    void *target;
    MMU32 time;
    MMI32 value;
} MMClockAlarm;

typedef struct MMAlarmClock
{
    struct MMAlarmClock *next;
    void *owner;
    MMI32 id;
    MMU32 flags, stamp, base, current, alarm_delay, next_time;
    MMCollectionStorage alarms;
    MMU32 count;
    int trash;
} MMAlarmClock;

typedef struct MMClockCommandIds
{
    MMI32 alarm, key, track_mouse, mouse_move, first_non_menu;
} MMClockCommandIds;

typedef struct MMClockServices
{
    void *context;
    MMClockSample now;
    int (*deliver)(void *, MMI32 id, const MMClockAlarm *);
} MMClockServices;

/* Clocks own their alarm storage. The registry and alarm targets are
 * borrowed and must be unlinked or removed before their lifetimes end.
 * Start rebases time and preserves pending alarms. A time query can use the
 * dispatch snapshot or an explicit timestamp. Tied alarms fire newest first.
 * A synchronous delivery may change the pending alarms or destroy the clock.
 * It returns 1 if the clock still exists, 0 if destroyed, or a negative host
 * error. Dispatch immediately returns on destruction or error. The delivery
 * receives a copied alarm valid for the duration of the callback. */
int mm_alarm_clock_init(MMAlarmClock *, void *owner, MMI32 id, MMU32 flags,
                        const MMStorageAllocator *, int trash);
int mm_alarm_clock_dispose(MMAlarmClock *);
void mm_clock_register(MMAlarmClock **head, MMAlarmClock *);
int mm_clock_unregister(MMAlarmClock **head, MMAlarmClock *);
MMAlarmClock *mm_clock_find(MMAlarmClock *head, MMI32 id);
void mm_clock_remove_target(MMAlarmClock *, void *target);
void mm_clock_bury_target(MMAlarmClock *head, void *target);
int mm_alarm_clock_start(MMAlarmClock *, MMU32 time, const MMClockServices *);
MMU32 mm_alarm_clock_time(const MMAlarmClock *, int adjusted, MMU32 timestamp);
int mm_alarm_clock_schedule(MMAlarmClock *, MMI32 delay, void *target, MMI32 value,
                            int adjusted, MMU32 timestamp);
int mm_alarm_clock_at(const MMAlarmClock *, MMU32 index, MMClockAlarm *);
/* Returns zero for normal dispatch or destruction, or a negative host error. */
int mm_alarm_clock_dispatch(MMAlarmClock *, MMI32 command, const MMClockCommandIds *, const MMClockServices *);

#ifdef __cplusplus
}
#endif
#endif
