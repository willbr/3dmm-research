#include "mm/playback.h"
#include "mm/frame_sound.h"
#include "mm/caption_render.h"
#include "mm/movie_timing_snapshot.h"
#include "mm/graphics_transition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct PolledMovieHost
{
    MMPolledMovie movie;
    MMPolledPlayback clock;
    MMPlaybackPosition position;
    char trace[16];
    MMI32 values[16];
    int count, polls, fail_poll, advance, fail_effect;
} PolledMovieHost;

static void movie_trace(PolledMovieHost *host, char step, MMI32 value)
{
    CHECK(host->count < (int)sizeof(host->trace) - 1);
    host->values[host->count] = value;
    host->trace[host->count++] = step;
    host->trace[host->count] = 0;
}

static int movie_poll(void *context, int playing, double elapsed, int click, int *advance)
{
    PolledMovieHost *host = (PolledMovieHost *)context;
    movie_trace(host, 'P', playing);
    CHECK(elapsed == (host->polls ? 0 : 200));
    CHECK(click == !host->polls);
    *advance = !host->polls && host->advance;
    return ++host->polls != host->fail_poll;
}

static void movie_position(void *context, MMPlaybackPosition *position, MMI32 *first_frame)
{
    PolledMovieHost *host = (PolledMovieHost *)context;
    movie_trace(host, '?', 0);
    *position = host->position;
    *first_frame = -7;
}

static int movie_effect(void *context, MMPolledMovieEffect effect, MMI32 value)
{
    static const char steps[] = "CAFRBSTX";
    PolledMovieHost *host = (PolledMovieHost *)context;
    movie_trace(host, steps[effect], value);
    if (host->fail_effect == steps[effect])
        return 0;
    switch (effect)
    {
    case MM_POLLED_MOVIE_SET_FRAME:
        host->clock.wait = MM_PLAYBACK_WAIT_CLICK;
        break;
    case MM_POLLED_MOVIE_REWIND:
        mm_polled_movie_reset(&host->movie, 0);
        break;
    case MM_POLLED_MOVIE_SELECT_SCENE:
        mm_polled_movie_reset(&host->movie, 1);
        break;
    case MM_POLLED_MOVIE_START_TRANSITION:
        mm_polled_playback_begin_transition(&host->clock, value);
        break;
    case MM_POLLED_MOVIE_STOP:
        mm_polled_playback_reset(&host->clock, -1, 0);
        break;
    default:
        break;
    }
    return 1;
}

static int run_polled_movie(PolledMovieHost *host, int playing, int loop, MMPolledMovieResult *result)
{
    MMPolledMovieServices services = {NULL, movie_poll, movie_position, movie_effect};
    services.context = host;
    host->trace[0] = 0;
    host->count = host->polls = 0;
    return mm_polled_movie_update(&host->movie, &host->clock, host->position.scene, playing, loop, 200, -1,
                                  &services, result);
}

