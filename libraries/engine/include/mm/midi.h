/* MIDI policy converted from Kauai midi.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MIDI_H
#define MM_ENGINE_MIDI_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMidiEvent
    {
        MMU32 timestamp;
        MMI32 count, tempo;
        unsigned char send[4];
    } MMMidiEvent;
    /* The stream is borrowed and immutable. Peeking preserves cursor/time/tempo,
     * but retains legacy running-status changes. Failure exhausts the parser. */
    typedef struct MMMidiParser
    {
        const unsigned char *data;
        MMU32 size, cursor, timestamp;
        MMI32 tempo;
        unsigned char status;
    } MMMidiParser;
    void mm_midi_parser_init(MMMidiParser *, const void *, MMU32, MMU32, MMI32);
    int mm_midi_next(MMMidiParser *, MMMidiEvent *, int advance);
    unsigned mm_midi_encode_time(MMU32, unsigned char *);
    /* Imports SMF into millisecond track bytes. Initialized scratch owns output in
     * section 0 and temporary tracks in section 1; no host or C++ objects are used.
     * The caller disposes scratch after copying/consuming the published prefix.
     * Failure may change scratch, but does not publish a length. Input must not
     * alias scratch. Formats 1/2 merge/sequence; other formats use the first track.
     * Unsupported arithmetic overflow fails instead of invoking undefined behavior. */
    int mm_midi_import(const void *, MMU32, MMCollectionStorage *, MMU32 *);
#ifdef __cplusplus
}
#endif
#endif
