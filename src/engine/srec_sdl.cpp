/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_recording.h"
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
    if (!mm_sdl_recorder_open(&_recorder, rate, channels, bytes * 8, maxMs))
    {
        PushErc(ercSocNoWaveIn);
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
    mm_sdl_recorder_destroy(_recorder);
    if (_fSoundManagerSuspended && vpsndm)
        vpsndm->Suspend(false);
}
void SoundRecorder::_UpdateStatus()
{
    if (!_recorder)
        return;
    const bool wasPlaying = _fPlaying;
    mm_sdl_recorder_suspend_preview(_recorder, vpsndm && (!vpsndm->FActive() || vpsndm->FSuspended()));
    _fRecording = mm_sdl_recorder_recording(_recorder) != 0;
    _fPlaying = mm_sdl_recorder_playing(_recorder) != 0;
    _fHaveSound = mm_sdl_recorder_wave(_recorder, nullptr) != nullptr;
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
    const bool success = mm_sdl_recorder_start(_recorder) != 0;
    _UpdateStatus();
    return success;
}
bool SoundRecorder::FStop()
{
    const bool success = _recorder && mm_sdl_recorder_stop(_recorder);
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
    mm_sdl_recorder_suspend_preview(_recorder, vpsndm && (!vpsndm->FActive() || vpsndm->FSuspended()));
    if (!mm_sdl_recorder_play(_recorder, vpsndm ? static_cast<float>(vpsndm->VlmCur()) / kvlmFull : 1))
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
    MMU32 bytes;
    const auto wave = mm_sdl_recorder_wave(_recorder, &bytes);
    const bool saved = file->FWriteRgb(const_cast<unsigned char *>(wave), bytes, 0);
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
