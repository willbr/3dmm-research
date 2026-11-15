/* Chunk index storage converted from Kauai chunk.cpp and groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_TABLE_H
#define MM_ENGINE_CHUNK_TABLE_H
#include "collections.h"
#include "chunky_file.h"
#if USHRT_MAX != 0xffffU
#error Compact chunk records require sixteen-bit unsigned short
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkRecordKey
    {
        MMU32 ctg, cno;
    } MMChunkRecordKey;
    typedef struct MMChunkRecordSmall
    {
        MMChunkRecordKey cki;
        MMI32 fp;
        MMU32 luGrfcrpCb;
        unsigned short ckid, ccrpRef;
    } MMChunkRecordSmall;
    typedef struct MMChunkRecordBig
    {
        MMChunkRecordKey cki;
        MMI32 fp, cb, ckid, ccrpRef, rti;
        MMU32 grfcrp;
    } MMChunkRecordBig;
    typedef char MMChunkSmallSize[(sizeof(MMChunkRecordSmall) == 20) ? 1 : -1];
    typedef char MMChunkBigSize[(sizeof(MMChunkRecordBig) == 32) ? 1 : -1];
    /* Trusted native-order fixed records. Compact sizes/flags and counts retain
     * their original 24/8/16-bit storage behavior. Full assignment preserves expanded
     * runtime IDs; masked flag updates preserve sizes and unrelated flag bits. */
    MMU32 mm_chunk_record_flags(const void *, int expanded, MMU32 mask);
    MMI32 mm_chunk_record_bytes(const void *, int expanded);
    MMI32 mm_chunk_record_name_bytes(const void *, int expanded, MMI32 variable);
    void mm_chunk_record_set_bytes(void *, int expanded, MMI32 bytes);
    void mm_chunk_record_set_flags(void *, int expanded, MMU32 flags, MMU32 mask);
    void mm_chunk_record_get(const void *, int expanded, MMU32 variable, MMChunkEntry *);
    void mm_chunk_record_set(void *, int expanded, const MMChunkEntry *);
    typedef struct MMChunkTable
    {
        MMCollectionStorage storage;
        MMU32 fixed_bytes, count, bytes;
        int trash_unused;
    } MMChunkTable;
    /* Unique allocator-owned record and two buffers; never copy/move it. Outputs are
     * unchanged on failed create. Fixed records and variable child/name bytes use
     * native order. Borrowed pointers expire on resizing/mutation; insertion inputs
     * cannot alias this table. Locks pin the payload buffer, matching legacy GG.
     * Failed mutations preserve logical entries but may change storage capacity. */
    int mm_chunk_table_create(const MMStorageAllocator *, MMU32 width, MMU32 reserve,
                                    int trash_unused, MMChunkTable **);
    int mm_chunk_table_close(MMChunkTable *);
    void *mm_chunk_table_fixed(MMChunkTable *, MMU32, MMU32 *variable);
    void *mm_chunk_table_variable(MMChunkTable *, MMU32, MMU32 *bytes);
    MMU32 mm_chunk_table_variable_bytes(MMChunkTable *, MMU32);
    int mm_chunk_table_read(MMChunkTable *, MMU32 index, MMU32 offset, MMU32 bytes, void *);
    int mm_chunk_table_put(MMChunkTable *, MMU32 index, MMU32 offset, MMU32 bytes,
                                 const void *);
    int mm_chunk_table_insert(MMChunkTable *, MMU32 index, MMU32 bytes, const void *variable,
                                    const void *fixed);
    int mm_chunk_table_delete(MMChunkTable *, MMU32);
    int mm_chunk_table_move(MMChunkTable *, MMU32 source, MMU32 target);
    int mm_chunk_table_insert_bytes(MMChunkTable *, MMU32, MMU32 offset, MMU32 bytes,
                                          const void *);
    int mm_chunk_table_delete_bytes(MMChunkTable *, MMU32, MMU32 offset, MMU32 bytes);
    int mm_chunk_table_move_bytes(MMChunkTable *, MMU32 source, MMU32 source_offset,
                                        MMU32 target, MMU32 target_offset, MMU32 bytes);
    int mm_chunk_table_lock(MMChunkTable *);
    int mm_chunk_table_unlock(MMChunkTable *);
    int mm_chunk_table_entry(MMChunkTable *, MMU32, MMChunkEntry *);
    int mm_chunk_table_child(MMChunkTable *, MMU32 parent, MMU32 position, MMChunkChild *);
    MMChunkAccess mm_chunk_table_access(MMChunkTable *);
    int mm_chunk_table_clear_flags(MMChunkTable *, MMU32 mask);
    MMU32 mm_chunk_table_wire_size(const MMChunkTable *);
    /* Fixed/variable payloads are emitted as supplied; only header/location byte
     * order is converted, as with native GG. Location words are restored on every
     * write failure. Exact writer length required; partial output remains on failure.
     * Writer callbacks cannot mutate/re-enter this table. */
    int mm_chunk_table_write(MMChunkTable *, const MMStorageWriter *, int big_endian, int system);
#ifdef __cplusplus
}
#endif
#endif
