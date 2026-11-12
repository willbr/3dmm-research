/* Source-name policy converted from engine tagman.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SOURCE_NAMES_H
#define THREE_MM_ENGINE_SOURCE_NAMES_H
#include "collections.h"
#include "string_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMSourceNames
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 count, bytes;
        unsigned unit;
        int encoding, trash_unused;
    } ThreeMMSourceNames;
    typedef struct ThreeMMSourceNameParts
    {
        ThreeMMU32 long_count, short_offset, short_count;
    } ThreeMMSourceNameParts;
    typedef int (*ThreeMMSourceNameEqual)(void *, const void *, ThreeMMU32, const void *, ThreeMMU32);
    typedef int (*ThreeMMSourceNameProvider)(void *, const void *long_name, ThreeMMU32, const void *short_name,
                                             ThreeMMU32, void *path);
    typedef struct ThreeMMSourceNameInput
    {
        void *context;
        ThreeMMU32 count;
        void (*get)(void *, ThreeMMU32, const void **text, ThreeMMU32 *characters, ThreeMMI32 *sid);
        void (*invalid_sid)(void *, ThreeMMI32); /* Optional legacy diagnostic, not rejection. */
    } ThreeMMSourceNameInput;
    /* Unique owning record; never copy/move. Stores native one/two-byte counted
     * strings and native signed SID extras. First SID wins; order stays insertion
     * order. Inputs must not alias storage. Borrowed text expires on mutation.
     * Merging preserves successful earlier rows on failure. Negative incoming SIDs
     * retain release-build acceptance; native diagnostics remain a host callback. */
    int three_mm_source_names_init(ThreeMMSourceNames *, unsigned unit, int encoding, int trash,
                                   const ThreeMMStorageAllocator *);
    int three_mm_source_names_dispose(ThreeMMSourceNames *);
    int three_mm_source_names_at(ThreeMMSourceNames *, ThreeMMU32, const void **text, ThreeMMU32 *count,
                                 ThreeMMI32 *sid);
    int three_mm_source_names_find_id(ThreeMMSourceNames *, ThreeMMI32 sid, ThreeMMU32 *position);
    int three_mm_source_names_add(ThreeMMSourceNames *, ThreeMMI32 sid, const void *, ThreeMMU32 count);
    int three_mm_source_names_merge(ThreeMMSourceNames *, const ThreeMMSourceNameInput *, int swap_sid);
    /* Split scans until the first zero for '/', like Psz. A slash suffix ends at
     * its first zero; with no slash both outputs retain the full counted string. */
    ThreeMMSourceNameParts three_mm_source_name_split(const void *, ThreeMMU32 count, unsigned unit);
    int three_mm_source_names_find_name(ThreeMMSourceNames *, const void *, ThreeMMU32 count, ThreeMMSourceNameEqual,
                                        void *, ThreeMMI32 *sid);
    /* On missing SID in a nonempty table, publishes the last name, matching legacy. */
    int three_mm_source_names_merged(ThreeMMSourceNames *, ThreeMMI32 sid, const void **text, ThreeMMU32 *count);
    /* Missing provider/name returns LEGACY (0). Provider sees a stable stack copy,
     * allowing it to add names without invalidating the borrowed query spans. */
    int three_mm_source_names_resolve(ThreeMMSourceNames *, ThreeMMI32 sid, void *path, ThreeMMSourceNameProvider,
                                      void *);
    ThreeMMU32 three_mm_source_names_wire_size(const ThreeMMSourceNames *);
    /* Native text/SID payload; only header/offset words follow requested byte order.
     * Restores offsets on every failure; exact writer length required. */
    int three_mm_source_names_write(ThreeMMSourceNames *, const ThreeMMStorageWriter *, int big_endian);
#ifdef __cplusplus
}
#endif
#endif
