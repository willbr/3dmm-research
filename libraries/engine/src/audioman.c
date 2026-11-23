/* AudioMan runtime policy converted from Kauai sndam.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/audioman.h"

int mm_audioman_stream_init(MMAudioManStream *stream, const MMHandleServices *handles,
                             const MMBlockFileServices *files)
{
    int status = mm_owned_block_init(&stream->block, handles, files);
    if (status == MM_DATA_OK)
        stream->position = 0;
    return status;
}

void mm_audioman_stream_dispose(MMAudioManStream *stream)
{
    mm_owned_block_clear(&stream->block);
    stream->position = 0;
}

int mm_audioman_stream_read(MMAudioManStream *stream, void *out, MMU32 requested, MMU32 *read)
{
    MMBlockIO io;
    MMU32 count;
    int status;
    if (read)
        *read = 0;
    if (stream->position > stream->block.view.length)
        return MM_DATA_ARGUMENT;
    count = stream->block.view.length - stream->position;
    if (count > requested)
        count = requested;
    status = mm_owned_block_io(&stream->block, &io);
    if (status == MM_DATA_OK)
        status = mm_block_read(&io, stream->position, out, count, 0);
    if (status == MM_DATA_OK)
    {
        stream->position += count;
        if (read)
            *read = count;
    }
    return status;
}

int mm_audioman_stream_seek(MMAudioManStream *stream, MMWide offset, MMU32 origin, MMU32 *position)
{
    MMWide base = {0, 0};
    MMWide limit = {0, 0};
    int ok;
    limit.low = stream->block.view.length;
    if (origin == MM_AUDIOMAN_SEEK_CUR)
        base.low = stream->position;
    else if (origin == MM_AUDIOMAN_SEEK_END)
        base = limit;
    ok = mm_wide_compare(offset, mm_wide_subtract(mm_wide_i32(0), base)) >= 0 &&
         mm_wide_compare(offset, mm_wide_subtract(limit, base)) <= 0;
    if (ok)
        stream->position = mm_wide_add(base, offset).low;
    if (position)
        *position = stream->position;
    return ok ? MM_DATA_OK : MM_DATA_ARGUMENT;
}

MMU32 mm_audioman_stream_resident(const MMAudioManStream *stream)
{
    return stream->block.view.kind == MM_BLOCK_MEMORY ? stream->block.view.capacity : 0;
}

void mm_audioman_device_init(MMAudioManDevice *device)
{
    device->system_volume = 0xffffffffUL;
    device->volume = 0x10000L;
    device->attached = 0;
}

int mm_audioman_device_open(MMAudioManDevice *device, MMAudioManMixer *mixer, MMI32 format,
                            const MMAudioManDeviceServices *s)
{
    const MMU32 buffer_ms = 600;
    void *p = s->context;
    if (mixer->object)
    {
        s->retain(p, mixer->object);
        device->attached = 1;
        return 1;
    }

    mixer->object = s->create(p);
    if (!mixer->object)
        return 0;
    device->attached = 1;
    if (format < 0 || format >= MM_AUDIOMAN_FORMAT_COUNT)
        format = MM_AUDIOMAN_22M16;
    if (!s->supports_format(p, format))
    {
        if (format < MM_AUDIOMAN_11M16)
            return 0;
        /* Match the 8-bit rate and channel count. Let the wave mapper
         * convert the fallback even if no device advertises it. */
        format -= MM_AUDIOMAN_11M16 - MM_AUDIOMAN_11M8;
    }

    mixer->format = format;
    mixer->cache_ms = 2 * buffer_ms;
    return s->initialize(p, mixer->object, format, 12, 1, buffer_ms) && s->activate(p, mixer->object);
}

void mm_audioman_device_close(MMAudioManDevice *device, MMAudioManMixer *mixer,
                              const MMAudioManDeviceServices *s)
{
    if (device->attached)
    {
        if (!s->release(s->context, mixer->object))
            mixer->object = 0;
        device->attached = 0;
    }
}

void mm_audioman_device_suspend(MMAudioManDevice *device, const MMAudioManMixer *mixer, int suspended,
                                const MMAudioManDeviceServices *s)
{
    void *p = s->context;
    if (suspended)
    {
        s->set_volume(p, mixer->object, device->system_volume);
        s->suspend(p, mixer->object, 1);
    }
    else if (s->suspend(p, mixer->object, 0))
    {
        s->get_volume(p, mixer->object, &device->system_volume);
        s->publish_volume(p, device->system_volume);
        s->set_volume(p, mixer->object, mm_sound_volume_scale(device->system_volume, device->volume));
    }
    else
        s->device_busy(p);
}

