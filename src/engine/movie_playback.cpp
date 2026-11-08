/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME

bool MoviePlaybackState::FShouldRender(bool tracking)
{
    if (!idleSeen && !tracking)
        return false;
    idleSeen = false;
    return true;
}

MoviePlaybackEvaluator::MoviePlaybackEvaluator(MoviePlaybackState &state, LegacyUlong frameStartTimestamp)
    : _state(state), _frameStart(frameStartTimestamp)
{
}

LegacyLong MoviePlaybackEvaluator::FrameDelay(LegacyUlong frameStart, LegacyUlong now)
{
    return LwMax(0, kPlaybackFrameTicks - LwMulDivAway(now - frameStart, kdtimSecond, kdtsSecond));
}

MoviePlaybackCommand MoviePlaybackEvaluator::_Command(MoviePlaybackCommandKind kind, LegacyLong value, bool flag)
{
    _waiting = true;
    return {kind, value, flag};
}

MoviePlaybackCommand MoviePlaybackEvaluator::Next()
{
    Assert(!_waiting, "Unacknowledged playback command");
    for (;;)
    {
        switch (_phase)
        {
        case Phase::Start:
            if (_state.stopRequested)
                _phase = Phase::Stop;
            else if (_state.fadeOriginalVolume != 0)
                _phase = Phase::ReadVolume;
            else if (_state.scrolling)
            {
                _state.scrolling = false;
                _phase = Phase::Scroll;
            }
            else if (!_state.pausing)
                _phase = Phase::Present;
            else
                _phase = _state.wait == witUntilSnd ? Phase::CheckPause : Phase::ClickOff;
            break;
        case Phase::CheckPause:
            if (_state.unpaced)
            {
                switch (_state.wait)
                {
                case witUntilClick: ++_state.skippedClickPauses; break;
                case witForTime: ++_state.skippedTimedPauses; break;
                case witUntilSnd: ++_state.skippedSoundPauses; break;
                default: break;
                }
                _state.wait = witNil;
                _state.pausing = false;
                _phase = Phase::ReadTime;
                break;
            }
            switch (_state.wait)
            {
            case witUntilClick:
                _state.wait = witNil;
                _state.pausing = true;
                _phase = Phase::ClickOn;
                break;
            case witUntilSnd:
                _phase = Phase::QuerySound;
                break;
            case witForTime:
                _state.wait = witNil;
                _state.pausing = true;
                _phase = Phase::TimedAlarm;
                break;
            case witNil:
                _state.pausing = false;
                _phase = Phase::ReadTime;
                break;
            default:
                Bug("Bad pause type");
                _phase = Phase::ReadTime;
                break;
            }
            break;
        case Phase::Stop:
            return _Command(MoviePlaybackCommandKind::Stop);
        case Phase::ReadVolume:
            return _Command(MoviePlaybackCommandKind::ReadVolume);
        case Phase::SetVolume:
            if (_state.suppressAudioFades)
            {
                _state.fadeVolume = LwMax(0, _volume);
                _phase = _volume <= 0 ? Phase::Stop : Phase::FadeAlarm;
                break;
            }
            return _Command(MoviePlaybackCommandKind::SetVolume, LwMax(0, _volume));
        case Phase::FadeAlarm:
            return _Command(MoviePlaybackCommandKind::SetAlarm, kPlaybackFadeTicks);
        case Phase::Scroll:
            return _Command(MoviePlaybackCommandKind::ScrollText);
        case Phase::ScrollAlarm:
        case Phase::StartScrollAlarm:
            return _Command(MoviePlaybackCommandKind::SetAlarm, kPlaybackScrollTicks);
        case Phase::Present:
            return _Command(MoviePlaybackCommandKind::PresentFrame);
        case Phase::Display:
            return _Command(MoviePlaybackCommandKind::DisplayFrame);
        case Phase::QueryScrolling:
            return _Command(MoviePlaybackCommandKind::QueryScrolling);
        case Phase::ClickOn:
            return _Command(MoviePlaybackCommandKind::SetClickPause, 0, true);
        case Phase::QuerySound:
            return _Command(MoviePlaybackCommandKind::QuerySound);
        case Phase::SoundAlarm:
            return _Command(MoviePlaybackCommandKind::SetAlarm, 0);
        case Phase::TimedAlarm:
            return _Command(MoviePlaybackCommandKind::SetAlarm, _state.pauseTicks);
        case Phase::ClickOff:
            return _Command(MoviePlaybackCommandKind::SetClickPause, 0, false);
        case Phase::ReadTime:
            return _Command(MoviePlaybackCommandKind::ReadTime);
        case Phase::FrameAlarm:
            return _Command(MoviePlaybackCommandKind::SetAlarm, _delay, true);
        case Phase::ReadPosition:
            return _Command(MoviePlaybackCommandKind::ReadPosition);
        case Phase::SwitchScene:
            return _Command(MoviePlaybackCommandKind::SwitchScene, _position.scene + 1);
        case Phase::ReplayScene:
            return _Command(MoviePlaybackCommandKind::ReplayScene);
        case Phase::AdvanceFrame:
            return _Command(MoviePlaybackCommandKind::AdvanceFrame, _position.frame + 1);
        case Phase::Render:
            return _Command(MoviePlaybackCommandKind::RenderWorld);
        case Phase::Done:
            return {MoviePlaybackCommandKind::Finished};
        }
    }
}