static void check_polled_movie(void)
{
    PolledMovieHost host;
    MMPolledMovieResult result;
    memset(&host, 0, sizeof(host));
    host.position.sceneCount = 2;
    host.position.frame = 1;
    host.position.lastFrame = 2;
    host.position.transition = MM_GRAPHICS_MOVIE_DISSOLVE;
    mm_polled_movie_reset(&host.movie, 0);
    mm_polled_playback_reset(&host.clock, -1, 0);
    host.clock.scroll.holding = 1;
    CHECK(run_polled_movie(&host, 0, 0, &result) == 1 && !strcmp(host.trace, "P"));
    CHECK(!result.playing && !result.changed && result.scrolling && host.movie.restore_audio);
    CHECK(run_polled_movie(&host, -1, 0, &result) == 1 && !strcmp(host.trace, "CAP"));
    CHECK(result.playing && result.changed && result.scrolling && !host.movie.restore_audio);
    host.clock.scroll.holding = 0;
    host.advance = 1;
    CHECK(run_polled_movie(&host, 1, 1, &result) == 1 && !strcmp(host.trace, "CP?FP"));
    CHECK(host.values[3] == 2 && result.changed && result.wait == MM_PLAYBACK_WAIT_CLICK);
    host.position.frame = 2;
    CHECK(run_polled_movie(&host, 1, 1, &result) == 1 && !strcmp(host.trace, "CP?R"));
    CHECK(host.values[3] == -7 && host.movie.restore_audio && !result.scene_changed);
    CHECK(run_polled_movie(&host, 1, 0, &result) == 1 && !strcmp(host.trace, "CAP?BST"));
    CHECK(host.values[4] == 0 && host.values[5] == 1 && host.values[6] == MM_GRAPHICS_MOVIE_DISSOLVE);
    CHECK(result.changed && result.scene_changed && result.transition && !host.movie.restore_audio);
    host.fail_effect = 'S';
    CHECK(!run_polled_movie(&host, 1, 0, &result) && !strcmp(host.trace, "CP?BSX"));
    CHECK(!result.playing && !result.changed && !result.scene_changed && !result.wait &&
          !result.scrolling && !result.transition && host.movie.restore_audio && !host.movie.finished);
    host.fail_effect = 0;
    host.position.frame = 1;
    host.fail_poll = 2;
    CHECK(!run_polled_movie(&host, 1, 0, &result) && !strcmp(host.trace, "CAP?FPX"));
    CHECK(host.movie.restore_audio && !result.playing && !result.changed);
    host.fail_poll = 0;
    host.position.frame = 2;
    host.position.scene = 1;
    CHECK(run_polled_movie(&host, 1, 0, &result) == 1 && !strcmp(host.trace, "CAP?X"));
    CHECK(!result.playing && result.changed && host.movie.finished && !host.movie.restore_audio);
    CHECK(run_polled_movie(&host, 1, 0, NULL) == MM_POLLED_MOVIE_INVALID && !strcmp(host.trace, "X"));
    CHECK(host.movie.finished && host.movie.restore_audio);
}

typedef struct FrameSoundHost
{
    size_t played_count, fail_at;
    struct
    {
        int actor;
        size_t index;
        MMAudioCounter offset;
    } played[16];
    int actor_reads, pause_reads, fail_actor_read;
} FrameSoundHost;

static const MMFrameSoundCue scene_cues[] = {
    {-8, 4, 0, 0, 0}, {7, 4, 0, 0, 0}, {6, 2, 0, 0, 0}, {9, 2, 0, 0, 0},
    {7, 4, 0, 1, 0}, {11, 4, 0, 0, 0}, {9, 4, 0, 0, 0}, {3, 4, 0, 0, 0}
};
static const MMFrameSoundCue actor_cues[] = {
    {9, 2, 12, 0, 1}, {9, 3, 12, 0, 1}, {9, 2, 12, 1, 0}, {9, 2, 15, 0, 1}, {9, 3, 15, 0, 0}
};

static int frame_actor_cues(void *p, size_t *count)
{
    FrameSoundHost *host = (FrameSoundHost *)p;
    ++host->actor_reads;
    *count = sizeof(actor_cues) / sizeof(*actor_cues);
    return !host->fail_actor_read;
}

static void frame_cue(void *p, int actor, size_t i, MMFrameSoundCue *out)
{
    (void)p;
    *out = actor ? actor_cues[i] : scene_cues[i];
}

static int play_cue(void *p, int actor, size_t i, MMAudioCounter offset)
{
    FrameSoundHost *host = (FrameSoundHost *)p;
    size_t at = host->played_count++;
    CHECK(at < sizeof(host->played) / sizeof(*host->played));
    host->played[at].actor = actor;
    host->played[at].index = i;
    host->played[at].offset = offset;
    return host->played_count != host->fail_at;
}

static void frame_pause(void *p, size_t i, MMI32 *frame, MMPolledFrameWait *out)
{
    FrameSoundHost *host = (FrameSoundHost *)p;
    ++host->pause_reads;
    *frame = i == 1 ? 8 : 9;
    out->kind = i != 2 ? MM_PLAYBACK_WAIT_TIME : MM_PLAYBACK_WAIT_CLICK;
    out->milliseconds = i != 2 ? 375 : 0;
}

