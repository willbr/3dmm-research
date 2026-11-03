/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef SOUND_QUEUE_H
#define SOUND_QUEUE_H
#include "kauai_core.h"

enum
{
    styNil = 0,
    styUnused, // Existing content depends on subsequent values.
    stySfx,
    stySpeech,
    styMidi,
    styLim
};
const LegacyLong sclNonLoop = 0;
const LegacyLong sclLoopWav = 1;
const LegacyLong sclLoopMidi = 2;
#define vlmNil (-1)
enum
{
    sqnActr = 0x10000000,
    sqnBkgd = 0x20000000,
    sqnLim
};
const LegacyLong ksqnStyShift = 16;
const LegacyLong kdtimOffMsq = 0;
const LegacyLong kdtimLongMsq = klwMax;
const LegacyLong kdtim2Msq = ((kdtimSecond * 2) * 10) / 12;

// A backend-owned resource plus explicit immutable sound metadata. Enqueue
// borrows sound and acquires a reference; successful dequeue transfers that
// reference to the caller. Core never casts the resource to a playback type.
struct SoundPlaybackRequest
{
    BASE *sound = pvNil;
    LegacyLong style = styNil;
    LegacyLong objectId = 0;
    bool loop = false;
    bool queue = false;
    LegacyLong volume = 0;
    LegacyLong priority = 0;
    bool actor = false;
    LegacyUlong startOffset = 0;
};

enum class SoundQueueDispatch
{
    Empty,
    Flush,
    Play
};

// Owns pending requests and preview-duration policy, not a device or clock.
// High-priority entries retain FIFO ordering; low-priority entries are pushed
// onto the next-to-play end. Replacement removes the first matching queue.
class SoundPlaybackQueue
{
  public:
    LegacyLong duration = kdtim2Msq;
    SoundPlaybackQueue() = default;
    ~SoundPlaybackQueue();
    SoundPlaybackQueue(const SoundPlaybackQueue &) = delete;
    SoundPlaybackQueue &operator=(const SoundPlaybackQueue &) = delete;
    bool FInit();
    bool FEnqueue(const SoundPlaybackRequest &request, bool lowPriority = false);
    bool FDequeue(SoundPlaybackRequest *request);
    void Clear();
    LegacyLong Count() const;
    SoundQueueDispatch Dispatch() const;
    // Query after draining a nonempty playing batch; zero is an immediate
    // timeout, while long mode requests no new alarm (existing alarms remain).
    bool FTimeout(LegacyLong *interval) const;
    static LegacyLong QueueNumber(LegacyLong style, LegacyLong objectId, bool actor);
#ifdef DEBUG
    void AssertValid() const;
    void MarkMem() const;
#endif

  private:
    PDynamicArray _entries = pvNil;
};
#endif
