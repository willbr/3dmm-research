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
#include "mm/string_wire.h"
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

RTCLASS(MovieSoundQueue)

BEGIN_CMD_MAP(MovieSoundQueue, CommandHandler)
ON_CID_ME(cidAlarm, &MovieSoundQueue::FCmdAlarm, pvNil)
END_CMD_MAP_NIL()

static MMSoundResourceServices SoundServices()
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
    const unsigned short one = 1;
    s.host_big_endian = *reinterpret_cast<const unsigned char *>(&one) == 0;
    s.invalidate_output = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    s.decode = KauaiDecodeBlockHandle;
    s.graph = [](void *, void *file, MMMovieSessionGraph *out) {
        *out = EngineMovieGraph::FromFile(static_cast<PChunkyFile>(file));
    };
    s.read_name = [](void *, void *file, MMResourceCache *cache, MMU32 type, MMU32 number, MMSoundOwner *sound) -> int {
        String name;
        const bool found = cache ? ReadResourceName(cache, type, number, &name) :
                                   static_cast<PChunkyFile>(file)->FGetName(type, number, &name);
        const auto storage = mm_sound_owner_name(sound);
        mm_string_value_copy(&storage, name.Pst());
        return found;
    };
    s.publish_invalid = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    s.bad_order = [](void *) { Assert(false, "bad MovieSoundFile"); };
    s.playing = [](void *, MMI32 instance) -> int { return vpsndm->FPlaying(instance); };
    s.stop = [](void *, MMI32 queue) { vpsndm->StopAll(queue, sclNil); };
    s.play = [](void *, const MMSoundResource *sound, MMI32 queue, MMI32 volume, MMI32 repeat,
                MMU32 start, MMI32 priority, MMI32 soundClass) -> MMI32 {
        return vpsndm->SiiPlay(sound->cache, sound->type, sound->number, queue, volume,
                               repeat, start, priority, soundClass);
    };
    return s;
}

const MMSoundResourceHost &EngineSound::Host()
{
    static const auto services = SoundServices();
    // Preserve the historical cache charge, including the native title width.
    size_t prefix = sizeof(void *) == 8 ? 40 : 28;
#ifdef DEBUG
    if (sizeof(void *) == 4)
        prefix += 4;
#endif
    const size_t bytes = prefix + sizeof(MMSoundResource) + sizeof(MMI32) + (MM_STRING_MAX + 2) * sizeof(achar) + 1;
    static const MMSoundResourceHost host = {
        &services, KauaiResourceObjectAllocator(),
        static_cast<MMI32>((bytes + sizeof(void *) - 1) & ~(sizeof(void *) - 1)), sizeof(achar),
#ifdef DEBUG
        [](void *p) { MarkPv(p); },
        [](MMResourceCache *cache) { mm_resource_cache_mark(cache); }
#else
        nullptr, nullptr
#endif
    };
    return host;
}

int EngineSound::Read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_sound_fetch(cache, type, number, block, out, bytes, &Host());
}

PMovieSoundMSND EngineSound::Fetch(PTAG tag)
{
    return static_cast<PMovieSoundMSND>(vptagm->FetchResource(tag, Read, mm_sound_owner_services()));
}

PMovieSoundMSND EngineSound::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PMovieSoundMSND>(mm_resource_cache_fetch_owned_chunk(cache, type, number, Read,
                                                                          mm_sound_owner_services()));
}

bool EngineSound::Info(PChunkyFile file, ChunkTagOrType type, ChunkNumber number, bool *invalid, long *style, long *volume)
{
    AssertPo(file, 0);
    return mm_sound_info(file, type, number, invalid, style, volume, &Host()) != 0;
}

void EngineSound::GetName(PMovieSoundMSND sound, PString name)
{
    const auto value = mm_sound_owner_name(sound);
    name->SetSt(static_cast<achar *>(value.buffer));
}

/***************************************************************************

    Write an MovieSoundMSND MIDI chunk to file *pcfl
    ie, write the MovieSoundMSND chunk, its name, and the midi child

***************************************************************************/
bool EngineSound::WriteMidi(PChunkyFile pcflDest, PMidiStream pmids, String *pstnName, ChunkNumber *pcno)
{
    AssertPo(pcflDest, 0);
    AssertPo(pmids, 0);
    AssertVarMem(pstnName);
    AssertVarMem(pcno);
    const EngineSoundWriter writer(pcflDest, pstnName);
    const auto payload = pmids->BorrowStream();
    return mm_sound_write(&payload, 0, styMidi, &writer.name, pcno, &writer.archive) != 0;
}