static void check_frame_sound(void)
{
    FrameSoundHost host = {0};
    const FrameSoundHost empty = {0};
    MMFrameSoundServices services = {
        NULL, sizeof(scene_cues) / sizeof(*scene_cues), 3, frame_actor_cues, frame_cue, play_cue, frame_pause};
    MMPolledFrameWait wait = {MM_PLAYBACK_WAIT_NONE, 0};
    MMAudioCounter offset;
    size_t i;
    static const size_t expected[] = {1, 4, 3, 6, 1, 2, 3, 4};
    services.context = &host;
    CHECK(mm_frame_sound_enter(9, 1, 12, &services, &wait));
    CHECK(host.played_count == sizeof(expected) / sizeof(*expected));
    CHECK(host.actor_reads == 1 && host.pause_reads == 3);
    CHECK(wait.kind == MM_PLAYBACK_WAIT_CLICK && wait.milliseconds == 0);
    for (i = 0; i < host.played_count; ++i)
    {
        CHECK(host.played[i].actor == (i >= 4) && host.played[i].index == expected[i]);
        CHECK(host.played[i].offset.low == (i == 0 ? 8000U : 0U) && host.played[i].offset.high == 0);
    }
    host = empty;
    host.fail_at = 3;
    wait.kind = MM_PLAYBACK_WAIT_SOUND;
    CHECK(!mm_frame_sound_enter(9, 1, 0, &services, &wait));
    CHECK(host.played_count == 3 && host.played[0].offset.low == 16000);
    CHECK(host.actor_reads == 0 && host.pause_reads == 0 && wait.kind == MM_PLAYBACK_WAIT_SOUND);
    host = empty;
    host.fail_actor_read = 1;
    CHECK(!mm_frame_sound_enter(9, 0, -1, &services, &wait));
    CHECK(host.played_count == 2 && host.played[0].index == 3 && host.pause_reads == 0);
    host = empty;
    host.fail_at = 4;
    CHECK(!mm_frame_sound_enter(9, 0, -1, &services, &wait));
    CHECK(host.played_count == 4 && host.played[3].index == 2 && host.pause_reads == 0);
    offset = mm_frame_sound_resume_samples(100000);
    CHECK(offset.high == 1 && offset.low == 505032704);
    offset = mm_frame_sound_resume_samples(.75 / 48000);
    CHECK(offset.high == 0 && offset.low == 1);
}

typedef struct CaptionPresentation
{
    size_t rows[4], count, fail_at;
    MMI32 scrolls[4];
} CaptionPresentation;

static int caption_draw(void *context, size_t row, MMI32 scroll)
{
    CaptionPresentation *calls = (CaptionPresentation *)context;
    size_t at = calls->count++;
    CHECK(at < 4);
    calls->rows[at] = row;
    calls->scrolls[at] = scroll;
    return calls->count != calls->fail_at;
}

static int caption_height(void *context, size_t row, MMI32 *height)
{
    *height = row == 1 ? 32 : 30;
    return caption_draw(context, row, *height);
}

