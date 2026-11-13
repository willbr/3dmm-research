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
#ifndef KAUAI_SDL3
#include "audioman.h"
#endif

ASSERTNAME

RTCLASS(MovieSoundMSND)
RTCLASS(MovieSoundQueue)

BEGIN_CMD_MAP(MovieSoundQueue, CommandHandler)
ON_CID_ME(cidAlarm, &MovieSoundQueue::FCmdAlarm, pvNil)
END_CMD_MAP_NIL()

// default sound import format
const uint16_t knSamplesPerSec = 11025;
const uint16_t knAvgBytesPerSec = 11025;
const uint16_t kwBitsPerSample = 8;
const uint16_t knChannels = 1;
const uint16_t knBlockAlign = 1;

struct SoundResourceContext
{
    DataBlock block;
    FileLocation source, destination;
    Filename filename;
    String name;
};
ThreeMMSoundResourceServices MovieSoundMSND::_ResourceServices(void *context)
{
    ThreeMMSoundResourceServices s = {}; s.context = context; s.system = koskCur;
    static_assert(sizeof(LegacyLong) == 4 && sizeof(ChunkNumber) == 4 && sizeof(ThreeMMSoundResource) == sizeof(void *) + 16, "sound resource ABI");
    static_assert(stySfx == 2 && stySpeech == 3 && styMidi == 4 && toolLooper == toolSounder + 1 && toolMatcher == toolSounder + 2, "sound policy enums");
    static_assert(kctgMsnd == THREE_MM_SOUND_TYPE && kctgMidi == THREE_MM_SOUND_MIDI && kctgWave == THREE_MM_SOUND_WAVE && tNo == 0 && tYes == 1, "sound wire enums");
    s.owner = [](void *, void *object, ThreeMMSoundOwner *out) {
        auto sound = static_cast<PMovieSoundMSND>(object); *out = {&sound->_resource, &sound->_silent, &sound->_invalid};
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
    s.read = [](void *, void *block, void *out, ThreeMMI32 bytes) -> int { return static_cast<PDataBlock>(block)->FReadRgb(out, bytes, 0); };
    s.read_name = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, void *object) -> int {
        return static_cast<PChunkyFile>(file)->FGetName(type, number, &static_cast<PMovieSoundMSND>(object)->_stn);
    };
    s.child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMSoundChild *out) -> int {
        ChildChunkIdentification child; if (!static_cast<PChunkyFile>(file)->FGetKidChid(type, number, kchidSnd, &child)) return 0;
        *out = {child.cki.ctg, child.cki.cno}; return 1;
    };
    s.publish_invalid = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    s.add = [](void *, void *file, const void *header, void *out) -> int {
        return static_cast<PChunkyFile>(file)->FAddPv(const_cast<void *>(header), 16, kctgMsnd, static_cast<ChunkNumber *>(out));
    };
    s.wave_size = [](void *p, void *source) -> ThreeMMI32 {
        auto location = &static_cast<SoundResourceContext *>(p)->source;
        location->pfil = static_cast<PFileObject>(source); location->fp = 0; location->cb = location->pfil->FpMac(); return location->cb;
    };
    s.midi_size = [](void *, void *source) -> ThreeMMI32 { return static_cast<PMidiStream>(source)->CbOnFile(); };
    s.add_child = [](void *p, void *file, ThreeMMU32 parent, ThreeMMU32 type, ThreeMMI32 bytes, ThreeMMU32 *out) -> int {
        auto block = &static_cast<SoundResourceContext *>(p)->block; ChunkNumber child;
        if (!static_cast<PChunkyFile>(file)->FAddChild(kctgMsnd, parent, kchidSnd, bytes, type, &child, type == kctgMidi ? block : nullptr)) return 0;
        *out = child; return 1;
    };
    s.write_midi = [](void *p, void *source) -> int { return static_cast<PMidiStream>(source)->FWrite(&static_cast<SoundResourceContext *>(p)->block); };
    s.find_wave = [](void *p, void *file, ThreeMMU32 number) -> int { return static_cast<PChunkyFile>(file)->FFindFlo(kctgWave, number, &static_cast<SoundResourceContext *>(p)->destination); };
    s.copy_wave = [](void *p) -> int { auto c = static_cast<SoundResourceContext *>(p); return c->source.FCopy(&c->destination); };
    s.set_name = [](void *, void *file, ThreeMMU32 number, const void *name) -> int { return static_cast<PChunkyFile>(file)->FSetName(kctgMsnd, number, const_cast<PString>(static_cast<const String *>(name))); };
    s.delete_parent = [](void *, void *file, ThreeMMU32 number) { static_cast<PChunkyFile>(file)->Delete(kctgMsnd, number); };
    s.prepare_name = [](void *p, void *source, const void *name) -> void * {
        auto c = static_cast<SoundResourceContext *>(p); static_cast<PFileObject>(source)->GetFni(&c->filename);
        if (name) c->name = *const_cast<PString>(static_cast<const String *>(name)); else c->filename.GetLeaf(&c->name);
        return &c->name;
    };
    s.read_midi = [](void *p) -> void * { return MidiStream::PmidsReadNative(&static_cast<SoundResourceContext *>(p)->filename); };
    s.bad_sound = [](void *) { PushErc(ercSocBadSoundFile); };
    s.binding = [](void *, void *object, void **file, ThreeMMU32 *type, ThreeMMU32 *number) {
        auto sound = static_cast<PMovieSoundMSND>(object); *file = sound->Pcrf()->Pcfl(); *type = sound->Ctg(); *number = sound->Cno();
    };
    s.put = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const void *header) -> int { return static_cast<PChunkyFile>(file)->FPutPv(const_cast<void *>(header), 16, type, number); };
    s.delete_child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMSoundChild *child) { static_cast<PChunkyFile>(file)->DeleteChild(type, number, child->type, child->number); };
    s.bad_order = [](void *) { Assert(false, "bad MovieSoundFile"); };
    s.queue_number = [](void *, ThreeMMI32 style, ThreeMMI32 object, int actor) -> ThreeMMI32 { return SoundPlaybackQueue::QueueNumber(style, object, actor != 0); };
    s.playing = [](void *, ThreeMMI32 instance) -> int { return vpsndm->FPlaying(instance); };
    s.stop = [](void *, ThreeMMI32 queue) { vpsndm->StopAll(queue, sclNil); };
    s.play = [](void *, const ThreeMMSoundResource *sound, ThreeMMI32 queue, ThreeMMI32 volume, ThreeMMI32 repeat, ThreeMMU32 start, ThreeMMI32 priority, ThreeMMI32 soundClass) -> ThreeMMI32 {
        return vpsndm->SiiPlay(static_cast<PResourceCache>(sound->cache), sound->type, sound->number, queue, volume, repeat, start, priority, soundClass);
    };
    return s;
}

