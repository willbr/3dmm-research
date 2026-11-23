/* Serialized strings converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_STRING_WIRE_H
#define MM_ENGINE_STRING_WIRE_H
#include "read_stream.h"
#include "text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_STRING_MAX = 255,
        MM_STRING_UTF8_MAX = MM_STRING_MAX * 3
    };
    /* Display a counted native legacy string (ANSI or native UTF-16).
     * Return 1 on success, 0 for unavailable legacy conversion, -1 for
     * malformed UTF-16 or short output. Only success sets written. */
    int mm_string_text_utf8(const MMTextHost *, const void *, size_t count, unsigned unit,
                            char *, size_t capacity, size_t *written);
    typedef struct MMStringWire
    {
        void *buffer; /* (255+2) native one/two-byte units: count, text, terminator. */
        unsigned unit;
        int current_encoding, swapped_unicode_encoding, native_big_endian;
        void *context;
        MMI32 (*convert)(void *, const void *, MMU32 bytes, int source_encoding, void *, MMU32 capacity);
    } MMStringWire;
    typedef struct MMStringWriter
    {
        void *context;
        MMU32 length;
        int (*write_at)(void *, MMU32 offset, const void *, size_t bytes);
    } MMStringWriter;
    /* Legacy native OSK prefix, counted characters and terminating zero. The host
     * supplies the historical family-specific swapped-Unicode encoding. No newline
     * normalization or new encodings are introduced. Input/output buffers must not
     * alias. Invalid reads clear only count/first character, as String::SetNil did.
     * A supplied end receives the absolute ending offset, including a nonzero start;
     * otherwise the record must end at stream.length. Failed reads leave end alone. */
    MMU32 mm_string_wire_size(const MMStringWire *);
    int mm_string_wire_get(const MMStringWire *, void *output, MMU32 capacity);
    int mm_string_wire_write(const MMStringWire *, const MMStringWriter *, MMU32 offset);
    int mm_string_wire_read(const MMStringWire *, const MMReadStream *, MMU32 offset,
                                  MMU32 *end);
#ifdef __cplusplus
}
#endif
#endif
