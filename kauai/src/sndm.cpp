/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Sound manager class implementation.

***************************************************************************/
#include "kauai_core.h"
#include "sndm.h"
#include "three_mm/wide.h"
ASSERTNAME

RTCLASS(SoundDevice)
RTCLASS(SoundManager)
RTCLASS(SoundManagerQueue)
RTCLASS(SoundQueue)

LegacyLong SoundDevice::_siiLast;

/***************************************************************************
    This is the volume to use as the system volume if it is determined
    by a device that the system lies when asked what the current
    volume setting is. Eg, for many drivers, midiOutGetVolume on Win95
    always returns full volume. When we determine that this is the case,
    we'll use this value instead. Any device that determines that its
    reading is valid should set this so other devices can use the same
    value.
***************************************************************************/
LegacyUlong vluSysVolFake = (LegacyUlong)-1;

/***************************************************************************
    Start a synchronized group.
***************************************************************************/
void SoundDevice::BeginSynch(void)
{
}

/***************************************************************************
    End a synchronized group.
***************************************************************************/
void SoundDevice::EndSynch(void)
{
}

/***************************************************************************
    Constructor for the sound manager.
***************************************************************************/
SoundManager::SoundManager(void)
{
}

/***************************************************************************
    Destructor for the sound manager.
***************************************************************************/
SoundManager::~SoundManager(void)
{
    if (_pglsndmpe)
    {
        auto s = _Services();
        three_mm_sound_endpoints_release(&s, 0);
        ReleasePpo(&_pglsndmpe);
    }
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a SoundManager.
***************************************************************************/
void SoundManager::AssertValid(LegacyUlong grf)
{
    SoundManager_PAR::AssertValid(0);
    AssertPo(_pglsndmpe, 0);
}

/***************************************************************************
    Mark memory for the SoundManager.
***************************************************************************/
void SoundManager::MarkMem(void)
{
    AssertValid(0);
    LegacyLong isndmpe;
    SNDMPE sndmpe;

    SoundManager_PAR::MarkMem();
    MarkMemObj(_pglsndmpe);
    for (isndmpe = 0; isndmpe < _pglsndmpe->IvMac(); isndmpe++)
    {
        _pglsndmpe->Get(isndmpe, &sndmpe);
        MarkMemObj(sndmpe.psndv);
    }
}
#endif // DEBUG

/***************************************************************************
    Create the sound manager.
***************************************************************************/
PSoundManager SoundManager::PsndmNew(void)
{
    PSoundManager psndm;

    if (pvNil == (psndm = NewObj SoundManager))
        return pvNil;

    if (!psndm->_FInit())
        ReleasePpo(&psndm);

    AssertNilOrPo(psndm, 0);
    return psndm;
}

/***************************************************************************
    Initialize the sound manager.
***************************************************************************/
bool SoundManager::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglsndmpe = DynamicArray::PglNew(size(SNDMPE))))
        return fFalse;

    _pglsndmpe->SetMinGrow(1);
    _control.active = fTrue;

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Find the device that sounds of the given ctg are to be played on.
***************************************************************************/
bool SoundManager::_FFindCtg(ChunkTagOrType ctg, SNDMPE *psndmpe, LegacyLong *pisndmpe)
{
    auto s = _Services();
    ThreeMMSoundEndpoint e;
    ThreeMMI32 i;
    if (!three_mm_sound_manager_find(&s, ctg, &e, &i))
    {
        TrashVar(psndmpe);
        if (pisndmpe)
            *pisndmpe = i;
        return false;
    }
    if (psndmpe)
        *psndmpe = {e.tag, static_cast<PSoundDevice>(e.object)};
    if (pisndmpe)
        *pisndmpe = i;
    return true;
}

/***************************************************************************
    Add a device to the device map to handle the particular ctg.
***************************************************************************/
bool SoundManager::FAddDevice(ChunkTagOrType ctg, PSoundDevice psndv)
{
    auto s = _Services();
    return three_mm_sound_manager_add(&s, &_control, ctg, psndv) != 0;
}

/***************************************************************************
    Return the sound device that is registered for the given ctg.
***************************************************************************/
PSoundDevice SoundManager::PsndvFromCtg(ChunkTagOrType ctg)
{
    AssertThis(0);
    SNDMPE sndmpe;

    if (!_FFindCtg(ctg, &sndmpe))
        return pvNil;

    return sndmpe.psndv;
}

