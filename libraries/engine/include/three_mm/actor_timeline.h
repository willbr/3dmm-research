/* Actor timeline and seek policy converted from engine actor_timeline.cpp and actor_seek.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_TIMELINE_H
#define THREE_MM_ENGINE_ACTOR_TIMELINE_H
#include "actor_frame.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_ACTOR_UNKNOWN_EVENT,
        THREE_MM_ACTOR_CORRUPT_ROUTE,
        THREE_MM_ACTOR_LIFETIME_LOGIC
    };
    typedef struct ThreeMMActorTimelineServices
    {
        ThreeMMActorMotionServices motion;
        void *context;
        int (*resolve)(void *, ThreeMMI32 action, ThreeMMI32 cel, ThreeMMI32 *unscaled_step);
        void (*put_event)(void *events, ThreeMMI32 index, const ThreeMMActorEvent *);
        void (*diagnostic)(void *, int fault);
        /* Optional native-view publication before a metadata/diagnostic callback.
         * Callback observers see intermediate step-back state, including the
         * decremented cel. Publication must not mutate/re-enter the C state. */
        void (*publish)(void *, const ThreeMMActorState *);
        void (*publish_frame)(void *, ThreeMMI32 last_attempted_frame);
    } ThreeMMActorTimelineServices;
    /* Resolver output starts at zero. Failure preserves any value written by the
     * resolver. Playback scaling is applied only after successful resolution.
     * Metadata callbacks cannot mutate authored tables or evaluated state. */
    int three_mm_actor_step(ThreeMMI32 action, ThreeMMI32 cel, ThreeMMI32 *, const ThreeMMActorTimelineServices *);
    int three_mm_actor_playback_step(const ThreeMMActorState *, ThreeMMI32 *, const ThreeMMActorTimelineServices *);
    /* Valid runtime tables required. Lifetime updates frame caches immediately;
     * failure leaves partial caches and the last attempted frame. The enabled
     * retail BUG1960/BUG1906 policies are retained. max_frames is a signed bound on
     * last-first, measured without 32-bit subtraction overflow. */
    int three_mm_actor_lifetime(void *route, void *events, ThreeMMI32 first, int rejoining, ThreeMMI32 *last,
                                ThreeMMI32 max_frames, const ThreeMMActorTimelineServices *);
    int three_mm_actor_stalled(void *events, ThreeMMI32 first, const ThreeMMActorLocation *, ThreeMMI32 *last_event,
                               const ThreeMMActorTimelineServices *);
    /* Step failure restores cel/frame/location and leaves output unchanged. A
     * successful probe restores these values too; probe output cannot alias state.
     * Quick backup requires target<frame with no intervening cached events; failure
     * retains completed earlier steps. Vertical transitions request full replay. */
    int three_mm_actor_step_back(ThreeMMActorState *, void *route, void *events, ThreeMMActorLocation *output,
                                 int update, const ThreeMMActorTimelineServices *);
    typedef struct ThreeMMActorQuickSeekResult
    {
        int success, valid, applyEffects;
    } ThreeMMActorQuickSeekResult;
    ThreeMMActorQuickSeekResult three_mm_actor_quick_backup(ThreeMMActorState *, void *route, void *events,
                                                            ThreeMMI32 last, ThreeMMI32 target,
                                                            const ThreeMMActorTimelineServices *);
    enum
    {
        THREE_MM_ACTOR_SEEK_HIDE,
        THREE_MM_ACTOR_SEEK_RESET_COSTUME,
        THREE_MM_ACTOR_SEEK_QUICK_EFFECTS,
        THREE_MM_ACTOR_SEEK_FRAME,
        THREE_MM_ACTOR_SEEK_FINISHED
    };
    typedef struct ThreeMMActorSeekCommand
    {
        int kind, displayFrame;
    } ThreeMMActorSeekCommand;
    typedef struct ThreeMMActorSeek
    {
        ThreeMMI32 firstFrame, lastFrame, targetFrame;
        int phase, midPath, waiting, success, pending;
    } ThreeMMActorSeek;
    /* Caller-owned control storage. Borrow valid state/tables; implementation fields
     * must not be modified. Host acknowledges every command and stops at Finished.
     * Frame caches/state may change between Next/Complete; table structure may not.
     * Target must be below INT32_MAX, which denotes an invalid current frame. */
    void three_mm_actor_seek_init(ThreeMMActorSeek *, ThreeMMI32 first, ThreeMMI32 last, ThreeMMI32 target);
    /* Protocol rejection returns zero without changing control/state/output. */
    int three_mm_actor_seek_next(ThreeMMActorSeek *, ThreeMMActorState *, void *route, void *events,
                                 ThreeMMActorSeekCommand *, const ThreeMMActorTimelineServices *);
    int three_mm_actor_seek_complete(ThreeMMActorSeek *, ThreeMMActorState *, int success);
#ifdef __cplusplus
}
#endif
#endif
