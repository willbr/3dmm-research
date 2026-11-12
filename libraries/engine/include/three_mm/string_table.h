/* String storage converted from Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_STRING_TABLE_H
#define THREE_MM_ENGINE_STRING_TABLE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMStringTable ThreeMMStringTable;
    /* Borrowed mutable storage. Entries contain a native 32-bit offset followed by
     * opaque extra bytes; strings have a native one/two-byte length, no terminator.
     * free_count=-1 selects dense storage; otherwise -1 offsets denote free slots.
     * reserve may relocate both buffers and must refresh pointers/capacities before
     * returning success. It must preserve old bytes on failure. No re-entry/aliases
     * into these buffers are allowed for mutation inputs or callback arguments.
     * The host owns allocation; this record owns all logical mutation state. */
    struct ThreeMMStringTable
    {
        unsigned char *strings, *entries;
        ThreeMMU32 bytes, count, stride, string_capacity, entry_capacity;
        ThreeMMI32 free_count;
        unsigned unit_bytes;
        int trash_unused;
        void *context;
        int (*reserve)(void *, ThreeMMStringTable *, ThreeMMU32, ThreeMMU32);
    };
    typedef int (*ThreeMMStringCompare)(void *, const void *, ThreeMMU32, const void *, ThreeMMU32);
    int three_mm_strings_get(const ThreeMMStringTable *, ThreeMMU32, const void **, ThreeMMU32 *);
    int three_mm_strings_insert(ThreeMMStringTable *, ThreeMMU32, const void *, ThreeMMU32, const void *);
    int three_mm_strings_add(ThreeMMStringTable *, const void *, ThreeMMU32, const void *, ThreeMMU32 *);
    int three_mm_strings_put(ThreeMMStringTable *, ThreeMMU32, const void *, ThreeMMU32);
    int three_mm_strings_delete(ThreeMMStringTable *, ThreeMMU32);
    int three_mm_strings_move(ThreeMMStringTable *, ThreeMMU32, ThreeMMU32);
    /* Exact linear search skips free slots. Sorted search uses the host's existing
     * comparison policy and midpoint behavior; a miss publishes insertion position. */
    int three_mm_strings_find(const ThreeMMStringTable *, const void *, ThreeMMU32, ThreeMMStringCompare, void *,
                              ThreeMMU32 *);
    /* Swap only offset words; extra bytes are never interpreted. */
    void three_mm_strings_swap_offsets(ThreeMMStringTable *);
    /* Same-width legacy conversion walks physical packed strings, independent of
     * entry order. Different-width conversion retains the existing host converter
     * and rebuilds packed strings in entry order. Extra bytes remain untouched.
     * As with the legacy loader, failure during translation can leave partial
     * state; discard the loading table. Ordinary mutation failures preserve data
     * and counts, though reserve may have grown either allocation. */
    typedef size_t (*ThreeMMStringConvert)(void *, const void *, size_t, int, void *, size_t);
    int three_mm_strings_translate(ThreeMMStringTable *, int source_encoding, int current_encoding,
                                   ThreeMMStringConvert, void *);
    int three_mm_strings_translate_same(ThreeMMStringTable *, int other_encoding, int current_encoding, int to_current);
    /* Saving chooses the requested Mac/Windows family with the current unit
     * width. This is independent of native code-page conversion. */
    int three_mm_strings_wire_system(int current_encoding, int requested_encoding, int *stored_encoding);
#ifdef __cplusplus
}
#endif
#endif
