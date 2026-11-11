/* Playback policy converted from the original engine implementation.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_PLAYBACK_H
#define THREE_MM_ENGINE_PLAYBACK_H
#include <limits.h>

/* C89 has no stdint.h. Fail at compile time rather than change wire/clock widths. */
#if UINT_MAX == 0xffffffffUL && INT_MAX == 0x7fffffffL
typedef unsigned int ThreeMMPlaybackU32;
typedef int ThreeMMPlaybackI32;
#elif ULONG_MAX == 0xffffffffUL && LONG_MAX == 0x7fffffffL
typedef unsigned long ThreeMMPlaybackU32;
typedef long ThreeMMPlaybackI32;
#else
#error Playback requires a 32-bit C integer type
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        THREE_MM_PLAYBACK_TICKS_PER_SECOND = 60,
        THREE_MM_PLAYBACK_FRAME_TICKS = 10,
        THREE_MM_PLAYBACK_SCROLL_TICKS = 5,
        THREE_MM_PLAYBACK_FADE_SECONDS = 3,
        THREE_MM_PLAYBACK_FADE_TICKS = 15,
        THREE_MM_PLAYBACK_WAIT_NONE = 0,
        THREE_MM_PLAYBACK_WAIT_CLICK = 1,
        THREE_MM_PLAYBACK_WAIT_SOUND = 2,
        THREE_MM_PLAYBACK_WAIT_TIME = 3
    };

    /* Caller-owned, non-owning records. Initialize before use; pass valid pointers.
     * Flags are 0/1.
     * Timestamps are wrapping 32-bit milliseconds. No allocation or host services
     * are required. State may be updated by host effects between command replies.
     * Enum wait values and transition numbers retain the legacy engine meanings. */
    typedef struct ThreeMMFramePacer
    {
        double fps, fraction;
        ThreeMMPlaybackU32 due, pausedAt;
        int pending;
    } ThreeMMFramePacer;

    typedef struct ThreeMMPlaybackState
    {
        int unpaced;
        ThreeMMPlaybackI32 skippedClickPauses, skippedTimedPauses, skippedSoundPauses;
        int playing, scrolling, pausing, idleSeen, stopRequested;
        int soundsEnabled, oldSoundsEnabled;
        ThreeMMPlaybackU32 startTimestamp;
        ThreeMMPlaybackI32 renderedFrames;
        int wait;
        ThreeMMPlaybackI32 pauseTicks;
        int transition;
        ThreeMMPlaybackI32 fadeOriginalVolume;
        int suppressAudioFades, fadeChangedVolume;
        ThreeMMPlaybackI32 fadeVolume;
    } ThreeMMPlaybackState;

    typedef struct ThreeMMPlaybackPosition
    {
        ThreeMMPlaybackI32 scene, sceneCount, frame, lastFrame;
        int transition;
    } ThreeMMPlaybackPosition;
    typedef enum ThreeMMPlaybackCommandKind
    {
        THREE_MM_PLAYBACK_STOP,
        THREE_MM_PLAYBACK_READ_VOLUME,
        THREE_MM_PLAYBACK_SET_VOLUME,
        THREE_MM_PLAYBACK_SCROLL_TEXT,
        THREE_MM_PLAYBACK_PRESENT_FRAME,
        THREE_MM_PLAYBACK_DISPLAY_FRAME,
        THREE_MM_PLAYBACK_QUERY_SCROLLING,
        THREE_MM_PLAYBACK_SET_CLICK_PAUSE, /* flag is the click-pause value. */
        THREE_MM_PLAYBACK_QUERY_SOUND,
        THREE_MM_PLAYBACK_READ_TIME,
        THREE_MM_PLAYBACK_SET_ALARM,     /* value is ticks; flag requests ordinary frame-delay adjustment. */
        THREE_MM_PLAYBACK_READ_POSITION, /* Host disables text-box evaluation before supplying scene facts. */
        THREE_MM_PLAYBACK_SWITCH_SCENE,
        THREE_MM_PLAYBACK_REPLAY_SCENE,
        THREE_MM_PLAYBACK_ADVANCE_FRAME,
        THREE_MM_PLAYBACK_RENDER_WORLD,
        THREE_MM_PLAYBACK_FINISHED
    } ThreeMMPlaybackCommandKind;
    typedef enum ThreeMMPlayCommandKind
    {
        THREE_MM_PLAY_RENDER_STOP,
        THREE_MM_PLAY_CHECK_SCENE,
        THREE_MM_PLAY_RESET_SOUNDS,
        THREE_MM_PLAY_QUERY_END,
        THREE_MM_PLAY_REWIND,
        THREE_MM_PLAY_READ_TIME,
        THREE_MM_PLAY_START_CLOCK, /* Host starts clock, publishes playing=1, then sets the initial alarm. */
        THREE_MM_PLAY_PREPARE_SCENE,
        THREE_MM_PLAY_NOTIFY_STOPPED,
        THREE_MM_PLAY_STARTUP_FAILED, /* flag=1 requests deferred stop; flag=0 clears playing. */
        THREE_MM_PLAY_DISABLE_ACCELERATORS,
        THREE_MM_PLAY_QUERY_OPENING,
        THREE_MM_PLAY_REFRESH_OPENING,
        THREE_MM_PLAY_BACKGROUND_SOUND,
        THREE_MM_PLAY_DISPATCH_SOUNDS, /* flag selects play rather than flush. */
        THREE_MM_PLAY_FINISHED
    } ThreeMMPlayCommandKind;

    typedef struct ThreeMMPlaybackCommand
    {
        ThreeMMPlaybackCommandKind kind;
        ThreeMMPlaybackI32 value;
        int flag;
    } ThreeMMPlaybackCommand;
    typedef struct ThreeMMPlayCommand
    {
        ThreeMMPlayCommandKind kind;
        int flag;
    } ThreeMMPlayCommand;
    typedef struct ThreeMMPlaybackReply
    {
        /* init defaults success to 1. success is checked for presentation,
         * scene/frame advances and ordinary alarms. Void effects ignore it.
         * value supplies scrolling/sound or startup scene/end/opening facts. */
        int success, value;
        ThreeMMPlaybackI32 number;
        ThreeMMPlaybackU32 timestamp;
        ThreeMMPlaybackPosition position;
    } ThreeMMPlaybackReply;

    /* Evaluators borrow state, which must outlive them. Internal fields are exposed
     * for stack allocation only: use init/next/complete, never edit phase/waiting.
     * Apply and acknowledge each command in order. Stop after FINISHED. next and
     * complete return 0 on protocol misuse, leaving evaluator/state unchanged.
     * Timed-pause alarm failure remains best effort, matching retail behavior. */
    typedef struct ThreeMMPlaybackEvaluator
    {
        ThreeMMPlaybackState *state;
        ThreeMMPlaybackU32 frameStart;
        int phase, waiting;
        ThreeMMPlaybackI32 volume, delay;
        ThreeMMPlaybackPosition position;
    } ThreeMMPlaybackEvaluator;
    typedef struct ThreeMMPlayEvaluator
    {
        ThreeMMPlaybackState *state;
        int phase, waiting;
    } ThreeMMPlayEvaluator;

    void three_mm_frame_pacer_init(ThreeMMFramePacer *pacer);
    void three_mm_frame_pacer_reset(ThreeMMFramePacer *pacer, double rate);
    void three_mm_frame_pacer_schedule(ThreeMMFramePacer *pacer, ThreeMMPlaybackU32 frameStart);
    int three_mm_frame_pacer_take_due(ThreeMMFramePacer *pacer, ThreeMMPlaybackU32 now);
    void three_mm_frame_pacer_pause(ThreeMMFramePacer *pacer, ThreeMMPlaybackU32 now);
    void three_mm_frame_pacer_resume(ThreeMMFramePacer *pacer, ThreeMMPlaybackU32 now);
    void three_mm_playback_state_init(ThreeMMPlaybackState *state);
    void three_mm_playback_reply_init(ThreeMMPlaybackReply *reply);
    int three_mm_playback_should_render(ThreeMMPlaybackState *state, int tracking);
    ThreeMMPlaybackI32 three_mm_playback_frame_delay(ThreeMMPlaybackU32 frameStart, ThreeMMPlaybackU32 now);
    void three_mm_playback_init(ThreeMMPlaybackEvaluator *evaluator, ThreeMMPlaybackState *state,
                                ThreeMMPlaybackU32 frameStart);
    int three_mm_playback_next(ThreeMMPlaybackEvaluator *evaluator, ThreeMMPlaybackCommand *command);
    int three_mm_playback_complete(ThreeMMPlaybackEvaluator *evaluator, const ThreeMMPlaybackReply *reply);
    void three_mm_play_init(ThreeMMPlayEvaluator *evaluator, ThreeMMPlaybackState *state);
    int three_mm_play_next(ThreeMMPlayEvaluator *evaluator, ThreeMMPlayCommand *command);
    int three_mm_play_complete(ThreeMMPlayEvaluator *evaluator, const ThreeMMPlaybackReply *reply);

    typedef struct ThreeMMScrollPlayback
    {
        int started, holding;
        double remaining;
    } ThreeMMScrollPlayback;
    typedef struct ThreeMMScrollUpdate
    {
        int step, holding;
    } ThreeMMScrollUpdate;
    /* One pixel per due callback; lateness is discarded. The host supplies layout
     * facts and applies the step. The final pixel retains its completion alarm. */
    void three_mm_scroll_reset(ThreeMMScrollPlayback *scroll);
    ThreeMMScrollUpdate three_mm_scroll_update(ThreeMMScrollPlayback *scroll, double milliseconds, int needsScroll);
    double three_mm_scroll_nominal_seconds(int pixels);

#ifdef __cplusplus
}
#endif
#endif
