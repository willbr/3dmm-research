/* Roll-call storage converted from engine movie_data.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ROLLCALL_STORAGE_H
#define MM_ENGINE_ROLLCALL_STORAGE_H
#include "collections.h"
#include "string_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMRollCallTable
    {
        MMCollectionStorage storage;
        MMU32 count, bytes, stride;
        unsigned unit;
        int encoding, trash_unused;
    } MMRollCallTable;
    /* Unique owning dense table. Counted names use native one/two-byte units; extras
     * are opaque native bytes (runtime MMRollCallActor or 28-byte wire rows).
     * Storage does not retain/close embedded tags: ownership transfer and cleanup
     * belong to the movie/roll-call transaction APIs, including edit rollback.
     * Borrowed names expire on mutation. Inputs may not alias either buffer.
     * Failed mutations preserve rows but may grow capacity. Failed reads can leave
     * partial translated state and must be discarded. No reentrant callbacks. */
    int mm_rollcall_table_init(MMRollCallTable *, MMU32 extra, unsigned unit, int encoding, int trash,
                                     const MMStorageAllocator *);
    int mm_rollcall_table_dispose(MMRollCallTable *);
    int mm_rollcall_table_name(MMRollCallTable *, MMU32, const void **, MMU32 *);
    int mm_rollcall_table_extra(MMRollCallTable *, MMU32, void *);
    int mm_rollcall_table_set_extra(MMRollCallTable *, MMU32, const void *);
    int mm_rollcall_table_add(MMRollCallTable *, const void *, MMU32 characters, const void *extra);
    int mm_rollcall_table_rename(MMRollCallTable *, MMU32, const void *, MMU32 characters);
    int mm_rollcall_table_delete(MMRollCallTable *, MMU32);
    /* Uses the legacy dense GST header/translation policy, without Archive's stricter
     * validator. Only offsets follow the header byte order; extras remain opaque.
     * Read publishes original header byte order only on success. Foreign-width text
     * uses the supplied platform converter. Same-width legacy maps remain C policy. */
    int mm_rollcall_table_read(MMRollCallTable *, const MMReadStream *, MMStringConvert, void *,
                                     int *big_endian);
    MMU32 mm_rollcall_table_wire_size(const MMRollCallTable *);
    int mm_rollcall_table_write(MMRollCallTable *, const MMStorageWriter *, int big_endian);
#ifdef __cplusplus
}
#endif
#endif