/***************************************************************************
    Remove the sound device for the given ctg.
***************************************************************************/
void SoundManager::RemoveSndv(ChunkTagOrType ctg)
{
    auto s = _Services();
    three_mm_sound_manager_remove(&s, ctg);
}

/***************************************************************************
    Return whether the sound manager is active.
***************************************************************************/
bool SoundManager::FActive(void)
{
    AssertThis(0);

    return _control.active;
}

/***************************************************************************
    Activate or deactivate the sound manager.
***************************************************************************/
void SoundManager::Activate(bool fActive)
{
    auto s = _Services();
    three_mm_sound_activate(&s, &_control, fActive, 0);
}

/***************************************************************************
    Suspend or resume the sound manager.
***************************************************************************/
void SoundManager::Suspend(bool fSuspend)
{
    auto s = _Services();
    three_mm_sound_suspend(&s, &_control, fSuspend, 0);
}

/***************************************************************************
    Set the volume of all the devices.
***************************************************************************/
void SoundManager::SetVlm(LegacyLong vlm)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_VOLUME, 0, 0, vlm, 0);
}

/***************************************************************************
    Get the max of the volumes of all the devices.
***************************************************************************/
LegacyLong SoundManager::VlmCur(void)
{
    auto s = _Services();
    return three_mm_sound_dispatch(&s, THREE_MM_SOUND_GET_VOLUME, 0, 0, 0, 0);
}

/***************************************************************************
    Play the given sound.
***************************************************************************/
LegacyLong SoundManager::SiiPlay(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong sqn,
                                 LegacyLong vlm, LegacyLong cactPlay, LegacyUlong dtsStart, LegacyLong spr,
                                 LegacyLong scl)
{
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
    if (_playObserver)
        _playObserver(ctg, cno, sqn, vlm, cactPlay, dtsStart, spr, scl);
#endif
    ThreeMMSoundPlay p = {prca, ctg, cno, sqn, vlm, cactPlay, dtsStart, spr, scl};
    auto s = _Services();
    return three_mm_sound_manager_play(&s, &p, _sclFirst, _sclLast);
}

/***************************************************************************
    Stop the given sound instance.
***************************************************************************/
void SoundManager::Stop(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_STOP, 0, 0, sii, 0);
}

/***************************************************************************
    Stop all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SoundManager::StopAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_STOP, 0, 0, sqn, scl);
}

/***************************************************************************
    Pause the given sound.
***************************************************************************/
void SoundManager::Pause(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_PAUSE, 0, 0, sii, 0);
}

/***************************************************************************
    Pause all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SoundManager::PauseAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_PAUSE, 0, 0, sqn, scl);
}

/***************************************************************************
    Resume the given sound.
***************************************************************************/
void SoundManager::Resume(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_RESUME, 0, 0, sii, 0);
}

/***************************************************************************
    Resume all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SoundManager::ResumeAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_RESUME, 0, 0, sqn, scl);
}

/***************************************************************************
    Return whether the given sound is playing.
***************************************************************************/
bool SoundManager::FPlaying(LegacyLong sii)
{
    auto s = _Services();
    return three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_PLAYING, 0, 0, sii, 0) != 0;
}

/***************************************************************************
    Return whether any sounds of the given queue and class are playing
    (one or both of (sqn, scl) may be nil).
***************************************************************************/
bool SoundManager::FPlayingAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    return three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_PLAYING, 0, 0, sqn, scl) != 0;
}

/***************************************************************************
    Free anything that's no longer in use.
***************************************************************************/
void SoundManager::Flush(void)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_FLUSH, 0, 0, 0, 0);
}

/***************************************************************************
    Start a synchronized group.
***************************************************************************/
void SoundManager::BeginSynch(void)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_BEGIN, 0, 0, 0, 0);
}

/***************************************************************************
    End a synchronized group.
***************************************************************************/
void SoundManager::EndSynch(void)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_END, 0, 0, 0, 0);
}

/***************************************************************************
    A convenient base class for a multiple queue sound device.
***************************************************************************/