void mm_audioman_device_set_volume(MMAudioManDevice *device, const MMAudioManMixer *mixer, MMI32 volume,
                                   const MMSoundControl *control, const MMAudioManDeviceServices *s)
{
    if (device->volume != volume)
    {
        device->volume = volume;
        if (control->suspended <= 0 && control->active)
            s->set_volume(s->context, mixer->object, mm_sound_volume_scale(device->system_volume, volume));
    }
}

void mm_audioman_group_begin(MMAudioManGroup *group, const MMAudioManGroupServices *s)
{
    MMI32 old = group->depth;
    group->depth = mm_signed_word((MMU32)old + 1U);
    if (!old)
        group->active = s->allocate(s->context, &group->id);
}

void mm_audioman_group_end(MMAudioManGroup *group, const MMAudioManGroupServices *s)
{
    group->depth = mm_signed_word((MMU32)group->depth - 1U);
    if (!group->depth && group->active)
    {
        s->start(s->context, group->id);
        s->release(s->context, group->id);
        group->active = 0;
    }
}

static void start_entry(MMU32 *start, const MMSoundEntry *entry, const MMAudioManGroup *group,
                         const MMAudioManQueueServices *s)
{
    void *p = s->context;
    s->stop(p);
    s->volume(p, mm_sound_volume_scale(0xffffffffUL, entry->volume));
    if (s->in_memory(p, entry->object))
        s->source(p, entry->object);
    else
        s->cached_source(p, entry->object);
    if (entry->offset)
        s->seek(p, entry->offset);
    if (!group->active || !s->enlist(p, group->id))
        s->play(p);
    *start = s->clock(p) - entry->offset;
}

void mm_audioman_queue_run(MMU32 *start, MMI32 first, const MMAudioManGroup *group, const MMAudioManQueueServices *s)
{
    const MMSoundQueueServices *q = &s->entries;
    MMSoundQueue *queue = q->queue;
    void *p = q->context;
    MMSoundEntry entry;
    MMI32 i;
    void *loop;
    q->enter(p);
    for (i = first; i < mm_sound_queue_count(queue); ++i)
    {
        entry = mm_sound_queue_get(queue, i);
        if (entry.plays == 1)
            continue;
        loop = s->loop(s->context, entry.object, entry.plays);
        if (loop)
        {
            q->release(p, entry.object);
            entry.object = loop;
            entry.plays = 1;
            mm_sound_queue_put(queue, i, &entry);
        }
    }
    if (queue->current == first && mm_sound_queue_count(queue) >= 0)
    {
        i = first;
        while (i < mm_sound_queue_count(queue))
        {
            entry = mm_sound_queue_get(queue, i);
            if (entry.pause >= 0)
                break;
            queue->current = ++i;
        }
        if (i < mm_sound_queue_count(queue) && !entry.pause)
            start_entry(start, &entry, group, s);
        else
        {
            s->stop(s->context);
            s->source(s->context, NULL);
        }
    }
    q->leave(p);
}

void mm_audioman_queue_pause(MMU32 *start, MMI32 first, const MMAudioManGroup *group, const MMAudioManQueueServices *s)
{
    const MMSoundQueueServices *q = &s->entries;
    MMSoundQueue *queue = q->queue;
    void *p = q->context;
    MMSoundEntry entry;
    q->enter(p);
    if (queue->current == first && first < mm_sound_queue_count(queue))
    {
        entry = mm_sound_queue_get(queue, first);
        entry.offset = s->clock(s->context) - *start;
        mm_sound_queue_put(queue, first, &entry);
        mm_audioman_queue_run(start, first, group, s);
    }
    q->leave(p);
}

void mm_audioman_queue_notify(MMU32 *start, void *sound, const MMAudioManGroup *group,
                              const MMAudioManQueueServices *s)
{
    const MMSoundQueueServices *q = &s->entries;
    MMSoundQueue *queue = q->queue;
    void *p = q->context;
    MMSoundEntry entry;
    MMI32 i;
    q->enter(p);
    i = queue->current;
    if (i < mm_sound_queue_count(queue))
    {
        entry = mm_sound_queue_get(queue, i);
        if (sound == s->identity(s->context, entry.object))
        {
            entry.plays = mm_signed_word((MMU32)entry.plays - 1U);
            if (!entry.plays)
            {
                queue->current = ++i;
                mm_audioman_queue_run(start, i, group, s);
            }
            else
            {
                mm_sound_queue_put(queue, i, &entry);
                s->source(s->context, entry.object);
                *start = s->clock(s->context);
            }
        }
    }
    q->leave(p);
}