static void check_caption_render(void)
{
    static unsigned char rgba[MM_CAPTION_WIDTH * MM_CAPTION_HEIGHT * 4];
    static const unsigned char source[] = {40, 80, 120, 160};
    static const unsigned char ink[] = {128};
    MMCaptionRow rows[3] = {
        {0, 12, 0, 0, 0, 0, {0, 0, 40, 40}, 21, 0, 0},
        {4, 7, 2, 0, 0, 0, {0, 0, 40, 40}, 21, 0, 0},
        {3, 10, 0, 0, 0, 0, {0, 0, 40, 40}, 22, 1, 0}
    };
    const MMTextBoxBounds box = {0, 0, 40, 40};
    const MMCaptionMask from = {-1, -1, 2, 2, 2};
    MMCaptionGlyph glyph = {{0, 0, 1, 1, 1}, ink, NULL, 0, 0, 0, 0, 1, 0, 0xff000000UL, 0xffffffffUL};
    MMCaptionStack stack = {0, 0};
    MMCaptionCanvas canvas;
    MMCaptionMask to;
    unsigned char reduced[4];
    unsigned char *pixel;
    MMU32 hit, hidden;
    CaptionPresentation calls = {0};
    int needs;

    mm_caption_observe(&stack, rows, 3, 5);
    CHECK(stack.first == 1 && stack.last == 3 && rows[0].next == 2 && rows[1].next == 3);
    CHECK(mm_caption_hit(&stack, rows, 3, 8, NULL, &hit) && hit == 22);
    hidden = 22;
    CHECK(mm_caption_hit(&stack, rows, 3, 8, &hidden, &hit) && hit == 21);
    CHECK(!mm_caption_hit(&stack, rows, 40, 8, &hidden, &hit) && hit == 21);
    hidden = 21;
    CHECK(mm_caption_draw_stack(&stack, rows, &hidden, 1, 9, &calls, caption_draw));
    CHECK(calls.count == 2 && !calls.rows[0] && calls.scrolls[0] == 9 && calls.rows[1] == 3);
    calls.count = 0;
    CHECK(mm_caption_draw_stack(&stack, rows, NULL, 1, 9, &calls, caption_draw));
    CHECK(calls.count == 4 && calls.rows[0] == 1 && calls.rows[1] == 2 && calls.scrolls[1] == 2 &&
          calls.rows[2] == 3 && !calls.rows[3]);
    calls.count = 0;
    calls.fail_at = 2;
    CHECK(!mm_caption_draw_stack(&stack, rows, NULL, 1, 9, &calls, caption_draw) && calls.count == 2);

    mm_caption_observe(&stack, rows, 3, 8);
    CHECK(stack.first == 1 && stack.last == 3 && rows[0].next == 3 && rows[2].previous == 1);
    mm_caption_observe(&stack, rows, 3, 5);
    CHECK(stack.first == 1 && stack.last == 2 && rows[2].next == 2 && rows[1].previous == 3);
    mm_caption_observe(&stack, rows, 3, 12);
    CHECK(!stack.first && !stack.last);
    mm_caption_reset_scroll(rows, 3);
    CHECK(!rows[1].scroll);
    rows[1].first = rows[2].first = 0;
    calls.count = 0;
    needs = -1;
    CHECK(!mm_caption_measure_scroll(rows, 3, 0, &needs, &calls, caption_height) && needs == -1);
    CHECK(calls.count == 2 && calls.rows[0] == 1 && calls.rows[1] == 2);
    calls.count = calls.fail_at = 0;
    CHECK(mm_caption_measure_scroll(rows, 3, 0, &needs, &calls, caption_height) && needs && calls.count == 2);
    mm_caption_step_scroll(rows, 3, 0);
    CHECK(rows[0].scroll == 1 && !rows[1].scroll && !rows[2].scroll);
    mm_caption_step_scroll(rows, 3, 1);
    CHECK(rows[0].scroll == 1);
    mm_caption_step_scroll(rows, 3, 0);
    mm_caption_step_scroll(rows, 3, 0);
    CHECK(rows[0].scroll == 2);

    CHECK(mm_caption_canvas(&canvas, rgba, sizeof(rgba), MM_CAPTION_WIDTH, MM_CAPTION_HEIGHT,
                            (float)MM_CAPTION_WIDTH / MM_CAPTION_HEIGHT));
    CHECK(mm_caption_box_visible(&box) && mm_caption_scroll_overflow(40, 0, 40) == 10);
    mm_caption_box(&canvas, &box, 0x01200080UL);
    mm_caption_glyph(&canvas, &box, 0, &glyph);
    pixel = rgba + 5 * canvas.stride + 14 * 4;
    CHECK(pixel[0] == (32 * 127 + 223 * 128) / 255);
    CHECK(pixel[1] == 128 && pixel[2] == 127 && pixel[3] == 255);
    CHECK(rgba[4 * canvas.stride + 14 * 4 + 3] == 0);

    mm_caption_mask_half_size(&from, &to);
    CHECK(to.left == -1 && to.top == -1 && to.width == 2 && to.height == 2);
    mm_caption_mask_half(&from, source, &to, reduced);
    CHECK(reduced[0] == 10 && reduced[1] == 20 && reduced[2] == 30 && reduced[3] == 40);
}

typedef struct TimingBytes
{
    unsigned char bytes[157];
    size_t used, offset, scenes;
    MMU32 expected_chunk;
} TimingBytes;

static int timing_write(void *context, const void *bytes, size_t size)
{
    TimingBytes *data = (TimingBytes *)context;
    if (size > sizeof(data->bytes) - data->used)
        return 0;
    memcpy(data->bytes + data->used, bytes, size);
    data->used += size;
    return 1;
}

static int timing_read(void *context, void *bytes, size_t size)
{
    TimingBytes *data = (TimingBytes *)context;
    if (size > data->used - data->offset)
        return 0;
    memcpy(bytes, data->bytes + data->offset, size);
    data->offset += size;
    return 1;
}

static int timing_at_end(void *context)
{
    const TimingBytes *data = (const TimingBytes *)context;
    return data->used == data->offset;
}

