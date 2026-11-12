/* Roll-call storage converted from engine movie_data.cpp and Kauai groups2.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ROLLCALL_STORAGE_H
#define THREE_MM_ENGINE_ROLLCALL_STORAGE_H
#include "collections.h"
#include "string_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMRollCallTable
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 count, bytes, stride;
        unsigned unit;
        int encoding, trash_unused;
    } ThreeMMRollCallTable;
    /* Unique owning dense table. Counted names use native one/two-byte units; extras
     * are opaque native bytes (runtime ThreeMMRollCallActor or 28-byte wire rows).
     * Storage does not retain/close embedded tags: ownership transfer and cleanup
     * belong to the movie/roll-call transaction APIs, including edit rollback.
     * Borrowed names expire on mutation. Inputs may not alias either buffer.
     * Failed mutations preserve rows but may grow capacity. Failed reads can leave
     * partial translated state and must be discarded. No reentrant callbacks. */
    int three_mm_rollcall_table_init(ThreeMMRollCallTable *, ThreeMMU32 extra, unsigned unit, int encoding, int trash,
                                     const ThreeMMStorageAllocator *);
    int three_mm_rollcall_table_dispose(ThreeMMRollCallTable *);
    int three_mm_rollcall_table_name(ThreeMMRollCallTable *, ThreeMMU32, const void **, ThreeMMU32 *);
    int three_mm_rollcall_table_extra(ThreeMMRollCallTable *, ThreeMMU32, void *);
    int three_mm_rollcall_table_set_extra(ThreeMMRollCallTable *, ThreeMMU32, const void *);
    int three_mm_rollcall_table_add(ThreeMMRollCallTable *, const void *, ThreeMMU32 characters, const void *extra);
    int three_mm_rollcall_table_rename(ThreeMMRollCallTable *, ThreeMMU32, const void *, ThreeMMU32 characters);
    int three_mm_rollcall_table_delete(ThreeMMRollCallTable *, ThreeMMU32);
    /* Uses the legacy dense GST header/translation policy, without Archive's stricter
     * validator. Only offsets follow the header byte order; extras remain opaque.
     * Read publishes original header byte order only on success. Foreign-width text
     * uses the supplied platform converter. Same-width legacy maps remain C policy. */
    int three_mm_rollcall_table_read(ThreeMMRollCallTable *, const ThreeMMReadStream *, ThreeMMStringConvert, void *,
                                     int *big_endian);
    ThreeMMU32 three_mm_rollcall_table_wire_size(const ThreeMMRollCallTable *);
    int three_mm_rollcall_table_write(ThreeMMRollCallTable *, const ThreeMMStorageWriter *, int big_endian);
#ifdef __cplusplus
}
#endif
#endif
