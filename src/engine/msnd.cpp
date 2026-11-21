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
#include "handle_services.h"
#include "mm/actor_sound.h"
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
    s.decode = KauaiDecodeBlockHandle;
    s.graph = [](void *, void *file, MMMovieSessionGraph *out) {
        *out = EngineMovieGraph::FromFile(static_cast<PChunkyFile>(file));
    };
    s.read_name = [](void *, void *file, MMResourceCache *cache, MMU32 type, MMU32 number, void *object) -> int {
        auto name = &static_cast<PMovieSoundMSND>(object)->_stn;
        if (cache)
            return static_cast<PChunkyResourceFile>(cache->owner)->FGetName(type, number, name);
        return static_cast<PChunkyFile>(file)->FGetName(type, number, name);
    };
    s.publish_invalid = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    s.put = [](void *, void *file, MMU32 type, MMU32 number, const void *header) -> int {
        return static_cast<PChunkyFile>(file)->FPutPv(const_cast<void *>(header), 16, type, number);
    };
    s.delete_child = [](void *, void *file, MMU32 type, MMU32 number, const MMChunkChild *child) {
        static_cast<PChunkyFile>(file)->DeleteChild(type, number, child->type, child->number);
    };
    s.bad_order = [](void *) { Assert(false, "bad MovieSoundFile"); };
    s.playing = [](void *, MMI32 instance) -> int { return vpsndm->FPlaying(instance); };
    s.stop = [](void *, MMI32 queue) { vpsndm->StopAll(queue, sclNil); };
    s.play = [](void *, const MMSoundResource *sound, MMI32 queue, MMI32 volume, MMI32 repeat,
                MMU32 start, MMI32 priority, MMI32 soundClass) -> MMI32 {
        auto cache = sound->cache ? static_cast<PChunkyResourceFile>(sound->cache->owner) : nullptr;
        return vpsndm->SiiPlay(cache, sound->type, sound->number, queue, volume,
                               repeat, start, priority, soundClass);
    };
    return s;
}

/***************************************************************************

    A PFNRPO to read a MovieSoundMSND from a file

***************************************************************************/
int MovieSoundMSND::FReadMsnd(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *,
    void **out, MMI32 *bytes)
{
    const auto services = _ResourceServices();
    return mm_sound_fetch(cache, type, number, out, bytes, &services);
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
    return mm_sound_invalidate(this, ResourceBinding(), &services) != 0;
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
    AssertIn(sty, 0, styLim);
    return mm_sound_queue_number(static_cast<MMI32>(sty), static_cast<MMI32>(objid), 1);
}

