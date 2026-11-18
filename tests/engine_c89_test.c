#include "mm/playback.h"
#include "mm/graphics_transition.h"
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

typedef struct PolledHost
{
    MMPolledFrameWait wait;
    int entered, ended, needs_scroll, sound_pending, queried_sound, fail;
    int restored_music;
} PolledHost;

static int enter_frame(void *context, int restore_music, double fps, MMPolledFrameWait *wait)
{
    PolledHost *host = (PolledHost *)context;
    CHECK(fps == -1 || fps == 24 || fps == 0);
    ++host->entered;
    host->restored_music = restore_music;
    *wait = host->wait;
    return !host->fail;
}
static int measure_scroll(void *context, int *needs_scroll)
{
    *needs_scroll = ((PolledHost *)context)->needs_scroll;
    return 1;
}
static int sound_pending(void *context, int *pending)
{
    PolledHost *host = (PolledHost *)context;
    ++host->queried_sound;
    *pending = host->sound_pending;
    return !host->fail;
}
static void transition_ended(void *context)
{
    PolledHost *host = (PolledHost *)context;
    CHECK(host->entered == 0);
    ++host->ended;
}

static void check_polled_playback(void)
{
    MMPlaybackPosition position = {0, 2, 1, 2, MM_GRAPHICS_MOVIE_DISSOLVE};
    PolledHost host = {{MM_PLAYBACK_WAIT_SOUND, 0}, 0, 0, 1, 1, 0, 0, 0};
    MMPolledPlaybackServices services = {NULL, enter_frame, measure_scroll, sound_pending, transition_ended};
    MMPolledPlayback state;
    MMPolledPlaybackUpdate update;

    services.context = &host;
    CHECK(mm_playback_after_frame(&position, 1) == MM_PLAYBACK_ADVANCE_FRAME);
    position.frame = position.lastFrame;
    CHECK(mm_playback_after_frame(&position, 1) == MM_PLAYBACK_REPLAY_SCENE);
    CHECK(mm_playback_after_frame(&position, 0) == MM_PLAYBACK_SWITCH_SCENE);
    position.scene = 1;
    CHECK(mm_playback_after_frame(&position, 0) == MM_PLAYBACK_FINISHED);

    mm_polled_playback_reset(&state, -1, 0);
    mm_polled_playback_begin_transition(&state, MM_GRAPHICS_MOVIE_DISSOLVE);
    CHECK(mm_polled_playback_frame_seconds(&state) == -4);
    CHECK(mm_polled_playback_update(&state, 1, 3000, 0, &services, &update));
    CHECK(mm_polled_playback_frame_seconds(&state) == -1 && !host.entered && !update.advance);
    CHECK(mm_polled_playback_update(&state, 0, 2000, 0, &services, &update));
    CHECK(mm_polled_playback_update(&state, 1, 2000, 0, &services, &update));
    CHECK(mm_polled_playback_frame_seconds(&state) == -1 && !host.ended);
    CHECK(mm_polled_playback_update(&state, 1, 1100, 0, &services, &update));
    CHECK(host.ended == 1 && host.entered == 1 && host.restored_music);
    CHECK(mm_polled_playback_frame_seconds(&state) == 0 && state.scroll.holding && !update.scroll_step);
    CHECK(mm_polled_playback_update(&state, 1, 84, 0, &services, &update));
    CHECK(update.scroll_step && state.scroll.holding && !host.queried_sound);
    host.needs_scroll = 0;
    CHECK(mm_polled_playback_update(&state, 1, 84, 0, &services, &update));
    CHECK(!state.scroll.holding && host.queried_sound == 1 && !update.advance);
    host.fail = 1;
    CHECK(!mm_polled_playback_update(&state, 1, 600, 0, &services, &update));
    CHECK(!update.advance && state.wait == MM_PLAYBACK_WAIT_SOUND);
    host.fail = host.sound_pending = 0;
    CHECK(mm_polled_playback_update(&state, 1, 600, 0, &services, &update));
    CHECK(state.wait == MM_PLAYBACK_WAIT_NONE && !update.advance);
    CHECK(mm_polled_playback_update(&state, 1, 170, 0, &services, &update) && update.advance);

    host.wait.kind = MM_PLAYBACK_WAIT_CLICK;
    mm_polled_playback_set_rate(&state, 24);
    CHECK(mm_polled_playback_update(&state, 1, 0, 0, &services, &update));
    CHECK(host.entered == 2 && !host.restored_music);
    CHECK(mm_polled_playback_update(&state, 1, 500, 0, &services, &update));
    CHECK(mm_polled_playback_frame_seconds(&state) == 0 && !update.advance);
    CHECK(mm_polled_playback_update(&state, 1, 500, 1, &services, &update) && !update.advance);
    CHECK(mm_polled_playback_update(&state, 1, 40, 0, &services, &update) && !update.advance);
    CHECK(mm_polled_playback_update(&state, 1, 5, 0, &services, &update) && update.advance);

    host.wait.kind = MM_PLAYBACK_WAIT_TIME;
    host.wait.milliseconds = 50;
    mm_polled_playback_set_rate(&state, 0);
    CHECK(mm_polled_playback_update(&state, 1, 0, 0, &services, &update) && !update.advance);
    CHECK(mm_polled_playback_update(&state, 1, 49, 0, &services, &update) && !update.advance);
    CHECK(state.wait == MM_PLAYBACK_WAIT_TIME);
    CHECK(mm_polled_playback_update(&state, 1, 2, 0, &services, &update) && !update.advance);
    CHECK(state.wait == MM_PLAYBACK_WAIT_NONE);
    CHECK(mm_polled_playback_update(&state, 1, 0, 0, &services, &update) && update.advance);
}