/***************************************************************************

    A PFNRPO to read a MovieSoundMSND from a file

***************************************************************************/
bool MovieSoundMSND::FReadMsnd(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0); AssertPo(pblck, 0); AssertNilOrVarMem(ppbaco); AssertVarMem(pcb);
    SoundResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_sound_fetch(pcrf, ctg, cno, ppbaco, pcb, &services) != 0;
}

/***************************************************************************

    Retrieve information contained in the msnd chunk

***************************************************************************/
bool MovieSoundMSND::FGetMsndInfo(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfInvalid, long *psty, long *pvlm)
{
    AssertPo(pcfl, 0);
    SoundResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_sound_info(pcfl, ctg, cno, pfInvalid, psty, pvlm, &services) != 0;
}

/***************************************************************************

    Init a MovieSoundMSND from the given chunk of the given ChunkyFile

***************************************************************************/
bool MovieSoundMSND::_FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0); AssertPo(pcfl, 0);
    SoundResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_sound_read(this, pcfl, ctg, cno, &services) != 0;
}

/***************************************************************************

    Write an MovieSoundMSND MIDI chunk to file *pcfl
    ie, write the MovieSoundMSND chunk, its name, and the midi child

***************************************************************************/
bool MovieSoundMSND::FWriteMidi(PChunkyFile pcflDest, PMidiStream pmids, String *pstnName, ChunkNumber *pcno)
{
    AssertPo(pcflDest, 0); AssertPo(pmids, 0); AssertVarMem(pstnName); AssertVarMem(pcno);
    SoundResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_sound_write(pmids, 0, pcflDest, styMidi, pstnName, pcno, &services) != 0;
}

/***************************************************************************

    Write an MovieSoundMSND Wave file to a file
    ie, write the MovieSoundMSND chunk, its name, and the midi child

***************************************************************************/
bool MovieSoundMSND::FWriteWave(PFileObject pfilSrc, PChunkyFile pcflDest, long sty, String *pstnName, ChunkNumber *pcno)
{
    AssertPo(pfilSrc, 0); AssertIn(sty, 0, styLim); AssertVarMem(pstnName); AssertVarMem(pcno);
    SoundResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_sound_write(pfilSrc, 1, pcflDest, sty, pstnName, pcno, &services) != 0;
}

