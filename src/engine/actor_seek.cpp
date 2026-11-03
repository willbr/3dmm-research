/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
using namespace ActorEvent;

ActorSeekEvaluator::ActorSeekEvaluator(ActorFrameState &state, const ActorTrack &track, LegacyLong firstFrame, LegacyLong lastFrame,
                                       LegacyLong targetFrame, const ActorStepSource &steps)
    : _state(state), _track(track), _steps(steps), _firstFrame(firstFrame), _lastFrame(lastFrame),
      _targetFrame(targetFrame)
{
    AssertIn(targetFrame, klwMin, klwMax);
}

ActorSeekCommand ActorSeekEvaluator::_Command(ActorSeekCommandKind kind, bool display)
{
    _pending = kind;
    _waiting = true;
    return {kind, display};
}

void ActorSeekEvaluator::_Rewind()
{
    LegacyLong entrance = ivNil;
    if (_midPath)
    {
        for (LegacyLong index = _state.nextEvent - 1; index >= 0; --index)
        {
            Base event;
            _track.events->GetFixed(index, &event);
            if (event.aet == aetAdd && event.nfrm <= _targetFrame)
            {
                entrance = index;
                break;
            }
        }
    }
    _state.RewindRoute(_track);
    if (_midPath)
    {
        AssertIn(entrance, 0, _track.events->IvMac());
        _state.firstEventInFrame = _state.nextEvent = _state.addEvent = entrance;
        Base event;
        _track.events->GetFixed(entrance, &event);
        _state.location = event.rtel;
        _state.frame = event.nfrm;
        --_state.location.dnfrm;
        ActorRouteEvaluator::Position(_track, &_state.location, &_state.position);
    }
    else
        _state.frame = LwMin(_targetFrame, _firstFrame);
}

ActorSeekCommand ActorSeekEvaluator::Next()
{
    Assert(!_waiting, "Unacknowledged seek command");
    while (_phase != Phase::Done)
    {
        switch (_phase)
        {
        case Phase::Initialize:
            if (_targetFrame == _state.frame)
            {
                _phase = Phase::Done;
                break;
            }
            if (_targetFrame < _state.frame || _state.frame == knfrmInvalid)
            {
                _midPath = _track.events->IvMac() > 0 && _state.nextEvent > 0 && _targetFrame > _firstFrame;
                _phase = Phase::Rewind;
                if (_targetFrame < _firstFrame && _state.onStage)
                    return _Command(ActorSeekCommandKind::Hide);
            }
            else
            {
                ++_state.frame;
                _state.firstEventInFrame = _state.nextEvent;
                _phase = Phase::PrepareFrames;
            }
            break;
        case Phase::Rewind:
            if (_targetFrame > _firstFrame && _state.frame != knfrmInvalid)
            {
                Assert(_state.nextEvent > 0, "Invalid seek event cursor");
                Base event;
                _track.events->GetFixed(_state.nextEvent - 1, &event);
                if (event.nfrm < _targetFrame)
                {
                    ActorQuickSeekResult result =
                        ActorTimeline::QuickBackup(_state, _track, _lastFrame, _targetFrame, _steps);
                    if (!result.success)
                    {
                        _success = false;
                        _phase = Phase::Done;
                        break;
                    }
                    if (result.valid)
                    {
                        _phase = Phase::Done;
                        if (result.applyEffects)
                            return _Command(ActorSeekCommandKind::ApplyQuickEffects);
                        break;
                    }
                }
            }
            _Rewind();
            _phase = Phase::PrepareFrames;
            return _Command(ActorSeekCommandKind::ResetCostume);
        case Phase::PrepareFrames:
            if (_targetFrame < _firstFrame)
                _phase = Phase::Done;
            else if (_track.events->IvMac() == 0)
            {
                _state.frame = _targetFrame;
                _phase = Phase::Done;
            }
            else
                _phase = Phase::Frames;
            break;
        case Phase::Frames:
            return _Command(ActorSeekCommandKind::EvaluateFrame, _state.frame == _targetFrame);
        case Phase::Done:
            break;
        }
    }
    return {ActorSeekCommandKind::Finished};
}

void ActorSeekEvaluator::Complete(bool success)
{
    Assert(_waiting, "No seek command to complete");
    _waiting = false;
    if (_pending == ActorSeekCommandKind::ApplyQuickEffects)
        _success = success;
    else if (_pending == ActorSeekCommandKind::EvaluateFrame)
    {
        if (!success)
            _success = false;
        if (_state.frame == _targetFrame)
            _phase = Phase::Done;
        else
        {
            ++_state.frame;
            _state.firstEventInFrame = _state.nextEvent;
            AssertIn(_state.actionEvent, -1, _track.events->IvMac());
        }
    }
}
