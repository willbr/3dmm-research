/* Temporary native services for the C89 actor timeline/seek evaluator. */
#pragma once
#include "engine_actor_motion.h"
#include "mm/actor_timeline.h"
namespace EngineActorTimeline
{
struct Host
{
    const ActorStepSource *steps;
    MMActorState *state = nullptr;
    BRS *stepOutput = nullptr;
    LegacyLong *lastFrame = nullptr;
};
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
    }
}
inline MMActorTimelineServices Services(Host *host)
{
    MMActorTimelineServices s = {EngineActorMotion::Services(),
                                      host,
                                      nullptr,
                                      [](void *g, MMI32 i, const MMActorEvent *e) {
                                          mm_actor_events_set_fixed(static_cast<MMActorEventTable *>(g), i, e);
                                      },
                                      Diagnostic,
                                      [](void *p, const MMActorState *state) {
                                          auto h = static_cast<Host *>(p);
                                          if (h->state)
                                              EngineActorMotion::Publish(*h->state, *state);
                                      },
                                      [](void *p, MMI32 frame) {
                                          auto h = static_cast<Host *>(p);
                                          if (h->lastFrame)
                                              *h->lastFrame = frame;
                                      }};
    if (host->steps->resolve)
        s.resolve = [](void *p, MMI32 action, MMI32 cel, MMI32 *step) -> int {
            auto h = static_cast<Host *>(p);
            BRS value = *step;
            BRS *output = h->stepOutput ? h->stepOutput : &value;
            *output = *step;
            const bool ok = h->steps->resolve(h->steps->context, action, cel, output);
            *step = *output;
            return ok;
        };
    return s;
}
} // namespace EngineActorTimeline