void MoviePlaybackEvaluator::Complete(const MoviePlaybackReply &reply)
{
    Assert(_waiting, "No playback command to complete");
    _waiting = false;
    switch (_phase)
    {
    case Phase::Stop:
        _phase = Phase::Done;
        break;
    case Phase::ReadVolume:
        _volume = (_state.suppressAudioFades && _state.fadeVolume >= 0 ? _state.fadeVolume : reply.number) -
                  _state.fadeOriginalVolume / (kPlaybackFadeSeconds * 4);
        if (_volume <= 0)
            _state.stopRequested = true;
        _phase = Phase::SetVolume;
        break;
    case Phase::SetVolume:
        _state.fadeChangedVolume = true;
        _state.fadeVolume = LwMax(0, _volume);
        _phase = _volume <= 0 ? Phase::Stop : Phase::FadeAlarm;
        break;
    case Phase::FadeAlarm:
    case Phase::SoundAlarm:
        _phase = reply.success ? Phase::Done : Phase::Stop;
        break;
    case Phase::Scroll:
        _state.scrolling = reply.value;
        _phase = reply.value ? Phase::ScrollAlarm : Phase::CheckPause;
        break;
    case Phase::ScrollAlarm:
        if (!reply.success)
            _state.scrolling = false;
        _phase = reply.success ? Phase::Done : Phase::Stop;
        break;
    case Phase::Present:
        _phase = reply.success ? Phase::Display : Phase::Stop;
        break;
    case Phase::Display:
        _phase = Phase::QueryScrolling;
        break;
    case Phase::QueryScrolling:
        _phase = reply.value ? Phase::StartScrollAlarm : Phase::CheckPause;
        break;
    case Phase::StartScrollAlarm:
        if (reply.success)
            _state.scrolling = true;
        _phase = reply.success ? Phase::Done : Phase::Stop;
        break;
    case Phase::ClickOn:
    case Phase::TimedAlarm: // The original timed-pause alarm's return value is ignored.
        _phase = Phase::Done;
        break;
    case Phase::QuerySound:
        if (reply.value)
        {
            _state.pausing = true;
            _phase = Phase::SoundAlarm;
        }
        else
        {
            _state.pausing = false;
            _state.wait = witNil;
            _phase = Phase::ReadTime;
        }
        break;
    case Phase::ClickOff:
        _state.pausing = false;
        _phase = Phase::ReadTime;
        break;
    case Phase::ReadTime:
        _delay = _state.unpaced ? 0 : FrameDelay(_frameStart, reply.timestamp);
        _phase = Phase::FrameAlarm;
        break;
    case Phase::FrameAlarm:
        _phase = reply.success ? Phase::ReadPosition : Phase::Stop;
        break;
    case Phase::ReadPosition:
        _position = reply.position;
        if (_position.frame == _position.lastFrame)
        {
            if (_position.scene == _position.sceneCount - 1)
            {
                _state.stopRequested = true;
                _phase = Phase::Done;
            }
            else
            {
                _state.transition = _position.transition;
                _phase = Phase::SwitchScene;
            }
        }
        else
            _phase = Phase::AdvanceFrame;
        break;
    case Phase::SwitchScene:
        _phase = reply.success ? Phase::ReplayScene : Phase::Done;
        if (!reply.success)
            _state.stopRequested = true;
        break;
    case Phase::ReplayScene:
    case Phase::AdvanceFrame:
        _phase = reply.success ? Phase::Render : Phase::Done;
        if (!reply.success)
            _state.stopRequested = true;
        break;
    case Phase::Render:
        ++_state.renderedFrames;
        _phase = Phase::Done;
        break;
    default:
        Bug("No playback command for this phase");
        break;
    }
}

