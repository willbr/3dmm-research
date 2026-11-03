#ifndef ACTOR_FRAME_FIXTURE_H
#define ACTOR_FRAME_FIXTURE_H
#include "soc_core.h"
#include <cstring>
namespace ActorFrameTest
{
inline bool SameState(const ActorFrameState &a, const ActorFrameState &b)
{
    return std::memcmp(&a.routeTranslation, &b.routeTranslation, sizeof(RoutePoint)) == 0 &&
           std::memcmp(&a.subrouteTranslation, &b.subrouteTranslation, sizeof(RoutePoint)) == 0 &&
           a.onStage == b.onStage && a.frozen == b.frozen && a.useCurrentRotation == b.useCurrentRotation &&
           a.step == b.step && a.action == b.action && a.celCount == b.celCount && a.cel == b.cel &&
           a.frame == b.frame && a.nextEvent == b.nextEvent && a.firstEventInFrame == b.firstEventInFrame &&
           a.actionEvent == b.actionEvent && a.addEvent == b.addEvent && a.actionFlags == b.actionFlags &&
           std::memcmp(&a.location, &b.location, sizeof(RouteLocation)) == 0 &&
           std::memcmp(&a.position, &b.position, sizeof(RoutePoint)) == 0 &&
           std::memcmp(&a.transform.bmat34Fwd, &b.transform.bmat34Fwd, sizeof(BMAT34)) == 0 &&
           std::memcmp(&a.transform.bmat34Cur, &b.transform.bmat34Cur, sizeof(BMAT34)) == 0 &&
           std::memcmp(&a.transform.aevpull, &b.transform.aevpull, sizeof(ActorEvent::Stretch)) == 0 &&
           a.transform.rScaleStep == b.transform.rScaleStep && a.transform.xaPath == b.transform.xaPath &&
           a.transform.yaPath == b.transform.yaPath && a.transform.zaPath == b.transform.zaPath;
}

// Fixed authored track shared by headless sequencing and real runtime seeking.
inline bool FCreateTrack(ActorTrack &track, bool includeSounds = true)
{
    ActorTrack created;
    if (!created.FCreate())
        return false;
    RouteDistancePoint points[] = {{{rZero, rZero, rZero}, BR_SCALAR(8)},
                                   {{BR_SCALAR(8), rZero, rZero}, rZero},
                                   {{BR_SCALAR(20), rZero, rZero}, rZero}};
    for (LegacyLong index = 0; index < 3; ++index)
        if (!created.route->FInsert(index, &points[index]))
            return false;
    auto append = [&](LegacyLong type, LegacyLong frame, RouteLocation location, void *payload, LegacyLong bytes) {
        ActorEvent::Base event = {static_cast<int32_t>(type), static_cast<int32_t>(frame), location};
        return created.events->FInsert(created.events->IvMac(), bytes, payload, &event);
    };
    RouteLocation location = {0, rZero, 0};
    ActorEvent::Add addition = {};
    ActorEvent::Action action = {0, 0};
    BRS step = BR_SCALAR(2);
    if (!append(ActorEvent::aetAdd, 1, location, &addition, sizeof(addition)) ||
        !append(ActorEvent::aetActn, 1, location, &action, sizeof(action)) ||
        !append(ActorEvent::aetStep, 1, location, &step, sizeof(step)))
        return false;
    if (includeSounds)
    {
        ActorEvent::Sound sound = {};
        sound.tag.sid = ksidInvalid;
        sound.celn = ivNil;
        location.dwrOffset = BR_SCALAR(2);
        if (!append(ActorEvent::aetSnd, 2, location, &sound, sizeof(sound)))
            return false;
        sound.celn = 1;
        if (!append(ActorEvent::aetSnd, 2, location, &sound, sizeof(sound)))
            return false;
    }
    location.dwrOffset = BR_SCALAR(4);
    int32_t freeze = 1;
    RoutePoint tweak = {BR_SCALAR(4), rOne, rZero};
    if (!append(ActorEvent::aetFreeze, 3, location, &freeze, sizeof(freeze)) ||
        !append(ActorEvent::aetTweak, 3, location, &tweak, sizeof(tweak)))
        return false;
    location.dwrOffset = BR_SCALAR(6);
    step = rZero;
    if (!append(ActorEvent::aetStep, 4, location, &step, sizeof(step)))
        return false;
    location.dnfrm = 2;
    if (!append(ActorEvent::aetRem, 6, location, pvNil, 0))
        return false;
    location = {2, rZero, 0};
    addition.dxr = BR_SCALAR(100);
    if (!append(ActorEvent::aetAdd, 8, location, &addition, sizeof(addition)))
        return false;
    location.dnfrm = 1;
    if (!append(ActorEvent::aetStep, 9, location, &step, sizeof(step)))
        return false;
    track.Swap(created);
    return true;
}
inline bool FCreateMetadataTrack(ActorTrack &track, bool freezeAtFour)
{
    ActorTrack created;
    if (!created.FCreate())
        return false;
    RouteDistancePoint point = {{rZero, rZero, rZero}, BR_SCALAR(6)};
    if (!created.route->FInsert(0, &point))
        return false;
    point = {{BR_SCALAR(6), rZero, rZero}, rZero};
    if (!created.route->FInsert(1, &point))
        return false;
    ActorEvent::Base event = {ActorEvent::aetAdd, -2, {0, rZero, 0}};
    ActorEvent::Add addition = {};
    if (!created.events->FInsert(0, sizeof(addition), &addition, &event))
        return false;
    event.aet = ActorEvent::aetActn;
    event.nfrm = -777; // Calculation must replace stale caches.
    ActorEvent::Action action = {0, 1};
    if (!created.events->FInsert(1, sizeof(action), &action, &event))
        return false;
    event.aet = ActorEvent::aetStep;
    BRS step = kdwrNil;
    if (!created.events->FInsert(2, sizeof(step), &step, &event))
        return false;
    event.aet = ActorEvent::aetSize;
    BRS scale = BR_SCALAR(2);
    if (!created.events->FInsert(3, sizeof(scale), &scale, &event))
        return false;
    if (freezeAtFour)
    {
        event.aet = ActorEvent::aetFreeze;
        event.rtel.dwrOffset = BR_SCALAR(4);
        int32_t frozen = 1;
        if (!created.events->FInsert(4, sizeof(frozen), &frozen, &event))
            return false;
    }
    track.Swap(created);
    return true;
}
} // namespace ActorFrameTest
#endif
