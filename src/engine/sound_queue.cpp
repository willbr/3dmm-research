/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "handle_services.h"
#include "mm/actor_sound.h"
#include "mm/frame_sound.h"
ASSERTNAME

class SoundPlaybackQueue::Storage final : public BASE
{
  public:
    MMFrameSoundQueue queue{};
    bool initialized = false;
    ~Storage() override
    {
        if (initialized)
            mm_frame_sound_queue_dispose(&queue);
    }
#ifdef DEBUG
    void MarkMem() override
    {
        BASE::MarkMem();
        MarkPv(queue.storage.buffers[0].data);
        for (MMU32 i = 0; i < queue.count; ++i)
            MarkMemObj(static_cast<BASE *>(mm_frame_sound_queue_at(&queue, i)->sound));
    }
#endif
};

SoundPlaybackQueue::~SoundPlaybackQueue()
{
    ReleasePpo(&_entries);
}

bool SoundPlaybackQueue::FInit()
{
    if (_entries)
        return true;
    auto next = NewObj Storage;
    if (!next)
        return false;
#ifdef DEBUG
    constexpr int trashUnused = 1;
#else
    constexpr int trashUnused = 0;
#endif
    next->initialized = mm_frame_sound_queue_init(
        &next->queue, KauaiCopyAllocator(),
        [](void *p) { static_cast<BASE *>(p)->AddRef(); },
        [](void *p) { static_cast<BASE *>(p)->Release(); }, trashUnused) != 0;
    if (!next->initialized)
    {
        ReleasePpo(&next);
        return false;
    }
    _entries = next;
    return true;
}

LegacyLong SoundPlaybackQueue::QueueNumber(LegacyLong style, LegacyLong objectId, bool actor)
{
    AssertIn(style, 0, styLim);
    return mm_sound_queue_number(style, objectId, actor);
}

bool SoundPlaybackQueue::FEnqueue(const SoundPlaybackRequest &request, bool lowPriority)
{
    AssertPo(request.sound, 0);
    const MMFrameSoundRequest cue = {
        request.sound, request.style, request.objectId, request.volume, request.priority, request.startOffset,
        request.loop, request.queue, request.actor};
    return mm_frame_sound_queue_enqueue(_entries ? &_entries->queue : nullptr, &cue, duration, lowPriority) != 0;
}

bool SoundPlaybackQueue::FDequeue(SoundPlaybackRequest *request)
{
    AssertVarMem(request);
    MMFrameSoundRequest cue;
    if (!mm_frame_sound_queue_dequeue(_entries ? &_entries->queue : nullptr, &cue))
        return false;
    *request = {static_cast<BASE *>(cue.sound), cue.style, cue.object, cue.loop != 0, cue.queued != 0,
                cue.volume, cue.priority, cue.actor != 0, cue.offset};
    return true;
}

void SoundPlaybackQueue::Clear()
{
    if (_entries)
        mm_frame_sound_queue_clear(&_entries->queue);
}

LegacyLong SoundPlaybackQueue::Count() const
{
    return _entries ? static_cast<LegacyLong>(_entries->queue.count) : 0;
}

SoundQueueDispatch SoundPlaybackQueue::Dispatch() const
{
    static_assert(static_cast<int>(SoundQueueDispatch::Empty) == MM_FRAME_SOUND_EMPTY &&
                  static_cast<int>(SoundQueueDispatch::Flush) == MM_FRAME_SOUND_FLUSH &&
                  static_cast<int>(SoundQueueDispatch::Play) == MM_FRAME_SOUND_PLAY, "sound queue dispatch ABI");
    static_assert(kdtimOffMsq == MM_FRAME_SOUND_OFF && kdtimLongMsq == MM_FRAME_SOUND_LONG,
                  "sound queue duration ABI");
    return static_cast<SoundQueueDispatch>(mm_frame_sound_queue_dispatch(_entries ? &_entries->queue : nullptr, duration));
}

bool SoundPlaybackQueue::FTimeout(LegacyLong *interval) const
{
    AssertVarMem(interval);
    MMI32 value;
    if (!mm_frame_sound_queue_timeout(duration, &value))
        return false;
    *interval = value;
    return true;
}

#ifdef DEBUG
void SoundPlaybackQueue::AssertValid() const
{
    AssertPo(_entries, 0);
    Assert(_entries->initialized, "Invalid sound request storage");
    Assert(_entries->queue.count <= _entries->queue.storage.buffers[0].capacity / sizeof(MMFrameSoundRequest),
           "Invalid sound request count");
}

void SoundPlaybackQueue::MarkMem() const
{
    MarkMemObj(_entries);
}
#endif
