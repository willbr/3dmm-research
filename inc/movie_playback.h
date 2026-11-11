/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef MOVIE_PLAYBACK_H
#define MOVIE_PLAYBACK_H
#include <cstdint>
#include "three_mm/playback.h"
#include "scene_data.h"
#include "socdef.h"

const LegacyLong kPlaybackFrameTicks = kdtimSecond / kfps;
const LegacyLong kPlaybackScrollTicks = 5;
const LegacyLong kPlaybackFadeSeconds = 3;
const LegacyLong kPlaybackFadeTicks = kdtimSecond / 4;

// Movie-frame pacing only. Other alarms retain their authored timing. Negative
// FPS selects the original clock; zero removes the frame delay, not pauses.
struct MovieFramePacer : ThreeMMFramePacer
{
    MovieFramePacer()
    {
        three_mm_frame_pacer_init(this);
    }
    void Reset(double rate)
    {
        three_mm_frame_pacer_reset(this, rate);
    }
    void Schedule(LegacyUlong now)
    {
        three_mm_frame_pacer_schedule(this, now);
    }
    bool TakeDue(LegacyUlong now)
    {
        return three_mm_frame_pacer_take_due(this, now) != 0;
    }
    void Pause(LegacyUlong now)
    {
        three_mm_frame_pacer_pause(this, now);
    }
    void Resume(LegacyUlong now)
    {
        three_mm_frame_pacer_resume(this, now);
    }
};

// Transitional facade: all policy state and decisions live in the C backend.
struct MoviePlaybackState : ThreeMMPlaybackState
{
    MovieFramePacer framePacer;
    MoviePlaybackState()
    {
        three_mm_playback_state_init(this);
    }
    bool FShouldRender(bool tracking)
    {
        return three_mm_playback_should_render(this, tracking) != 0;
    }
};

struct MoviePlaybackTestProgress
{
    bool active = false, complete = false, failed = false;
    const char *error = "";
    std::uint64_t frames = 0, expectedFrames = 0, framesInScene = 0, steps = 0, scrollSteps = 0;
    LegacyLong scenes = 0, scene = -1, frame = 0, first = 0, last = 0;
    LegacyLong failureCommand = -1, originalVolume = 0;
    LegacyUlong started = 0, elapsed = 0;
};

struct MoviePlaybackPosition
{
    LegacyLong scene = 0;
    LegacyLong sceneCount = 0;
    LegacyLong frame = 0;
    LegacyLong lastFrame = 0;
    TRANS transition = transNil;
};

enum class MoviePlaybackCommandKind
{
    Stop = THREE_MM_PLAYBACK_STOP,
    ReadVolume = THREE_MM_PLAYBACK_READ_VOLUME,
    SetVolume = THREE_MM_PLAYBACK_SET_VOLUME,
    ScrollText = THREE_MM_PLAYBACK_SCROLL_TEXT,
    PresentFrame = THREE_MM_PLAYBACK_PRESENT_FRAME,
    DisplayFrame = THREE_MM_PLAYBACK_DISPLAY_FRAME,
    QueryScrolling = THREE_MM_PLAYBACK_QUERY_SCROLLING,
    SetClickPause = THREE_MM_PLAYBACK_SET_CLICK_PAUSE,
    QuerySound = THREE_MM_PLAYBACK_QUERY_SOUND,
    ReadTime = THREE_MM_PLAYBACK_READ_TIME,
    SetAlarm = THREE_MM_PLAYBACK_SET_ALARM,
    ReadPosition = THREE_MM_PLAYBACK_READ_POSITION,
    SwitchScene = THREE_MM_PLAYBACK_SWITCH_SCENE,
    ReplayScene = THREE_MM_PLAYBACK_REPLAY_SCENE,
    AdvanceFrame = THREE_MM_PLAYBACK_ADVANCE_FRAME,
    RenderWorld = THREE_MM_PLAYBACK_RENDER_WORLD,
    Finished = THREE_MM_PLAYBACK_FINISHED
};

