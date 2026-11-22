#include "mm/playback.h"
#include "mm/frame_sound.h"
#include "mm/caption_render.h"
#include "mm/movie_timing_snapshot.h"
#include "mm/movie_sound_schedule.h"
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

typedef struct CaptionOwnerFixture
{
    CaptionPresentation calls;
    unsigned char *layer;
    int deny, first_byte;
} CaptionOwnerFixture;

static void *caption_resize(void *context, void *old, MMU32 before, MMU32 bytes, int priority)
{
    (void)before;
    (void)priority;
    return ((CaptionOwnerFixture *)context)->deny ? NULL : realloc(old, bytes);
}

static void caption_release(void *context, void *bytes, MMU32 size)
{
    (void)context;
    (void)size;
    free(bytes);
}

static void caption_read_row(void *context, size_t index, MMCaptionRowInput *out)
{
    const MMCaptionRowInput *rows = (const MMCaptionRowInput *)context;
    *out = rows[index];
}

static void caption_read_stop(void *context, size_t index, MMCaptionCaretStop *out)
{
    const MMCaptionCaretStop *stops = (const MMCaptionCaretStop *)context;
    *out = stops[index];
}

static int caption_paint_row(void *context, size_t row, MMI32 scroll, MMCaptionCanvas *canvas)
{
    CaptionOwnerFixture *fixture = (CaptionOwnerFixture *)context;
    if (!fixture->calls.count)
    {
        fixture->first_byte = canvas->rgba[0];
        fixture->layer = canvas->rgba;
        canvas->rgba[0] = 23;
    }
    return caption_draw(&fixture->calls, row, scroll);
}

