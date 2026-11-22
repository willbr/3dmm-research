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
        MM_KIDSPACE_SOUND_FALLBACK,
        MM_KIDSPACE_DESTROY_WORLD,
        MM_KIDSPACE_INVALID_STATE,
        MM_KIDSPACE_FILTER_FAILED,
        MM_KIDSPACE_MISSING_EDIT,
        MM_KIDSPACE_GET_PROPERTY_FAILED,
        MM_KIDSPACE_SET_PROPERTY_FAILED,
        MM_KIDSPACE_LAUNCH_STRING_MISSING
    };

    enum
    {
        MM_KIDSPACE_REP_KILL_ANIMATION = 1,
        MM_KIDSPACE_REP_SKIP_ANIMATION = 2,
        MM_KIDSPACE_STATE_LIMIT = 0x7fff,
        MM_KIDSPACE_TEXT_LIMIT = 255
    };

#define MM_KIDSPACE_TAG_ANIMATION 0x474c5343UL /* GLSC */
#define MM_KIDSPACE_TAG_BITMAP    0x4d424d50UL /* MBMP */

    typedef struct MMKidspaceObjectServices
    {
        void *(*world)(void *);
        void *(*create_child)(void *, void *parent, MMI32 id, MMU32 descriptor);
        void *(*create_help)(void *, void *parent, MMU32 topic);
        void (*release)(void *, void *object);
        void (*change_state)(void *, void *object, MMI32 state);
        MMI32 (*state)(void *, void *object);
        MMI32 (*z_plane)(void *, void *object);
        void (*set_z_plane)(void *, void *object, MMI32 plane);
        void (*play)(void *, void *object);
        int (*playing)(void *, void *object);
        void (*stop)(void *, void *object);
        MMI32 (*current_frame)(void *, void *object);
        MMI32 (*frame_count)(void *, void *object);
        void (*go_to_frame)(void *, void *object, MMI32 frame);
        int (*filter)(void *, void *object, MMI32 command, MMI32 id, MMU32 script);
        void (*set_no_slip)(void *, void *object, int enabled);
        void (*set_tooltip_source)(void *, void *object, MMI32 id);
        void (*stream)(void *, void *object, int enabled);
    } MMKidspaceObjectServices;

    enum MMKidspaceMessageDestination
    {
        MM_KIDSPACE_ALERT,
        MM_KIDSPACE_PRINT,
        MM_KIDSPACE_STATISTICS
    };

    typedef struct MMKidspacePalette
    {
        const void *rows; /* Borrowed native four-byte colour records. */
        MMU32 count;
    } MMKidspacePalette;

    struct MMScriptCollection;

    typedef struct MMKidspaceAppServices
    {
        /* The VM holds the resource reference until after the palette call. */
        struct MMScriptCollection *(*load_palette)(void *, MMU32 number);
        void (*set_palette)(void *, const MMKidspacePalette *);
        void (*transition)(void *, MMI32 kind, MMI32 parameter, MMU32 milliseconds,
                           MMU32 color, const MMKidspacePalette *);
        MMU32 (*modifiers)(void *);
        void (*change_modifiers)(void *, MMU32 set, MMU32 toggle);
        int (*is_edit)(void *, void *object);
        MMU32 (*read_edit)(void *, void *object, void *text, MMU32 capacity);
        void (*write_edit)(void *, void *object, const void *text, MMU32 count);
        int (*get_property)(void *, MMI32 id, MMI32 *value);
        int (*set_property)(void *, MMI32 id, MMI32 value);
        int (*launch)(void *, const void *text, MMU32 count);
        void (*begin_work)(void *);
        void (*end_work)(void *, int all);
        int (*modal_help)(void *, MMU32 topic, MMI32 *result);
        void (*flush_events)(void *, MMU32 mask);
        void (*message)(void *, int destination, const void *text, MMU32 count);
        int statistics_enabled;
    } MMKidspaceAppServices;

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
        /* May run a script that destroys object. */
        void (*set_rep)(void *, void *object, MMU32 child, unsigned flags, MMU32 tag,
                        MMI32 dx, MMI32 dy, MMU32 delay);
        void (*notice)(void *, int reason, unsigned opcode, MMI32 id, MMU32 detail);
        const MMKidspaceClockServices *clocks;
        const MMKidspaceSoundServices *sound;
        int (*is_kidspace_object)(void *, void *object);
        const MMKidspaceObjectServices *objects;
        void (*forget_current)(void *);
        const MMKidspaceAppServices *app;
    } MMKidspaceScriptServices;

    struct MMScriptVM;
    /* Returns nonzero for a handled opcode, including execution failure. */
    int mm_kidspace_script_execute(struct MMScriptVM *, unsigned opcode);
#ifdef __cplusplus
}
#endif
#endif
