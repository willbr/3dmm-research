/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    Midi playback class.

***************************************************************************/
#ifndef MIDI_H
#define MIDI_H

#include "mm/sound_payload.h"
#include "mm/midi.h"
#include "mm/read_stream.h"

using namespace Chunky;

typedef class MidiStream *PMidiStream;

// midi event
struct MIDEV
{
    LegacyUlong ts;     // time stamp of this event
    LegacyLong cb;      // number of bytes to send (in rgbSend)
    LegacyLong lwTempo; // the current tempo - at a tempo change, cb will be 0
    union {
        byte rgbSend[4];   // bytes to send if pvLong is nil
        LegacyLong lwSend; // for convenience
    };
};
typedef MIDEV *PMIDEV;

/***************************************************************************
    Midi stream parser. Knows how to parse standard MIDI streams.
***************************************************************************/
typedef class MidiStreamParser *PMidiStreamParser;
#define MidiStreamParser_PAR BASE
#define kclsMidiStreamParser 'MSTP'
class MidiStreamParser : public MidiStreamParser_PAR
{
    RTCLASS_DEC
    NOCOPY(MidiStreamParser)
    ASSERT
    MARKMEM

  protected:
    MMMidiParser _parser;

    Debug(LegacyLong _cactLongLock;) PMidiStream _pmids;

  public:
    MidiStreamParser(void);
    ~MidiStreamParser(void);

    void Init(PMidiStream pmids, LegacyUlong tsStart = 0, LegacyLong lwTempo = 500000);
    bool FGetEvent(PMIDEV pmidev, bool fAdvance = fTrue);
};

/***************************************************************************
    Midi Stream object - this is like a MTrk chunk in a standard MIDI file,
    with timing in milliseconds.
***************************************************************************/
#define MidiStream_PAR BaseCacheableObject
#define kclsMidiStream 'MIDS'
class MidiStream : public MidiStream_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    static MMSoundPayloadServices _PayloadServices();
    HQ _hqrgb;

    friend MidiStreamParser;

    MidiStream(void);

  public:
    static int FReadMids(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *,
                           void **object, MMI32 *bytes);
    static PMidiStream PmidsRead(PDataBlock pblck);
    static PMidiStream PmidsReadNative(Filename *pfni);
    ~MidiStream(void);

    virtual bool FWrite(PDataBlock pblck);
    virtual LegacyLong CbOnFile(void);
    MMReadStream BorrowStream();
};

#endif //! MIDI_H
