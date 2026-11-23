/* Studio's diagnostic reporting for the C89 actor timeline. */
#pragma once
#include "mm/actor_timeline.h"
namespace EngineActorTimeline
{
inline void Diagnostic(void *, int fault)
{
    ASSERTNAME
    switch (fault)
    {
    case MM_ACTOR_CORRUPT_ROUTE:
        Bug("Corrupted event list");
        break;
    case MM_ACTOR_UNKNOWN_EVENT:
        Assert(false, "Unknown event type");
        break;
    case MM_ACTOR_LIFETIME_LOGIC:
        Assert(false, "Logic error");
        break;
    case MM_ACTOR_CORRUPT_ENTRANCE:
        Assert(false, "Corrupt event list");
        break;
    case MM_ACTOR_LIFETIME_BAD_RANGE:
        Assert(false, "fLifeDirty incorrect");
        break;
    case MM_ACTOR_LIFETIME_INCOMPLETE:
        PushErc(ercSocBadFrameSlider);
        break;
    }
}
} // namespace EngineActorTimeline