static void check_caption_owner(void)
{
    unsigned char rgba[80 * 46 * 4];
    MMCaptionOutput output = {0};
    MMPreviewProjection camera = {0};
    MMCaptionPresentation owner;
    CaptionOwnerFixture fixture = {0};
    MMStorageAllocator allocator = {NULL, caption_resize, caption_release};
    MMCaptionRowInput rows[] = {
        {3, 10, {5, 5, 60, 40}, 17, 0}, {6, 12, {20, 20, 60, 40}, 23, 1}
    };
    MMCaptionCaretStop stops[] = {{1, 10, 5, 12}, {1, 20, 70, 12}};
    MMU32 id, hidden = 23;
    unsigned char *layer;
    int needs_scroll;
    allocator.context = &fixture;
    CHECK(mm_caption_presentation_init(&owner, &allocator));
    CHECK(mm_caption_presentation_load(&owner, 2, caption_read_row, rows));
    rows[0].chunk = 50;
    mm_caption_presentation_observe(&owner, 7);
    CHECK(mm_caption_presentation_hit(&owner, 25, 25, &hidden, &id) && id == 17);
    fixture.deny = 1;
    CHECK(!mm_caption_presentation_load(&owner, 4096, caption_read_row, rows));
    CHECK(mm_caption_presentation_hit(&owner, 25, 25, NULL, &id) && id == 23);
    CHECK(mm_caption_presentation_measure(&owner, 3, &needs_scroll, &fixture.calls, caption_height) && needs_scroll);
    mm_caption_presentation_scroll(&owner, 3);
    mm_caption_presentation_draft(&owner, 90, 100, 0, 40);
    CHECK(owner.draft_scroll == 70);
    CHECK(mm_caption_presentation_caret(&owner, 1, 100, 0, 40, 2, stops, caption_read_stop) &&
          owner.draft_scroll == 65);
    CHECK(mm_caption_presentation_caret(&owner, -1, 100, 0, 40, 2, stops, caption_read_stop) && !owner.draft_scroll);
    CHECK(mm_caption_layout_width(&rows[0].bounds, 65536) == 27 &&
          !mm_caption_layout_width(&rows[0].bounds, 65537));

    memset(rgba, 0, sizeof(rgba));
    output.rgba = rgba;
    output.rgba_bytes = sizeof(rgba);
    output.width = 80;
    output.height = 46;
    output.camera = &camera;
    output.authored = 1;
    fixture.calls.count = 0;
    CHECK(mm_caption_presentation_paint(&owner, 7, &hidden, 1, &output, &fixture, caption_paint_row) ==
          MM_CAPTION_PAINT_OK);
    CHECK(fixture.calls.count == 2 && fixture.calls.rows[0] == 1 && fixture.calls.scrolls[0] == 1 &&
          fixture.calls.rows[1] == 0 && fixture.layer == rgba);
    output.authored = 0;
    camera.valid = 1;
    fixture.calls.count = 0;
    CHECK(mm_caption_presentation_paint(&owner, 7, NULL, 0, &output, &fixture, caption_paint_row) ==
          MM_CAPTION_PAINT_MEMORY && !fixture.calls.count);
    fixture.deny = 0;
    CHECK(mm_caption_presentation_paint(&owner, 7, NULL, 0, &output, &fixture, caption_paint_row) ==
          MM_CAPTION_PAINT_OK && fixture.layer != rgba && !fixture.first_byte);
    layer = fixture.layer;
    fixture.deny = 1;
    fixture.calls.count = 0;
    fixture.calls.fail_at = 2;
    CHECK(mm_caption_presentation_paint(&owner, 7, NULL, 0, &output, &fixture, caption_paint_row) ==
          MM_CAPTION_PAINT_FAILED && fixture.layer == layer && !fixture.first_byte);
    mm_caption_presentation_rewind(&owner);
    mm_caption_presentation_clear(&owner);
    CHECK(!mm_caption_presentation_hit(&owner, 25, 25, NULL, &id));
    mm_caption_presentation_dispose(&owner);
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
    MMMovieTimingReader reader = {NULL, timing_read, timing_at_end, timing_expected, timing_received, NULL};
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

static void check_sound_schedule_sources(void)
{
    MMMovieTimingCue cues[2] = {{0}, {0}};
    MMMovieTimingScroll scroll = {2, .5};
    MMMovieTimingScene source = {0}, stored;
    MMMovieTimingRange range = {7, 1, 6};
    MMMovieSoundPosition position;
    MMMovieSoundSheet sheet;
    MMMovieSoundSchedule *schedule, *reframed = NULL;
    MMScheduledSound sound;
    MMScheduledResume resume[1];
    double start, end, duration, elapsed;
    size_t count;
    schedule = mm_movie_sound_schedule_create(.25);
    CHECK(schedule != NULL);
    source.range = range;
    source.range.last = 3;
    source.transition = .5;
    source.scroll_count = 1;
    source.scrolls = &scroll;
    source.cue_count = 2;
    source.cues = cues;
    cues[0].frame = 1;
    cues[0].seconds = 2.0;
    cues[0].sound_number = 3;
    cues[0].audio_kind = MM_SOUND_CLIP_SAMPLED;
    cues[1] = cues[0];
    cues[1].frame = 6;
    cues[1].sound_number = 17;
    CHECK(mm_movie_sound_schedule_append(schedule, &source));
    CHECK(mm_movie_sound_schedule_time_at(schedule, 0, 4) == 1.25);
    CHECK(mm_movie_sound_schedule_sound_count(schedule, 0) == 1);
    cues[0].seconds = 0;
    cues[1].sound_number = 999;
    scroll.seconds = 0;
    mm_movie_sound_schedule_source(schedule, 0, &stored);
    CHECK(stored.cues != cues && stored.cues[0].seconds == 2.0 && stored.cues[1].sound_number == 17 &&
          stored.scrolls[0].seconds == .5);
    memset(&source, 0, sizeof(source));
    source.range.chunk = 8;
    source.range.last = 2;
    CHECK(mm_movie_sound_schedule_append(schedule, &source) && mm_movie_sound_schedule_size(schedule) == 2);
    CHECK(mm_movie_sound_schedule_time_at(schedule, 1, 0) == 1.75);
    mm_movie_sound_schedule_scene_time(schedule, 1, &start, &end);
    CHECK(start == 1.25 && end == 2.5);
    mm_movie_sound_schedule_movie_time(schedule, 1, 0, .125, 0, &duration, &elapsed);
    CHECK(duration == 2.5 && elapsed == 1.875);
    mm_movie_sound_schedule_movie_time(schedule, 1, 0, 1, 0, &duration, &elapsed);
    CHECK(elapsed == 2);
    mm_movie_sound_schedule_movie_time(schedule, 1, 0, 0, 1, &duration, &elapsed);
    CHECK(elapsed == duration);
    CHECK(mm_movie_sound_schedule_locate(schedule, -1, &position) && position.scene == 0 &&
          position.frame == 1 && !position.finished);
    CHECK(mm_movie_sound_schedule_locate(schedule, 1.25, &position) && position.scene == 1 &&
          position.frame == 0 && !position.finished);
    CHECK(mm_movie_sound_schedule_locate(schedule, 2.5, &position) && position.scene == 1 &&
          position.frame == 2 && position.finished);
    CHECK(mm_movie_sound_schedule_sheet(schedule, 1, &sheet) && sheet.track_count == 1 && sheet.cue_count == 1);
    CHECK(sheet.cues[0].track == 0 && sheet.cues[0].carried && sheet.cues[0].origin_scene == 0 &&
          sheet.cues[0].frame == 0 && sheet.cues[0].origin_frame == 1 &&
          sheet.cues[0].timeline_frame == -7 && sheet.cues[0].end_frame == 1);
    mm_movie_sound_sheet_dispose(&sheet);
    CHECK(mm_movie_sound_schedule_resume(schedule, 1, 0, resume, &count) && count == 1 &&
          resume[0].scene == 0 && resume[0].source_row == 0 && resume[0].seconds == 1.75);
    ++range.chunk;
    CHECK(mm_movie_sound_schedule_reframe(schedule, &range, 1, &reframed) == MM_SOUND_REFRAME_MISMATCH &&
          reframed == NULL && mm_movie_sound_schedule_size(schedule) == 2);
    --range.chunk;
    CHECK(mm_movie_sound_schedule_reframe(schedule, &range, 1, &reframed) == MM_SOUND_REFRAME_OK);
    mm_movie_sound_schedule_destroy(schedule);
    CHECK(mm_movie_sound_schedule_size(reframed) == 1 && mm_movie_sound_schedule_sound_count(reframed, 0) == 2);
    sound = mm_movie_sound_schedule_sound(reframed, 0, 1);
    CHECK(sound.scene == 0 && sound.source_row == 1 && sound.start == 1.75);
    mm_movie_sound_schedule_source(reframed, 0, &stored);
    CHECK(stored.range.last == 6 && stored.cues[1].sound_number == 17);
    mm_movie_sound_schedule_destroy(reframed);
}

static void check_sound_sheet_order(void)
{
    static const MMI32 actors[] = {0, 99, -4, 0, 33, 16};
    static const MMI32 styles[] = {2, 2, 2, 1, 2, 1};
    static const size_t order[] = {4, 1, 2, 3, 1, 0};
    MMMovieTimingCue cues[6] = {{0}};
    MMMovieTimingScene source = {0};
    MMMovieSoundSheet sheet;
    MMMovieSoundSchedule *schedule = mm_movie_sound_schedule_create(.25);
    size_t i;
    CHECK(schedule != NULL);
    source.range.first = -2;
    source.range.last = 4;
    source.cue_count = 6;
    source.cues = cues;
    for (i = 0; i < 6; ++i)
    {
        cues[i].frame = -1;
        cues[i].actor = actors[i];
        cues[i].style = styles[i];
        cues[i].flags = i == 0 || i == 2 || i == 3 ? MM_TIMING_ACTOR_SOUND : 0;
    }
    CHECK(mm_movie_sound_schedule_append(schedule, &source));
    CHECK(mm_movie_sound_schedule_sheet(schedule, 0, &sheet) && sheet.track_count == 5 && sheet.cue_count == 6);
    CHECK(!sheet.tracks[0].actor_sound && !sheet.tracks[0].actor && sheet.tracks[0].style == 1);
    CHECK(!sheet.tracks[1].actor_sound && !sheet.tracks[1].actor && sheet.tracks[1].cue_count == 2);
    CHECK(sheet.tracks[2].actor_sound && sheet.tracks[2].actor == -4 && sheet.tracks[2].style == 2);
    CHECK(sheet.tracks[3].actor_sound && !sheet.tracks[3].actor && sheet.tracks[3].style == 1);
    CHECK(sheet.tracks[4].actor_sound && !sheet.tracks[4].actor && sheet.tracks[4].style == 2);
    for (i = 0; i < sheet.cue_count; ++i)
        CHECK(sheet.cues[i].track == order[i] && !sheet.cues[i].carried && sheet.cues[i].origin_frame == 2);
    mm_movie_sound_sheet_dispose(&sheet);
    mm_movie_sound_schedule_destroy(schedule);
}

enum { ALARM_WALL = 64, ALARM_CLOCK, ALARM_START, ALARM_HALT, ALARM_SET,
       ALARM_TRACKING, ALARM_PAUSE_AUDIO, ALARM_RESUME_AUDIO, ALARM_FADES, ALARM_VOLUME };
typedef struct AlarmFixture
{
    MMPlaybackState state;
    MMAlarmPlayback clock;
    MMPlaybackU32 now, clock_start;
    MMPlaybackI32 delay, volume;
    int adjust, alarm_ok, tracking, fades_disabled, rearm_failed;
    int effects[32];
    size_t count;
} AlarmFixture;
static void alarm_record(AlarmFixture *f, int event)
{
    CHECK(f->count < sizeof(f->effects) / sizeof(f->effects[0]));
    f->effects[f->count++] = event;
}
static MMPlaybackU32 alarm_wall(void *p)
{
    AlarmFixture *f = p;
    alarm_record(f, ALARM_WALL);
    return f->now;
}
static MMPlaybackU32 alarm_clock(void *p) { alarm_record(p, ALARM_CLOCK); return 17; }
static void alarm_start(void *p, MMPlaybackU32 time)
{
    AlarmFixture *f = p;
    alarm_record(f, ALARM_START);
    f->clock_start = time;
}
static void alarm_halt(void *p) { alarm_record(p, ALARM_HALT); }
static int alarm_set(void *p, MMPlaybackI32 delay, int adjust)
{
    AlarmFixture *f = p;
    alarm_record(f, ALARM_SET);
    f->delay = delay;
    f->adjust = adjust;
    return f->alarm_ok;
}
static int alarm_tracking(void *p)
{
    AlarmFixture *f = p;
    alarm_record(f, ALARM_TRACKING);
    return f->tracking;
}
static void alarm_pause_audio(void *p, int paused)
{
    AlarmFixture *f = p;
    CHECK(f->clock.paused == paused);
    alarm_record(f, paused ? ALARM_PAUSE_AUDIO : ALARM_RESUME_AUDIO);
}
static int alarm_fades(void *p)
{
    AlarmFixture *f = p;
    alarm_record(f, ALARM_FADES);
    return f->fades_disabled;
}
static void alarm_volume(void *p, MMPlaybackI32 volume)
{
    AlarmFixture *f = p;
    alarm_record(f, ALARM_VOLUME);
    f->volume = volume;
}
static void alarm_effect(void *p, MMAlarmPlaybackEffect effect, int value)
{
    AlarmFixture *f = p;
    alarm_record(f, effect);
    if (effect == MM_ALARM_PLAYING_NOTICE)
        CHECK(f->state.playing == value);
    if (effect == MM_ALARM_RESET_SCENE || effect == MM_ALARM_STOP_SCENE)
        CHECK(value == f->rearm_failed && !f->state.playing && !f->state.stopRequested &&
              !f->state.wait && !f->state.scrolling);
}
static void alarm_expect(AlarmFixture *f, const int *effects, size_t count)
{
    CHECK(f->count == count && !memcmp(f->effects, effects, count * sizeof(*effects)));
    f->count = 0;
}
#define ALARM_EXPECT(f, sequence) alarm_expect((f), (sequence), sizeof(sequence) / sizeof((sequence)[0]))
static void check_alarm_playback(void)
{
    const int start[] = {ALARM_START, MM_ALARM_PLAYING_NOTICE, ALARM_SET};
    const int set[] = {ALARM_SET};
    const int pause[] = {ALARM_WALL, ALARM_CLOCK, ALARM_HALT, ALARM_PAUSE_AUDIO, MM_ALARM_INVALIDATE_VIEWS};
    const int resume[] = {ALARM_WALL, ALARM_RESUME_AUDIO, ALARM_START, ALARM_SET, MM_ALARM_INVALIDATE_VIEWS};
    const int due[] = {ALARM_WALL, MM_ALARM_QUEUE_RENDER};
    const int stop[] = {MM_ALARM_ENABLE_ACCELERATORS, ALARM_HALT, MM_ALARM_REMOVE, MM_ALARM_CLEAR_PLAYING,
        ALARM_WALL, ALARM_RESUME_AUDIO, MM_ALARM_INVALIDATE_VIEWS, MM_ALARM_CLEAR_CLICK_PAUSE,
        MM_ALARM_RESET_SCENE, MM_ALARM_REFRESH_VIEWS, MM_ALARM_STOP_SCENE, MM_ALARM_PLAYING_NOTICE,
        MM_ALARM_SHORT_SOUNDS, MM_ALARM_NOTIFY_STOPPED, MM_ALARM_STOP_AUDIO, ALARM_VOLUME};
    const int retry_failed[] = {ALARM_TRACKING, ALARM_SET, MM_ALARM_ENABLE_ACCELERATORS, MM_ALARM_CLEAR_PLAYING,
        MM_ALARM_REMOVE, MM_ALARM_CLEAR_CLICK_PAUSE, MM_ALARM_RESET_SCENE, MM_ALARM_REFRESH_VIEWS,
        MM_ALARM_STOP_SCENE, MM_ALARM_PLAYING_NOTICE, MM_ALARM_SHORT_SOUNDS, MM_ALARM_NOTIFY_STOPPED,
        MM_ALARM_STOP_AUDIO};
    AlarmFixture f = {0};
    MMAlarmPlaybackServices s = {NULL, alarm_wall, alarm_clock, alarm_start, alarm_halt, alarm_set,
        alarm_tracking, alarm_pause_audio, alarm_fades, alarm_volume, alarm_effect};
    s.context = &f;
    f.alarm_ok = 1;
    mm_playback_state_init(&f.state);
    mm_alarm_playback_init(&f.clock);
    mm_alarm_playback_set_paused(&f.clock, &f.state, 1, &s);
    CHECK(!f.clock.paused && !f.count && !mm_alarm_playback_stop_frozen(&f.clock, &f.state));
    CHECK(mm_alarm_playback_start(&f.clock, &f.state, 12, &s) && f.state.playing && !f.clock_start);
    ALARM_EXPECT(&f, start);
    CHECK(mm_alarm_playback_schedule(&f.clock, &f.state, 1000, 10, 1, &s));
    CHECK(!f.count && f.clock.pacer.pending && f.clock.pacer.due == 1083);
    CHECK(mm_alarm_playback_schedule(&f.clock, &f.state, 1000, 15, 0, &s) && f.delay == 15 && !f.adjust);
    ALARM_EXPECT(&f, set);
    f.state.wait = MM_PLAYBACK_WAIT_SOUND;
    f.now = 1020;
    mm_alarm_playback_set_paused(&f.clock, &f.state, 1, &s);
    mm_alarm_playback_set_paused(&f.clock, &f.state, 1, &s);
    mm_alarm_playback_idle(&f.clock, &f.state, 1, &s);
    mm_alarm_playback_alarm(&f.clock, &f.state, &s);
    CHECK(f.clock.resume_render && f.clock.paused_clock == 17 && f.state.wait == MM_PLAYBACK_WAIT_SOUND);
    ALARM_EXPECT(&f, pause);
    f.now = 1520;
    f.alarm_ok = 0;
    mm_alarm_playback_set_paused(&f.clock, &f.state, 0, &s);
    CHECK(f.clock_start == 17 && !f.clock.paused && !f.clock.resume_render && f.state.stopRequested &&
          f.clock.pacer.due == 1583);
    ALARM_EXPECT(&f, resume);
    f.state.stopRequested = 0;
    f.now = 1583;
    mm_alarm_playback_idle(&f.clock, &f.state, 1, &s);
    CHECK(!f.clock.pacer.pending && !f.state.idleSeen);
    ALARM_EXPECT(&f, due);

    f.state.fadeOriginalVolume = 900;
    f.state.fadeChangedVolume = 1;
    f.state.oldSoundsEnabled = 1;
    f.clock.pacer.pending = 1;
    mm_alarm_playback_set_paused(&f.clock, &f.state, 1, &s);
    ALARM_EXPECT(&f, pause);
    CHECK(mm_alarm_playback_stop_frozen(&f.clock, &f.state) && !mm_alarm_playback_defer_render(&f.clock, &f.state));
    mm_alarm_playback_stop(&f.clock, &f.state, &s);
    CHECK(!f.clock.pacer.pending && !f.state.fadeOriginalVolume && !f.state.fadeChangedVolume &&
          f.state.fadeVolume == -1 && f.state.soundsEnabled && f.volume == 900);
    ALARM_EXPECT(&f, stop);

    f.rearm_failed = f.state.playing = f.state.scrolling = f.clock.pacer.pending = 1;
    f.state.fadeOriginalVolume = 900;
    f.state.fadeVolume = 50;
    mm_alarm_playback_alarm(&f.clock, &f.state, &s);
    CHECK(f.clock.pacer.pending && f.state.fadeOriginalVolume == 900 && f.state.fadeVolume == 50);
    ALARM_EXPECT(&f, retry_failed);
}

typedef struct SoundOutputFixture
{
    MMFrameSoundQueue queue;
    char events[32];
    size_t count;
    int refs, alarm_ok, clock_ok, playing_class;
    MMI32 interval;
} SoundOutputFixture;
static void output_record(SoundOutputFixture *f, char event)
{
    CHECK(f->count + 1 < sizeof(f->events));
    f->events[f->count++] = event;
    f->events[f->count] = '\0';
}
static void output_expect(SoundOutputFixture *f, const char *events)
{
    CHECK(!strcmp(f->events, events));
    f->events[0] = '\0';
    f->count = 0;
}
static void *output_create_clock(void *p)
{
    SoundOutputFixture *f = p;
    CHECK(f->queue.initialized);
    output_record(f, 'c');
    return f->clock_ok ? p : NULL;
}
static void output_release_clock(void *p, void *clock)
{
    SoundOutputFixture *f = p;
    CHECK(clock == p && f->refs == 1 && !f->queue.count && f->queue.initialized);
    output_record(f, 'd');
}
static void output_start_clock(void *p, void *clock) { CHECK(p == clock); output_record(p, 's'); }
static void output_stop_clock(void *p, void *clock) { CHECK(p == clock); output_record(p, 't'); }
static int output_set_alarm(void *p, void *clock, MMI32 interval)
{
    SoundOutputFixture *f = p;
    CHECK(clock == p);
    f->interval = interval;
    output_record(f, 'a');
    return f->alarm_ok;
}
static void output_begin(void *p) { output_record(p, 'B'); }
static void output_end(void *p) { output_record(p, 'E'); }
static void output_play(void *p, const MMFrameSoundRequest *request)
{
    SoundOutputFixture *f = p;
    CHECK(request->sound == p && f->refs > 1 && request->offset == 15 && request->queued);
    output_record(f, (char)('i' + request->object));
}
static void output_stop_class(void *p, MMI32 id) { output_record(p, (char)('0' + id)); }
static int output_playing_class(void *p, MMI32 id)
{
    SoundOutputFixture *f = p;
    output_record(f, (char)('X' + id));
    return f->playing_class == id;
}
static void output_retain(void *p) { ++((SoundOutputFixture *)p)->refs; }
static void output_release(void *p)
{
    SoundOutputFixture *f = p;
    CHECK(f->refs > 1);
    --f->refs;
    output_record(f, 'r');
}
static void check_sound_output(void)
{
    SoundOutputFixture f = {0};
    MMFrameSoundRequest request = {NULL, 2, 0, 500, 6, 15, 0, 1, 1};
    MMFrameSoundOutput s = {NULL, output_create_clock, output_release_clock, output_start_clock,
        output_stop_clock, output_set_alarm, output_begin, output_end, output_play, output_stop_class,
        output_playing_class};
    s.context = request.sound = &f;
    f.refs = f.alarm_ok = 1;
    mm_frame_sound_queue_prepare(&f.queue);
    CHECK(!mm_frame_sound_queue_open(&f.queue, NULL, output_retain, output_release, 0, &s));
    CHECK(f.queue.initialized && !f.queue.clock);
    mm_frame_sound_queue_close(&f.queue, &s);
    CHECK(!f.queue.initialized);
    output_expect(&f, "c012");
    f.clock_ok = 1;
    CHECK(mm_frame_sound_queue_open(&f.queue, NULL, output_retain, output_release, 0, &s));
    CHECK(mm_frame_sound_queue_open(&f.queue, NULL, output_retain, output_release, 0, &s));
    mm_frame_sound_queue_play(&f.queue, &s);
    output_expect(&f, "c");

    CHECK(mm_frame_sound_queue_enqueue(&f.queue, &request, 0));
    request.object = 1;
    CHECK(mm_frame_sound_queue_enqueue(&f.queue, &request, 1) && f.refs == 3);
    mm_frame_sound_queue_play(&f.queue, &s);
    CHECK(f.refs == 1 && !f.queue.count && f.interval == MM_FRAME_SOUND_SHORT);
    output_expect(&f, "BjrirEsa");

    f.queue.duration = MM_FRAME_SOUND_LONG;
    CHECK(mm_frame_sound_queue_enqueue(&f.queue, &request, 0));
    mm_frame_sound_queue_play(&f.queue, &s);
    output_expect(&f, "BjrE");
    CHECK(mm_frame_sound_queue_enqueue(&f.queue, &request, 0));
    f.queue.duration = MM_FRAME_SOUND_OFF;
    mm_frame_sound_queue_play(&f.queue, &s);
    output_expect(&f, "r");

    f.queue.duration = 37;
    f.alarm_ok = 0;
    CHECK(mm_frame_sound_queue_enqueue(&f.queue, &request, 0));
    mm_frame_sound_queue_play(&f.queue, &s);
    CHECK(f.refs == 1 && f.interval == 37);
    output_expect(&f, "BjrEsat012");
    mm_frame_sound_queue_alarm(&f.queue, &s);
    output_expect(&f, "tt012");
    f.playing_class = 2;
    CHECK(mm_frame_sound_playing(1, &s));
    output_expect(&f, "Z");
    f.playing_class = 1;
    CHECK(mm_frame_sound_playing(1, &s) && !mm_frame_sound_playing(0, &s));
    output_expect(&f, "ZYX");
    CHECK(mm_frame_sound_queue_enqueue(&f.queue, &request, 0));
    mm_frame_sound_queue_close(&f.queue, &s);
    CHECK(f.refs == 1 && !f.queue.initialized && !f.queue.clock);
    output_expect(&f, "t012rd");
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
    check_alarm_playback();
    check_frame_sound();
    check_sound_output();
    check_caption_render();
    check_caption_owner();
    check_timing_snapshot();
    check_sound_schedule_sources();
    check_sound_sheet_order();
    return 0;
}
