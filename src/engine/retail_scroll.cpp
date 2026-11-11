#include "retail_scroll.h"
#include "soc_core.h"
ASSERTNAME

namespace
{
RetailScrollUpdate EvaluateScroll(bool continuing, bool needsScroll, double &remaining)
{
    MoviePlaybackState state;
    state.scrolling = continuing;
    MoviePlaybackEvaluator evaluator(state, 0);
    RetailScrollUpdate result;
    for (;;)
    {
        const auto command = evaluator.Next();
        MoviePlaybackReply reply;
        switch (command.kind)
        {
        case MoviePlaybackCommandKind::Finished:
            result.holding = state.scrolling;
            return result;
        case MoviePlaybackCommandKind::ScrollText:
            // Retail tests need-before-step, so moving the final pixel still
            // schedules one more scroll alarm before it discovers completion.
            result.step = needsScroll;
            reply.value = needsScroll;
            break;
        case MoviePlaybackCommandKind::QueryScrolling:
            reply.value = needsScroll;
            break;
        case MoviePlaybackCommandKind::SetAlarm:
            if (command.flag)
            {
                // Hand ordinary frame pacing back to the host. This adapter
                // owns only scrolling; it never evaluates or advances actors.
                evaluator.Complete(reply);
                return result;
            }
            remaining = double(command.value) * 1000 / kdtimSecond;
            break;
        default:
            // Present/Display are already performed by the host. ReadTime is
            // irrelevant once the evaluator hands off to ordinary frame pacing.
            break;
        }
        evaluator.Complete(reply);
    }
}
}

void RetailScrollPlayback::Reset()
{
    started = holding = false;
    remaining = 0;
}
RetailScrollUpdate RetailScrollPlayback::Update(double milliseconds, bool needsScroll)
{
    if (started)
    {
        if (!holding)
            return {};
        remaining -= milliseconds;
        if (remaining > 0)
            return {false, true};
    }
    const auto result = EvaluateScroll(started, needsScroll, remaining);
    started = true;
    holding = result.holding;
    return result;
}
double RetailScrollPlayback::NominalSeconds(int pixels)
{
    // Include the completion-check alarm following the last pixel move.
    return pixels > 0 ? double(pixels + 1) * kPlaybackScrollTicks / kdtimSecond : 0;
}
