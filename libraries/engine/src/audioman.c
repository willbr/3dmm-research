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
    void *p = q->context;
    MMSoundEntry entry;
    MMI32 i;
    void *loop;
    q->enter(p);
    for (i = first; i < q->count(p); ++i)
    {
        q->get(p, i, &entry);
        if (entry.plays == 1)
            continue;
        loop = s->loop(s->context, entry.object, entry.plays);
        if (loop)
        {
            q->release(p, entry.object);
            entry.object = loop;
            entry.plays = 1;
            q->put(p, i, &entry);
        }
    }
    if (q->current(p) == first && q->count(p) >= 0)
    {
        i = first;
        while (i < q->count(p))
        {
            q->get(p, i, &entry);
            if (entry.pause >= 0)
                break;
            q->set_current(p, ++i);
        }
        if (i < q->count(p) && !entry.pause)
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
    void *p = q->context;
    MMSoundEntry entry;
    q->enter(p);
    if (q->current(p) == first && first < q->count(p))
    {
        q->get(p, first, &entry);
        entry.offset = s->clock(s->context) - *start;
        q->put(p, first, &entry);
        mm_audioman_queue_run(start, first, group, s);
    }
    q->leave(p);
}

void mm_audioman_queue_notify(MMU32 *start, void *sound, const MMAudioManGroup *group,
                              const MMAudioManQueueServices *s)
{
    const MMSoundQueueServices *q = &s->entries;
    void *p = q->context;
    MMSoundEntry entry;
    MMI32 i;
    q->enter(p);
    i = q->current(p);
    if (i < q->count(p))
    {
        q->get(p, i, &entry);
        if (sound == s->identity(s->context, entry.object))
        {
            entry.plays = mm_signed_word((MMU32)entry.plays - 1U);
            if (!entry.plays)
            {
                q->set_current(p, ++i);
                mm_audioman_queue_run(start, i, group, s);
            }
            else
            {
                q->put(p, i, &entry);
                s->source(s->context, entry.object);
                *start = s->clock(s->context);
            }
        }
    }
    q->leave(p);
}
