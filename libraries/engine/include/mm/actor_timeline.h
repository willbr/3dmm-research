/* Actor timeline and seek policy converted from engine actor_timeline.cpp and actor_seek.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_TIMELINE_H
#define MM_ENGINE_ACTOR_TIMELINE_H
#include "actor_frame.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_ACTOR_UNKNOWN_EVENT,
        MM_ACTOR_CORRUPT_ROUTE,
        MM_ACTOR_LIFETIME_LOGIC
    };
    typedef struct MMActorTimelineServices
    {
        MMActorMotionServices motion;
        void *context;
        int (*resolve)(void *, MMI32 action, MMI32 cel, MMI32 *unscaled_step);
        void (*put_event)(void *events, MMI32 index, const MMActorEvent *);
        void (*diagnostic)(void *, int fault);
        /* Optional native-view publication before a metadata/diagnostic callback.
         * Callback observers see intermediate step-back state, including the
         * decremented cel. Publication must not mutate/re-enter the C state. */
        void (*publish)(void *, const MMActorState *);
        void (*publish_frame)(void *, MMI32 last_attempted_frame);
    } MMActorTimelineServices;
    /* Resolver output starts at zero. Failure preserves any value written by the
     * resolver. Playback scaling is applied only after successful resolution.
     * Metadata callbacks cannot mutate authored tables or evaluated state. */
    int mm_actor_step(MMI32 action, MMI32 cel, MMI32 *, const MMActorTimelineServices *);
    int mm_actor_playback_step(const MMActorState *, MMI32 *, const MMActorTimelineServices *);
    /* Valid runtime tables required. Lifetime updates frame caches immediately;
     * failure leaves partial caches and the last attempted frame. The enabled
     * retail BUG1960/BUG1906 policies are retained. max_frames is a signed bound on
     * last-first, measured without 32-bit subtraction overflow. */
    int mm_actor_lifetime(void *route, void *events, MMI32 first, int rejoining, MMI32 *last,
                                MMI32 max_frames, const MMActorTimelineServices *);
    int mm_actor_stalled(void *events, MMI32 first, const MMActorLocation *, MMI32 *last_event,
                               const MMActorTimelineServices *);
    /* Step failure restores cel/frame/location and leaves output unchanged. A
     * successful probe restores these values too; probe output cannot alias state.
     * Quick backup requires target<frame with no intervening cached events; failure
     * retains completed earlier steps. Vertical transitions request full replay. */
    int mm_actor_step_back(MMActorState *, void *route, void *events, MMActorLocation *output,
                                 int update, const MMActorTimelineServices *);
    typedef struct MMActorQuickSeekResult
    {
        int success, valid, applyEffects;
    } MMActorQuickSeekResult;
    MMActorQuickSeekResult mm_actor_quick_backup(MMActorState *, void *route, void *events,
                                                            MMI32 last, MMI32 target,
                                                            const MMActorTimelineServices *);
    enum
    {
        MM_ACTOR_SEEK_HIDE,
        MM_ACTOR_SEEK_RESET_COSTUME,
        MM_ACTOR_SEEK_QUICK_EFFECTS,
        MM_ACTOR_SEEK_FRAME,
        MM_ACTOR_SEEK_FINISHED
    };
    typedef struct MMActorSeekCommand
    {
        int kind, displayFrame;
    } MMActorSeekCommand;
    typedef struct MMActorSeek
    {
        MMI32 firstFrame, lastFrame, targetFrame;
        int phase, midPath, waiting, success, pending;
    } MMActorSeek;
    /* Caller-owned control storage. Borrow valid state/tables; implementation fields
     * must not be modified. Host acknowledges every command and stops at Finished.
     * Frame caches/state may change between Next/Complete; table structure may not.
     * Target must be below INT32_MAX, which denotes an invalid current frame. */
    void mm_actor_seek_init(MMActorSeek *, MMI32 first, MMI32 last, MMI32 target);
    /* Protocol rejection returns zero without changing control/state/output. */
    int mm_actor_seek_next(MMActorSeek *, MMActorState *, void *route, void *events,
                                 MMActorSeekCommand *, const MMActorTimelineServices *);
    int mm_actor_seek_complete(MMActorSeek *, MMActorState *, int success);
    typedef struct MMActorSeekEffects
    {
        void *context;
        int (*apply)(void *, const MMActorSeekCommand *, MMActorState *);
        int (*fault)(void *);
    } MMActorSeekEffects;
    /* Run the seek protocol over the live state. apply returns success (1),
     * ordinary failure (0), or a negative terminal error. Hide and costume
     * reset are best-effort on ordinary failure; frame failures are accumulated
     * while later frames still run. An optional sticky fault is checked after
     * each state transition, including one that finishes without an effect.
     * Effects are synchronous and may update state and event frame caches.
     * They must not reenter the seek or change the table structure. */
    int mm_actor_seek_run(MMActorState *, void *route, void *events, MMI32 first, MMI32 last, MMI32 target,
                          const MMActorTimelineServices *, const MMActorSeekEffects *);

    typedef struct MMActorPlaybackEffects
    {
        void *context;
        void (*hide)(void *);
        int (*reset_costume)(void *);
        /* Optional frame settings and preparation before resolving the step. */
        void (*prepare)(void *, MMActorFrameInputs *);
        /* Optional lifetime refresh before testing an entrance's cached frame. */
        int (*prepare_entrance)(void *, MMI32 index);
        int (*apply_event)(void *, MMI32 index);
        /* Optional current lifetime end, queried after applying a frame's events. */
        MMI32 (*last_frame)(void *);
        int (*suppress_sounds)(void *);
        int (*motion_sounds)(void *);
        void (*position_body)(void *);
        int (*select_cel)(void *);
        void (*update_pose)(void *);
        int (*fault)(void *);
    } MMActorPlaybackEffects;
    /* Seek and display the live actor using the given frame profile. Studio
     * accumulates position dirtiness across frames and considers motion sound
     * during quick backup. Preview evaluates each frame with a dirty pose and
     * propagates callback errors. Optional sound is set if a cue is encountered. */
    int mm_actor_playback_seek(MMActorState *, void *route, void *events, MMI32 first, MMI32 last, MMI32 target,
                              int profile, int *sound, const MMActorTimelineServices *, const MMActorPlaybackEffects *);

    /* Replay this on-stage actor's sound events in frame order, then motion sound.
     * Return a nonpositive effect result immediately if playback fails. */
    int mm_actor_replay_frame_sounds(const MMActorState *, void *events, int enabled,
                                     const MMActorMotionServices *, void *context,
                                     int (*apply)(void *, MMI32 index), int (*motion_sounds)(void *));
#ifdef __cplusplus
}
#endif
#endif
