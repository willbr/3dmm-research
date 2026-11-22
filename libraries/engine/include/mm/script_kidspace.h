/* Host bindings for the Kidspace script operations implemented in C. */
#ifndef MM_ENGINE_SCRIPT_KIDSPACE_H
#define MM_ENGINE_SCRIPT_KIDSPACE_H
#include "clock.h"
#include "command_dispatch.h"
#include "pixel_paint.h"
#include "sound_manager.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMKidspaceRelation
    {
        MM_KIDSPACE_PARENT,
        MM_KIDSPACE_NEXT_SIBLING,
        MM_KIDSPACE_PREVIOUS_SIBLING,
        MM_KIDSPACE_FIRST_CHILD
    };

    enum MMKidspaceRectMode
    {
        MM_KIDSPACE_PARENT_RECT,
        MM_KIDSPACE_LOCAL_RECT,
        MM_KIDSPACE_MOVABLE_RECT
    };

    enum MMKidspacePointMode
    {
        MM_KIDSPACE_PARENT_REGISTRATION,
        MM_KIDSPACE_LOCAL_REGISTRATION,
        MM_KIDSPACE_MOUSE
    };

    enum MMKidspaceScriptNotice
    {
        MM_KIDSPACE_MISSING_OBJECT,
        MM_KIDSPACE_ARGUMENT_ALLOCATION,
        MM_KIDSPACE_NESTED_FAILURE,
        MM_KIDSPACE_CLOCK_RETAINED,
        MM_KIDSPACE_CLOCK_MISSING,
        MM_KIDSPACE_ALARM_FAILED,
        MM_KIDSPACE_SOUND_FALLBACK
    };

    typedef struct MMKidspaceClockServices
    {
        MMAlarmClock *(*find)(void *, MMI32 id);
        int (*create)(void *, MMI32 id);
        void (*retain)(void *, MMAlarmClock *);
        void (*release)(void *, MMAlarmClock *);
        void (*start)(void *, MMAlarmClock *, MMU32 time);
        void (*stop)(void *, MMAlarmClock *);
        int (*alarm)(void *, MMAlarmClock *, void *target, MMI32 delay, MMI32 script);
    } MMKidspaceClockServices;

    typedef struct MMKidspaceSoundServices
    {
        int (*is_sound_object)(void *, void *object);
        /* The host resolves source. A null object uses the global mixer. */
        MMI32 (*play)(void *, void *object, const MMSoundPlay *);
        MMI32 (*play_mouse)(void *, void *object, MMU32 type, MMU32 number);
        /* Zero reports that no mixer is installed and leaves result untouched. */
        int (*control)(void *, int operation, MMI32 first, MMI32 second, MMI32 *result);
    } MMKidspaceSoundServices;

    typedef struct MMKidspaceScriptServices
    {
        /* Object identities are borrowed until the next host mutation. Host
         * lookup must resolve this_object to the original object identity. */
        void *(*find)(void *, MMI32 id);
        MMI32 (*id)(void *, void *object);
        void *(*relative)(void *, void *object, int relation);
        MMI32 (*unique_id)(void *);
        int (*rect)(void *, void *object, int mode, MMPixelRect *);
        void (*set_rect)(void *, void *object, const MMPixelRect *);
        int (*point)(void *, void *object, int mode, MMPixelPoint *);
        void *(*handler)(void *, MMI32 id);
        void (*enqueue)(void *, const MMQueuedCommand *);
        /* Return 1 on success, 0 for a failed script, or -1 for an object
         * that cannot run scripts. The object may die even on success.
         * Parameters are mutable native words valid for this call. */
        int (*run)(void *, void *object, MMU32 script, int by_number, void *parameters,
                   MMI32 count, MMI32 *result);
        void (*cell)(void *, void *object, MMU32 child, MMI32 dx, MMI32 dy, MMI32 delay);
        void (*notice)(void *, int reason, unsigned opcode, MMI32 id, MMU32 detail);
        const MMKidspaceClockServices *clocks;
        const MMKidspaceSoundServices *sound;
    } MMKidspaceScriptServices;

    struct MMScriptVM;
    /* Returns nonzero for a handled opcode, including execution failure. */
    int mm_kidspace_script_execute(struct MMScriptVM *, unsigned opcode);
#ifdef __cplusplus
}
#endif
#endif
