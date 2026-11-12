/* Temporary native services for the C89 actor timeline/seek evaluator. */
#pragma once
#include "engine_actor_motion.h"
#include "three_mm/actor_timeline.h"
namespace EngineActorTimeline
{
struct Host
{
    const ActorStepSource *steps;
    ActorFrameState *state = nullptr;
    BRS *stepOutput = nullptr;
    LegacyLong *lastFrame = nullptr;
};
void Diagnostic(void *, int fault);
inline ThreeMMActorTimelineServices Services(Host *host)
{
    ThreeMMActorTimelineServices s = {EngineActorMotion::Services(),
                                      host,
                                      nullptr,
                                      [](void *g, ThreeMMI32 i, const ThreeMMActorEvent *e) {
                                          static_cast<PGeneralGroup>(g)->PutFixed(i,
                                                                                  const_cast<ThreeMMActorEvent *>(e));
                                      },
                                      Diagnostic,
                                      [](void *p, const ThreeMMActorState *state) {
                                          auto h = static_cast<Host *>(p);
                                          if (h->state)
                                              EngineActorMotion::Publish(*h->state, *state);
                                      },
                                      [](void *p, ThreeMMI32 frame) {
                                          auto h = static_cast<Host *>(p);
                                          if (h->lastFrame)
                                              *h->lastFrame = frame;
                                      }};
    if (host->steps->resolve)
        s.resolve = [](void *p, ThreeMMI32 action, ThreeMMI32 cel, ThreeMMI32 *step) -> int {
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