/***************************************************************************
    Destructor for a multiple queue sound device.
***************************************************************************/
SoundManagerQueue::~SoundManagerQueue(void)
{
    if (_pglsnqd)
    {
        auto s = _Services();
        three_mm_sound_endpoints_release(&s, 1);
        ReleasePpo(&_pglsnqd);
    }
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a SoundManagerQueue.
***************************************************************************/
void SoundManagerQueue::AssertValid(LegacyUlong grf)
{
    SoundManagerQueue_PAR::AssertValid(0);
    AssertPo(_pglsnqd, 0);
}

/***************************************************************************
    Mark memory for the SoundManagerQueue.
***************************************************************************/
void SoundManagerQueue::MarkMem(void)
{
    AssertValid(0);
    LegacyLong isnqd;
    SNQD snqd;

    SoundManagerQueue_PAR::MarkMem();
    MarkMemObj(_pglsnqd);
    for (isnqd = 0; isnqd < _pglsnqd->IvMac(); isnqd++)
    {
        _pglsnqd->Get(isnqd, &snqd);
        MarkMemObj(snqd.psnque);
    }
}
#endif // DEBUG

/***************************************************************************
    Initialize the multiple queue device.
***************************************************************************/
bool SoundManagerQueue::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglsnqd = DynamicArray::PglNew(size(SNQD))))
        return fFalse;

    _control.active = fTrue;
    AssertThis(0);

    return fTrue;
}

/***************************************************************************
    Ensure a queue exists for the given sqn and return it.
***************************************************************************/
bool SoundManagerQueue::_FEnsureQueue(LegacyLong sqn, SNQD *psnqd, LegacyLong *pisnqd)
{
    auto s = _Services();
    ThreeMMSoundEndpoint e;
    ThreeMMI32 i;
    if (!three_mm_sound_ensure_queue(&s, sqn, ksqnNone, sclNil, &e, &i))
    {
        TrashVar(psnqd);
        TrashVar(pisnqd);
        return false;
    }
    if (psnqd)
        *psnqd = {static_cast<PSoundQueue>(e.object), three_mm_signed_word(e.tag)};
    if (pisnqd)
        *pisnqd = i;
    return true;
}

/***************************************************************************
    Return whether the device is active.
***************************************************************************/
bool SoundManagerQueue::FActive(void)
{
    AssertThis(0);

    return _control.active;
}

/***************************************************************************
    Activate or deactivate the device.
***************************************************************************/
void SoundManagerQueue::Activate(bool fActive)
{
    auto s = _Services();
    three_mm_sound_activate(&s, &_control, fActive, 1);
}

/***************************************************************************
    Suspend or resume the device.
***************************************************************************/
void SoundManagerQueue::Suspend(bool fSuspend)
{
    auto s = _Services();
    three_mm_sound_suspend(&s, &_control, fSuspend, 1);
}

/***************************************************************************
    Play the given sound.
***************************************************************************/
LegacyLong SoundManagerQueue::SiiPlay(PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong sqn,
                                      LegacyLong vlm, LegacyLong cactPlay, LegacyUlong dtsStart, LegacyLong spr,
                                      LegacyLong scl)
{
    AssertThis(0);
    AssertPo(prca, 0);
    LegacyLong isnqd;
    SNQD snqd;
    LegacyLong sii = _SiiAlloc();

    if (sqn == sqnNil)
        sqn = ksqnNone;

    if (_FEnsureQueue(sqn, &snqd, &isnqd))
    {
        snqd.psnque->Enqueue(sii, prca, ctg, cno, vlm, cactPlay, dtsStart, spr, scl);
    }
    else
        Warn("couldn't allocate queue");

    return sii;
}

/***************************************************************************
    Stop the given sound instance.
***************************************************************************/
void SoundManagerQueue::Stop(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_STOP, 0, 0, sii, 0);
}

/***************************************************************************
    Stop all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SoundManagerQueue::StopAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_STOP, sqn != sqnNil, (ThreeMMU32)sqn, scl, 0);
}

/***************************************************************************
    Pause the given sound.
***************************************************************************/
void SoundManagerQueue::Pause(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_PAUSE, 0, 0, sii, 0);
}