/* This executable links with the C linker and only the C89 backend. Existing
 * engine-core-link and Marcus regressions cover the host adapters and policy
 * branches. These checks exercise the public C contract independently. */
int main(void)
{
    MMPlaybackState state;
    MMPlaybackEvaluator evaluator;
    MMPlaybackCommand command;
    MMPlaybackReply reply;
    MMPlayEvaluator play;
    MMPlayCommand playCommand;
    MMFramePacer pacer;
    MMScrollPlayback scroll;
    MMScrollUpdate update;
    mm_playback_state_init(&state);
    mm_playback_reply_init(&reply);
    CHECK(state.transition == -1 && state.fadeVolume == -1 && !state.playing);
    CHECK(reply.success && reply.position.transition == -1);
    mm_playback_init(&evaluator, &state, 1000);
    CHECK(!mm_playback_complete(&evaluator, &reply));
    CHECK(mm_playback_next(&evaluator, &command));
    CHECK(command.kind == MM_PLAYBACK_PRESENT_FRAME);
    CHECK(!mm_playback_next(&evaluator, &command));
    CHECK(command.kind == MM_PLAYBACK_PRESENT_FRAME);
    /* A host effect changes borrowed state while its command is outstanding. */
    state.wait = MM_PLAYBACK_WAIT_CLICK;
    CHECK(mm_playback_complete(&evaluator, &reply));
    CHECK(!mm_playback_complete(&evaluator, &reply));
    CHECK(mm_playback_next(&evaluator, &command));
    CHECK(command.kind == MM_PLAYBACK_DISPLAY_FRAME);
    CHECK(mm_playback_complete(&evaluator, &reply));
    CHECK(mm_playback_next(&evaluator, &command));
    CHECK(command.kind == MM_PLAYBACK_QUERY_SCROLLING);
    CHECK(mm_playback_complete(&evaluator, &reply));
    CHECK(mm_playback_next(&evaluator, &command));
    CHECK(command.kind == MM_PLAYBACK_SET_CLICK_PAUSE && command.flag);
    CHECK(state.pausing && state.wait == MM_PLAYBACK_WAIT_NONE);
    CHECK(mm_playback_complete(&evaluator, &reply));
    CHECK(mm_playback_next(&evaluator, &command));
    CHECK(command.kind == MM_PLAYBACK_FINISHED);

    mm_playback_state_init(&state);
    mm_play_init(&play, &state);
    CHECK(!mm_play_complete(&play, &reply));
    CHECK(mm_play_next(&play, &playCommand));
    CHECK(playCommand.kind == MM_PLAY_CHECK_SCENE);
    CHECK(!mm_play_next(&play, &playCommand));
    CHECK(mm_play_complete(&play, &reply));
    CHECK(mm_play_next(&play, &playCommand));
    CHECK(playCommand.kind == MM_PLAY_NOTIFY_STOPPED);
    CHECK(mm_play_complete(&play, &reply));
    CHECK(mm_play_next(&play, &playCommand));
    CHECK(playCommand.kind == MM_PLAY_FINISHED);

    CHECK(mm_playback_frame_delay(1000, 1000) == 10);
    CHECK(mm_playback_frame_delay(1000, 1001) == 9);
    CHECK(mm_playback_frame_delay(1000, 1017) == 8);
    CHECK(mm_playback_frame_delay(0xfffffff0UL, 4) == 8);
    CHECK(mm_playback_frame_delay(1, 0) == 9);
    CHECK(mm_playback_frame_delay(50, 0) == 13);
    CHECK(mm_playback_frame_delay(0, 0x80000000UL) == 128849029);
    mm_frame_pacer_init(&pacer);
    CHECK(pacer.fps == -1 && !pacer.pending);
    mm_frame_pacer_reset(&pacer, 24);
    mm_frame_pacer_schedule(&pacer, 0xfffffff0UL);
    CHECK(!mm_frame_pacer_take_due(&pacer, 0xfffffff1UL));
    CHECK(mm_frame_pacer_take_due(&pacer, 25));
    CHECK(!mm_frame_pacer_take_due(&pacer, 25));

    mm_scroll_reset(&scroll);
    update = mm_scroll_update(&scroll, 10000, 1);
    CHECK(!update.step && update.holding);
    update = mm_scroll_update(&scroll, 83, 1);
    CHECK(!update.step && update.holding);
    update = mm_scroll_update(&scroll, 1, 1);
    CHECK(update.step && update.holding);
    update = mm_scroll_update(&scroll, 10000, 0);
    CHECK(!update.step && !update.holding);
    CHECK(mm_scroll_nominal_seconds(0) == 0);
    CHECK(mm_scroll_nominal_seconds(3) == 4.0 / 12);
    check_polled_playback();
    return 0;
}
