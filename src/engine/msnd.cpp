/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    msnd.cpp: Movie Sound class

    Primary Authors: *****, *****
    Status:  Reviewed

    A MovieSoundMSND (movie sound) owns a MIDI or WAVE child chunk, and also
    specifies what sound type (sty) this sound is, and the default
    volume for the sound.

    Here's how the chunks look:

    MovieSoundMSND
     |
     +---MIDI or WAVE (chid 0) // actual sound data

    An MovieSoundMSND chunk with no child is a "no sound" or silent sound.
    An invalid MovieSoundMSND chunk requires no child.

***************************************************************************/
#ifdef KAUAI_SDL3
#include "sdl_audio.h"
#endif
#include "soc.h"
#include "engine_movie_graph.h"
#include "engine_read_stream.h"
#include "engine_sound_writer.h"
#include "mm/wave_import.h"
#ifndef KAUAI_SDL3
#include "audioman.h"
#endif

ASSERTNAME

RTCLASS(MovieSoundMSND)
RTCLASS(MovieSoundQueue)

BEGIN_CMD_MAP(MovieSoundQueue, CommandHandler)
ON_CID_ME(cidAlarm, &MovieSoundQueue::FCmdAlarm, pvNil)
END_CMD_MAP_NIL()

MMSoundResourceServices MovieSoundMSND::_ResourceServices()
{
    MMSoundResourceServices s = {};
    s.system = koskCur;
    static_assert(sizeof(LegacyLong) == 4 && sizeof(ChunkNumber) == 4 &&
                      sizeof(MMSoundResource) == sizeof(void *) + 16,
                  "sound resource ABI");
    static_assert(stySfx == MM_SOUND_STYLE_EFFECT && stySpeech == MM_SOUND_STYLE_SPEECH &&
                      styMidi == MM_SOUND_STYLE_MUSIC && toolLooper == toolSounder + 1 &&
                      toolMatcher == toolSounder + 2,
                  "sound policy enums");
    static_assert(kctgMsnd == MM_SOUND_TYPE && kctgMidi == MM_SOUND_MIDI &&
                      kctgWave == MM_SOUND_WAVE && kchidSnd == 0 && tNo == 0 && tYes == 1,
                  "sound wire enums");
    s.owner = [](void *, void *object, MMSoundOwner *out) {
        auto sound = static_cast<PMovieSoundMSND>(object);
        *out = {&sound->_resource, &sound->_silent, &sound->_invalid};
    };
    s.create = [](void *) -> void * { return NewObj MovieSoundMSND(); };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.object_size = [](void *) -> MMI32 { return sizeof(MovieSoundMSND); };
    s.invalidate_output = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    s.file = [](void *, void *cache) -> void * { return static_cast<PChunkyResourceFile>(cache)->Pcfl(); };
    s.graph = [](void *, void *file, MMMovieSessionGraph *out) {
        *out = EngineMovieGraph::FromFile(static_cast<PChunkyFile>(file));
    };
    s.read_name = [](void *, void *file, MMU32 type, MMU32 number, void *object) -> int {
        return static_cast<PChunkyFile>(file)->FGetName(type, number, &static_cast<PMovieSoundMSND>(object)->_stn);
    };
    s.publish_invalid = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    s.binding = [](void *, void *object, void **file, MMU32 *type, MMU32 *number) {
        auto sound = static_cast<PMovieSoundMSND>(object);
        *file = sound->Pcrf()->Pcfl();
        *type = sound->Ctg();
        *number = sound->Cno();
    };
    s.put = [](void *, void *file, MMU32 type, MMU32 number, const void *header) -> int {
        return static_cast<PChunkyFile>(file)->FPutPv(const_cast<void *>(header), 16, type, number);
    };
    s.delete_child = [](void *, void *file, MMU32 type, MMU32 number, const MMChunkChild *child) {
        static_cast<PChunkyFile>(file)->DeleteChild(type, number, child->type, child->number);
    };
    s.bad_order = [](void *) { Assert(false, "bad MovieSoundFile"); };
    s.queue_number = [](void *, MMI32 style, MMI32 object, int actor) -> MMI32 {
        return SoundPlaybackQueue::QueueNumber(style, object, actor != 0);
    };
    s.playing = [](void *, MMI32 instance) -> int { return vpsndm->FPlaying(instance); };
    s.stop = [](void *, MMI32 queue) { vpsndm->StopAll(queue, sclNil); };
    s.play = [](void *, const MMSoundResource *sound, MMI32 queue, MMI32 volume, MMI32 repeat,
                MMU32 start, MMI32 priority, MMI32 soundClass) -> MMI32 {
        return vpsndm->SiiPlay(static_cast<PResourceCache>(sound->cache), sound->type, sound->number, queue, volume,
                               repeat, start, priority, soundClass);
    };
    return s;
}

