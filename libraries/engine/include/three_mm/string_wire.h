/* Serialized strings converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_STRING_WIRE_H
#define THREE_MM_ENGINE_STRING_WIRE_H
#include "read_stream.h"
#include "text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_STRING_MAX = 255
    };
    typedef struct ThreeMMStringWire
    {
        void *buffer; /* (255+2) native one/two-byte units: count, text, terminator. */
        unsigned unit;
        int current_encoding, swapped_unicode_encoding, native_big_endian;
        void *context;
        ThreeMMI32 (*convert)(void *, const void *, ThreeMMU32 bytes, int source_encoding, void *, ThreeMMU32 capacity);
    } ThreeMMStringWire;
    typedef struct ThreeMMStringWriter
    {
        void *context;
        ThreeMMU32 length;
        int (*write_at)(void *, ThreeMMU32 offset, const void *, size_t bytes);
    } ThreeMMStringWriter;
    /* Legacy native OSK prefix, counted characters and terminating zero. The host
     * supplies the historical family-specific swapped-Unicode encoding. No newline
     * normalization or new encodings are introduced. Input/output buffers must not
     * alias. Invalid reads clear only count/first character, as String::SetNil did.
     * A supplied end receives the absolute ending offset, including a nonzero start;
     * otherwise the record must end at stream.length. Failed reads leave end alone. */
    ThreeMMU32 three_mm_string_wire_size(const ThreeMMStringWire *);
    int three_mm_string_wire_get(const ThreeMMStringWire *, void *output, ThreeMMU32 capacity);
    int three_mm_string_wire_write(const ThreeMMStringWire *, const ThreeMMStringWriter *, ThreeMMU32 offset);
    int three_mm_string_wire_read(const ThreeMMStringWire *, const ThreeMMReadStream *, ThreeMMU32 offset,
                                  ThreeMMU32 *end);
#ifdef __cplusplus
}
#endif
#endif
