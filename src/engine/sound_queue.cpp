/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "three_mm/actor_sound.h"
ASSERTNAME

SoundPlaybackQueue::~SoundPlaybackQueue()
{
    Clear();
    ReleasePpo(&_entries);
}

bool SoundPlaybackQueue::FInit()
{
    if (_entries == pvNil)
        _entries = DynamicArray::PglNew(sizeof(SoundPlaybackRequest), 10);
    return _entries != pvNil;
}

LegacyLong SoundPlaybackQueue::QueueNumber(LegacyLong style, LegacyLong objectId, bool actor)
{
    AssertIn(style, 0, styLim);
    // Preserve the legacy signed-low-word identity used by sound devices.
    return three_mm_sound_queue_number(style, objectId, actor);
}

bool SoundPlaybackQueue::FEnqueue(const SoundPlaybackRequest &request, bool lowPriority)
{
    AssertPo(request.sound, 0);
    if (_entries == pvNil)
        return false;
    if (duration == kdtimOffMsq)
        return true;
    if (!request.queue)
    {
        LegacyLong number = QueueNumber(request.style, request.objectId, request.actor);
        for (LegacyLong index = 0; index < _entries->IvMac(); ++index)
        {
            auto current = (SoundPlaybackRequest *)_entries->QvGet(index);
            if (QueueNumber(current->style, current->objectId, current->actor) == number)
            {
                if (lowPriority)
                    return true;
                ReleasePpo(&current->sound);
                _entries->Delete(index);
                break;
            }
        }
    }
    SoundPlaybackRequest owned = request;
    owned.sound->AddRef();
    bool inserted = lowPriority ? _entries->FPush(&owned) : _entries->FEnqueue(&owned);
    if (!inserted)
        ReleasePpo(&owned.sound);
    return inserted;
}

bool SoundPlaybackQueue::FDequeue(SoundPlaybackRequest *request)
{
    AssertVarMem(request);
    return _entries != pvNil && _entries->FDequeue(request);
}

void SoundPlaybackQueue::Clear()
{
    SoundPlaybackRequest request;
    while (FDequeue(&request))
        ReleasePpo(&request.sound);
}

LegacyLong SoundPlaybackQueue::Count() const
{
    return _entries == pvNil ? 0 : _entries->IvMac();
}

SoundQueueDispatch SoundPlaybackQueue::Dispatch() const
{
    if (Count() == 0)
        return SoundQueueDispatch::Empty;
    return duration == kdtimOffMsq ? SoundQueueDispatch::Flush : SoundQueueDispatch::Play;
}

bool SoundPlaybackQueue::FTimeout(LegacyLong *interval) const
{
    AssertVarMem(interval);
    if (duration >= kdtimLongMsq)
        return false;
    *interval = duration;
    return true;
}

#ifdef DEBUG
void SoundPlaybackQueue::AssertValid() const
{
    AssertPo(_entries, 0);
    Assert(_entries->CbEntry() == sizeof(SoundPlaybackRequest), "Invalid sound request stride");
}

void SoundPlaybackQueue::MarkMem() const
{
    MarkMemObj(_entries);
    for (LegacyLong index = 0; index < Count(); ++index)
        MarkMemObj(((SoundPlaybackRequest *)_entries->QvGet(index))->sound);
}
#endif
