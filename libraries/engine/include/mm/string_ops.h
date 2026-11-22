/* String rules converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_STRING_OPS_H
#define MM_ENGINE_STRING_OPS_H
#include "string_value.h"
#include "text.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_STRING_EQUAL = 1, MM_STRING_GREATER = 2, MM_STRING_LESS = 4 };
typedef struct MMStringOps {
    unsigned unit; /* Native one-byte or unsigned-short units, as MMStringValue. */
    int signed_units; /* Internal sorting follows the native character's sign. */
    void *context;
    void (*check_range)(void *, const void *, size_t);
    void (*check_copy)(void *, const void *, void *, size_t);
    void (*check_counted)(void *, const void *); /* Optional stz count-access diagnostic. */
    void (*upper)(void *, void *, size_t count);
    int (*compare)(void *, const void *, size_t, const void *, size_t, int ignore_case);
} MMStringOps;
/* Length reads through the first zero, without imposing a maximum. The caller
 * supplies a readable terminated buffer; native length diagnostics remain host
 * services. Counted validation rejects counts above 255 before checking payload.
 * A terminated counted buffer also supplies its final unit (count+2 total). */
size_t mm_string_zero_length(const void *, unsigned unit);
int mm_string_counted_valid(const MMStringOps *, const void *, int terminated);
int mm_string_raw_equal(const MMStringOps *, const void *, MMU32, const void *, MMU32);
int mm_string_equals_ascii(const char *ascii, const void *text, MMU32 length, unsigned unit);
MMU32 mm_string_internal_compare(const MMStringOps *, const void *, MMU32, const void *, MMU32);
/* Equality uppercases two copied blocks of at most 255 units before comparing
 * bytes; it is distinct from user collation. Null upper uses portable text case
 * rules. Collation accepts -1/0/+1; any other provider result falls back to the
 * length-first, sign-aware internal comparison. Null compare uses portable text. */
int mm_string_user_equal(const MMStringOps *, const void *, MMU32, const void *, MMU32, int ignore_case);
MMU32 mm_string_user_compare(const MMStringOps *, const void *, MMU32, const void *, MMU32, int ignore_case);
void mm_string_translate(void *, MMU32 count, unsigned unit, int encoding, int current_encoding, int to_current);
/* Legacy character flags: white-overhang=1, break=2, may-break=4, ignore=8,
 * control=16, tab=32. Input is an unsigned character value. */
MMU32 mm_string_character_flags(MMU32);
/* ANSI convenience conversion preserves the legacy 255-unit conversion capacity
 * (including the terminator), and UTF16_LE source marker for wide export. Input
 * chars excludes its terminator. A failed import clears count/first character;
 * a failed export clears only output[0]. Native code-page providers are borrowed.
 * Return one on success, zero on conversion failure or truncated byte import. */
int mm_string_from_ansi(const MMStringValue *, const MMTextHost *, const char *, size_t chars);
int mm_string_to_ansi(const MMStringValue *, const MMTextHost *, char *);
#ifdef __cplusplus
}
#endif
#endif