MoviePlayEvaluator::MoviePlayEvaluator(MoviePlaybackState &state) : _state(state)
{
}

MoviePlayCommand MoviePlayEvaluator::_Command(MoviePlayCommandKind kind, bool flag)
{
    _waiting = true;
    return {kind, flag};
}

MoviePlayCommand MoviePlayEvaluator::Next()
{
    Assert(!_waiting, "Unacknowledged play command");
    for (;;)
    {
        switch (_phase)
        {
        case Phase::Toggle:
            if (_state.playing)
            {
                if (_state.stopRequested)
                    _phase = Phase::Done;
                else
                {
                    _state.stopRequested = true;
                    _phase = _state.pausing ? Phase::RenderStop : Phase::DispatchSounds;
                }
            }
            else
                _phase = Phase::CheckScene;
            break;
        case Phase::Begin:
            _state.oldSoundsEnabled = _state.soundsEnabled;
            _state.soundsEnabled = true;
            _state.renderedFrames = 0;
            _phase = Phase::ReadTime;
            break;
        case Phase::RenderStop:
            return _Command(MoviePlayCommandKind::RenderStop);
        case Phase::CheckScene:
            return _Command(MoviePlayCommandKind::CheckScene);
        case Phase::ResetSounds:
            return _Command(MoviePlayCommandKind::ResetSounds);
        case Phase::QueryEnd:
            return _Command(MoviePlayCommandKind::QueryEnd);
        case Phase::Rewind:
            return _Command(MoviePlayCommandKind::Rewind);
        case Phase::ReadTime:
            return _Command(MoviePlayCommandKind::ReadTime);
        case Phase::StartClock:
            return _Command(MoviePlayCommandKind::StartClock);
        case Phase::PrepareScene:
            return _Command(MoviePlayCommandKind::PrepareScene);
        case Phase::NotifyStopped:
            return _Command(MoviePlayCommandKind::NotifyStopped);
        case Phase::ClockFailed:
            return _Command(MoviePlayCommandKind::StartupFailed, false);
        case Phase::PrepareFailed:
            return _Command(MoviePlayCommandKind::StartupFailed, true);
        case Phase::DisableAccelerators:
            return _Command(MoviePlayCommandKind::DisableAccelerators);
        case Phase::QueryOpening:
            return _Command(MoviePlayCommandKind::QueryOpening);
        case Phase::RefreshOpening:
            return _Command(MoviePlayCommandKind::RefreshOpening);
        case Phase::BackgroundSound:
            return _Command(MoviePlayCommandKind::BackgroundSound);
        case Phase::DispatchSounds:
            return _Command(MoviePlayCommandKind::DispatchSounds, _state.soundsEnabled);
        case Phase::Done:
            return {MoviePlayCommandKind::Finished};
        }
    }
}

void MoviePlayEvaluator::Complete(const MoviePlaybackReply &reply)
{
    Assert(_waiting, "No play command to complete");
    _waiting = false;
    switch (_phase)
    {
    case Phase::RenderStop:
        _phase = Phase::DispatchSounds;
        break;
    case Phase::CheckScene:
        _phase = reply.value ? Phase::ResetSounds : Phase::NotifyStopped;
        break;
    case Phase::ResetSounds:
        _phase = Phase::QueryEnd;
        break;
    case Phase::QueryEnd:
        _phase = reply.value ? Phase::Rewind : Phase::Begin;
        break;
    case Phase::Rewind:
        _phase = reply.success ? Phase::Begin : Phase::NotifyStopped;
        break;
    case Phase::ReadTime:
        _state.startTimestamp = reply.timestamp;
        _state.stopRequested = false;
        _phase = Phase::StartClock;
        break;
    case Phase::StartClock:
        _phase = reply.success ? Phase::PrepareScene : Phase::ClockFailed;
        break;
    case Phase::PrepareScene:
        _phase = reply.success ? Phase::DisableAccelerators : Phase::PrepareFailed;
        break;
    case Phase::DisableAccelerators:
        _phase = Phase::QueryOpening;
        break;
    case Phase::QueryOpening:
        _phase = reply.value ? Phase::RefreshOpening : Phase::BackgroundSound;
        break;
    case Phase::RefreshOpening:
        _phase = Phase::BackgroundSound;
        break;
    case Phase::BackgroundSound:
        _phase = Phase::DispatchSounds;
        break;
    case Phase::NotifyStopped:
    case Phase::ClockFailed:
    case Phase::PrepareFailed:
    case Phase::DispatchSounds:
        _phase = Phase::Done;
        break;
    default:
        Bug("No play command for this phase");
        break;
    }
}