/***************************************************************************
    Pause all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SoundManagerQueue::PauseAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_PAUSE, sqn != sqnNil, (ThreeMMU32)sqn, scl, 0);
}

/***************************************************************************
    Resume the given sound.
***************************************************************************/
void SoundManagerQueue::Resume(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_RESUME, 0, 0, sii, 0);
}

/***************************************************************************
    Resume all sounds of the given queue and class (one or both may be nil).
***************************************************************************/
void SoundManagerQueue::ResumeAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_RESUME, sqn != sqnNil, (ThreeMMU32)sqn, scl, 0);
}

/***************************************************************************
    Return whether the given sound is playing.
***************************************************************************/
bool SoundManagerQueue::FPlaying(LegacyLong sii)
{
    auto s = _Services();
    return three_mm_sound_dispatch(&s, THREE_MM_SOUND_INSTANCE_PLAYING, 0, 0, sii, 0) != 0;
}

/***************************************************************************
    Return whether any sounds of the given queue and class are playing
    (one or both of (sqn, scl) may be nil).
***************************************************************************/
bool SoundManagerQueue::FPlayingAll(LegacyLong sqn, LegacyLong scl)
{
    auto s = _Services();
    return three_mm_sound_dispatch(&s, THREE_MM_SOUND_ALL_PLAYING, sqn != sqnNil, (ThreeMMU32)sqn, scl, 0) != 0;
}

/***************************************************************************
    Free anything that's no longer being used.
***************************************************************************/
void SoundManagerQueue::Flush()
{
    auto s = _Services();
    three_mm_sound_queues_flush(&s, sclNil);
}

/***************************************************************************
    Constructor for an sound queue.
***************************************************************************/
SoundQueue::SoundQueue(void)
{
}

/***************************************************************************
    Destructor for an sound queue.
***************************************************************************/
SoundQueue::~SoundQueue(void)
{
    AssertBaseThis(0);

    _Enter();
    if (pvNil != _pglsndin)
    {
        LegacyLong isndin;
        SNDIN sndin;

        for (isndin = _pglsndin->IvMac(); isndin-- > 0;)
        {
            _pglsndin->Get(isndin, &sndin);
            ReleasePpo(&sndin.pbaco);
        }
        ReleasePpo(&_pglsndin);
    }
    _Leave();
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a SoundQueue.
***************************************************************************/
void SoundQueue::AssertValid(LegacyUlong grf)
{
    SoundQueue_PAR::AssertValid(0);

    _Enter();
    AssertPo(_pglsndin, 0);
    AssertIn(_isndinCur, 0, _pglsndin->IvMac() + 1);
    _Leave();
}

/***************************************************************************
    Mark memory for the SoundQueue.
***************************************************************************/
void SoundQueue::MarkMem(void)
{
    AssertValid(0);
    LegacyLong isndin;
    SNDIN sndin;

    SoundQueue_PAR::MarkMem();

    _Enter();
    MarkMemObj(_pglsndin);
    for (isndin = 0; isndin < _pglsndin->IvMac(); isndin++)
    {
        _pglsndin->Get(isndin, &sndin);
        MarkMemObj(sndin.pbaco);
    }
    _Leave();
}
#endif // DEBUG

/***************************************************************************
    Initialize the sound queue. Allocate the _pglsndin.
***************************************************************************/
bool SoundQueue::_FInit(void)
{
    AssertBaseThis(0);

    if (pvNil == (_pglsndin = DynamicArray::PglNew(size(SNDIN))))
        return fFalse;

    AssertThis(0);
    return fTrue;
}

/***************************************************************************
    Enter any critical section that's necessary to ensure access to
    member variables.
***************************************************************************/
void SoundQueue::_Enter(void)
{
    AssertBaseThis(0);
}

/***************************************************************************
    Leave any critical section that's necessary to ensure access to
    member variables.
***************************************************************************/
void SoundQueue::_Leave(void)
{
    AssertBaseThis(0);
}

/***************************************************************************
    Free any sounds below _isndinCur.
***************************************************************************/
void SoundQueue::_Flush(void)
{
    auto s = _Services();
    three_mm_sound_queue_flush(&s);
}

/***************************************************************************
    Put the given sound on the queue.
***************************************************************************/
void SoundQueue::Enqueue(LegacyLong sii, PResourceCache prca, ChunkTagOrType ctg, ChunkNumber cno, LegacyLong vlm,
                         LegacyLong cactPlay, LegacyUlong dtsStart, LegacyLong spr, LegacyLong scl)
{
    AssertThis(0);
    AssertPo(prca, 0);
    auto object = _PbacoFetch(prca, ctg, cno);
    if (!object)
        return;
    ThreeMMSoundEntry e = {object, sii, vlm, cactPlay, dtsStart, spr, scl, 0};
    auto s = _Services();
    three_mm_sound_queue_enqueue(&s, &e);
}

/***************************************************************************
    Return the priority of the frontmost sound in the queue.
***************************************************************************/
LegacyLong SoundQueue::SprCur(void)
{
    auto s = _Services();
    return three_mm_sound_queue_priority(&s);
}

/***************************************************************************
    If the given sound is in our queue, nuke it.
***************************************************************************/
void SoundQueue::Stop(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_STOP, 0, sii, sclNil);
}

