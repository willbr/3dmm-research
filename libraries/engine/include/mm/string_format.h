/* String formatting converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_STRING_FORMAT_H
#define MM_ENGINE_STRING_FORMAT_H
#include "string_value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMStringFormatArgument {
    MMU32 number; /* c/d/u/x/f: character, signed bits, unsigned value or tag. */
    const void *text; /* s/z: borrowed characters in the output's unit width. */
    MMI32 count; /* s/z: nonnegative character count, excluding a terminator. */
} MMStringFormatArgument;
typedef struct MMStringFormatArguments {
    void *context;
    /* Called in encounter order for c/s/z/f/x/d/u, after qualifiers and width
     * are parsed but before the field's capacity check. Text remains borrowed
     * until this field has been copied. Zero stops with the completed prefix. */
    int (*next)(void *, unsigned control, MMStringFormatArgument *);
    /* Optional malformed-format diagnostic. Output characters already written
     * are visible, but the original count remains until final publication. */
    void (*bad_format)(void *);
} MMStringFormatArguments;
/* Pattern contains 0..255 native units. The output borrows MMStringValue storage
 * and copy diagnostics. No varargs, native String types or allocations enter C.
 * Preserve legacy qualifier ordering, hexadecimal width masking, tag zero-byte
 * substitution, partial-prefix failure, and rejection of positional %<N>.
 * Fields fail when they would exactly fill the 255-unit output; literals may
 * fill it. On every return, publish count and terminator. Width accumulation
 * uses defined 32-bit wrapping; original signed-overflow inputs were undefined.
 * Return one on complete consumption, zero on malformed/overflow/provider failure.
 * Character-copy ranges must not overlap, as for mm_string_value_set. */
int mm_string_format(const MMStringValue *, const void *pattern, MMI32 count,
                     const MMStringFormatArguments *);
#ifdef __cplusplus
}
#endif
#endif
