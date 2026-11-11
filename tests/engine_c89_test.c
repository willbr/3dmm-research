#include "three_mm/playback.h"
#include <stdio.h>
#include <stdlib.h>

static void check(int condition, int line)
{
    if (!condition)
    {
        fprintf(stderr, "C89 engine check failed at line %d\n", line);
        exit(1);
    }
}
#define CHECK(condition) check((condition), __LINE__)

/* This executable links with the C linker and only the C89 backend. Existing
 * engine-core-link and Marcus regressions cover the host adapters and policy
 * branches. These checks exercise the public C contract independently. */
int main(void)
{
    ThreeMMPlaybackState state;
    ThreeMMPlaybackEvaluator evaluator;
    ThreeMMPlaybackCommand command;
    ThreeMMPlaybackReply reply;
    ThreeMMPlayEvaluator play;
    ThreeMMPlayCommand playCommand;
    ThreeMMFramePacer pacer;
    ThreeMMScrollPlayback scroll;
    ThreeMMScrollUpdate update;
    three_mm_playback_state_init(&state);
    three_mm_playback_reply_init(&reply);
    CHECK(state.transition == -1 && state.fadeVolume == -1 && !state.playing);
    CHECK(reply.success && reply.position.transition == -1);
    three_mm_playback_init(&evaluator, &state, 1000);
    CHECK(!three_mm_playback_complete(&evaluator, &reply));
    CHECK(three_mm_playback_next(&evaluator, &command));
    CHECK(command.kind == THREE_MM_PLAYBACK_PRESENT_FRAME);
    CHECK(!three_mm_playback_next(&evaluator, &command));
    CHECK(command.kind == THREE_MM_PLAYBACK_PRESENT_FRAME);
    /* A host effect changes borrowed state while its command is outstanding. */
    state.wait = THREE_MM_PLAYBACK_WAIT_CLICK;
    CHECK(three_mm_playback_complete(&evaluator, &reply));
    CHECK(!three_mm_playback_complete(&evaluator, &reply));
    CHECK(three_mm_playback_next(&evaluator, &command));
    CHECK(command.kind == THREE_MM_PLAYBACK_DISPLAY_FRAME);
    CHECK(three_mm_playback_complete(&evaluator, &reply));
    CHECK(three_mm_playback_next(&evaluator, &command));
    CHECK(command.kind == THREE_MM_PLAYBACK_QUERY_SCROLLING);
    CHECK(three_mm_playback_complete(&evaluator, &reply));
    CHECK(three_mm_playback_next(&evaluator, &command));
    CHECK(command.kind == THREE_MM_PLAYBACK_SET_CLICK_PAUSE && command.flag);
    CHECK(state.pausing && state.wait == THREE_MM_PLAYBACK_WAIT_NONE);
    CHECK(three_mm_playback_complete(&evaluator, &reply));
    CHECK(three_mm_playback_next(&evaluator, &command));
    CHECK(command.kind == THREE_MM_PLAYBACK_FINISHED);

    three_mm_playback_state_init(&state);
    three_mm_play_init(&play, &state);
    CHECK(!three_mm_play_complete(&play, &reply));
    CHECK(three_mm_play_next(&play, &playCommand));
    CHECK(playCommand.kind == THREE_MM_PLAY_CHECK_SCENE);
    CHECK(!three_mm_play_next(&play, &playCommand));
    CHECK(three_mm_play_complete(&play, &reply));
    CHECK(three_mm_play_next(&play, &playCommand));
    CHECK(playCommand.kind == THREE_MM_PLAY_NOTIFY_STOPPED);
    CHECK(three_mm_play_complete(&play, &reply));
    CHECK(three_mm_play_next(&play, &playCommand));
    CHECK(playCommand.kind == THREE_MM_PLAY_FINISHED);

    CHECK(three_mm_playback_frame_delay(1000, 1000) == 10);
    CHECK(three_mm_playback_frame_delay(1000, 1001) == 9);
    CHECK(three_mm_playback_frame_delay(1000, 1017) == 8);
    CHECK(three_mm_playback_frame_delay(0xfffffff0UL, 4) == 8);
    CHECK(three_mm_playback_frame_delay(1, 0) == 9);
    CHECK(three_mm_playback_frame_delay(50, 0) == 13);
    CHECK(three_mm_playback_frame_delay(0, 0x80000000UL) == 128849029);
    three_mm_frame_pacer_init(&pacer);
    CHECK(pacer.fps == -1 && !pacer.pending);
    three_mm_frame_pacer_reset(&pacer, 24);
    three_mm_frame_pacer_schedule(&pacer, 0xfffffff0UL);
    CHECK(!three_mm_frame_pacer_take_due(&pacer, 0xfffffff1UL));
    CHECK(three_mm_frame_pacer_take_due(&pacer, 25));
    CHECK(!three_mm_frame_pacer_take_due(&pacer, 25));

    three_mm_scroll_reset(&scroll);
    update = three_mm_scroll_update(&scroll, 10000, 1);
    CHECK(!update.step && update.holding);
    update = three_mm_scroll_update(&scroll, 83, 1);
    CHECK(!update.step && update.holding);
    update = three_mm_scroll_update(&scroll, 1, 1);
    CHECK(update.step && update.holding);
    update = three_mm_scroll_update(&scroll, 10000, 0);
    CHECK(!update.step && !update.holding);
    CHECK(three_mm_scroll_nominal_seconds(0) == 0);
    CHECK(three_mm_scroll_nominal_seconds(3) == 4.0 / 12);
    return 0;
}
