/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef MOVIE_PLAYBACK_H
#define MOVIE_PLAYBACK_H
#include <cstdint>
#include "scene_data.h"
#include "socdef.h"

const LegacyLong kPlaybackFrameTicks = kdtimSecond / kfps;
const LegacyLong kPlaybackScrollTicks = 5;
const LegacyLong kPlaybackFadeSeconds = 3;
const LegacyLong kPlaybackFadeTicks = kdtimSecond / 4;

// Movie-frame pacing only. Other alarms retain their authored timing. Negative
// FPS selects the original clock; zero removes the frame delay, not pauses.
struct MovieFramePacer
{
    double fps = -1, fraction = 0;
    LegacyUlong due = 0, pausedAt = 0;
    bool pending = false;
    void Reset(double rate);
    void Schedule(LegacyUlong frameStart);
    bool TakeDue(LegacyUlong now);
    void Pause(LegacyUlong now);
    void Resume(LegacyUlong now);
};

struct MoviePlaybackState
{
    MovieFramePacer framePacer;
    bool unpaced = false; // Enabled only by the opt-in batch-test host.
    LegacyLong skippedClickPauses = 0, skippedTimedPauses = 0, skippedSoundPauses = 0;
    bool playing = false;
    bool scrolling = false;
    bool pausing = false;
    bool idleSeen = false;
    bool stopRequested = false;
    bool soundsEnabled = false;
    bool oldSoundsEnabled = false;
    LegacyUlong startTimestamp = 0;
    LegacyLong renderedFrames = 0;
    WaitReason wait = witNil;
    LegacyLong pauseTicks = 0;
    TRANS transition = transNil;
    LegacyLong fadeOriginalVolume = 0;
    bool suppressAudioFades = false;
    bool fadeChangedVolume = false;
    LegacyLong fadeVolume = -1; // Logical progress while output volume is held constant.
    bool FShouldRender(bool tracking);
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
    Stop,
    ReadVolume,
    SetVolume,
    ScrollText,
    PresentFrame,
    DisplayFrame,
    QueryScrolling,
    SetClickPause,
    QuerySound,
    ReadTime,
    SetAlarm,
    ReadPosition, // Host first disables text-box evaluation, then supplies scene facts.
    SwitchScene,
    ReplayScene,
    AdvanceFrame,
    RenderWorld,
    Finished
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
    bool value = false; // Scrolling / non-loop sound queries.
    LegacyLong number = 0;    // Current volume.
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
    enum class Phase
    {
        Start,
        Stop,
        ReadVolume,
        SetVolume,
        FadeAlarm,
        Scroll,
        ScrollAlarm,
        Present,
        Display,
        QueryScrolling,
        StartScrollAlarm,
        CheckPause,
        ClickOn,
        QuerySound,
        SoundAlarm,
        TimedAlarm,
        ClickOff,
        ReadTime,
        FrameAlarm,
        ReadPosition,
        SwitchScene,
        ReplayScene,
        AdvanceFrame,
        Render,
        Done
    };
    MoviePlaybackState &_state;
    LegacyUlong _frameStart;
    Phase _phase = Phase::Start;
    bool _waiting = false;
    LegacyLong _volume = 0;
    LegacyLong _delay = 0;
    MoviePlaybackPosition _position;
    MoviePlaybackCommand _Command(MoviePlaybackCommandKind kind, LegacyLong value = 0, bool flag = false);
};

enum class MoviePlayCommandKind
{
    RenderStop,
    CheckScene,
    ResetSounds,
    QueryEnd,
    Rewind,
    ReadTime,
    StartClock, // Host starts clock, publishes playing=true, then sets the initial alarm.
    PrepareScene,
    NotifyStopped,
    StartupFailed, // flag=true requests deferred stop; false clears playing.
    DisableAccelerators,
    QueryOpening,
    RefreshOpening,
    BackgroundSound,
    DispatchSounds, // flag selects play rather than flush.
    Finished
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
    enum class Phase
    {
        Toggle,
        RenderStop,
        CheckScene,
        ResetSounds,
        QueryEnd,
        Rewind,
        Begin,
        ReadTime,
        StartClock,
        PrepareScene,
        NotifyStopped,
        ClockFailed,
        PrepareFailed,
        DisableAccelerators,
        QueryOpening,
        RefreshOpening,
        BackgroundSound,
        DispatchSounds,
        Done
    };
    MoviePlaybackState &_state;
    Phase _phase = Phase::Toggle;
    bool _waiting = false;
    MoviePlayCommand _Command(MoviePlayCommandKind kind, bool flag = false);
};
#endif