/***************************************************************************
    Nuke all sounds of the given sound class. sclNil means nuke all.
***************************************************************************/
void SoundQueue::StopAll(LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_STOP, 1, scl, sclNil);
}

/***************************************************************************
    If the given sound is in our queue, pause it.
***************************************************************************/
void SoundQueue::Pause(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_PAUSE, 0, sii, sclNil);
}

/***************************************************************************
    Pause all sounds of the given sound class. sclNil means nuke all.
***************************************************************************/
void SoundQueue::PauseAll(LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_PAUSE, 1, scl, sclNil);
}

/***************************************************************************
    If the given sound is in our queue, make sure it's not paused.
***************************************************************************/
void SoundQueue::Resume(LegacyLong sii)
{
    auto s = _Services();
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_RESUME, 0, sii, sclNil);
}

/***************************************************************************
    Resume all sounds of the given sound class. sclNil means nuke all.
***************************************************************************/
void SoundQueue::ResumeAll(LegacyLong scl)
{
    auto s = _Services();
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_RESUME, 1, scl, sclNil);
}

/***************************************************************************
    Return whether the given sound is in our queue.
***************************************************************************/
bool SoundQueue::FPlaying(LegacyLong sii)
{
    auto s = _Services();
    return three_mm_sound_queue_playing(&s, 0, sii, sclNil) != 0;
}

/***************************************************************************
    Return whether any sounds of the given sound class are in our queue.
    sclNil means any sounds at all.
***************************************************************************/
bool SoundQueue::FPlayingAll(LegacyLong scl)
{
    auto s = _Services();
    return three_mm_sound_queue_playing(&s, 1, scl, sclNil) != 0;
}

/***************************************************************************
    Free anything that's not being used. This should only be called from
    the main thread.
***************************************************************************/
void SoundQueue::Flush(void)
{
    AssertThis(0);

    _Flush();
}

/***************************************************************************
    Scale the given system volume by the given Kauai volume.
***************************************************************************/
LegacyUlong LuVolScale(LegacyUlong luVol, LegacyLong vlm)
{
    return three_mm_sound_volume_scale(luVol, vlm);
}

