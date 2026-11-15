#include "mm/playback.h"

static MMScrollUpdate evaluate_scroll(int continuing, int needsScroll, double *remaining)
{
    MMPlaybackState state;
    MMPlaybackEvaluator evaluator;
    MMPlaybackCommand command;
    MMPlaybackReply reply;
    MMScrollUpdate result = {0, 0};
    mm_playback_state_init(&state);
    state.scrolling = continuing;
    mm_playback_init(&evaluator, &state, 0);
    for (;;)
    {
        if (!mm_playback_next(&evaluator, &command))
            return result;
        mm_playback_reply_init(&reply);
        switch (command.kind)
        {
        case MM_PLAYBACK_FINISHED:
            result.holding = state.scrolling;
            return result;
        case MM_PLAYBACK_SCROLL_TEXT:
            /* Need-before-step: the final pixel still schedules its alarm. */
            result.step = needsScroll;
            reply.value = needsScroll;
            break;
        case MM_PLAYBACK_QUERY_SCROLLING:
            reply.value = needsScroll;
            break;
        case MM_PLAYBACK_SET_ALARM:
            if (command.flag)
            {
                mm_playback_complete(&evaluator, &reply);
                return result;
            }
            *remaining = (double)command.value * 1000 / MM_PLAYBACK_TICKS_PER_SECOND;
            break;
        default:
            /* Presentation and ordinary pacing remain host responsibilities. */
            break;
        }
        mm_playback_complete(&evaluator, &reply);
    }
}
void mm_scroll_reset(MMScrollPlayback *scroll)
{
    scroll->started = scroll->holding = 0;
    scroll->remaining = 0;
}
MMScrollUpdate mm_scroll_update(MMScrollPlayback *scroll, double milliseconds, int needsScroll)
{
    MMScrollUpdate result = {0, 0};
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
double mm_scroll_nominal_seconds(int pixels)
{
    /* Convert before adding, avoiding signed overflow for large layouts. */
    return pixels > 0 ? ((double)pixels + 1) * MM_PLAYBACK_SCROLL_TICKS / MM_PLAYBACK_TICKS_PER_SECOND : 0;
}
