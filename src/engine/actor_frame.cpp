/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
using namespace ActorEvent;

ActorFrameEvaluator::ActorFrameEvaluator(ActorFrameState &state, const ActorTrack &track,
                                         const ActorFrameInputs &inputs, bool positionDirty)
    : _state(state), _track(track), _inputs(inputs), _phase(inputs.recording ? Phase::Events : Phase::Entrances),
      _entranceScan(state.nextEvent), _freezeCel(state.frozen), _oldPosition(state.position)
{
    _result.success = inputs.stepSucceeded;
    _result.positionDirty = positionDirty;
    ActorRouteEvaluator::Advance(track, inputs.step, &state.location, state.nextEvent, state.frame, inputs.firstFrame,
                                 !inputs.recording && !inputs.rejoining, &_endRoute);
    ActorRouteEvaluator::Position(track, &state.location, &state.position);
    state.useCurrentRotation =
        inputs.step == rZero || (_oldPosition.dxr == state.position.dxr && _oldPosition.dzr == state.position.dzr);
    if (inputs.recording)
        _Finish();
}

bool ActorFrameEvaluator::FSubrouteEventsDone(const ActorTrack &track, LegacyLong index, RouteLocation location)
{
    AssertPo(track.events, 0);
    AssertIn(index, 0, track.events->IvMac() + 1);
    for (; index < track.events->IvMac(); ++index)
    {
        Base event;
        track.events->GetFixed(index, &event);
        if (event.aet == aetAdd)
            return true;
        if (event.rtel > location)
            return false;
    }
    return true;
}

void ActorFrameEvaluator::_Abort()
{
    _result.success = false;
    _result.aborted = true;
    _phase = Phase::Done;
    _pending = Pending::None;
}

void ActorFrameEvaluator::_Finish()
{
    if (!_freezeCel && !(_endRoute && FSubrouteEventsDone(_track, _state.nextEvent, _state.location)))
    {
        ++_state.cel;
        if (_state.celCount > 1)
            _result.positionDirty = true;
    }
    if (_oldPosition != _state.position)
        _result.positionDirty = true;
    _result.considerMotionSounds = _inputs.displayFrame && !_inputs.suppressSounds && _state.onStage;
    _result.positionBody = _inputs.displayFrame && _result.positionDirty;
    _result.updatePose = !_inputs.displayFrame && _result.positionDirty;
    _result.selectCel = _inputs.displayFrame && _state.onStage;
    _phase = Phase::Done;
}

ActorFrameCommand ActorFrameEvaluator::Next()
{
    // Queries acknowledge before producing another request. A completed query
    // can queue an event request, represented by one of the Event pending states.
    Assert(_pending != Pending::EntranceQuery && _pending != Pending::OrderedEntranceQuery,
           "Unanswered entrance query");
    if (_pending == Pending::EntranceEvent || _pending == Pending::OrderedEntranceEvent || _pending == Pending::Event)
        return {ActorFrameCommandKind::ApplyEvent, _state.nextEvent};
    while (_phase != Phase::Done)
    {
        if (_phase == Phase::Entrances)
        {
            for (; _entranceScan < _track.events->IvMac(); ++_entranceScan)
            {
                Base event;
                _track.events->GetFixed(_entranceScan, &event);
                if (event.aet == aetAdd)
                {
                    _entrance = _entranceScan++;
                    _pending = Pending::EntranceQuery;
                    return {ActorFrameCommandKind::CheckEntrance, _entrance};
                }
            }
            _phase = Phase::Events;
            _scan = _state.nextEvent;
        }
        if (_scan == _track.events->IvMac())
        {
            _Finish();
            break;
        }
        Base event;
        _track.events->GetFixed(_scan, &event);
        if (event.aet == aetAdd)
        {
            _pending = Pending::OrderedEntranceQuery;
            return {ActorFrameCommandKind::CheckEntrance, _scan};
        }
        if (event.rtel > _state.location)
        {
            _Finish();
            break;
        }
        if (event.aet == aetRotF || event.aet == aetSize || event.aet == aetPull || event.aet == aetRotH ||
            event.aet == aetMove)
            _result.positionDirty = true;
        else if (event.aet == aetActn)
        {
            _freezeCel = true;
            _result.positionDirty = true;
        }
        if (event.aet == aetSnd)
        {
            if (_inputs.displayFrame)
                _result.soundInFrame = true;
            if (!_inputs.displayFrame || _inputs.suppressSounds)
            {
                Sound sound;
                _track.events->Get(_scan, &sound);
                if (sound.celn == ivNil)
                {
                    ++_state.nextEvent;
                    ++_scan;
                    continue;
                }
            }
        }
        _pending = Pending::Event;
        return {ActorFrameCommandKind::ApplyEvent, _state.nextEvent};
    }
    return {ActorFrameCommandKind::Finished, ivNil};
}

void ActorFrameEvaluator::Complete(bool succeededOrDue)
{
    Assert(_pending != Pending::None, "No frame command to complete");
    Pending pending = _pending;
    _pending = Pending::None;
    if (pending == Pending::EntranceQuery)
    {
        if (succeededOrDue)
        {
            _state.nextEvent = _entrance;
            _pending = Pending::EntranceEvent;
        }
        else
        {
            _phase = Phase::Events;
            _scan = _state.nextEvent;
        }
        return;
    }
    if (pending == Pending::OrderedEntranceQuery)
    {
        if (succeededOrDue)
            _pending = Pending::OrderedEntranceEvent;
        else
            _Finish();
        return;
    }
    if (succeededOrDue)
        ++_state.nextEvent;
    else if (pending == Pending::EntranceEvent || pending == Pending::OrderedEntranceEvent)
    {
        _Abort();
        return;
    }
    else
        _result.success = false;
    if (pending == Pending::EntranceEvent || pending == Pending::OrderedEntranceEvent)
        _result.positionDirty = true;
    if (pending != Pending::EntranceEvent)
        ++_scan;
}