struct MoviePlaybackCommand
{
    MoviePlaybackCommandKind kind;
    LegacyLong value = 0;
    bool flag = false; // Click-pause value or alarm delay adjustment.
};

struct MoviePlaybackReply
{
    // Checked for presentation, scene/frame advance and ordinary alarms.
    // Void effects and the legacy best-effort timed-pause alarm ignore it.
    bool success = true;
    bool value = false;    // Scrolling / non-loop sound queries.
    LegacyLong number = 0; // Current volume.
    LegacyUlong timestamp = 0;
    MoviePlaybackPosition position;
};

// One render/alarm callback's control flow. Commands are applied and completed
// in order; replies provide facts at the original query points. Scene effects
// may update borrowed playback state (e.g. DoPause) without replacing it.
// Stop performs the runtime cleanup; clock, view, sound and scene ownership
// stay in the host. Stop after Finished. The timed-pause alarm remains best-
// effort, unlike other alarms, preserving the existing runtime behavior.
class MoviePlaybackEvaluator
{
  public:
    MoviePlaybackEvaluator(MoviePlaybackState &state, LegacyUlong frameStartTimestamp);
    MoviePlaybackEvaluator(const MoviePlaybackEvaluator &) = delete;
    MoviePlaybackEvaluator &operator=(const MoviePlaybackEvaluator &) = delete;
    MoviePlaybackCommand Next();
    void Complete(const MoviePlaybackReply &reply);
    static LegacyLong FrameDelay(LegacyUlong frameStart, LegacyUlong now);

  private:
    ThreeMMPlaybackEvaluator _backend;
};

enum class MoviePlayCommandKind
{
    RenderStop = THREE_MM_PLAY_RENDER_STOP,
    CheckScene = THREE_MM_PLAY_CHECK_SCENE,
    ResetSounds = THREE_MM_PLAY_RESET_SOUNDS,
    QueryEnd = THREE_MM_PLAY_QUERY_END,
    Rewind = THREE_MM_PLAY_REWIND,
    ReadTime = THREE_MM_PLAY_READ_TIME,
    StartClock = THREE_MM_PLAY_START_CLOCK,
    PrepareScene = THREE_MM_PLAY_PREPARE_SCENE,
    NotifyStopped = THREE_MM_PLAY_NOTIFY_STOPPED,
    StartupFailed = THREE_MM_PLAY_STARTUP_FAILED,
    DisableAccelerators = THREE_MM_PLAY_DISABLE_ACCELERATORS,
    QueryOpening = THREE_MM_PLAY_QUERY_OPENING,
    RefreshOpening = THREE_MM_PLAY_REFRESH_OPENING,
    BackgroundSound = THREE_MM_PLAY_BACKGROUND_SOUND,
    DispatchSounds = THREE_MM_PLAY_DISPATCH_SOUNDS,
    Finished = THREE_MM_PLAY_FINISHED
};

struct MoviePlayCommand
{
    MoviePlayCommandKind kind;
    bool flag = false;
};

// Play-button/startup control. Commands preserve startup and failure ordering.
// Replies use value for scene/end/opening facts, timestamp for ReadTime and
// success for rewind/clock/scene preparation. StartupFailed performs the host's
// notification, playing/stop flag update, pending-sound play and sound-setting
// restoration in that order. Acknowledge each command; stop after Finished.
class MoviePlayEvaluator
{
  public:
    explicit MoviePlayEvaluator(MoviePlaybackState &state);
    MoviePlayEvaluator(const MoviePlayEvaluator &) = delete;
    MoviePlayEvaluator &operator=(const MoviePlayEvaluator &) = delete;
    MoviePlayCommand Next();
    void Complete(const MoviePlaybackReply &reply);

  private:
    ThreeMMPlayEvaluator _backend;
};
#endif