/***************************************************************************

    Copy the midi file to a chunk in the current movie
    The *pcno is returned

***************************************************************************/
bool MovieSoundMSND::FCopyMidi(PFileObject pfilSrc, PChunkyFile pcflDest, ChunkNumber *pcno, PString pstn)
{
    AssertPo(pfilSrc, 0); AssertNilOrPo(pstn, 0);
    SoundResourceContext context; auto services = _ResourceServices(&context);
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
    // Decode and compress without AudioMan/ACM. The existing writer retains
    // the MSND envelope, name, default volume and WAVE child relationship.
    try
    {
        const auto count = pfilSrc->FpMac();
        if (count < 12 || count > INT32_MAX)
        {
            PushErc(ercSocBadSoundFile);
            return false;
        }
        std::vector<std::uint8_t> source(static_cast<std::size_t>(count)), prepared;
        if (!pfilSrc->FReadRgb(source.data(), static_cast<long>(count), 0) ||
            !KauaiAudio::PrepareWaveImport(source.data(), (source.size)(), prepared))
        {
            PushErc(ercSocBadSoundFile);
            return false;
        }
        String name;
        if (pstn)
            name = *pstn;
        else
        {
            Filename filename;
            pfilSrc->GetFni(&filename);
            filename.GetLeaf(&name);
        }
        auto file = FileObject::PfilCreateTemp();
        if (!file)
            return false;
        const bool written = file->FWriteRgb(prepared.data(), static_cast<long>((prepared.size)()), 0) &&
                             FWriteWave(file, pcflDest, sty, &name, pcno);
        ReleasePpo(&file);
        return written;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
#else
    Filename fniSrc;
    String stnName; // sound name
    String stn;     // src file path name
    WAVEFORMATEX wfxSrc;
    LPSOUND psnd = pvNil;
    LPSOUND psndTemp = pvNil;
    FileObject *pfilNew = pvNil;
    String stnNew;
    Filename fniNew;
    WAVEFORMATEX *pwfxDst = pvNil;
    DWORD cbwfx;          // size of waveformatex needed
    DWORD csampSrc;       // number of source samples
    DWORD cbSrc;          // size of source in bytes
    LPBYTE pbSrc = pvNil; // src data buffer
    DWORD cbDst;          // size of dest in bytes
    LPBYTE pbDst = pvNil; // dst data buffer
    HACMSTREAM hacmstream = pvNil;
    ACMSTREAMHEADER acmhdr;
    DWORD dwTag;
    DWORD dwLength;
    long cbOriginalFile = 0;
    long cbCompressedFile = 0;
    FilePosition fpNew;

    pfilSrc->GetFni(&fniSrc);
    if (pvNil == pstn)
        fniSrc.GetLeaf(&stnName);
    else
        stnName = *pstn;
    fniSrc.GetStnPath(&stn);

    if (!fniNew.FGetTemp())
        goto LFail;

    // get the original file size
    if (pvNil == (pfilNew = FileObject::PfilOpen(&fniSrc)))
        goto LFailPushError;
    cbOriginalFile = pfilNew->FpMac();
    ReleasePpo(&pfilNew);

    // open the file as a pSound
    if (FAILED(AllocSoundFromFile(&psnd, stn.Psz(), 0, fTrue, pvNil)))
        goto LFailPushError;
    if (FAILED(psnd->GetFormat((LPWAVEFORMATEX)&wfxSrc, size(WAVEFORMATEX))))
        goto LFailPushError;

    // if the file format is not 11M8, then create a converter to 11M8
    if ((wfxSrc.nSamplesPerSec != knSamplesPerSec) || (wfxSrc.wBitsPerSample != kwBitsPerSample) ||
        (wfxSrc.nChannels != knChannels))
    {
        // Convert the wave import to 3mm's 11M8
        wfxSrc.nSamplesPerSec = knSamplesPerSec;
        wfxSrc.nAvgBytesPerSec = knAvgBytesPerSec;
        wfxSrc.wBitsPerSample = kwBitsPerSample;
        wfxSrc.nChannels = knChannels;
        wfxSrc.nBlockAlign = knBlockAlign;
        if (FAILED(AllocConvertFilter(&psndTemp, psnd, (LPWAVEFORMATEX)&wfxSrc)))
            goto LFailPushError;

        ReleasePpo(&psnd);
        psnd = psndTemp;
        psndTemp = pvNil;
    }

    // now figure out how many total samples there are
    csampSrc = psnd->GetSamples();
    if (csampSrc == -1) // Don't allow infinite sources to be written
        goto LFailPushError;
    cbSrc = csampSrc * (wfxSrc.wBitsPerSample >> 3) * wfxSrc.nChannels;

    if (FAILED(psnd->SetMode(TRUE, TRUE))) // Activate the Sound Audio Stream
        goto LFailPushError;

    // allocate a buffer big enough to hold them all
    if (!FAllocPv((LPVOID *)&pbSrc, cbSrc, fmemNil, mprNormal))
        goto LFail;

    // get all of the samples
    if (FAILED(psnd->GetSampleData(pbSrc, 0, &csampSrc, NULL)))
        goto LFailPushError;

    // -------------------------------------
    // inplace compress the samples to ADPCM

    // figure out the correct size for dest waveformatex (so we get the codec info)
    if (acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &cbwfx))
        goto LFailPushError;

    // allocate a buffer of this size
    if (!FAllocPv((LPVOID *)&pwfxDst, cbwfx, fmemNil, mprNormal))
        goto LFail;

    // allocate a destination buffer big enough to hold them all
    cbDst = cbSrc;
    if (!FAllocPv((LPVOID *)&pbDst, cbDst, fmemNil, mprNormal))
        goto LFail;

    // set up the dest wfx
    pwfxDst->wFormatTag = WAVE_FORMAT_ADPCM;
    pwfxDst->nChannels = knChannels;
    pwfxDst->nSamplesPerSec = knSamplesPerSec;
    // pwfxDst->wBitsPerSample, pwfxDst->nAvgBytesPerSec, and
    // pwfxDst->nBlockAlign  are calced by the acmFormatSuggest()
    pwfxDst->cbSize = (WORD)cbwfx - sizeof(WAVEFORMATEX);

    // fill in the coeffiecients in the destination WFX by calling acmFormatSuggest
    if (acmFormatSuggest(NULL, &wfxSrc, pwfxDst, cbwfx,
                         ACM_FORMATSUGGESTF_WFORMATTAG | ACM_FORMATSUGGESTF_NCHANNELS |
                             ACM_FORMATSUGGESTF_NSAMPLESPERSEC))
        goto LFailPushError;

    // open convert stream
    if (acmStreamOpen(&hacmstream, NULL, &wfxSrc, pwfxDst, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME))
        goto LFailPushError;

    acmhdr.cbStruct = sizeof(ACMSTREAMHEADER);
    acmhdr.fdwStatus = 0;
    acmhdr.dwUser = 0;
    acmhdr.pbSrc = pbSrc;
    acmhdr.cbSrcLength = cbSrc;
    acmhdr.cbSrcLengthUsed = 0;
    acmhdr.dwSrcUser = 0;
    acmhdr.pbDst = pbDst;
    acmhdr.cbDstLength = cbDst;
    acmhdr.cbDstLengthUsed = 0;
    acmhdr.dwDstUser = 0;

    // prep acm convert header
    if (acmStreamPrepareHeader(hacmstream, &acmhdr, NULL))
        goto LFailPushError;

    // now convert
    if (acmStreamConvert(hacmstream, &acmhdr, ACM_STREAMCONVERTF_END))
        goto LFailPushError;

    // unprep header
    if (acmStreamUnprepareHeader(hacmstream, &acmhdr, NULL))
        goto LFailPushError;

    // close stream
    acmStreamClose(hacmstream, NULL);
    hacmstream = pvNil;

    // OK, now we have the sound file converted in memory, write out the data file...
    // open file
    pfilNew = FileObject::PfilCreate(&fniNew);
    if (pvNil == pfilNew)
        goto LFail;
    fpNew = 0;

    // write out 'riff' header
    dwTag = RIFF_TAG;
    if (!pfilNew->FWriteRgbSeq(&dwTag, sizeof(dwTag), &fpNew))
        goto LFail;
    cbCompressedFile = sizeof(RiffHeader) + pwfxDst->cbSize + 12 - 8 +
                       acmhdr.cbDstLengthUsed; // +12 is for fact chunk chunk, -8 for is riff chunk
    dwLength = cbCompressedFile;
    if (!pfilNew->FWriteRgbSeq(&dwLength, sizeof(dwLength), &fpNew))
        goto LFail;

    // write WAVE tag
    dwTag = WAVE_TAG;
    if (!pfilNew->FWriteRgbSeq(&dwTag, sizeof(dwTag), &fpNew))
        goto LFail;

    // write out 'fmt '
    dwTag = FMT__TAG;
    if (!pfilNew->FWriteRgbSeq(&dwTag, sizeof(dwTag), &fpNew))
        goto LFail;
    dwLength = sizeof(WAVEFORMATEX) + pwfxDst->cbSize;
    if (!pfilNew->FWriteRgbSeq(&dwLength, sizeof(dwLength), &fpNew))
        goto LFail;
    if (!pfilNew->FWriteRgbSeq(pwfxDst, dwLength, &fpNew))
        goto LFail;

    // write out 'fact'
    dwTag = FACT_TAG;
    if (!pfilNew->FWriteRgbSeq(&dwTag, sizeof(dwTag), &fpNew))
        goto LFail;
    dwLength = sizeof(DWORD);
    if (!pfilNew->FWriteRgbSeq(&dwLength, sizeof(dwLength), &fpNew))
        goto LFail;
    dwLength = csampSrc;
    if (!pfilNew->FWriteRgbSeq(&dwLength, sizeof(dwLength), &fpNew))
        goto LFail;

    // write out 'data'
    dwTag = DATA_TAG;
    if (!pfilNew->FWriteRgbSeq(&dwTag, sizeof(dwTag), &fpNew))
        goto LFail;
    dwLength = acmhdr.cbDstLengthUsed;
    if (!pfilNew->FWriteRgbSeq(&dwLength, sizeof(dwLength), &fpNew))
        goto LFail;
    if (!pfilNew->FWriteRgbSeq(pbDst, dwLength, &fpNew))
        goto LFail;

    // clean up conversion
    FreePpv((LPVOID *)&pwfxDst); // free up destination waveformat
    FreePpv((LPVOID *)&pbSrc);   // free convert src buffer
    FreePpv((LPVOID *)&pbDst);   // free convert dest buffer
    ReleasePpo(&pfilNew);        // release open file
    if (psnd)
        psnd->SetMode(FALSE, TRUE);
    ReleasePpo(&psnd); // release sound

    // -------------------------------------------------------------------
    // Copy the original or converted file to a chunk in the current movie

    // if the compressed file is larger then the original
    if (cbCompressedFile > cbOriginalFile)
    {
        // then just use original file
        if (pvNil == (pfilNew = FileObject::PfilOpen(&fniSrc)))
            goto LFail;
    }
    else
    {
        // then we use the newly compressed file
        if (pvNil == (pfilNew = FileObject::PfilOpen(&fniNew)))
            goto LFail;
    }

    if (!MovieSoundMSND::FWriteWave(pfilNew, pcflDest, sty, &stnName, pcno))
        goto LFail;

    ReleasePpo(&pfilNew);
    if (tYes == fniNew.TExists())
        fniNew.FDelete();
    return fTrue;

LFailPushError:
    PushErc(ercSocBadSoundFile);
LFail:
    if (hacmstream)
    {
        acmStreamClose(hacmstream, NULL);
        hacmstream = pvNil;
    }
    if (psnd)
        psnd->SetMode(FALSE, TRUE);

    FreePpv((LPVOID *)&pwfxDst); // free dest waveformatex struct
    FreePpv((LPVOID *)&pbSrc);   // free convert src buffer
    FreePpv((LPVOID *)&pbDst);   // free convert dest buffer
    ReleasePpo(&pfilNew);
    ReleasePpo(&psnd);
    if (tYes == fniNew.TExists())
        fniNew.FDelete();
    return fFalse;
#endif
}

/***************************************************************************

    Invalidate a sound

***************************************************************************/
bool MovieSoundMSND::FInvalidate(void)
{
    AssertThis(0);
    SoundResourceContext context; auto services = _ResourceServices(&context);
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
    AssertThis(0); Assert(tool == toolMatcher || tool == toolSounder || tool == toolLooper, "Invalid tool");
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
    SoundResourceContext context; auto services = _ResourceServices(&context);
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
bool MovieSoundQueue::FEnqueue(PMovieSoundMSND pmsnd, long objID, bool fLoop, bool fQueue, long vlm, long spr, bool fActr, ulong dtsStart,
                   bool fLowPri)
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