static int timing_scene(void *context, size_t index, MMMovieTimingScene *out)
{
    static const MMMovieTimingScroll scrolls[] = {{-3, .5}, {0, .75}};
    static const MMMovieTimingPause pauses[] = {{1, MM_PLAYBACK_WAIT_TIME, 1250}};
    static const MMMovieTimingCue cues[] = {
        {-4, 2, -1, 1, 65536, 0x4d534e44UL, 17, 0x5343454eUL, 9, 5,
         MM_TIMING_ACTOR_SOUND | MM_TIMING_LOOP | MM_TIMING_QUEUE | MM_TIMING_SILENT | MM_TIMING_MOTION,
         -1, 3, 0x57415645UL, 400, -2, 1.5}
    };
    (void)context;
    CHECK(!index);
    out->range.chunk = 0x01020304UL;
    out->range.first = -4;
    out->range.last = 21;
    out->transition = .25;
    out->scroll_count = 2; out->pause_count = 1; out->cue_count = 1;
    out->scrolls = scrolls; out->pauses = pauses; out->cues = cues;
    return 1;
}

static int timing_expected(void *context, size_t index, MMMovieTimingRange *range)
{
    const TimingBytes *data = (const TimingBytes *)context;
    CHECK(!index);
    range->chunk = data->expected_chunk;
    range->first = -4;
    range->last = 21;
    return 1;
}

static int timing_received(void *context, size_t index, const MMMovieTimingScene *scene)
{
    TimingBytes *data = (TimingBytes *)context;
    const MMMovieTimingCue *cue = scene->cues;
    CHECK(!index && scene->range.chunk == data->expected_chunk && scene->transition == .25);
    CHECK(scene->scroll_count == 2 && scene->pause_count == 1 && scene->cue_count == 1);
    CHECK(scene->scrolls[0].frame == -3 && scene->scrolls[0].seconds == .5 &&
          scene->scrolls[1].frame == 0 && scene->scrolls[1].seconds == .75);
    CHECK(scene->pauses[0].frame == 1 && scene->pauses[0].kind == MM_PLAYBACK_WAIT_TIME &&
          scene->pauses[0].milliseconds == 1250);
    CHECK(cue->frame == -4 && cue->source == 2 && cue->actor == -1 && cue->style == 1 && cue->volume == 65536);
    CHECK(cue->sound_type == 0x4d534e44UL && cue->sound_number == 17 &&
          cue->scene_type == 0x5343454eUL && cue->scene_number == 9 && cue->child == 5);
    CHECK(cue->flags == 31 && cue->motion_cel == -1 && cue->audio_kind == 3 && cue->resolved_type == 0x57415645UL &&
          cue->resolved_chunk == 400 && cue->resolved_source == -2 && cue->seconds == 1.5);
    ++data->scenes;
    return 1;
}

static void check_timing_snapshot(void)
{
    TimingBytes data = {0};
    TimingBytes good;
    MMMovieTimingWriter writer = {NULL, timing_write, timing_scene};
    MMMovieTimingReader reader = {NULL, timing_read, timing_at_end, timing_expected, timing_received};
    static const unsigned char header[] = {'M', 'M', 'T', '4', 0, 0, 0, 0, 0, 0, 192, 63, 1, 0, 0, 0,
                                           4, 3, 2, 1, 252, 255, 255, 255, 21, 0, 0, 0};
    double frame_seconds = -1;
    size_t i;
    writer.context = reader.context = &data;
    data.expected_chunk = 0x01020304UL;
    CHECK(mm_movie_timing_snapshot_write(&writer, 1, .125));
    CHECK(data.used == 156 && !memcmp(data.bytes, header, sizeof(header)));
    good = data;
    CHECK(mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && data.scenes == 1 && frame_seconds == .125);
    for (i = 0; i < good.used; ++i)
    {
        data = good;
        data.used = i;
        frame_seconds = -1;
        CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes && frame_seconds == -1);
    }
    data = good;
    ++data.used;
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && frame_seconds == -1);
    data = good;
    ++data.expected_chunk;
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes);
    data = good;
    memcpy(data.bytes + 60, data.bytes + 48, 4);
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes);
    data = good;
    data.bytes[39] = 127;
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes);
    data = good;
    data.bytes[76] = 4;
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes);
    data = good;
    data.bytes[124] = 32;
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes);
    data = good;
    data.bytes[154] = 240;
    data.bytes[155] = 127;
    CHECK(!mm_movie_timing_snapshot_read(&reader, 1, &frame_seconds) && !data.scenes);
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
    check_polled_movie();
    check_frame_sound();
    check_caption_render();
    check_timing_snapshot();
    return 0;
}
