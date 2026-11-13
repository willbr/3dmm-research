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
#include "three_mm/wave_import.h"
#ifndef KAUAI_SDL3
#include "audioman.h"
#endif

ASSERTNAME

RTCLASS(MovieSoundMSND)
RTCLASS(MovieSoundQueue)

BEGIN_CMD_MAP(MovieSoundQueue, CommandHandler)
ON_CID_ME(cidAlarm, &MovieSoundQueue::FCmdAlarm, pvNil)
END_CMD_MAP_NIL()

struct SoundResourceContext
{
    DataBlock block;
    FileLocation source, destination;
    Filename filename;
    String name;
};
ThreeMMSoundResourceServices MovieSoundMSND::_ResourceServices(void *context)
{
    ThreeMMSoundResourceServices s = {};
    s.context = context;
    s.system = koskCur;
    static_assert(sizeof(LegacyLong) == 4 && sizeof(ChunkNumber) == 4 &&
                      sizeof(ThreeMMSoundResource) == sizeof(void *) + 16,
                  "sound resource ABI");
    static_assert(stySfx == 2 && stySpeech == 3 && styMidi == 4 && toolLooper == toolSounder + 1 &&
                      toolMatcher == toolSounder + 2,
                  "sound policy enums");
    static_assert(kctgMsnd == THREE_MM_SOUND_TYPE && kctgMidi == THREE_MM_SOUND_MIDI &&
                      kctgWave == THREE_MM_SOUND_WAVE && tNo == 0 && tYes == 1,
                  "sound wire enums");
    s.owner = [](void *, void *object, ThreeMMSoundOwner *out) {
        auto sound = static_cast<PMovieSoundMSND>(object);
        *out = {&sound->_resource, &sound->_silent, &sound->_invalid};
    };
    s.create = [](void *) -> void * { return NewObj MovieSoundMSND(); };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.object_size = [](void *) -> ThreeMMI32 { return sizeof(MovieSoundMSND); };
    s.invalidate_output = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    s.file = [](void *, void *cache) -> void * { return static_cast<PChunkyResourceFile>(cache)->Pcfl(); };
    s.find = [](void *p, void *file, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        auto block = &static_cast<SoundResourceContext *>(p)->block;
        return static_cast<PChunkyFile>(file)->FFind(type, number, block) ? block : nullptr;
    };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block) -> ThreeMMI32 { return static_cast<PDataBlock>(block)->Cb(); };
    s.read = [](void *, void *block, void *out, ThreeMMI32 bytes) -> int {
        return static_cast<PDataBlock>(block)->FReadRgb(out, bytes, 0);
    };
    s.read_name = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, void *object) -> int {
        return static_cast<PChunkyFile>(file)->FGetName(type, number, &static_cast<PMovieSoundMSND>(object)->_stn);
    };
    s.child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMSoundChild *out) -> int {
        ChildChunkIdentification child;
        if (!static_cast<PChunkyFile>(file)->FGetKidChid(type, number, kchidSnd, &child))
            return 0;
        *out = {child.cki.ctg, child.cki.cno};
        return 1;
    };
    s.publish_invalid = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    s.add = [](void *, void *file, const void *header, void *out) -> int {
        return static_cast<PChunkyFile>(file)->FAddPv(const_cast<void *>(header), 16, kctgMsnd,
                                                      static_cast<ChunkNumber *>(out));
    };
    s.wave_size = [](void *p, void *source) -> ThreeMMI32 {
        auto location = &static_cast<SoundResourceContext *>(p)->source;
        location->pfil = static_cast<PFileObject>(source);
        location->fp = 0;
        location->cb = location->pfil->FpMac();
        return location->cb;
    };
    s.midi_size = [](void *, void *source) -> ThreeMMI32 { return static_cast<PMidiStream>(source)->CbOnFile(); };
    s.add_child = [](void *p, void *file, ThreeMMU32 parent, ThreeMMU32 type, ThreeMMI32 bytes,
                     ThreeMMU32 *out) -> int {
        auto block = &static_cast<SoundResourceContext *>(p)->block;
        ChunkNumber child;
        if (!static_cast<PChunkyFile>(file)->FAddChild(kctgMsnd, parent, kchidSnd, bytes, type, &child,
                                                       type == kctgMidi ? block : nullptr))
            return 0;
        *out = child;
        return 1;
    };
    s.write_midi = [](void *p, void *source) -> int {
        return static_cast<PMidiStream>(source)->FWrite(&static_cast<SoundResourceContext *>(p)->block);
    };
    s.find_wave = [](void *p, void *file, ThreeMMU32 number) -> int {
        return static_cast<PChunkyFile>(file)->FFindFlo(kctgWave, number,
                                                        &static_cast<SoundResourceContext *>(p)->destination);
    };
    s.copy_wave = [](void *p) -> int {
        auto c = static_cast<SoundResourceContext *>(p);
        return c->source.FCopy(&c->destination);
    };
    s.set_name = [](void *, void *file, ThreeMMU32 number, const void *name) -> int {
        return static_cast<PChunkyFile>(file)->FSetName(kctgMsnd, number,
                                                        const_cast<PString>(static_cast<const String *>(name)));
    };
    s.delete_parent = [](void *, void *file, ThreeMMU32 number) {
        static_cast<PChunkyFile>(file)->Delete(kctgMsnd, number);
    };
    s.prepare_name = [](void *p, void *source, const void *name) -> void * {
        auto c = static_cast<SoundResourceContext *>(p);
        static_cast<PFileObject>(source)->GetFni(&c->filename);
        if (name)
            c->name = *const_cast<PString>(static_cast<const String *>(name));
        else
            c->filename.GetLeaf(&c->name);
        return &c->name;
    };
    s.read_midi = [](void *p) -> void * {
        return MidiStream::PmidsReadNative(&static_cast<SoundResourceContext *>(p)->filename);
    };
    s.bad_sound = [](void *) { PushErc(ercSocBadSoundFile); };
    s.binding = [](void *, void *object, void **file, ThreeMMU32 *type, ThreeMMU32 *number) {
        auto sound = static_cast<PMovieSoundMSND>(object);
        *file = sound->Pcrf()->Pcfl();
        *type = sound->Ctg();
        *number = sound->Cno();
    };
    s.put = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const void *header) -> int {
        return static_cast<PChunkyFile>(file)->FPutPv(const_cast<void *>(header), 16, type, number);
    };
    s.delete_child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMSoundChild *child) {
        static_cast<PChunkyFile>(file)->DeleteChild(type, number, child->type, child->number);
    };
    s.bad_order = [](void *) { Assert(false, "bad MovieSoundFile"); };
    s.queue_number = [](void *, ThreeMMI32 style, ThreeMMI32 object, int actor) -> ThreeMMI32 {
        return SoundPlaybackQueue::QueueNumber(style, object, actor != 0);
    };
    s.playing = [](void *, ThreeMMI32 instance) -> int { return vpsndm->FPlaying(instance); };
    s.stop = [](void *, ThreeMMI32 queue) { vpsndm->StopAll(queue, sclNil); };
    s.play = [](void *, const ThreeMMSoundResource *sound, ThreeMMI32 queue, ThreeMMI32 volume, ThreeMMI32 repeat,
                ThreeMMU32 start, ThreeMMI32 priority, ThreeMMI32 soundClass) -> ThreeMMI32 {
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
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_fetch(pcrf, ctg, cno, ppbaco, pcb, &services) != 0;
}

