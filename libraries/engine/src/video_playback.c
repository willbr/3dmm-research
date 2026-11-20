/* Video timing policy converted from Kauai video.cpp and video_sdl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/video_playback.h"

void mm_video_stream_init(MMVideoStream *stream, MMI32 width, MMI32 height, MMI32 frames)
{
    MMVideoStream empty = {0};
    *stream = empty;
    stream->width = width;
    stream->height = height;
    stream->frames = frames;
    stream->marked = -1;
    mm_video_stream_bounds(stream, NULL);
}

void mm_video_stream_bounds(MMVideoStream *stream, const MMPixelRect *bounds)
{
    MMPixelRect full;
    full.left = full.top = 0;
    full.right = stream->width;
    full.bottom = stream->height;
    stream->bounds = bounds ? *bounds : full;
}

int mm_video_stream_position(MMVideoStream *stream, const MMVideoStreamServices *s)
{
    MMI32 frame;
    int status;
    if (!stream->playing)
        return 1;
    status = s->controls(s->context);
    if (status <= 0)
        return status < 0 ? status : 1;
    status = s->position(s->context, &frame);
    if (status >= 0)
        stream->frame = frame;
    return status;
}

int mm_video_stream_stop(MMVideoStream *stream, const MMVideoStreamServices *s)
{
    int first = 1, stopped, unbound;
    if (s->stop)
    {
        first = mm_video_stream_position(stream, s);
        stopped = s->stop(s->context);
        if (first >= 0 && stopped < 0)
            first = stopped;
    }
    unbound = s->bind(s->context, 0);
    stream->playing = 0;
    return first >= 0 && unbound < 0 ? unbound : first;
}

int mm_video_stream_start(MMVideoStream *stream, const MMVideoStreamServices *s, const MMPixelRect *bounds)
{
    MMU32 now;
    MMI32 time;
    int status, cleanup;
    status = mm_video_stream_stop(stream, s);
    if (status < 0)
        return status;
    status = s->bind(s->context, 1);
    if (status <= 0)
        return status;
    status = s->start(s->context, stream->frame);
    if (status <= 0)
    {
        cleanup = status < 0 ? mm_video_stream_stop(stream, s) : s->bind(s->context, 0);
        return status < 0 ? status : cleanup < 0 ? cleanup : 0;
    }

    mm_video_stream_bounds(stream, bounds);
    stream->playing = 1;
    status = s->now(s->context, &now);
    if (status >= 0)
        status = s->time_for_frame(s->context, stream->frame, &time);
    if (status < 0)
    {
        mm_video_stream_stop(stream, s);
        return status;
    }
    stream->origin = now - (MMU32)time;
    return 1;
}

int mm_video_stream_seek(MMVideoStream *stream, const MMVideoStreamServices *s, MMI32 frame)
{
    int status = mm_video_stream_stop(stream, s);
    if (status >= 0)
        stream->frame = frame;
    return status;
}

int mm_video_stream_playing(MMVideoStream *stream, const MMVideoStreamServices *s)
{
    int status;
    if (!stream->playing)
        return 0;
    status = s->controls(s->context);
    if (status <= 0)
        return status < 0 ? status : 1;
    status = s->playing(s->context);
    if (status)
        return status;
    status = mm_video_stream_stop(stream, s);
    return status < 0 ? status : 0;
}

int mm_video_stream_tick(MMVideoStream *stream, const MMVideoStreamServices *s)
{
    MMI32 frame;
    MMU32 now;
    int controlled, status;
    if (!stream->playing)
        return mm_video_stream_stop(stream, s);
    controlled = s->controls(s->context);
    if (controlled < 0)
        return controlled;
    if (controlled)
        status = s->position(s->context, &frame);
    else
    {
        status = s->now(s->context, &now);
        if (status >= 0)
            status = s->frame_for_time(s->context, now - stream->origin, &frame);
    }
    if (status < 0)
        return status;
    if (frame >= stream->frames)
        frame = stream->frames - 1;
    else if (frame < 0)
        frame = 0;
    stream->frame = frame;
    if (frame != stream->marked)
    {
        status = s->invalidate(s->context, &stream->bounds);
        if (status < 0)
            return status;
        stream->marked = stream->frame;
    }
    status = controlled ? s->playing(s->context) : stream->frame < stream->frames - 1;
    if (status)
        return status;
    return mm_video_stream_stop(stream, s);
}

int mm_video_stream_drawable(const MMVideoStream *stream, int controlled)
{
    return !controlled || (stream->bounds.left < stream->bounds.right && stream->bounds.top < stream->bounds.bottom);
}

static MMAudioCounter divide_by(MMAudioCounter value, MMU32 denominator)
{
    return mm_audio_count_divide(value, mm_audio_counter(denominator, 0), NULL);
}

int mm_video_pcm_window(MMI32 first_time, MMI32 duration, MMU32 rate, MMU32 alignment,
                        MMU32 source_start, MMU32 source_bytes, MMVideoPcmWindow *window)
{
    MMVideoPcmWindow next = {0};
    MMAudioCounter length, at, origin, offset;
    MMU32 source_frames, source = 0, destination = 0, count;
    if (first_time < 0 || duration <= 0 || !rate || !alignment || rate > 0xffffffffUL / alignment)
        return 0;
    length = mm_audio_count_multiply((MMU32)duration, rate);
    length = divide_by(mm_audio_count_add(length, mm_audio_counter(999, 0)), 1000);
    if (length.high || length.low > 0x7fffffffUL / alignment)
        return 0;
    next.bytes = length.low * alignment;
    next.byte_rate = rate * alignment;
    source_frames = source_bytes / alignment;
    at = divide_by(mm_audio_count_multiply((MMU32)first_time, rate), 1000);
    origin = mm_audio_counter(source_start, 0);
    if (mm_audio_count_compare(at, origin) >= 0)
    {
        offset = mm_audio_count_sub(at, origin);
        source = offset.high || offset.low > source_frames ? source_frames : offset.low;
    }
    else
    {
        offset = mm_audio_count_sub(origin, at);
        destination = offset.low > length.low ? length.low : offset.low;
    }
    count = source_frames - source;
    if (count > length.low - destination)
        count = length.low - destination;
    next.source_offset = source * alignment;
    next.destination_offset = destination * alignment;
    next.copy_bytes = count * alignment;
    *window = next;
    return 1;
}

int mm_video_device_prepare(MMVideoDeviceClock *clock, MMI32 frame, MMI32 frames,
                            MMI32 first_time, MMI32 end_time)
{
    if (frame < 0 || frame >= frames || first_time < 0 || end_time <= first_time)
        return 0;
    clock->first_frame = clock->position = frame;
    clock->first_time = first_time;
    clock->duration = end_time - first_time;
    clock->last_frame = frames - 1;
    return 1;
}

void mm_video_device_begin(MMVideoDeviceClock *clock, MMAudioCounter now, MMU32 queued, MMU32 byte_rate,
                           MMU32 device_frames, MMU32 device_frequency)
{
    clock->origin = now;
    clock->drained = mm_audio_counter(0, 0);
    clock->queued = clock->last_queued = queued;
    clock->byte_rate = byte_rate;
    clock->latency = divide_by(mm_audio_count_multiply(device_frames, 1000000000UL), device_frequency);
    clock->playing = 1;
}

void mm_video_device_stop(MMVideoDeviceClock *clock)
{
    clock->playing = 0;
}

int mm_video_device_update(MMVideoDeviceClock *clock, MMAudioCounter now, MMI32 remaining,
                           void *context, MMI32 (*frame_at)(void *, MMI32), MMU32 *consumed)
{
    MMAudioCounter elapsed, tail, limit;
    MMU32 interval;
    MMI32 frame;
    int done;
    *consumed = 0;
    if (!clock->playing)
        return MM_VIDEO_INACTIVE;
    limit = mm_audio_count_multiply((MMU32)clock->duration, 1000000UL);
    elapsed = mm_audio_count_sub(now, clock->origin);
    if (clock->byte_rate)
    {
        if (remaining < 0 || (MMU32)remaining > clock->queued)
        {
            mm_video_device_stop(clock);
            return MM_VIDEO_QUEUE_ERROR;
        }
        if ((MMU32)remaining < clock->last_queued)
            *consumed = clock->last_queued - (MMU32)remaining;
        clock->last_queued = (MMU32)remaining;
        elapsed = divide_by(mm_audio_count_multiply(clock->queued - (MMU32)remaining, 1000000000UL),
                            clock->byte_rate);
        elapsed = mm_audio_count_compare(elapsed, clock->latency) > 0 ?
            mm_audio_count_sub(elapsed, clock->latency) : mm_audio_counter(0, 0);
        if (!remaining)
        {
            if (!clock->drained.low && !clock->drained.high)
                clock->drained = now;
            tail = mm_audio_count_sub(now, clock->drained);
            elapsed = mm_audio_count_compare(tail, clock->latency) >= 0 ?
                limit : mm_audio_count_add(elapsed, tail);
        }
    }
    done = mm_audio_count_compare(elapsed, limit) >= 0;
    interval = done ? (MMU32)clock->duration : divide_by(elapsed, 1000000UL).low;
    frame = frame_at(context, clock->first_time + (MMI32)interval);
    clock->position = frame < clock->first_frame ? clock->first_frame : frame;
    if (!done)
        return MM_VIDEO_ADVANCED;
    clock->position = clock->last_frame;
    mm_video_device_stop(clock);
    return MM_VIDEO_FINISHED;
}