namespace
{
ThreeMMSoundEntry SoundEntry(const SNDIN &e)
{
    return {e.pbaco, e.sii, e.vlm, e.cactPlay, e.dtsStart, e.spr, e.scl, e.cactPause};
}
SNDIN NativeEntry(const ThreeMMSoundEntry &e)
{
    return {static_cast<PBaseCacheableObject>(e.object),
            e.id,
            e.volume,
            e.plays,
            e.offset,
            e.priority,
            e.sound_class,
            e.pause};
}
} // namespace
ThreeMMSoundQueueServices SoundQueue::_Services()
{
    ThreeMMSoundQueueServices s = {};
    s.context = this;
    s.enter = [](void *p) { static_cast<SoundQueue *>(p)->_Enter(); };
    s.leave = [](void *p) { static_cast<SoundQueue *>(p)->_Leave(); };
    s.flush = [](void *p) { static_cast<SoundQueue *>(p)->_Flush(); };
    s.count = [](void *p) -> ThreeMMI32 { return static_cast<SoundQueue *>(p)->_pglsndin->IvMac(); };
    s.current = [](void *p) -> ThreeMMI32 { return static_cast<SoundQueue *>(p)->_isndinCur; };
    s.set_current = [](void *p, ThreeMMI32 n) { static_cast<SoundQueue *>(p)->_isndinCur = n; };
    s.get = [](void *p, ThreeMMI32 i, ThreeMMSoundEntry *out) {
        SNDIN e;
        static_cast<SoundQueue *>(p)->_pglsndin->Get(i, &e);
        *out = SoundEntry(e);
    };
    s.put = [](void *p, ThreeMMI32 i, const ThreeMMSoundEntry *in) {
        auto e = NativeEntry(*in);
        static_cast<SoundQueue *>(p)->_pglsndin->Put(i, &e);
    };
    s.add = [](void *p, const ThreeMMSoundEntry *in, ThreeMMI32 *index) -> int {
        auto e = NativeEntry(*in);
        LegacyLong native;
        if (!static_cast<SoundQueue *>(p)->_pglsndin->FAdd(&e, &native))
            return 0;
        *index = native;
        return 1;
    };
    s.erase = [](void *p, ThreeMMI32 i) { static_cast<SoundQueue *>(p)->_pglsndin->Delete(i); };
    s.release = [](void *, void *object) { static_cast<PBaseCacheableObject>(object)->Release(); };
    s.changed = [](void *p, int op, ThreeMMI32 i) {
        auto q = static_cast<SoundQueue *>(p);
        switch (op)
        {
        case THREE_MM_SOUND_STOP:
            q->_Queue(i);
            break;
        case THREE_MM_SOUND_PAUSE:
            q->_PauseQueue(i);
            break;
        case THREE_MM_SOUND_RESUME:
            q->_ResumeQueue(i);
            break;
        }
    };
    return s;
}

