/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
using namespace ActorEvent;

ActorFrameState::ActorFrameState()
{
    InitTransform();
}

void ActorFrameState::InitTransform()
{
    BrMatrix34Identity(&transform.bmat34Fwd);
    BrMatrix34Identity(&transform.bmat34Cur);
    transform.aevpull.rScaleX = transform.aevpull.rScaleY = transform.aevpull.rScaleZ = rOne;
    transform.rScaleStep = rOne;
    transform.xaPath = transform.yaPath = transform.zaPath = aZero;
}

void ActorFrameState::RewindRoute(const ActorTrack &track)
{
    frozen = false;
    action = cel = 0;
    location = {0, rZero, -1};
    if (track.Route() != pvNil && track.Route()->IvMac() > 0)
    {
        RouteDistancePoint point;
        track.Route()->Get(0, &point);
        position = point.xyz;
    }
    nextEvent = firstEventInFrame = 0;
    actionEvent = addEvent = ivNil;
    step = rZero;
    InitTransform();
}

void ActorFrameState::UpdateOrigin(const RoutePoint &origin)
{
    routeTranslation.dxr = BrsAdd(origin.dxr, subrouteTranslation.dxr);
    routeTranslation.dyr = BrsAdd(origin.dyr, subrouteTranslation.dyr);
    routeTranslation.dzr = BrsAdd(origin.dzr, subrouteTranslation.dzr);
}

ActorValueEventResult ActorFrameState::ApplyValueEvent(const ActorTrack &track, LegacyLong index, const RoutePoint &origin)
{
    if (track.Events() == pvNil || track.Events()->CbFixed() != sizeof(Base) || index < 0 || index >= track.Events()->IvMac())
        return ActorValueEventResult::Invalid;
    Base event;
    track.Events()->GetFixed(index, &event);
    LegacyLong expectedSize;
    switch (event.aet)
    {
    case aetRotF:
    case aetRotH:
        expectedSize = sizeof(BMAT34);
        break;
    case aetPull:
        expectedSize = sizeof(Stretch);
        break;
    case aetSize:
    case aetStep:
        expectedSize = sizeof(BRS);
        break;
    case aetFreeze:
        expectedSize = sizeof(int32_t);
        break;
    case aetTweak:
    case aetMove:
        expectedSize = sizeof(RoutePoint);
        break;
    case aetActn:
    case aetAdd:
    case aetRem:
    case aetCost:
    case aetSnd:
        return ActorValueEventResult::NeedsRuntime;
    default:
        return ActorValueEventResult::Invalid;
    }
    if (track.Events()->Cb(index) != expectedSize)
        return ActorValueEventResult::Invalid;

    switch (event.aet)
    {
    case aetRotF:
        track.Events()->Get(index, &transform.bmat34Fwd);
        useCurrentRotation = false;
        break;
    case aetRotH:
        track.Events()->Get(index, &transform.bmat34Cur);
        useCurrentRotation = true;
        break;
    case aetPull:
        track.Events()->Get(index, &transform.aevpull);
        break;
    case aetSize:
        track.Events()->Get(index, &transform.rScaleStep);
        break;
    case aetStep: {
        BRS requestedStep;
        track.Events()->Get(index, &requestedStep);
        if (requestedStep == rZero)
        {
            if (track.Route() == pvNil || track.Route()->CbEntry() != sizeof(RouteDistancePoint) || event.rtel.irpt < 0 ||
                event.rtel.irpt >= track.Route()->IvMac() || event.rtel.dwrOffset < rZero)
                return ActorValueEventResult::Invalid;
            RouteDistancePoint point;
            track.Route()->Get(event.rtel.irpt, &point);
            if (event.rtel.dwrOffset > point.dwr)
                return ActorValueEventResult::Invalid;
            location = event.rtel;
            ActorRouteEvaluator::Position(track, &location, &position);
        }
        step = requestedStep;
        break;
    }
    case aetFreeze: {
        int32_t value;
        track.Events()->Get(index, &value);
        frozen = FPure(value);
        break;
    }
    case aetTweak:
        if (event.rtel.dnfrm == location.dnfrm)
            track.Events()->Get(index, &position);
        break;
    case aetMove: {
        RoutePoint delta;
        track.Events()->Get(index, &delta);
        subrouteTranslation.dxr = BrsAdd(delta.dxr, subrouteTranslation.dxr);
        subrouteTranslation.dyr = BrsAdd(delta.dyr, subrouteTranslation.dyr);
        subrouteTranslation.dzr = BrsAdd(delta.dzr, subrouteTranslation.dzr);
        UpdateOrigin(origin);
        break;
    }
    }
    return ActorValueEventResult::Applied;
}
