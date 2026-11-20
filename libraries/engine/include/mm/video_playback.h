/* Video timing policy converted from Kauai video.cpp and video_sdl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_VIDEO_PLAYBACK_H
#define MM_ENGINE_VIDEO_PLAYBACK_H
#include "audio_clock.h"
#include "pixel_raster.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMVideoStream
{
    MMI32 width, height, frames, frame, marked;
    MMU32 origin;
    MMPixelRect bounds;
    int playing;
} MMVideoStream;

/* UI-thread callbacks. A negative result is a host error. Bind/start return
 * positive on success. Controls/playing return positive for true. A missing
 * stop callback denotes a stream with no device, for partial construction. */
typedef struct MMVideoStreamServices
{
    void *context;
    int (*bind)(void *, int add);
    int (*start)(void *, MMI32 frame);
    int (*stop)(void *);
    int (*controls)(void *);
    int (*playing)(void *);
    int (*position)(void *, MMI32 *frame);
    int (*frame_for_time)(void *, MMU32 milliseconds, MMI32 *frame);
    int (*time_for_frame)(void *, MMI32 frame, MMI32 *milliseconds);
    int (*now)(void *, MMU32 *milliseconds);
    int (*invalidate)(void *, const MMPixelRect *);
} MMVideoStreamServices;

void mm_video_stream_init(MMVideoStream *, MMI32 width, MMI32 height, MMI32 frames);
void mm_video_stream_bounds(MMVideoStream *, const MMPixelRect *);
int mm_video_stream_start(MMVideoStream *, const MMVideoStreamServices *, const MMPixelRect *);
int mm_video_stream_stop(MMVideoStream *, const MMVideoStreamServices *);
int mm_video_stream_seek(MMVideoStream *, const MMVideoStreamServices *, MMI32 frame);
int mm_video_stream_position(MMVideoStream *, const MMVideoStreamServices *);
int mm_video_stream_playing(MMVideoStream *, const MMVideoStreamServices *);
int mm_video_stream_tick(MMVideoStream *, const MMVideoStreamServices *);
int mm_video_stream_drawable(const MMVideoStream *, int device_controlled);

typedef struct MMVideoWindow
{
    MMPixelRect bounds, placement;
    MMI32 width, height, frames;
    MMU32 palette;
    int opened, playing, visible;
} MMVideoWindow;

/* Native child-window services. The device provides its own playback clock.
 * open/info/start return true on success. stopped returns true only if the
 * device reported a terminal mode. A failed info query still leaves an open
 * device for mm_video_window_close to close and release its audio suspension. */
typedef struct MMVideoWindowServices
{
    void *context;
    int (*open)(void *, void *source);
    void (*close)(void *);
    void (*suspend_sound)(void *, int suspend);
    int (*info)(void *, MMI32 *frames, MMPixelRect *source);
    void (*origin)(void *, MMI32 *x, MMI32 *y);
    void (*move)(void *, const MMPixelRect *);
    void (*show)(void *);
    MMU32 (*palette_generation)(void *);
    void (*realize_palette)(void *);
    int (*start)(void *);
    void (*pause)(void *);
    int (*stopped)(void *);
} MMVideoWindowServices;

int mm_video_window_open(MMVideoWindow *, const MMVideoWindowServices *, void *source);
void mm_video_window_close(MMVideoWindow *, const MMVideoWindowServices *);
void mm_video_window_bounds(MMVideoWindow *, const MMPixelRect *);
void mm_video_window_place(MMVideoWindow *, const MMVideoWindowServices *);
int mm_video_window_start(MMVideoWindow *, const MMVideoWindowServices *, const MMPixelRect *);
void mm_video_window_stop(MMVideoWindow *, const MMVideoWindowServices *);
int mm_video_window_playing(MMVideoWindow *, const MMVideoWindowServices *);

typedef struct MMVideoPcmWindow
{
    MMU32 bytes, byte_rate, source_offset, destination_offset, copy_bytes;
} MMVideoPcmWindow;

/* Plan the fixed-format queue and its copy from source PCM. The host fills
 * all other bytes with format-appropriate silence. Source start is measured
 * in sample frames. Queue size fits a signed 32-bit device count. */
int mm_video_pcm_window(MMI32 first_time, MMI32 duration, MMU32 rate, MMU32 alignment,
                        MMU32 source_start, MMU32 source_bytes, MMVideoPcmWindow *);

typedef struct MMVideoDeviceClock
{
    MMI32 position, first_frame, first_time, duration, last_frame;
    MMAudioCounter origin, drained, latency;
    MMU32 byte_rate, queued, last_queued;
    int playing;
} MMVideoDeviceClock;

enum { MM_VIDEO_INACTIVE, MM_VIDEO_ADVANCED, MM_VIDEO_FINISHED, MM_VIDEO_QUEUE_ERROR };

/* Use prepare before the device opens, then begin after supplying its format.
 * A zero byte rate selects silent monotonic playback. Times are nanoseconds
 * from one monotonic source. Remaining is the count of unconsumed input bytes.
 * On finished/queue error the host closes any device. Stop retains position. */
int mm_video_device_prepare(MMVideoDeviceClock *, MMI32 frame, MMI32 frames, MMI32 first_time, MMI32 end_time);
void mm_video_device_begin(MMVideoDeviceClock *, MMAudioCounter now, MMU32 queued, MMU32 byte_rate,
                           MMU32 device_frames, MMU32 device_frequency);
void mm_video_device_stop(MMVideoDeviceClock *);
int mm_video_device_update(MMVideoDeviceClock *, MMAudioCounter now, MMI32 remaining,
                           void *context, MMI32 (*frame_at)(void *, MMI32), MMU32 *consumed);

#ifdef __cplusplus
}
#endif
#endif
