/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Midi stream and midi stream parser classes.

***************************************************************************/
#include "kauai_core.h"
#include "three_mm/midi.h"
ASSERTNAME

RTCLASS(MidiStreamParser)
RTCLASS(MidiStream)

/***************************************************************************
    Constructor for a midi stream parser.
***************************************************************************/
MidiStreamParser::MidiStreamParser(void)
{
    AssertBaseThis(0);
    _pmids = pvNil;
    three_mm_midi_parser_init(&_parser, nullptr, 0, 0, 500000);
}

/***************************************************************************
    Destructor for a midi stream parser.
***************************************************************************/
MidiStreamParser::~MidiStreamParser(void)
{
    AssertThis(0);

    if (pvNil != _pmids)
        UnlockHq(_pmids->_hqrgb);
    ReleasePpo(&_pmids);
}

/***************************************************************************
    Initialize this midi stream parser to the given midi stream and use the
    given time as the start time.
***************************************************************************/
void MidiStreamParser::Init(PMidiStream pmids, LegacyUlong tsStart, LegacyLong lwTempo)
{
    AssertThis(0);
    AssertNilOrPo(pmids, 0);

    if (_pmids != pmids)
    {
        if (_pmids)
        {
            UnlockHq(_pmids->_hqrgb);
            ReleasePpo(&_pmids);
        }
        _pmids = pmids;
        if (!_pmids)
        {
            three_mm_midi_parser_init(&_parser, nullptr, 0, 0, 500000);
            return;
        }
        _pmids->AddRef();
        three_mm_midi_parser_init(&_parser, PvLockHq(_pmids->_hqrgb), CbOfHq(_pmids->_hqrgb), tsStart, lwTempo);
    }
    else if (_pmids)
        three_mm_midi_parser_init(&_parser, _parser.data, _parser.size, tsStart, lwTempo);
    AssertThis(0);
}

/***************************************************************************
    Get the next midi event. If fAdvance is true, advance to the next event
    after getting this one.
***************************************************************************/
bool MidiStreamParser::FGetEvent(PMIDEV pmidev, bool fAdvance)
{
    AssertThis(0);
    AssertNilOrVarMem(pmidev);

    ThreeMMMidiEvent event;
    const bool ok = three_mm_midi_next(&_parser, &event, fAdvance) != 0;
    if (!ok)
    {
        TrashVar(pmidev);
        return false;
    }
    if (pmidev)
    {
        ClearPb(pmidev, size(*pmidev));
        pmidev->ts = event.timestamp;
        pmidev->cb = event.count;
        pmidev->lwTempo = event.tempo;
        CopyPb(event.send, pmidev->rgbSend, 4);
    }
    return true;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStreamParser.
***************************************************************************/
void MidiStreamParser::AssertValid(LegacyUlong grf)
{
    MidiStreamParser_PAR::AssertValid(0);
    if (pvNil == _pmids)
        return;

    AssertPo(_pmids, 0);
    Assert(_parser.data == (byte *)QvFromHq(_pmids->_hqrgb), 0);
    Assert(_parser.size == static_cast<ThreeMMU32>(CbOfHq(_pmids->_hqrgb)), 0);
    Assert(_parser.cursor <= _parser.size, 0);
}

/***************************************************************************
    Mark memory for the MidiStreamParser.
***************************************************************************/
void MidiStreamParser::MarkMem(void)
{
    AssertValid(0);
    MidiStreamParser_PAR::MarkMem();
    MarkMemObj(_pmids);
}
#endif // DEBUG

/***************************************************************************
    Constructor for a midi stream object.
***************************************************************************/
MidiStream::MidiStream(void)
{
}

/***************************************************************************
    Destructor for a midi stream object.
***************************************************************************/
MidiStream::~MidiStream(void)
{
    FreePhq(&_hqrgb);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a MidiStream.
***************************************************************************/
void MidiStream::AssertValid(LegacyUlong grf)
{
    MidiStream_PAR::AssertValid(0);
    AssertHq(_hqrgb);
}

/***************************************************************************
    Mark memory for the MidiStream.
***************************************************************************/
void MidiStream::MarkMem(void)
{
    AssertValid(0);
    MidiStream_PAR::MarkMem();
    MarkHq(_hqrgb);
}
#endif // DEBUG

/***************************************************************************
    A baco reader for a midi stream.
***************************************************************************/
bool MidiStream::FReadMids(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                           PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, fblckReadable);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    const auto services = _PayloadServices();
    return three_mm_midi_payload_fetch(pblck, ppbaco, pcb, &services) != 0;
}

ThreeMMSoundPayloadServices MidiStream::_PayloadServices()
{
    ThreeMMSoundPayloadServices s = {};
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block, int packed) -> ThreeMMI32 {
        return static_cast<PDataBlock>(block)->Cb(packed != 0);
    };
    s.create = [](void *) -> void * { return NewObj MidiStream; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.read_midi = [](void *, void *object, void *block) -> int {
        return static_cast<PDataBlock>(block)->FReadHq(&static_cast<PMidiStream>(object)->_hqrgb);
    };
    s.invalidate = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    return s;
}

/***************************************************************************
    Read a midi stream from the given block.
***************************************************************************/
PMidiStream MidiStream::PmidsRead(PDataBlock pblck)
{
    AssertPo(pblck, 0);

    const auto services = _PayloadServices();
    return static_cast<PMidiStream>(three_mm_midi_payload_read(pblck, &services));
}

/***************************************************************************
    Read a native standard midi file and create a midi stream from it.
***************************************************************************/
PMidiStream MidiStream::PmidsReadNative(Filename *pfni)
{
    AssertPo(pfni, ffniFile);

    auto file = FileObject::PfilOpen(pfni);
    if (!file)
        return nullptr;
    FileLocation location;
    location.pfil = file;
    location.fp = 0;
    location.cb = file->FpMac();
    HQ source = nullptr;
    PMidiStream result = nullptr;
    ThreeMMCollectionStorage scratch;
    three_mm_storage_init(&scratch, nullptr);
    if (location.cb > 0 && location.FReadHq(&source, location.cb, 0))
    {
        ThreeMMU32 bytes;
        const bool ok = three_mm_midi_import(PvLockHq(source), location.cb, &scratch, &bytes) != 0;
        UnlockHq(source);
        if (ok && (result = NewObj MidiStream))
        {
            if (FAllocHq(&result->_hqrgb, bytes, fmemNil, mprNormal))
            {
                CopyPb(scratch.buffers[0].data, PvLockHq(result->_hqrgb), bytes);
                UnlockHq(result->_hqrgb);
            }
            else
                ReleasePpo(&result);
        }
    }
    three_mm_storage_dispose(&scratch);
    FreePhq(&source);
    ReleasePpo(&file);
    return result;
}

/***************************************************************************
    Write a midi stream to the given block.
***************************************************************************/
bool MidiStream::FWrite(PDataBlock pblck)
{
    AssertThis(0);
    AssertPo(pblck, 0);

    return pblck->FWriteHq(_hqrgb, 0);
}

/***************************************************************************
    Return the length of this midi stream on file.
***************************************************************************/
LegacyLong MidiStream::CbOnFile(void)
{
    AssertThis(0);

    return CbOfHq(_hqrgb);
}
