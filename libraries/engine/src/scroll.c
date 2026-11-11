#include "three_mm/playback.h"

static ThreeMMScrollUpdate evaluate_scroll(int continuing, int needsScroll, double *remaining)
{
    ThreeMMPlaybackState state;
    ThreeMMPlaybackEvaluator evaluator;
    ThreeMMPlaybackCommand command;
    ThreeMMPlaybackReply reply;
    ThreeMMScrollUpdate result = {0, 0};
    three_mm_playback_state_init(&state);
    state.scrolling = continuing;
    three_mm_playback_init(&evaluator, &state, 0);
    for (;;)
    {
        if (!three_mm_playback_next(&evaluator, &command))
            return result;
        three_mm_playback_reply_init(&reply);
        switch (command.kind)
        {
        case THREE_MM_PLAYBACK_FINISHED:
            result.holding = state.scrolling;
            return result;
        case THREE_MM_PLAYBACK_SCROLL_TEXT:
            /* Need-before-step: the final pixel still schedules its alarm. */
            result.step = needsScroll;
            reply.value = needsScroll;
            break;
        case THREE_MM_PLAYBACK_QUERY_SCROLLING:
            reply.value = needsScroll;
            break;
        case THREE_MM_PLAYBACK_SET_ALARM:
            if (command.flag)
            {
                three_mm_playback_complete(&evaluator, &reply);
                return result;
            }
            *remaining = (double)command.value * 1000 / THREE_MM_PLAYBACK_TICKS_PER_SECOND;
            break;
        default:
            /* Presentation and ordinary pacing remain host responsibilities. */
            break;
        }
        three_mm_playback_complete(&evaluator, &reply);
    }
}
void three_mm_scroll_reset(ThreeMMScrollPlayback *scroll)
{
    scroll->started = scroll->holding = 0;
    scroll->remaining = 0;
}
ThreeMMScrollUpdate three_mm_scroll_update(ThreeMMScrollPlayback *scroll, double milliseconds, int needsScroll)
{
    ThreeMMScrollUpdate result = {0, 0};
    if (scroll->started)
    {
        if (!scroll->holding)
            return result;
        scroll->remaining -= milliseconds;
        if (scroll->remaining > 0)
        {
            result.holding = 1;
            return result;
        }
    }
    result = evaluate_scroll(scroll->started, needsScroll, &scroll->remaining);
    scroll->started = 1;
    scroll->holding = result.holding;
    return result;
}
double three_mm_scroll_nominal_seconds(int pixels)
{
    /* Convert before adding, avoiding signed overflow for large layouts. */
    return pixels > 0 ? ((double)pixels + 1) * THREE_MM_PLAYBACK_SCROLL_TICKS / THREE_MM_PLAYBACK_TICKS_PER_SECOND : 0;
}
