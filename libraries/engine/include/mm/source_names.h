/* Source-name policy converted from engine tagman.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SOURCE_NAMES_H
#define MM_ENGINE_SOURCE_NAMES_H
#include "collections.h"
#include "string_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMSourceNames
    {
        MMCollectionStorage storage;
        MMU32 count, bytes;
        unsigned unit;
        int encoding, trash_unused;
    } MMSourceNames;
    typedef struct MMSourceNameParts
    {
        MMU32 long_count, short_offset, short_count;
    } MMSourceNameParts;
    typedef int (*MMSourceNameEqual)(void *, const void *, MMU32, const void *, MMU32);
    typedef int (*MMSourceNameProvider)(void *, const void *long_name, MMU32, const void *short_name,
                                             MMU32, void *path);
    typedef struct MMSourceNameInput
    {
        void *context;
        MMU32 count;
        void (*get)(void *, MMU32, const void **text, MMU32 *characters, MMI32 *sid);
        void (*invalid_sid)(void *, MMI32); /* Optional legacy diagnostic, not rejection. */
    } MMSourceNameInput;
    /* Unique owning record; never copy/move. Stores native one/two-byte counted
     * strings and native signed SID extras. First SID wins; order stays insertion
     * order. Inputs must not alias storage. Borrowed text expires on mutation.
     * Merging preserves successful earlier rows on failure. Negative incoming SIDs
     * retain release-build acceptance; native diagnostics remain a host callback. */
    int mm_source_names_init(MMSourceNames *, unsigned unit, int encoding, int trash,
                                   const MMStorageAllocator *);
    int mm_source_names_dispose(MMSourceNames *);
    int mm_source_names_at(MMSourceNames *, MMU32, const void **text, MMU32 *count,
                                 MMI32 *sid);
    int mm_source_names_find_id(MMSourceNames *, MMI32 sid, MMU32 *position);
    int mm_source_names_add(MMSourceNames *, MMI32 sid, const void *, MMU32 count);
    int mm_source_names_merge(MMSourceNames *, const MMSourceNameInput *, int swap_sid);
    /* Split scans until the first zero for '/', like Psz. A slash suffix ends at
     * its first zero; with no slash both outputs retain the full counted string. */
    MMSourceNameParts mm_source_name_split(const void *, MMU32 count, unsigned unit);
    int mm_source_names_find_name(MMSourceNames *, const void *, MMU32 count, MMSourceNameEqual,
                                        void *, MMI32 *sid);
    /* On missing SID in a nonempty table, publishes the last name, matching legacy. */
    int mm_source_names_merged(MMSourceNames *, MMI32 sid, const void **text, MMU32 *count);
    /* Missing provider/name returns LEGACY (0). Provider sees a stable stack copy,
     * allowing it to add names without invalidating the borrowed query spans. */
    int mm_source_names_resolve(MMSourceNames *, MMI32 sid, void *path, MMSourceNameProvider,
                                      void *);
    MMU32 mm_source_names_wire_size(const MMSourceNames *);
    /* Native text/SID payload; only header/offset words follow requested byte order.
     * Restores offsets on every failure; exact writer length required. */
    int mm_source_names_write(MMSourceNames *, const MMStorageWriter *, int big_endian);
#ifdef __cplusplus
}
#endif
#endif
