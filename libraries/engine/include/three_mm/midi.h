/* MIDI policy converted from Kauai midi.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MIDI_H
#define THREE_MM_ENGINE_MIDI_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMidiEvent
    {
        ThreeMMU32 timestamp;
        ThreeMMI32 count, tempo;
        unsigned char send[4];
    } ThreeMMMidiEvent;
    /* The stream is borrowed and immutable. Peeking preserves cursor/time/tempo,
     * but retains legacy running-status changes. Failure exhausts the parser. */
    typedef struct ThreeMMMidiParser
    {
        const unsigned char *data;
        ThreeMMU32 size, cursor, timestamp;
        ThreeMMI32 tempo;
        unsigned char status;
    } ThreeMMMidiParser;
    void three_mm_midi_parser_init(ThreeMMMidiParser *, const void *, ThreeMMU32, ThreeMMU32, ThreeMMI32);
    int three_mm_midi_next(ThreeMMMidiParser *, ThreeMMMidiEvent *, int advance);
    unsigned three_mm_midi_encode_time(ThreeMMU32, unsigned char *);
    /* Imports SMF into millisecond track bytes. Initialized scratch owns output in
     * section 0 and temporary tracks in section 1; no host or C++ objects are used.
     * The caller disposes scratch after copying/consuming the published prefix.
     * Failure may change scratch, but does not publish a length. Input must not
     * alias scratch. Formats 1/2 merge/sequence; other formats use the first track.
     * Unsupported arithmetic overflow fails instead of invoking undefined behavior. */
    int three_mm_midi_import(const void *, ThreeMMU32, ThreeMMCollectionStorage *, ThreeMMU32 *);
#ifdef __cplusplus
}
#endif
#endif
