#include "mm/video_playback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, int line)
{
    if (!condition)
    {
        fprintf(stderr, "video playback check failed at line %d\n", line);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(condition) check((condition), __LINE__)

typedef struct Fixture
{
    MMU32 time, sampled;
    MMI32 position, started;
    MMPixelRect invalidated;
    int bound, stops, invalidations, controlled, playing, refuse_start, clock_error;
} Fixture;

static int bind(void *context, int add)
{
    Fixture *host = (Fixture *)context;
    host->bound = add;
    return 1;
}

static int start(void *context, MMI32 frame)
{
    Fixture *host = (Fixture *)context;
    CHECK(host->bound);
    host->started = frame;
    return !host->refuse_start;
}

static int stop(void *context)
{
    ++((Fixture *)context)->stops;
    return 1;
}

static int controls(void *context)
{
    return ((Fixture *)context)->controlled;
}

static int playing(void *context)
{
    return ((Fixture *)context)->playing;
}

static int position(void *context, MMI32 *frame)
{
    *frame = ((Fixture *)context)->position;
    return 1;
}

static int frame_for_time(void *context, MMU32 time, MMI32 *frame)
{
    Fixture *host = (Fixture *)context;
    host->sampled = time;
    *frame = ((MMI32)time - 100) / 10;
    return 1;
}

static int time_for_frame(void *context, MMI32 frame, MMI32 *time)
{
    (void)context;
    *time = 100 + 10 * frame;
    return 1;
}

static int now(void *context, MMU32 *time)
{
    Fixture *host = (Fixture *)context;
    *time = host->time;
    return host->clock_error ? -9 : 1;
}

static int invalidate(void *context, const MMPixelRect *bounds)
{
    Fixture *host = (Fixture *)context;
    host->invalidated = *bounds;
    ++host->invalidations;
    return 1;
}

static void test_stream(void)
{
    Fixture host = {0};
    MMVideoStream stream;
    MMVideoStreamServices services = {0, bind, start, stop, controls, playing, position, frame_for_time,
                                     time_for_frame, now, invalidate};
    MMPixelRect bounds = {4, 5, 11, 20}, empty = {4, 5, 4, 20};
    int stopped;
    services.context = &host;
    mm_video_stream_init(&stream, 12, 10, 4);
    CHECK(stream.frame == 0 && stream.marked == -1 && stream.bounds.right == 12 && stream.bounds.bottom == 10);
    CHECK(mm_video_stream_seek(&stream, &services, 1) > 0 && stream.frame == 1 && host.stops == 1);
    host.time = 0xfffffff0UL;
    CHECK(mm_video_stream_start(&stream, &services, &bounds) == 1);
    CHECK(host.bound && host.started == 1 && stream.origin == 0xffffff82UL);
    CHECK(mm_video_stream_tick(&stream, &services) > 0 && host.sampled == 110);
    CHECK(host.invalidations == 1 && host.invalidated.left == 4 && host.invalidated.bottom == 20);
    CHECK(mm_video_stream_tick(&stream, &services) > 0 && host.invalidations == 1);
    host.time = 16;
    CHECK(mm_video_stream_tick(&stream, &services) > 0 && host.sampled == 142);
    CHECK(stream.frame == 3 && stream.marked == 3 && !host.bound && !stream.playing && host.stops == 3);
    CHECK(host.invalidations == 2);

    CHECK(mm_video_stream_seek(&stream, &services, 1) > 0);
    host.controlled = host.playing = 1;
    host.position = 2;
    CHECK(mm_video_stream_start(&stream, &services, &empty) == 1);
    CHECK(!mm_video_stream_drawable(&stream, 1) && mm_video_stream_drawable(&stream, 0));
    CHECK(mm_video_stream_playing(&stream, &services) == 1);
    CHECK(mm_video_stream_position(&stream, &services) > 0 && stream.frame == 2);
    CHECK(mm_video_stream_tick(&stream, &services) > 0 && host.invalidations == 3);
    host.playing = 0;
    host.position = 6;
    CHECK(mm_video_stream_tick(&stream, &services) > 0);
    CHECK(!stream.playing && !host.bound && stream.marked == 3 && stream.frame == 6 && host.invalidations == 4);

    CHECK(mm_video_stream_seek(&stream, &services, 0) > 0);
    host.refuse_start = 1;
    CHECK(mm_video_stream_start(&stream, &services, NULL) == 0 && !host.bound && !stream.playing);
    host.refuse_start = 0;
    stopped = host.stops;
    host.clock_error = 1;
    CHECK(mm_video_stream_start(&stream, &services, NULL) == -9);
    CHECK(host.stops == stopped + 2 && !host.bound && !stream.playing);
    services.stop = NULL;
    CHECK(mm_video_stream_stop(&stream, &services) > 0 && host.stops == stopped + 2);
}

typedef struct WindowFixture
{
    char calls[16];
    int count, fail_open, fail_info, fail_start, stopped;
    MMI32 x, y;
    MMU32 generation;
    MMPixelRect moved;
} WindowFixture;

static void window_call(WindowFixture *host, char call)
{
    CHECK(host->count + 1 < (int)sizeof(host->calls));
    host->calls[host->count++] = call;
    host->calls[host->count] = '\0';
}

static void window_expect(WindowFixture *host, const char *calls)
{
    CHECK(strcmp(host->calls, calls) == 0);
    host->count = 0;
    host->calls[0] = '\0';
}

static int window_open(void *context, void *source)
{
    WindowFixture *host = (WindowFixture *)context;
    CHECK(source == context);
    window_call(host, 'O');
    return !host->fail_open;
}

static void window_close(void *context)
{
    window_call((WindowFixture *)context, 'C');
}

static void window_suspend(void *context, int suspend)
{
    window_call((WindowFixture *)context, suspend ? 'S' : 'U');
}

static int window_info(void *context, MMI32 *frames, MMPixelRect *source)
{
    WindowFixture *host = (WindowFixture *)context;
    MMPixelRect bounds = {2, 3, 18, 12};
    window_call(host, 'I');
    *frames = 27;
    *source = bounds;
    return !host->fail_info;
}

static void window_origin(void *context, MMI32 *x, MMI32 *y)
{
    WindowFixture *host = (WindowFixture *)context;
    *x = host->x;
    *y = host->y;
}

static void window_move(void *context, const MMPixelRect *rect)
{
    WindowFixture *host = (WindowFixture *)context;
    window_call(host, 'M');
    host->moved = *rect;
}

static void window_show(void *context)
{
    window_call((WindowFixture *)context, 'V');
}

static MMU32 window_palette(void *context)
{
    return ((WindowFixture *)context)->generation;
}

static void window_realize(void *context)
{
    WindowFixture *host = (WindowFixture *)context;
    window_call(host, 'R');
    ++host->generation;
}

static int window_start(void *context)
{
    WindowFixture *host = (WindowFixture *)context;
    window_call(host, 'B');
    return !host->fail_start;
}

static void window_pause(void *context)
{
    window_call((WindowFixture *)context, 'P');
}

static int window_stopped(void *context)
{
    WindowFixture *host = (WindowFixture *)context;
    window_call(host, 'T');
    return host->stopped;
}

static void test_window(void)
{
    WindowFixture host = {0};
    MMVideoWindow window = {0};
    MMVideoWindowServices services = {0, window_open, window_close, window_suspend, window_info,
                                      window_origin, window_move, window_show, window_palette, window_realize,
                                      window_start, window_pause, window_stopped};
    MMPixelRect custom = {3, 2, 23, 22};
    MMPixelRect empty = {1, 1, 1, 10}, other_empty = {9, 12, 20, 8};

    services.context = &host;
    host.fail_open = 1;
    CHECK(!mm_video_window_open(&window, &services, &host));
    mm_video_window_close(&window, &services);
    window_expect(&host, "O");

    host.fail_open = 0;
    host.fail_info = 1;
    CHECK(!mm_video_window_open(&window, &services, &host));
    mm_video_window_close(&window, &services);
    mm_video_window_close(&window, &services);
    window_expect(&host, "OSICU");

    host.fail_info = 0;
    host.x = 10;
    host.y = 20;
    CHECK(mm_video_window_open(&window, &services, &host));
    CHECK(window.width == 16 && window.height == 9 && window.frames == 27);
    CHECK(!mm_video_window_playing(&window, &services));
    mm_video_window_place(&window, &services);
    CHECK(host.moved.left == 12 && host.moved.top == 23 && host.moved.right == 28 && host.moved.bottom == 32);
    mm_video_window_place(&window, &services);
    window_expect(&host, "OSIRMV");

    ++host.generation;
    CHECK(mm_video_window_start(&window, &services, &custom));
    CHECK(host.moved.left == 13 && host.moved.top == 22 && host.moved.right == 33 && host.moved.bottom == 42);
    CHECK(mm_video_window_playing(&window, &services));
    window_expect(&host, "MRBT");

    host.fail_start = 1;
    CHECK(!mm_video_window_start(&window, &services, NULL));
    CHECK(!mm_video_window_playing(&window, &services));
    CHECK(host.moved.left == 10 && host.moved.top == 20 && host.moved.right == 26 && host.moved.bottom == 29);
    window_expect(&host, "PMB");

    host.fail_start = 0;
    CHECK(mm_video_window_start(&window, &services, NULL));
    host.stopped = 1;
    CHECK(!mm_video_window_playing(&window, &services));
    mm_video_window_stop(&window, &services);
    window_expect(&host, "BT");

    mm_video_window_bounds(&window, &empty);
    mm_video_window_place(&window, &services);
    mm_video_window_bounds(&window, &other_empty);
    mm_video_window_place(&window, &services);
    mm_video_window_bounds(&window, NULL);
    mm_video_window_place(&window, &services);
    host.x = 4;
    mm_video_window_place(&window, &services);
    CHECK(host.moved.left == 4 && host.moved.right == 20);
    mm_video_window_close(&window, &services);
    window_expect(&host, "MMMCU");
}

static void test_pcm(void)
{
    MMVideoPcmWindow window;
    CHECK(mm_video_pcm_window(100, 200, 1000, 2, 150, 600, &window));
    CHECK(window.bytes == 400 && window.byte_rate == 2000 && window.source_offset == 0 &&
          window.destination_offset == 100 && window.copy_bytes == 300);
    CHECK(mm_video_pcm_window(100, 200, 1000, 2, 50, 600, &window));
    CHECK(window.source_offset == 100 && window.destination_offset == 0 && window.copy_bytes == 400);
    CHECK(mm_video_pcm_window(100, 200, 1000, 2, 800, 600, &window));
    CHECK(window.destination_offset == window.bytes && window.copy_bytes == 0);
    CHECK(mm_video_pcm_window(2000, 200, 1000, 2, 50, 600, &window));
    CHECK(window.source_offset == 600 && window.copy_bytes == 0);
    CHECK(mm_video_pcm_window(0, 1, 44100, 4, 0, 119, &window));
    CHECK(window.bytes == 180 && window.copy_bytes == 116);
    CHECK(!mm_video_pcm_window(0, 0x7fffffffL, 44100, 4, 0, 119, &window) && window.bytes == 180);
    CHECK(!mm_video_pcm_window(-1, 3, 44100, 4, 0, 119, &window));
}

static MMI32 frame_at(void *context, MMI32 time)
{
    *(MMI32 *)context = time;
    return time / 100;
}

static MMAudioCounter ms(MMU32 milliseconds)
{
    return mm_audio_count_multiply(milliseconds, 1000000UL);
}

static void test_device(void)
{
    MMVideoDeviceClock clock = {0};
    MMU32 consumed;
    MMI32 requested = -1;
    CHECK(!mm_video_device_prepare(&clock, 12, 12, 100, 1100));
    CHECK(mm_video_device_prepare(&clock, 2, 12, 100, 1100));
    mm_video_device_begin(&clock, ms(50), 4000, 4000, 100, 1000);
    CHECK(mm_video_device_update(&clock, ms(300), 2000, &requested, frame_at, &consumed) == MM_VIDEO_ADVANCED);
    CHECK(consumed == 2000 && requested == 500 && clock.position == 5);
    CHECK(mm_video_device_update(&clock, ms(700), 0, &requested, frame_at, &consumed) == MM_VIDEO_ADVANCED);
    CHECK(consumed == 2000 && requested == 1000 && clock.position == 10 && clock.playing);
    CHECK(mm_video_device_update(&clock, ms(750), 0, &requested, frame_at, &consumed) == MM_VIDEO_ADVANCED);
    CHECK(!consumed && requested == 1050 && clock.position == 10);
    CHECK(mm_video_device_update(&clock, ms(800), 0, &requested, frame_at, &consumed) == MM_VIDEO_FINISHED);
    CHECK(!clock.playing && requested == 1100 && clock.position == 11);
    CHECK(mm_video_device_update(&clock, ms(900), 0, &requested, frame_at, &consumed) == MM_VIDEO_INACTIVE);

    CHECK(mm_video_device_prepare(&clock, 2, 12, 100, 1100));
    mm_video_device_begin(&clock, ms(5000), 0, 0, 0, 0);
    CHECK(mm_video_device_update(&clock, ms(5001), 0, &requested, frame_at, &consumed) == MM_VIDEO_ADVANCED);
    CHECK(requested == 101 && clock.position == 2);
    CHECK(mm_video_device_update(&clock, ms(6000), 0, &requested, frame_at, &consumed) == MM_VIDEO_FINISHED);
    CHECK(clock.position == 11);
    CHECK(mm_video_device_prepare(&clock, 2, 12, 100, 1100));
    mm_video_device_begin(&clock, ms(7000), 4000, 4000, 100, 1000);
    CHECK(mm_video_device_update(&clock, ms(7010), 4001, &requested, frame_at, &consumed) == MM_VIDEO_QUEUE_ERROR);
    CHECK(!clock.playing && !consumed && clock.position == 2);
}

int main(void)
{
    test_stream();
    test_window();
    test_pcm();
    test_device();
    return EXIT_SUCCESS;
}
