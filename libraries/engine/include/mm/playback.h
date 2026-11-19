/* Playback policy converted from the original engine implementation.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_PLAYBACK_H
#define MM_ENGINE_PLAYBACK_H
#include "types.h"

/* C89 has no stdint.h. Fail at compile time rather than change wire/clock widths. */
typedef MMU32 MMPlaybackU32;
typedef MMI32 MMPlaybackI32;

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        MM_PLAYBACK_TICKS_PER_SECOND = 60,
        MM_PLAYBACK_FRAME_TICKS = 10,
        MM_PLAYBACK_SCROLL_TICKS = 5,
        MM_PLAYBACK_FADE_SECONDS = 3,
        MM_PLAYBACK_FADE_TICKS = 15,
        MM_PLAYBACK_WAIT_NONE = 0,
        MM_PLAYBACK_WAIT_CLICK = 1,
        MM_PLAYBACK_WAIT_SOUND = 2,
        MM_PLAYBACK_WAIT_TIME = 3
    };

    /* Caller-owned, non-owning records. Initialize before use; pass valid pointers.
     * Flags are 0/1.
     * Timestamps are wrapping 32-bit milliseconds. No allocation or host services
     * are required. State may be updated by host effects between command replies.
     * Enum wait values and transition numbers retain the legacy engine meanings. */
    typedef struct MMFramePacer
    {
        double fps, fraction;
        MMPlaybackU32 due, pausedAt;
        int pending;
    } MMFramePacer;

    typedef struct MMPlaybackState
    {
        int unpaced;
        MMPlaybackI32 skippedClickPauses, skippedTimedPauses, skippedSoundPauses;
        int playing, scrolling, pausing, idleSeen, stopRequested;
        int soundsEnabled, oldSoundsEnabled;
        MMPlaybackU32 startTimestamp;
        MMPlaybackI32 renderedFrames;
        int wait;
        MMPlaybackI32 pauseTicks;
        int transition;
        MMPlaybackI32 fadeOriginalVolume;
        int suppressAudioFades, fadeChangedVolume;
        MMPlaybackI32 fadeVolume;
    } MMPlaybackState;

    typedef struct MMPlaybackPosition
    {
        MMPlaybackI32 scene, sceneCount, frame, lastFrame;
        int transition;
    } MMPlaybackPosition;
    typedef enum MMPlaybackCommandKind
    {
        MM_PLAYBACK_STOP,
        MM_PLAYBACK_READ_VOLUME,
        MM_PLAYBACK_SET_VOLUME,
        MM_PLAYBACK_SCROLL_TEXT,
        MM_PLAYBACK_PRESENT_FRAME,
        MM_PLAYBACK_DISPLAY_FRAME,
        MM_PLAYBACK_QUERY_SCROLLING,
        MM_PLAYBACK_SET_CLICK_PAUSE, /* flag is the click-pause value. */
        MM_PLAYBACK_QUERY_SOUND,
        MM_PLAYBACK_READ_TIME,
        MM_PLAYBACK_SET_ALARM,     /* value is ticks; flag requests ordinary frame-delay adjustment. */
        MM_PLAYBACK_READ_POSITION, /* Host disables text-box evaluation before supplying scene facts. */
        MM_PLAYBACK_SWITCH_SCENE,
        MM_PLAYBACK_REPLAY_SCENE,
        MM_PLAYBACK_ADVANCE_FRAME,
        MM_PLAYBACK_RENDER_WORLD,
        MM_PLAYBACK_FINISHED
    } MMPlaybackCommandKind;
    typedef enum MMPlayCommandKind
    {
        MM_PLAY_RENDER_STOP,
        MM_PLAY_CHECK_SCENE,
        MM_PLAY_RESET_SOUNDS,
        MM_PLAY_QUERY_END,
        MM_PLAY_REWIND,
        MM_PLAY_READ_TIME,
        MM_PLAY_START_CLOCK, /* Host starts clock, publishes playing=1, then sets the initial alarm. */
        MM_PLAY_PREPARE_SCENE,
        MM_PLAY_NOTIFY_STOPPED,
        MM_PLAY_STARTUP_FAILED, /* flag=1 requests deferred stop; flag=0 clears playing. */
        MM_PLAY_DISABLE_ACCELERATORS,
        MM_PLAY_QUERY_OPENING,
        MM_PLAY_REFRESH_OPENING,
        MM_PLAY_BACKGROUND_SOUND,
        MM_PLAY_DISPATCH_SOUNDS, /* flag selects play rather than flush. */
        MM_PLAY_FINISHED
    } MMPlayCommandKind;

    typedef struct MMPlaybackCommand
    {
        MMPlaybackCommandKind kind;
        MMPlaybackI32 value;
        int flag;
    } MMPlaybackCommand;
    typedef struct MMPlayCommand
    {
        MMPlayCommandKind kind;
        int flag;
    } MMPlayCommand;
    typedef struct MMPlaybackReply
    {
        /* init defaults success to 1. success is checked for presentation,
         * scene/frame advances and ordinary alarms. Void effects ignore it.
         * value supplies scrolling/sound or startup scene/end/opening facts. */
        int success, value;
        MMPlaybackI32 number;
        MMPlaybackU32 timestamp;
        MMPlaybackPosition position;
    } MMPlaybackReply;

    /* Evaluators borrow state, which must outlive them. Internal fields are exposed
     * for stack allocation only: use init/next/complete, never edit phase/waiting.
     * Apply and acknowledge each command in order. Stop after FINISHED. next and
     * complete return 0 on protocol misuse, leaving evaluator/state unchanged.
     * Timed-pause alarm failure remains best effort, matching retail behavior. */
    typedef struct MMPlaybackEvaluator
    {
        MMPlaybackState *state;
        MMPlaybackU32 frameStart;
        int phase, waiting;
        MMPlaybackI32 volume, delay;
        MMPlaybackPosition position;
    } MMPlaybackEvaluator;
    typedef struct MMPlayEvaluator
    {
        MMPlaybackState *state;
        int phase, waiting;
    } MMPlayEvaluator;

    /* Preference indices: Original/6/12/24/Uncapped and Normal/Win98->XP/XP->Win98.
     * Negative selects the original host clock; zero removes only frame delays. */
    double mm_playback_fps(int rate, int modifier);
    void mm_frame_pacer_init(MMFramePacer *pacer);
    void mm_frame_pacer_reset(MMFramePacer *pacer, double rate);
    void mm_frame_pacer_schedule(MMFramePacer *pacer, MMPlaybackU32 frameStart);
    int mm_frame_pacer_take_due(MMFramePacer *pacer, MMPlaybackU32 now);
    void mm_frame_pacer_pause(MMFramePacer *pacer, MMPlaybackU32 now);
    void mm_frame_pacer_resume(MMFramePacer *pacer, MMPlaybackU32 now);
    void mm_playback_state_init(MMPlaybackState *state);
    void mm_playback_reply_init(MMPlaybackReply *reply);
    int mm_playback_should_render(MMPlaybackState *state, int tracking);
    MMPlaybackI32 mm_playback_frame_delay(MMPlaybackU32 frameStart, MMPlaybackU32 now);
    /* Requires a valid cursor. The polling host can loop the current scene. */
    MMPlaybackCommandKind mm_playback_after_frame(const MMPlaybackPosition *, int loop_scene);
    void mm_playback_init(MMPlaybackEvaluator *evaluator, MMPlaybackState *state,
                                MMPlaybackU32 frameStart);
    int mm_playback_next(MMPlaybackEvaluator *evaluator, MMPlaybackCommand *command);
    int mm_playback_complete(MMPlaybackEvaluator *evaluator, const MMPlaybackReply *reply);
    void mm_play_init(MMPlayEvaluator *evaluator, MMPlaybackState *state);
    int mm_play_next(MMPlayEvaluator *evaluator, MMPlayCommand *command);
    int mm_play_complete(MMPlayEvaluator *evaluator, const MMPlaybackReply *reply);

    typedef struct MMScrollPlayback
    {
        int started, holding;
        double remaining;
    } MMScrollPlayback;
    typedef struct MMScrollUpdate
    {
        int step, holding;
    } MMScrollUpdate;
    /* One pixel per due callback; lateness is discarded. The host supplies layout
     * facts and applies the step. The final pixel retains its completion alarm. */
    void mm_scroll_reset(MMScrollPlayback *scroll);
    MMScrollUpdate mm_scroll_update(MMScrollPlayback *scroll, double milliseconds, int needsScroll);
    double mm_scroll_nominal_seconds(int pixels);

    typedef struct MMPolledPlayback
    {
        MMFramePacer pacer;
        MMScrollPlayback scroll;
        double frame_ms, wait_ms, transition_ms, frame_seconds, pacing_ms, carry_ms;
        int entered, was_playing, restore_music, wait, transition;
    } MMPolledPlayback;
    typedef struct MMPolledFrameWait
    {
        int kind;
        double milliseconds;
    } MMPolledFrameWait;
    typedef struct MMPolledPlaybackServices
    {
        void *context;
        int (*enter_frame)(void *, int restore_music, double fps, MMPolledFrameWait *);
        int (*measure_scroll)(void *, int *needs_scroll);
        int (*sound_pending)(void *, int *pending);
        void (*transition_ended)(void *);
    } MMPolledPlaybackServices;
    typedef struct MMPolledPlaybackUpdate
    {
        int advance, scroll_step;
    } MMPolledPlaybackUpdate;

    /* Reset after a seek/scene boundary; changing rate resets ordinary pacing only.
     * Begin a transition with an authored kind that has nonzero duration.
     * Hosts may clear restore_music after restoring audio themselves. Update
     * takes finite nonnegative milliseconds. Services are synchronous and must
     * report failure without unwinding through C. At most one frame advances. */
    void mm_polled_playback_reset(MMPolledPlayback *, double fps, int continuing);
    void mm_polled_playback_set_rate(MMPolledPlayback *, double fps);
    void mm_polled_playback_begin_transition(MMPolledPlayback *, int authored_kind);
    double mm_polled_playback_frame_seconds(const MMPolledPlayback *);
    int mm_polled_playback_update(MMPolledPlayback *, int playing, double elapsed, int click,
                                  const MMPolledPlaybackServices *, MMPolledPlaybackUpdate *);

    typedef struct MMPolledMovie
    {
        int restore_audio, finished;
    } MMPolledMovie;
    typedef struct MMPolledMovieResult
    {
        int playing, changed, scene_changed, wait, scrolling, transition;
    } MMPolledMovieResult;
    typedef enum MMPolledMovieEffect
    {
        MM_POLLED_MOVIE_CANCEL_PREVIEWS,
        MM_POLLED_MOVIE_RESTORE_AUDIO,
        MM_POLLED_MOVIE_SET_FRAME,
        MM_POLLED_MOVIE_REWIND,
        MM_POLLED_MOVIE_CAPTURE_OUTGOING,
        MM_POLLED_MOVIE_SELECT_SCENE,
        MM_POLLED_MOVIE_START_TRANSITION,
        MM_POLLED_MOVIE_STOP
    } MMPolledMovieEffect;
    typedef struct MMPolledMovieServices
    {
        void *context;
        int (*poll)(void *, int playing, double elapsed, int click, int *advance);
        void (*position)(void *, MMPlaybackPosition *, MMI32 *first_frame);
        /* Value is the frame for set/rewind, scene for capture/select, or
         * transition kind for start. The other effects receive zero. */
        int (*effect)(void *, MMPolledMovieEffect, MMI32 value);
    } MMPolledMovieServices;
    enum { MM_POLLED_MOVIE_INVALID = -1 };
    /* Reset on select, seek or open. A scene cut can keep existing audio. */
    void mm_polled_movie_reset(MMPolledMovie *, int keep_audio);
    /* Effects are synchronous and may update movie/clock state. Their failure
     * is reported as zero and must never unwind through C. A failed update
     * stops playback and clears the result. Returns 1 on success, 0 on effect
     * failure, or MM_POLLED_MOVIE_INVALID for invalid update arguments. */
    int mm_polled_movie_update(MMPolledMovie *, const MMPolledPlayback *, int selected, int playing, int loop_scene,
                               double elapsed, int click, const MMPolledMovieServices *, MMPolledMovieResult *);

#ifdef __cplusplus
}
#endif
#endif
