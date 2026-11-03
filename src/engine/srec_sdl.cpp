/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_recording.h"
#include <new>
#include "soc.h"
ASSERTNAME
RTCLASS(SoundRecorder)
PSoundRecorder SoundRecorder::PsrecNew(long rate, long channels, long bytes, ulong maxMs)
{
    auto recorder = NewObj SoundRecorder;
    if (recorder && !recorder->_FInit(rate, channels, bytes, maxMs))
        ReleasePpo(&recorder);
    return recorder;
}
bool SoundRecorder::_FInit(long rate, long channels, long bytes, ulong maxMs)
{
    _csampSec = rate;
    _cchan = channels;
    _cbSample = bytes;
    _dtsMax = maxMs;
    _fRecording = _fPlaying = _fHaveSound = false;
    if (rate <= 0 || rate > 192000 || channels < 1 || channels > 2 || (bytes != 1 && bytes != 2) || !maxMs)
        return false;
    try
    {
        _recorder = new KauaiAudio::Recorder;
        if (!_recorder->Open(static_cast<unsigned>(rate), static_cast<unsigned>(channels),
                             static_cast<unsigned>(bytes * 8), maxMs))
        {
            PushErc(ercSocNoWaveIn);
            return false;
        }
    }
    catch (const std::bad_alloc &)
    {
        PushErc(ercOomNew);
        return false;
    }
    if (vpsndm)
    {
        vpsndm->Suspend(true);
        _fSoundManagerSuspended = true;
    }
    return true;
}
SoundRecorder::~SoundRecorder()
{
    delete _recorder;
    if (_fSoundManagerSuspended && vpsndm)
        vpsndm->Suspend(false);
}
void SoundRecorder::_UpdateStatus()
{
    if (!_recorder)
        return;
    const bool wasPlaying = _fPlaying;
    _recorder->SuspendPreview(vpsndm && (!vpsndm->FActive() || vpsndm->FSuspended()));
    _fRecording = _recorder->Recording();
    _fPlaying = _recorder->Playing();
    _fHaveSound = _recorder->HaveSound();
    if (wasPlaying && !_fPlaying && !_fSoundManagerSuspended && vpsndm)
    {
        vpsndm->Suspend(true);
        _fSoundManagerSuspended = true;
    }
}
bool SoundRecorder::FStart()
{
    _UpdateStatus();
    if (_fPlaying && !FStop())
        return false;
    const bool success = _recorder && _recorder->Start();
    _UpdateStatus();
    return success;
}
bool SoundRecorder::FStop()
{
    const bool success = _recorder && _recorder->Stop();
    _UpdateStatus();
    return success;
}
bool SoundRecorder::FPlay()
{
    _UpdateStatus();
    if (!_fHaveSound || _fRecording || !_recorder)
        return false;
    if (vpsndm)
        vpsndm->StopAll();
    if (_fSoundManagerSuspended && vpsndm)
    {
        vpsndm->Suspend(false);
        _fSoundManagerSuspended = false;
    }
    _recorder->SuspendPreview(vpsndm && (!vpsndm->FActive() || vpsndm->FSuspended()));
    if (!_recorder->Play(vpsndm ? static_cast<float>(vpsndm->VlmCur()) / kvlmFull : 1))
    {
        if (!_fSoundManagerSuspended && vpsndm)
        {
            vpsndm->Suspend(true);
            _fSoundManagerSuspended = true;
        }
        return false;
    }
    _fPlaying = true;
    return true;
}
bool SoundRecorder::FRecording()
{
    _UpdateStatus();
    return _fRecording;
}
bool SoundRecorder::FPlaying()
{
    _UpdateStatus();
    return _fPlaying;
}
bool SoundRecorder::FSave(PFilename filename)
{
    _UpdateStatus();
    if (!_recorder || !_fHaveSound || _fRecording || !filename)
        return false;
    auto file = FileObject::PfilCreate(filename);
    if (!file)
    {
        PushErc(ercSocWaveSaveFailure);
        return false;
    }
    const auto &wave = _recorder->Wave();
    const bool saved = file->FWriteRgb(const_cast<std::uint8_t *>(wave.data()), static_cast<long>((wave.size)()), 0);
    ReleasePpo(&file);
    if (!saved)
        PushErc(ercSocWaveSaveFailure);
    return saved;
}
#ifdef DEBUG
void SoundRecorder::AssertValid(LegacyUlong)
{
    SoundRecorder_PAR::AssertValid(fobjAllocated);
}
void SoundRecorder::MarkMem()
{
    SoundRecorder_PAR::MarkMem();
}
#endif