/***************************************************************************

    Write an MovieSoundMSND Wave file to a file
    ie, write the MovieSoundMSND chunk, its name, and the midi child

***************************************************************************/
bool EngineSound::WriteWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, String *pstnName,
                                ChunkNumber *pcno)
{
    AssertPo(pfilSrc, 0);
    AssertIn(sty, 0, styLim);
    AssertVarMem(pstnName);
    AssertVarMem(pcno);
    const EngineSoundWriter writer(pcflDest, pstnName);
    const auto payload = EngineReadStream::FromFile(*pfilSrc);
    return mm_sound_write(&payload, 1, sty, &writer.name, pcno, &writer.archive) != 0;
}

/***************************************************************************

    Copy the midi file to a chunk in the current movie
    The *pcno is returned

***************************************************************************/
bool EngineSound::CopyMidi(PFileObject pfilSrc, PChunkyFile pcflDest, ChunkNumber *pcno, PString pstn)
{
    return CopyMidi(pfilSrc, pcflDest->WriteIndex(), pcno, pstn);
}

bool EngineSound::CopyMidi(PFileObject pfilSrc, const MMArchiveWriter &archive, ChunkNumber *pcno, PString pstn)
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
    const EngineSoundWriter writer(archive, pstn);
    const auto source = EngineReadStream::FromFile(*pfilSrc);
    const int result = mm_sound_import_midi(&source, &writer.name, pcno, &writer.archive);
    if (result == MM_SOUND_BAD_MIDI)
        PushErc(ercSocBadSoundFile);
    return result > 0;
}

/***************************************************************************

    Copy the wave file to a chunk in the current movie

***************************************************************************/
bool EngineSound::CopyWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, ChunkNumber *pcno, PString pstn)
{
    AssertPo(pcflDest, 0);
    return CopyWave(pfilSrc, pcflDest->WriteIndex(), sty, pcno, pstn);
}

bool EngineSound::CopyWave(PFileObject pfilSrc, const MMArchiveWriter &archive, long sty, ChunkNumber *pcno, PString pstn)
{
    AssertPo(pfilSrc, 0);
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
    const EngineSoundWriter writer(archive, pstn);
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
        return mm_wave_file_import(&source, sty, &writer.name, pcno, &s, &writer.archive) != 0;
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
    return mm_wave_native_import(&source, sty, &writer.name, pcno, &s, &writer.archive) != 0;
#endif
}

/***************************************************************************

    Invalidate a sound

***************************************************************************/
bool EngineSound::Invalidate(PMovieSoundMSND sound)
{
    const auto binding = mm_sound_owner_binding(sound);
    if (!binding->cache)
        return false;
    const auto edit = ResourceArchiveWriter(binding->cache).edit;
    return mm_sound_invalidate(mm_sound_owner_state(sound), binding, &edit, Host().services) != 0;
}

long EngineSound::Priority(PMovieSoundMSND sound, long tool)
{
    const auto style = Style(sound);
    Assert(tool == toolMatcher || tool == toolSounder || tool == toolLooper, "Invalid tool");
    Assert(style != styMidi || tool != toolMatcher, "No midi motion matching");
    AssertIn(style, stySfx, styLim);
    return mm_sound_priority(style, static_cast<int>(tool - toolSounder));
}

void EngineSound::Play(PMovieSoundMSND sound, long object, bool loop, bool queue, long volume, long priority,
                       bool actor, ulong start)
{
    static MMSoundLastMidi last = {};
    mm_sound_play(mm_sound_owner_state(sound), &last, object, loop, queue, volume, priority, actor, start,
                  Host().services);
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
        auto sound = static_cast<PMovieSoundMSND>(request->sound);
        EngineSound::Play(sound, request->object, request->loop != 0, request->queued != 0, request->volume,
                          request->priority, request->actor != 0, request->offset);
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
                                   [](void *p) { mm_sound_owner_retain(static_cast<MMSoundOwner *>(p)); },
                                   [](void *p) { mm_sound_owner_release(static_cast<MMSoundOwner *>(p)); },
                                   trashUnused, &output))
        ReleasePpo(&queue);
    return queue;
}

bool MovieSoundQueue::FEnqueue(PMovieSoundMSND sound, long object, bool loop, bool queued, long volume, long priority,
                               bool actor, ulong offset, bool lowPriority)
{
    AssertThis(0);
    Assert(sound, "Missing sound owner");
    const MMFrameSoundRequest request = {sound, static_cast<MMI32>(EngineSound::Style(sound)),
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
    Mark memory used by the MovieSoundQueue
***************************************************************************/
void MovieSoundQueue::MarkMem(void)
{
    AssertThis(0);
    MovieSoundQueue_PAR::MarkMem();
    MarkPv(_queue.storage.buffers[0].data);
    for (MMU32 index = 0; index < _queue.count; ++index)
        mm_sound_owner_services()->mark_memory(mm_frame_sound_queue_at(&_queue, index)->sound);
    MarkMemObj(static_cast<PClock>(_queue.clock));
}

#endif // DEBUG