/***************************************************************************

    Retrieve information contained in the msnd chunk

***************************************************************************/
bool MovieSoundMSND::FGetMsndInfo(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfInvalid, long *psty,
                                  long *pvlm)
{
    AssertPo(pcfl, 0);
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_info(pcfl, ctg, cno, pfInvalid, psty, pvlm, &services) != 0;
}

/***************************************************************************

    Init a MovieSoundMSND from the given chunk of the given ChunkyFile

***************************************************************************/
bool MovieSoundMSND::_FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_read(this, pcfl, ctg, cno, &services) != 0;
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
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_write(pmids, 0, pcflDest, styMidi, pstnName, pcno, &services) != 0;
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
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_write(pfilSrc, 1, pcflDest, sty, pstnName, pcno, &services) != 0;
}

/***************************************************************************

    Copy the midi file to a chunk in the current movie
    The *pcno is returned

***************************************************************************/
bool MovieSoundMSND::FCopyMidi(PFileObject pfilSrc, PChunkyFile pcflDest, ChunkNumber *pcno, PString pstn)
{
    AssertPo(pfilSrc, 0);
    AssertNilOrPo(pstn, 0);
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_copy_midi(pfilSrc, pcflDest, pstn, pcno, &services) != 0;
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

#ifdef KAUAI_SDL3
    struct Context
    {
        std::vector<std::uint8_t> prepared;
        String name;
    } context;
    ThreeMMWaveFileServices s = {};
    s.context = &context;
    s.size = [](void *, void *source) -> ThreeMMI32 { return static_cast<PFileObject>(source)->FpMac(); };
    s.read = [](void *, void *source, void *data, ThreeMMI32 bytes) -> int {
        return static_cast<PFileObject>(source)->FReadRgb(data, bytes, 0);
    };
    s.prepare = [](void *p, const void *data, ThreeMMI32 bytes, const void **out, ThreeMMI32 *count) -> int {
        auto &prepared = static_cast<Context *>(p)->prepared;
        if (!KauaiAudio::PrepareWaveImport(data, bytes, prepared))
            return 0;
        *out = prepared.data();
        *count = static_cast<ThreeMMI32>((prepared.size)());
        return 1;
    };
    s.name = [](void *p, void *source, const void *provided) -> const void * {
        auto &name = static_cast<Context *>(p)->name;
        if (provided)
            name = *const_cast<String *>(static_cast<const String *>(provided));
        else
        {
            Filename filename;
            static_cast<PFileObject>(source)->GetFni(&filename);
            filename.GetLeaf(&name);
        }
        return &name;
    };
    s.create_temp = [](void *) -> void * { return FileObject::PfilCreateTemp(); };
    s.write_temp = [](void *, void *file, const void *data, ThreeMMI32 bytes) -> int {
        return static_cast<PFileObject>(file)->FWriteRgb(const_cast<void *>(data), bytes, 0);
    };
    s.write_resource = [](void *, void *source, void *destination, ThreeMMI32 style, const void *name,
                          void *number) -> int {
        return FWriteWave(static_cast<PFileObject>(source), static_cast<PChunkyFile>(destination), style,
                          const_cast<String *>(static_cast<const String *>(name)), static_cast<ChunkNumber *>(number));
    };
    s.release = [](void *, void *file) { static_cast<PFileObject>(file)->Release(); };
    s.bad_sound = [](void *) { PushErc(ercSocBadSoundFile); };
    try
    {
        return three_mm_wave_file_import(pfilSrc, pcflDest, sty, pstn, pcno, &s) != 0;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
#else
    struct Context
    {
        Filename source, temporary;
        String name, path;
        WAVEFORMATEX format = {};
        LPSOUND sound = nullptr;
        PFileObject file = nullptr;
        WAVEFORMATEX *encodedFormat = nullptr;
        LPBYTE samples = nullptr, encoded = nullptr;
        HACMSTREAM stream = nullptr;
        ACMSTREAMHEADER header = {};
        FilePosition position = 0;
        void Finish()
        {
            FreePpv(reinterpret_cast<void **>(&encodedFormat));
            FreePpv(reinterpret_cast<void **>(&samples));
            FreePpv(reinterpret_cast<void **>(&encoded));
            ReleasePpo(&file);
            if (sound)
                sound->SetMode(FALSE, TRUE);
            ReleasePpo(&sound);
        }
    } context;
    ThreeMMNativeWaveServices s = {};
    s.context = &context;
    s.begin = [](void *p, void *source, const void *name) -> int {
        auto &c = *static_cast<Context *>(p);
        static_cast<PFileObject>(source)->GetFni(&c.source);
        if (name)
            c.name = *const_cast<String *>(static_cast<const String *>(name));
        else
            c.source.GetLeaf(&c.name);
        c.source.GetStnPath(&c.path);
        return c.temporary.FGetTemp();
    };
    s.original_size = [](void *p, ThreeMMI32 *bytes) -> int {
        auto &c = *static_cast<Context *>(p);
        auto file = FileObject::PfilOpen(&c.source);
        if (!file)
            return 0;
        *bytes = file->FpMac();
        file->Release();
        return 1;
    };
    s.open_sound = [](void *p, ThreeMMNativeWaveFormat *format) -> int {
        auto &c = *static_cast<Context *>(p);
        if (FAILED(AllocSoundFromFile(&c.sound, c.path.Psz(), 0, TRUE, nullptr)) ||
            FAILED(c.sound->GetFormat(&c.format, sizeof(c.format))))
            return 0;
        format->rate = c.format.nSamplesPerSec;
        format->bits = c.format.wBitsPerSample;
        format->channels = c.format.nChannels;
        return 1;
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
    s.samples = [](void *p) -> ThreeMMU32 { return static_cast<Context *>(p)->sound->GetSamples(); };
    s.activate = [](void *p) -> int { return SUCCEEDED(static_cast<Context *>(p)->sound->SetMode(TRUE, TRUE)); };
    s.allocate_samples = [](void *p, ThreeMMU32 bytes) -> int {
        return FAllocPv(reinterpret_cast<void **>(&static_cast<Context *>(p)->samples), bytes, fmemNil, mprNormal);
    };
    s.read_samples = [](void *p, ThreeMMU32 *samples) -> int {
        auto &c = *static_cast<Context *>(p);
        DWORD count = *samples;
        const bool ok = SUCCEEDED(c.sound->GetSampleData(c.samples, 0, &count, nullptr));
        *samples = count;
        return ok;
    };
    s.compress = [](void *p, ThreeMMU32 bytes, const void **format, ThreeMMU32 *formatBytes, const void **data,
                    ThreeMMU32 *dataBytes) -> int {
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
        if (acmStreamPrepareHeader(c.stream, &h, 0) || acmStreamConvert(c.stream, &h, ACM_STREAMCONVERTF_END) ||
            acmStreamUnprepareHeader(c.stream, &h, 0))
            return -1;
        acmStreamClose(c.stream, 0);
        c.stream = nullptr;
        *format = &f;
        *formatBytes = sizeof(WAVEFORMATEX) + f.cbSize;
        *data = c.encoded;
        *dataBytes = h.cbDstLengthUsed;
        return 1;
    };
    s.create_file = [](void *p) -> int {
        auto &c = *static_cast<Context *>(p);
        c.file = FileObject::PfilCreate(&c.temporary);
        c.position = 0;
        return c.file != nullptr;
    };
    s.write = [](void *p, const void *data, ThreeMMU32 bytes) -> int {
        auto &c = *static_cast<Context *>(p);
        return c.file->FWriteRgbSeq(const_cast<void *>(data), bytes, &c.position);
    };
    s.finish_conversion = [](void *p) { static_cast<Context *>(p)->Finish(); };
    s.open_selected = [](void *p, int original) -> int {
        auto &c = *static_cast<Context *>(p);
        c.file = FileObject::PfilOpen(original ? &c.source : &c.temporary);
        return c.file != nullptr;
    };
    s.write_resource = [](void *p, void *destination, ThreeMMI32 style, void *number) -> int {
        auto &c = *static_cast<Context *>(p);
        return FWriteWave(c.file, static_cast<PChunkyFile>(destination), style, &c.name,
                          static_cast<ChunkNumber *>(number));
    };
    s.bad_sound = [](void *) { PushErc(ercSocBadSoundFile); };
    s.cleanup = [](void *p) {
        auto &c = *static_cast<Context *>(p);
        if (c.stream)
        {
            acmStreamClose(c.stream, 0);
            c.stream = nullptr;
        }
        c.Finish();
        if (c.temporary.TExists() == tYes)
            c.temporary.FDelete();
    };
    return three_mm_wave_native_import(pfilSrc, pcflDest, sty, pstn, pcno, &s) != 0;
#endif
}

/***************************************************************************

    Invalidate a sound

***************************************************************************/
bool MovieSoundMSND::FInvalidate(void)
{
    AssertThis(0);
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    return three_mm_sound_invalidate(this, &services) != 0;
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
    return three_mm_sound_priority(_resource.style, static_cast<int>(tool - toolSounder));
}

/***************************************************************************

    Play this sound

***************************************************************************/
void MovieSoundMSND::Play(long objID, bool fLoop, bool fQueue, long vlm, long spr, bool fActr, ulong dtsStart)
{
    AssertThis(0);
    static ThreeMMSoundLastMidi last = {};
    SoundResourceContext context;
    auto services = _ResourceServices(&context);
    three_mm_sound_play(this, &last, objID, fLoop, fQueue, vlm, spr, fActr, dtsStart, &services);
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