/***************************************************************************

    Return the sqn for an msnd attached to an actor object of
    id == objid
    Static function

***************************************************************************/
long MovieSoundMSND::SqnBkgd(long sty, long objid)
{
    AssertIn(sty, 0, styLim);
    return mm_sound_queue_number(static_cast<MMI32>(sty), static_cast<MMI32>(objid), 0);
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

static void StopMovieSoundClass(void *, MMI32 soundClass)
{
    vpsndm->StopAll(sqnNil, soundClass);
}

void StopAllMovieSounds(void)
{
    mm_frame_sound_stop_classes(nullptr, StopMovieSoundClass);
}

MMFrameSoundOutput MovieSoundQueue::_Output()
{
    MMFrameSoundOutput output = {};
    output.context = this;
    output.create_clock = [](void *) -> void * { return NewObj Clock(khidMsqClock); };
    output.release_clock = [](void *, void *clock) { static_cast<PClock>(clock)->Release(); };
    output.start_clock = [](void *, void *clock) { static_cast<PClock>(clock)->Start(0); };
    output.stop_clock = [](void *, void *clock) { static_cast<PClock>(clock)->Stop(); };
    output.set_alarm = [](void *p, void *clock, MMI32 interval) -> int {
        return static_cast<PClock>(clock)->FSetAlarm(interval, static_cast<PMovieSoundQueue>(p));
    };
    output.begin = [](void *) { vpsndm->BeginSynch(); };
    output.end = [](void *) { vpsndm->EndSynch(); };
    output.play = [](void *, const MMFrameSoundRequest *request) {
        auto sound = static_cast<PMovieSoundMSND>(static_cast<BASE *>(request->sound));
        sound->Play(request->object, request->loop != 0, request->queued != 0, request->volume, request->priority,
                    request->actor != 0, request->offset);
    };
    output.stop_class = StopMovieSoundClass;
    output.playing_class = [](void *, MMI32 soundClass) -> int { return vpsndm->FPlayingAll(sqnNil, soundClass); };
    return output;
}

PMovieSoundQueue MovieSoundQueue::PmsqNew(void)
{
    auto queue = NewObj MovieSoundQueue(khidMsq);
    if (!queue)
        return pvNil;
    const auto output = queue->_Output();
#ifdef DEBUG
    constexpr int trashUnused = 1;
#else
    constexpr int trashUnused = 0;
#endif
    if (!mm_frame_sound_queue_open(&queue->_queue, KauaiCopyAllocator(),
                                   [](void *p) { static_cast<BASE *>(p)->AddRef(); },
                                   [](void *p) { static_cast<BASE *>(p)->Release(); }, trashUnused, &output))
        ReleasePpo(&queue);
    return queue;
}

bool MovieSoundQueue::FEnqueue(PMovieSoundMSND sound, long object, bool loop, bool queued, long volume, long priority,
                               bool actor, ulong offset, bool lowPriority)
{
    AssertThis(0);
    AssertPo(sound, 0);
    const MMFrameSoundRequest request = {static_cast<BASE *>(sound), static_cast<MMI32>(sound->Sty()),
        static_cast<MMI32>(object), static_cast<MMI32>(volume), static_cast<MMI32>(priority),
        static_cast<MMU32>(offset), loop, queued, actor};
    return mm_frame_sound_queue_enqueue(&_queue, &request, lowPriority) != 0;
}

void MovieSoundQueue::PlayMsq(void)
{
    AssertThis(0);
    const auto output = _Output();
    mm_frame_sound_queue_play(&_queue, &output);
}

void MovieSoundQueue::FlushMsq(void)
{
    AssertThis(0);
    mm_frame_sound_queue_clear(&_queue);
}

void MovieSoundQueue::StopAll(void)
{
    const auto output = _Output();
    mm_frame_sound_queue_stop(&_queue, &output);
}

bool MovieSoundQueue::FPlaying(bool loop)
{
    AssertThis(0);
    const auto output = _Output();
    return mm_frame_sound_playing(loop, &output) != 0;
}

bool MovieSoundQueue::FCmdAlarm(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    const auto output = _Output();
    mm_frame_sound_queue_alarm(&_queue, &output);
    return true;
}

MovieSoundQueue::~MovieSoundQueue(void)
{
    AssertBaseThis(0);
    const auto output = _Output();
    mm_frame_sound_queue_close(&_queue, &output);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the MovieSoundMSND.
***************************************************************************/
void MovieSoundMSND::AssertValid(LegacyUlong grf)
{
    MovieSoundMSND_PAR::AssertValid(fobjAllocated);
    auto cache = _resource.cache ? static_cast<PChunkyResourceFile>(_resource.cache->owner) : nullptr;
    AssertNilOrPo(cache, 0);
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
    Assert(_queue.initialized, "Invalid sound request storage");
    Assert(_queue.count <= _queue.storage.buffers[0].capacity / sizeof(MMFrameSoundRequest),
           "Invalid sound request count");
    AssertPo(static_cast<PClock>(_queue.clock), 0);
}

/***************************************************************************
    Mark memory used by the MovieSoundMSND
***************************************************************************/
void MovieSoundQueue::MarkMem(void)
{
    AssertThis(0);
    MovieSoundQueue_PAR::MarkMem();
    MarkPv(_queue.storage.buffers[0].data);
    for (MMU32 index = 0; index < _queue.count; ++index)
        MarkMemObj(static_cast<BASE *>(mm_frame_sound_queue_at(&_queue, index)->sound));
    MarkMemObj(static_cast<PClock>(_queue.clock));
}

#endif // DEBUG