/***************************************************************************

    A PFNRPO to read a MovieSoundMSND from a file

***************************************************************************/
bool MovieSoundMSND::FReadMsnd(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                               PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    const auto services = _ResourceServices();
    return mm_sound_fetch(pcrf, ctg, cno, ppbaco, pcb, &services) != 0;
}

/***************************************************************************

    Retrieve information contained in the msnd chunk

***************************************************************************/
bool MovieSoundMSND::FGetMsndInfo(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfInvalid, long *psty,
                                  long *pvlm)
{
    AssertPo(pcfl, 0);
    const auto services = _ResourceServices();
    return mm_sound_info(pcfl, ctg, cno, pfInvalid, psty, pvlm, &services) != 0;
}

/***************************************************************************

    Init a MovieSoundMSND from the given chunk of the given ChunkyFile

***************************************************************************/
bool MovieSoundMSND::_FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);
    const auto services = _ResourceServices();
    return mm_sound_read(this, pcfl, ctg, cno, &services) != 0;
}

/***************************************************************************

    Write an MovieSoundMSND MIDI chunk to file *pcfl
    ie, write the MovieSoundMSND chunk, its name, and the midi child

***************************************************************************/
bool MovieSoundMSND::FWriteMidi(PChunkyFile pcflDest, PMidiStream pmids, String *pstnName, ChunkNumber *pcno)
{
    AssertPo(pcflDest, 0);
    AssertPo(pmids, 0);
    AssertVarMem(pstnName);
    AssertVarMem(pcno);
    EngineSoundWriter writer{pcflDest, {}};
    const auto services = writer.Services();
    const auto payload = pmids->BorrowStream();
    return mm_sound_write(&payload, 0, styMidi, pstnName, pcno, &services) != 0;
}