ThreeMMSoundManagerServices SoundManager::_Services()
{
    ThreeMMSoundManagerServices s = {};
    s.context = this;
    s.count = [](void *p) -> ThreeMMI32 { return static_cast<SoundManager *>(p)->_pglsndmpe->IvMac(); };
    s.get = [](void *p, ThreeMMI32 i, ThreeMMSoundEndpoint *out) {
        SNDMPE e;
        static_cast<SoundManager *>(p)->_pglsndmpe->Get(i, &e);
        *out = {e.psndv, e.ctg};
    };
    s.put = [](void *p, ThreeMMI32 i, const ThreeMMSoundEndpoint *in) {
        SNDMPE e = {in->tag, static_cast<PSoundDevice>(in->object)};
        static_cast<SoundManager *>(p)->_pglsndmpe->Put(i, &e);
    };
    s.insert = [](void *p, ThreeMMI32 i, const ThreeMMSoundEndpoint *in) -> int {
        SNDMPE e = {in->tag, static_cast<PSoundDevice>(in->object)};
        return static_cast<SoundManager *>(p)->_pglsndmpe->FInsert(i, &e);
    };
    s.erase = [](void *p, ThreeMMI32 i) { static_cast<SoundManager *>(p)->_pglsndmpe->Delete(i); };
    s.retain = [](void *, void *p) { static_cast<PSoundDevice>(p)->AddRef(); };
    s.release = [](void *, void *p) { static_cast<PSoundDevice>(p)->Release(); };
    s.allocate_id = [](void *) -> ThreeMMI32 { return _SiiAlloc(); };
    s.play = [](void *, void *o, const ThreeMMSoundPlay *p) -> ThreeMMI32 {
        return static_cast<PSoundDevice>(o)->SiiPlay(static_cast<PResourceCache>(p->cache), p->type, p->number,
                                                     p->queue, p->volume, p->plays, p->offset, p->priority,
                                                     p->sound_class);
    };
    s.call = [](void *, void *p, int op, ThreeMMI32 a, ThreeMMI32 b) -> ThreeMMI32 {
        auto d = static_cast<PSoundDevice>(p);
        switch (op)
        {
        case THREE_MM_SOUND_ACTIVE:
            d->Activate(a != 0);
            break;
        case THREE_MM_SOUND_SUSPEND:
            d->Suspend(a != 0);
            break;
        case THREE_MM_SOUND_VOLUME:
            d->SetVlm(a);
            break;
        case THREE_MM_SOUND_GET_VOLUME:
            return d->VlmCur();
        case THREE_MM_SOUND_INSTANCE_STOP:
            d->Stop(a);
            break;
        case THREE_MM_SOUND_ALL_STOP:
            d->StopAll(a, b);
            break;
        case THREE_MM_SOUND_INSTANCE_PAUSE:
            d->Pause(a);
            break;
        case THREE_MM_SOUND_ALL_PAUSE:
            d->PauseAll(a, b);
            break;
        case THREE_MM_SOUND_INSTANCE_RESUME:
            d->Resume(a);
            break;
        case THREE_MM_SOUND_ALL_RESUME:
            d->ResumeAll(a, b);
            break;
        case THREE_MM_SOUND_INSTANCE_PLAYING:
            return d->FPlaying(a);
        case THREE_MM_SOUND_ALL_PLAYING:
            return d->FPlayingAll(a, b);
        case THREE_MM_SOUND_FLUSH:
            d->Flush();
            break;
        case THREE_MM_SOUND_BEGIN:
            d->BeginSynch();
            break;
        case THREE_MM_SOUND_END:
            d->EndSynch();
            break;
        }
        return 0;
    };
    return s;
}
ThreeMMSoundManagerServices SoundManagerQueue::_Services()
{
    ThreeMMSoundManagerServices s = {};
    s.context = this;
    s.count = [](void *p) -> ThreeMMI32 { return static_cast<SoundManagerQueue *>(p)->_pglsnqd->IvMac(); };
    s.get = [](void *p, ThreeMMI32 i, ThreeMMSoundEndpoint *out) {
        SNQD e;
        static_cast<SoundManagerQueue *>(p)->_pglsnqd->Get(i, &e);
        *out = {e.psnque, (ThreeMMU32)e.sqn};
    };
    s.put = [](void *p, ThreeMMI32 i, const ThreeMMSoundEndpoint *in) {
        SNQD e = {static_cast<PSoundQueue>(in->object), three_mm_signed_word(in->tag)};
        static_cast<SoundManagerQueue *>(p)->_pglsnqd->Put(i, &e);
    };
    s.insert = [](void *p, ThreeMMI32 i, const ThreeMMSoundEndpoint *in) -> int {
        SNQD e = {static_cast<PSoundQueue>(in->object), three_mm_signed_word(in->tag)};
        return static_cast<SoundManagerQueue *>(p)->_pglsnqd->FInsert(i, &e);
    };
    s.erase = [](void *p, ThreeMMI32 i) { static_cast<SoundManagerQueue *>(p)->_pglsnqd->Delete(i); };
    s.release = [](void *, void *p) { static_cast<PSoundQueue>(p)->Release(); };
    s.create_queue = [](void *p) -> void * { return static_cast<SoundManagerQueue *>(p)->_PsnqueNew(); };
    s.call = [](void *context, void *p, int op, ThreeMMI32 a, ThreeMMI32) -> ThreeMMI32 {
        if (op == THREE_MM_SOUND_SUSPEND)
        {
            static_cast<SoundManagerQueue *>(context)->_Suspend(a != 0);
            return 0;
        }
        auto q = static_cast<PSoundQueue>(p);
        switch (op)
        {
        case THREE_MM_SOUND_INSTANCE_STOP:
            q->Stop(a);
            break;
        case THREE_MM_SOUND_ALL_STOP:
            q->StopAll(a);
            break;
        case THREE_MM_SOUND_INSTANCE_PAUSE:
            q->Pause(a);
            break;
        case THREE_MM_SOUND_ALL_PAUSE:
            q->PauseAll(a);
            break;
        case THREE_MM_SOUND_INSTANCE_RESUME:
            q->Resume(a);
            break;
        case THREE_MM_SOUND_ALL_RESUME:
            q->ResumeAll(a);
            break;
        case THREE_MM_SOUND_INSTANCE_PLAYING:
            return q->FPlaying(a);
        case THREE_MM_SOUND_ALL_PLAYING:
            return q->FPlayingAll(a);
        case THREE_MM_SOUND_FLUSH:
            q->Flush();
            break;
        }
        return 0;
    };
    return s;
}
