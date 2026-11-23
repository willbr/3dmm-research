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
#include "engine_read_stream.h"
#include "engine_sound_writer.h"
#include "mm/wave_import.h"
#ifndef KAUAI_SDL3
#include "audioman.h"
#endif

ASSERTNAME

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

PMovieSoundMSND EngineSound::Fetch(PTAG tag)
{
    return static_cast<PMovieSoundMSND>(vptagm->FetchResource(tag, Read, mm_sound_owner_services()));
}

PMovieSoundMSND EngineSound::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return mm_kauai_movie_sound_fetch(cache, type, number);
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
MMSoundImportSource EngineSoundImportHost::Input(PString name)
{
    AssertPo(file, 0);
    AssertNilOrPo(name, 0);
    file->GetFni(&filename);
    String leaf;
    if (!name)
    {
        filename.GetLeaf(&leaf);
        name = &leaf;
    }
    return {EngineReadStream::FromFile(*file), ChunkyFile::EncodeName(name, wire),
            filename.Ftg() == kftgMidi, this, Wave};
}

static bool CopySoundFile(PFileObject file, PChunkyFile destination, bool midi, MMI32 style,
                          ChunkNumber *number, PString name)
{
    AssertPo(destination, 0);
    AssertVarMem(number);
    EngineSoundImportHost host(file);
    auto source = host.Input(name);
    source.midi = midi;
    const auto writer = destination->WriteIndex();
    const int result = mm_sound_import(&source, style, number, &writer);
    if (result < 0)
        PushErc(ercSocBadSoundFile);
    return result > 0;
}

bool EngineSound::CopyMidi(PFileObject pfilSrc, PChunkyFile pcflDest, ChunkNumber *pcno, PString pstn)
{
    return CopySoundFile(pfilSrc, pcflDest, true, styMidi, pcno, pstn);
}

bool EngineSound::CopyWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, ChunkNumber *pcno, PString pstn)
{
    return CopySoundFile(pfilSrc, pcflDest, false, sty, pcno, pstn);
}

int EngineSoundImportHost::Wave(void *host, const MMReadStream *source, MMI32 sty,
                               const MMChunkNameValue *name, MMU32 *pcno, const MMArchiveWriter *writer)
{
    AssertIn(sty, 0, styLim);
    Assert(sty != styMidi, "Illegal sty argument");
#ifdef KAUAI_SDL3
    return KauaiAudio::ImportWave(host, source, sty, name, pcno, writer);
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
    static_cast<EngineSoundImportHost *>(host)->filename.GetStnPath(&context.path);
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
    s.convert = [](void *p, const MMNativeWaveFormat *target) -> int {
        auto &c = *static_cast<Context *>(p);
        auto &f = c.format;
        f.nSamplesPerSec = target->rate;
        f.wBitsPerSample = static_cast<WORD>(target->bits);
        f.nChannels = static_cast<WORD>(target->channels);
        f.nBlockAlign = static_cast<WORD>(f.nChannels * (f.wBitsPerSample / 8));
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
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
        f.nChannels = c.format.nChannels;
        f.nSamplesPerSec = c.format.nSamplesPerSec;
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
    return mm_wave_native_import(source, sty, name, pcno, &s, writer);
#endif
}

/***************************************************************************

    Invalidate a sound

***************************************************************************/
bool EngineSound::Invalidate(PMovieSoundMSND sound)
{
    return mm_sound_owner_invalidate(sound) != 0;
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
    const MMFrameSoundRequest request = {sound, static_cast<MMI32>(Style(sound)),
        static_cast<MMI32>(object), static_cast<MMI32>(volume), static_cast<MMI32>(priority),
        static_cast<MMU32>(start), loop, queue, actor};
    mm_kauai_movie_sound_play(vpsndm, &request);
}

static void StopMovieSoundClass(void *, MMI32 soundClass)
{
    vpsndm->StopAll(sqnNil, soundClass);
}

void StopAllMovieSounds(void)
{
    mm_frame_sound_stop_classes(nullptr, StopMovieSoundClass);
}

MMFrameSoundOutput MovieSoundOutput(PCommandHandler alarmTarget)
{
    MMFrameSoundOutput output = {};
    output.context = alarmTarget;
    output.create_clock = [](void *) -> void * { return NewObj Clock(khidMsqClock); };
    output.release_clock = [](void *, void *clock) { static_cast<PClock>(clock)->Release(); };
    output.start_clock = [](void *, void *clock) { static_cast<PClock>(clock)->Start(0); };
    output.stop_clock = [](void *, void *clock) { static_cast<PClock>(clock)->Stop(); };
    output.set_alarm = [](void *p, void *clock, MMI32 interval) -> int {
        return static_cast<PClock>(clock)->FSetAlarm(interval, static_cast<PCommandHandler>(p));
    };
    output.begin = [](void *) { vpsndm->BeginSynch(); };
    output.end = [](void *) { vpsndm->EndSynch(); };
    output.play = [](void *, const MMFrameSoundRequest *request) {
        mm_kauai_movie_sound_play(vpsndm, request);
    };
    output.stop_class = StopMovieSoundClass;
    output.playing_class = [](void *, MMI32 soundClass) -> int { return vpsndm->FPlayingAll(sqnNil, soundClass); };
    return output;
}