/***************************************************************************

    Write an MovieSoundMSND Wave file to a file
    ie, write the MovieSoundMSND chunk, its name, and the midi child

***************************************************************************/
bool MovieSoundMSND::FWriteWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, String *pstnName,
                                ChunkNumber *pcno)
{
    AssertPo(pfilSrc, 0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(pstnName);
    AssertVarMem(pcno);
    EngineSoundWriter writer{pcflDest, {}};
    const auto services = writer.Services();
    const auto payload = EngineReadStream::FromFile(*pfilSrc);
    return mm_sound_write(&payload, 1, sty, pstnName, pcno, &services) != 0;
}

/***************************************************************************

    Copy the midi file to a chunk in the current movie
    The *pcno is returned

***************************************************************************/
bool MovieSoundMSND::FCopyMidi(PFileObject pfilSrc, PChunkyFile pcflDest, ChunkNumber *pcno, PString pstn)
{
    AssertPo(pfilSrc, 0);
    AssertNilOrPo(pstn, 0);
    String leaf;
    if (!pstn)
    {
        Filename filename;
        pfilSrc->GetFni(&filename);
        filename.GetLeaf(&leaf);
        pstn = &leaf;
    }
    EngineSoundWriter writer{pcflDest, {}};
    const auto services = writer.Services();
    const auto source = EngineReadStream::FromFile(*pfilSrc);
    const int result = mm_sound_import_midi(&source, pstn, pcno, &services);
    if (result == MM_SOUND_BAD_MIDI)
        PushErc(ercSocBadSoundFile);
    return result > 0;
}

/***************************************************************************

    Copy the wave file to a chunk in the current movie

***************************************************************************/
bool MovieSoundMSND::FCopyWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, ChunkNumber *pcno, PString pstn)
{
    AssertPo(pfilSrc, 0);
    AssertPo(pcflDest, 0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(pcno);
    Assert(sty != styMidi, "Illegal sty argument");
    AssertNilOrPo(pstn, 0);

    Filename filename;
    pfilSrc->GetFni(&filename);
    String leaf;
    if (!pstn)
    {
        filename.GetLeaf(&leaf);
        pstn = &leaf;
    }
    EngineSoundWriter writer{pcflDest, {}};
    const auto output = writer.Services();
    const auto source = EngineReadStream::FromFile(*pfilSrc);
#ifdef KAUAI_SDL3
    std::vector<std::uint8_t> prepared;
    MMWaveFileServices s = {};
    s.context = &prepared;
    s.prepare = [](void *p, const void *data, MMU32 bytes, const void **out, MMU32 *count) -> int {
        auto &prepared = *static_cast<std::vector<std::uint8_t> *>(p);
        if (!KauaiAudio::PrepareWaveImport(data, bytes, prepared))
            return 0;
        *out = prepared.data();
        *count = static_cast<MMU32>((prepared.size)());
        return 1;
    };
    s.bad_sound = [](void *) { PushErc(ercSocBadSoundFile); };
    try
    {
        return mm_wave_file_import(&source, sty, pstn, pcno, &s, &output) != 0;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
#else
    struct Context
    {
        String path;
        WAVEFORMATEX format = {};
        LPSOUND sound = nullptr;
        WAVEFORMATEX *encodedFormat = nullptr;
        LPBYTE samples = nullptr, encoded = nullptr;
        HACMSTREAM stream = nullptr;
        ACMSTREAMHEADER header = {};
        int ReadFormat(HRESULT opened, MMNativeWaveFormat *out)
        {
            format = {};
            if (FAILED(opened) || !sound || FAILED(sound->GetFormat(&format, sizeof(format))))
            {
                ReleasePpo(&sound);
                return 0;
            }
            out->rate = format.nSamplesPerSec;
            out->bits = format.wBitsPerSample;
            out->channels = format.nChannels;
            return 1;
        }
    } context;
    filename.GetStnPath(&context.path);
    MMNativeWaveServices s = {};
    s.context = &context;
    s.open_file = [](void *p, MMNativeWaveFormat *format) -> int {
        auto &c = *static_cast<Context *>(p);
        return c.ReadFormat(AllocSoundFromFile(&c.sound, c.path.Psz(), 0, TRUE, nullptr), format);
    };
    s.open_memory = [](void *p, unsigned char *data, MMU32 bytes, MMNativeWaveFormat *format) -> int {
        auto &c = *static_cast<Context *>(p);
        return c.ReadFormat(AllocSoundFromMemory(&c.sound, data, bytes), format);
    };
    s.convert = [](void *p) -> int {
        auto &c = *static_cast<Context *>(p);
        auto &f = c.format;
        f.nSamplesPerSec = 11025;
        f.nAvgBytesPerSec = 11025;
        f.wBitsPerSample = 8;
        f.nChannels = 1;
        f.nBlockAlign = 1;
        LPSOUND converted = nullptr;
        if (FAILED(AllocConvertFilter(&converted, c.sound, &f)))
            return 0;
        c.sound->Release();
        c.sound = converted;
        return 1;
    };
    s.samples = [](void *p) -> MMU32 { return static_cast<Context *>(p)->sound->GetSamples(); };
    s.activate = [](void *p) -> int { return SUCCEEDED(static_cast<Context *>(p)->sound->SetMode(TRUE, TRUE)); };
    s.allocate_samples = [](void *p, MMU32 bytes) -> int {
        return FAllocPv(reinterpret_cast<void **>(&static_cast<Context *>(p)->samples), bytes, fmemNil, mprNormal);
    };
    s.read_samples = [](void *p, MMU32 *samples) -> int {
        auto &c = *static_cast<Context *>(p);
        DWORD count = *samples;
        const bool ok = SUCCEEDED(c.sound->GetSampleData(c.samples, 0, &count, nullptr));
        *samples = count;
        return ok;
    };
    s.compress = [](void *p, MMU32 bytes, const void **format, MMU32 *formatBytes, const void **data,
                    MMU32 *dataBytes) -> int {
        auto &c = *static_cast<Context *>(p);
        DWORD size;
        // The ACM adapter performs one external encode operation. C controls
        // import policy, container writing and which physical file is retained.
        if (acmMetrics(nullptr, ACM_METRIC_MAX_SIZE_FORMAT, &size))
            return -1;
        if (!FAllocPv(reinterpret_cast<void **>(&c.encodedFormat), size, fmemNil, mprNormal) ||
            !FAllocPv(reinterpret_cast<void **>(&c.encoded), bytes, fmemNil, mprNormal))
            return 0;
        auto &f = *c.encodedFormat;
        f.wFormatTag = WAVE_FORMAT_ADPCM;
        f.nChannels = 1;
        f.nSamplesPerSec = 11025;
        f.cbSize = static_cast<WORD>(size - sizeof(WAVEFORMATEX));
        if (acmFormatSuggest(nullptr, &c.format, &f, size,
                             ACM_FORMATSUGGESTF_WFORMATTAG | ACM_FORMATSUGGESTF_NCHANNELS |
                                 ACM_FORMATSUGGESTF_NSAMPLESPERSEC) ||
            acmStreamOpen(&c.stream, nullptr, &c.format, &f, nullptr, 0, 0, ACM_STREAMOPENF_NONREALTIME))
            return -1;
        auto &h = c.header;
        h.cbStruct = sizeof(h);
        h.pbSrc = c.samples;
        h.cbSrcLength = bytes;
        h.pbDst = c.encoded;
        h.cbDstLength = bytes;
        if (acmStreamPrepareHeader(c.stream, &h, 0))
            return -1;
        const auto converted = acmStreamConvert(c.stream, &h, ACM_STREAMCONVERTF_END);
        const auto unprepared = acmStreamUnprepareHeader(c.stream, &h, 0);
        if (converted || unprepared)
            return -1;
        acmStreamClose(c.stream, 0);
        c.stream = nullptr;
        *format = &f;
        *formatBytes = sizeof(WAVEFORMATEX) + f.cbSize;
        *data = c.encoded;
        *dataBytes = h.cbDstLengthUsed;
        return 1;
    };
    s.bad_sound = [](void *) { PushErc(ercSocBadSoundFile); };
    s.cleanup = [](void *p) {
        auto &c = *static_cast<Context *>(p);
        if (c.stream)
        {
            acmStreamClose(c.stream, 0);
            c.stream = nullptr;
        }
        if (c.sound)
            c.sound->SetMode(FALSE, TRUE);
        ReleasePpo(&c.sound);
        FreePpv(reinterpret_cast<void **>(&c.encodedFormat));
        FreePpv(reinterpret_cast<void **>(&c.samples));
        FreePpv(reinterpret_cast<void **>(&c.encoded));
    };
    return mm_wave_native_import(&source, sty, pstn, pcno, &s, &output) != 0;
#endif
}

/***************************************************************************

    Invalidate a sound

***************************************************************************/
bool MovieSoundMSND::FInvalidate(void)
{
    AssertThis(0);
    const auto services = _ResourceServices();
    return mm_sound_invalidate(this, &services) != 0;
}

/***************************************************************************

    Clean up and delete this movie sound

***************************************************************************/
MovieSoundMSND::~MovieSoundMSND(void)
{
    AssertBaseThis(0);
}

/***************************************************************************

    Return the sqn for an msnd attached to an actor object of
    id == objid
    Static function

***************************************************************************/
long MovieSoundMSND::SqnActr(long sty, long objid)
{
    return SoundPlaybackQueue::QueueNumber(sty, objid, true);
}

/***************************************************************************

    Return the sqn for an msnd attached to an actor object of
    id == objid
    Static function

***************************************************************************/
long MovieSoundMSND::SqnBkgd(long sty, long objid)
{
    return SoundPlaybackQueue::QueueNumber(sty, objid, false);
}

/***************************************************************************

    Return the priority for a tool,sty combination

***************************************************************************/
long MovieSoundMSND::Spr(long tool)
{
    AssertThis(0);
    Assert(tool == toolMatcher || tool == toolSounder || tool == toolLooper, "Invalid tool");
    Assert(_resource.style != styMidi || tool != toolMatcher, "No midi motion matching");
    AssertIn(_resource.style, stySfx, styLim);
    return mm_sound_priority(_resource.style, static_cast<int>(tool - toolSounder));
}

/***************************************************************************

    Play this sound

***************************************************************************/
void MovieSoundMSND::Play(long objID, bool fLoop, bool fQueue, long vlm, long spr, bool fActr, ulong dtsStart)
{
    AssertThis(0);
    static MMSoundLastMidi last = {};
    const auto services = _ResourceServices();
    mm_sound_play(this, &last, objID, fLoop, fQueue, vlm, spr, fActr, dtsStart, &services);
}

/***************************************************************************

    New MovieSoundQueue

***************************************************************************/
PMovieSoundQueue MovieSoundQueue::PmsqNew(void)
{
    PMovieSoundQueue pmsq;
    if (pvNil == (pmsq = NewObj MovieSoundQueue(khidMsq)))
        return pvNil;

    if (!pmsq->_queue.FInit())
    {
        ReleasePpo(&pmsq);
        return pvNil;
    }

    if (pvNil == (pmsq->_pclok = NewObj Clock(khidMsqClock)))
    {
        ReleasePpo(&pmsq);
        return pvNil;
    }

    return pmsq;
}

/***************************************************************************

    Enqueue a sound	in the MovieSoundQueue.  Overwrites sounds of the same type.

***************************************************************************/
bool MovieSoundQueue::FEnqueue(PMovieSoundMSND pmsnd, long objID, bool fLoop, bool fQueue, long vlm, long spr,
                               bool fActr, ulong dtsStart, bool fLowPri)
{
    AssertThis(0);
    AssertPo(pmsnd, 0);
    SoundPlaybackRequest request;
    request.sound = pmsnd;
    request.style = pmsnd->Sty();
    request.objectId = objID;
    request.loop = fLoop;
    request.queue = fQueue;
    request.volume = vlm;
    request.priority = spr;
    request.actor = fActr;
    request.startOffset = dtsStart;
    return _queue.FEnqueue(request, fLowPri);
}

/***************************************************************************

    Dequeue and Play the MovieSoundQueue sounds
    If _dtim == kdtimOff, empty the queue

***************************************************************************/
void MovieSoundQueue::PlayMsq(void)
{
    AssertThis(0);
    switch (_queue.Dispatch())
    {
    case SoundQueueDispatch::Empty:
        return;
    case SoundQueueDispatch::Flush:
        _queue.Clear();
        return;
    case SoundQueueDispatch::Play:
        break;
    }
    vpsndm->BeginSynch();
    SoundPlaybackRequest request;
    while (_queue.FDequeue(&request))
    {
        ((PMovieSoundMSND)request.sound)
            ->Play(request.objectId, request.loop, request.queue, request.volume, request.priority, request.actor,
                   request.startOffset);
        ReleasePpo(&request.sound);
    }
    vpsndm->EndSynch();
    LegacyLong interval;
    if (_queue.FTimeout(&interval))
    {
        _pclok->Start(0);
        if (!_pclok->FSetAlarm(interval, this))
            StopAll();
    }
}

/***************************************************************************

    Flush Queue	 -  without playing the sounds

***************************************************************************/
void MovieSoundQueue::FlushMsq(void)
{
    AssertThis(0);
    _queue.Clear();
}

/***************************************************************************

    FCmdAlarm - Timeout has elapsed.  Stop all sounds

***************************************************************************/
bool MovieSoundQueue::FCmdAlarm(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    _pclok->Stop();
    StopAll();
    return fTrue;
}

/***************************************************************************

    Clean up and delete this movie sound queue

***************************************************************************/
MovieSoundQueue::~MovieSoundQueue(void)
{
    AssertBaseThis(0);
    StopAll();
    FlushMsq();
    ReleasePpo(&_pclok);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the MovieSoundMSND.
***************************************************************************/
void MovieSoundMSND::AssertValid(LegacyUlong grf)
{
    MovieSoundMSND_PAR::AssertValid(fobjAllocated);
    AssertNilOrPo(static_cast<PResourceCache>(_resource.cache), 0);
    AssertIn(_resource.style, 0, styLim);
}

/***************************************************************************
    Mark memory used by the MovieSoundMSND
***************************************************************************/
void MovieSoundMSND::MarkMem(void)
{
    AssertThis(0);
    MovieSoundMSND_PAR::MarkMem();
    // The weak _resource.cache marks us; marking it here would cause
    // an infinite recursive loop.
}

/***************************************************************************
    Assert the validity of the MovieSoundQueue.
***************************************************************************/
void MovieSoundQueue::AssertValid(LegacyUlong grf)
{
    MovieSoundQueue_PAR::AssertValid(fobjAllocated);
    _queue.AssertValid();
    AssertPo(_pclok, 0);
}

/***************************************************************************
    Mark memory used by the MovieSoundMSND
***************************************************************************/
void MovieSoundQueue::MarkMem(void)
{
    AssertThis(0);
    MovieSoundQueue_PAR::MarkMem();
    _queue.MarkMem();
    MarkMemObj(_pclok);
}

#endif // DEBUG
