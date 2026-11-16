/* Counted string operations converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_STRING_VALUE_H
#define MM_ENGINE_STRING_VALUE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed storage: 257 native one-byte or unsigned-short units, containing a
 * count (0..255), characters and a zero terminator. The host owns storage and
 * diagnostics. Units must be 1 or 2; unsigned short must be 16 bits for unit 2.
 * Mutators accept nonnegative counts; character copy ranges must not overlap.
 * Value self-copy is allowed when the diagnostic provider allows it.
 * No allocation or encoding conversion. */
typedef struct MMStringValue {
    void *buffer;
    unsigned unit;
    void *context;
    /* Optional native CopyPb diagnostics, called before character/value copies
     * (after the tail shift for insertion). Does not own or modify the buffers. */
    void (*check_copy)(void *context, const void *source, void *destination, size_t bytes);
} MMStringValue;
void mm_string_value_clear(const MMStringValue *);
void mm_string_value_copy(const MMStringValue *, const void *counted_source);
void mm_string_value_set(const MMStringValue *, const void *characters, MMI32 count);
void mm_string_value_delete(const MMStringValue *, MMI32 position, MMI32 count);
int mm_string_value_append(const MMStringValue *, const void *characters, MMI32 count);
/* Positions clamp to [0,length]. Preserve historical insertion truncation,
 * including its unusual retained-length calculation when only the tail overflows.
 * Append/insert publish a truncated result and return zero on overflow. */
int mm_string_value_insert(const MMStringValue *, MMI32 position, const void *characters, MMI32 count);
int mm_string_value_equal(const MMStringValue *, const void *characters, MMI32 count);
/* Base 0 accepts spaces/repeated signs and detects octal/hex. Explicit bases
 * 2..35 accept no leading signs. A digit equal to the radix is accepted; arithmetic
 * wraps at 32 bits. Failure leaves output untouched (native debug trash is a host
 * service). These are legacy rules, deliberately not strtol semantics. */
int mm_string_value_number(const MMStringValue *, MMI32 base, MMI32 *output);
/* Escape tab, CR, backslash and quote; LF stays literal. On overflow publish the
 * completed prefix only if an escape has been emitted, preserving legacy policy. */
int mm_string_value_expand(const MMStringValue *);
#ifdef __cplusplus
}
#endif
#endif
