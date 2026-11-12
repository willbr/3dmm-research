/* Chunk index storage converted from Kauai chunk.cpp and groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_TABLE_H
#define THREE_MM_ENGINE_CHUNK_TABLE_H
#include "collections.h"
#include "chunky_file.h"
#if USHRT_MAX != 0xffffU
#error Compact chunk records require sixteen-bit unsigned short
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkRecordKey
    {
        ThreeMMU32 ctg, cno;
    } ThreeMMChunkRecordKey;
    typedef struct ThreeMMChunkRecordSmall
    {
        ThreeMMChunkRecordKey cki;
        ThreeMMI32 fp;
        ThreeMMU32 luGrfcrpCb;
        unsigned short ckid, ccrpRef;
    } ThreeMMChunkRecordSmall;
    typedef struct ThreeMMChunkRecordBig
    {
        ThreeMMChunkRecordKey cki;
        ThreeMMI32 fp, cb, ckid, ccrpRef, rti;
        ThreeMMU32 grfcrp;
    } ThreeMMChunkRecordBig;
    typedef char ThreeMMChunkSmallSize[(sizeof(ThreeMMChunkRecordSmall) == 20) ? 1 : -1];
    typedef char ThreeMMChunkBigSize[(sizeof(ThreeMMChunkRecordBig) == 32) ? 1 : -1];
    /* Trusted native-order fixed records. Compact sizes/flags and counts retain
     * their original 24/8/16-bit storage behavior. Full assignment preserves expanded
     * runtime IDs; masked flag updates preserve sizes and unrelated flag bits. */
    ThreeMMU32 three_mm_chunk_record_flags(const void *, int expanded, ThreeMMU32 mask);
    ThreeMMI32 three_mm_chunk_record_bytes(const void *, int expanded);
    ThreeMMI32 three_mm_chunk_record_name_bytes(const void *, int expanded, ThreeMMI32 variable);
    void three_mm_chunk_record_set_bytes(void *, int expanded, ThreeMMI32 bytes);
    void three_mm_chunk_record_set_flags(void *, int expanded, ThreeMMU32 flags, ThreeMMU32 mask);
    void three_mm_chunk_record_get(const void *, int expanded, ThreeMMU32 variable, ThreeMMChunkEntry *);
    void three_mm_chunk_record_set(void *, int expanded, const ThreeMMChunkEntry *);
    typedef struct ThreeMMChunkTable
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 fixed_bytes, count, bytes;
        int trash_unused;
    } ThreeMMChunkTable;
    /* Unique allocator-owned record and two buffers; never copy/move it. Outputs are
     * unchanged on failed create. Fixed records and variable child/name bytes use
     * native order. Borrowed pointers expire on resizing/mutation; insertion inputs
     * cannot alias this table. Locks pin the payload buffer, matching legacy GG.
     * Failed mutations preserve logical entries but may change storage capacity. */
    int three_mm_chunk_table_create(const ThreeMMStorageAllocator *, ThreeMMU32 width, ThreeMMU32 reserve,
                                    int trash_unused, ThreeMMChunkTable **);
    int three_mm_chunk_table_close(ThreeMMChunkTable *);
    void *three_mm_chunk_table_fixed(ThreeMMChunkTable *, ThreeMMU32, ThreeMMU32 *variable);
    void *three_mm_chunk_table_variable(ThreeMMChunkTable *, ThreeMMU32, ThreeMMU32 *bytes);
    ThreeMMU32 three_mm_chunk_table_variable_bytes(ThreeMMChunkTable *, ThreeMMU32);
    int three_mm_chunk_table_read(ThreeMMChunkTable *, ThreeMMU32 index, ThreeMMU32 offset, ThreeMMU32 bytes, void *);
    int three_mm_chunk_table_put(ThreeMMChunkTable *, ThreeMMU32 index, ThreeMMU32 offset, ThreeMMU32 bytes,
                                 const void *);
    int three_mm_chunk_table_insert(ThreeMMChunkTable *, ThreeMMU32 index, ThreeMMU32 bytes, const void *variable,
                                    const void *fixed);
    int three_mm_chunk_table_delete(ThreeMMChunkTable *, ThreeMMU32);
    int three_mm_chunk_table_move(ThreeMMChunkTable *, ThreeMMU32 source, ThreeMMU32 target);
    int three_mm_chunk_table_insert_bytes(ThreeMMChunkTable *, ThreeMMU32, ThreeMMU32 offset, ThreeMMU32 bytes,
                                          const void *);
    int three_mm_chunk_table_delete_bytes(ThreeMMChunkTable *, ThreeMMU32, ThreeMMU32 offset, ThreeMMU32 bytes);
    int three_mm_chunk_table_move_bytes(ThreeMMChunkTable *, ThreeMMU32 source, ThreeMMU32 source_offset,
                                        ThreeMMU32 target, ThreeMMU32 target_offset, ThreeMMU32 bytes);
    int three_mm_chunk_table_lock(ThreeMMChunkTable *);
    int three_mm_chunk_table_unlock(ThreeMMChunkTable *);
    int three_mm_chunk_table_entry(ThreeMMChunkTable *, ThreeMMU32, ThreeMMChunkEntry *);
    int three_mm_chunk_table_child(ThreeMMChunkTable *, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *);
    ThreeMMChunkAccess three_mm_chunk_table_access(ThreeMMChunkTable *);
    int three_mm_chunk_table_clear_flags(ThreeMMChunkTable *, ThreeMMU32 mask);
    ThreeMMU32 three_mm_chunk_table_wire_size(const ThreeMMChunkTable *);
    /* Fixed/variable payloads are emitted as supplied; only header/location byte
     * order is converted, as with native GG. Location words are restored on every
     * write failure. Exact writer length required; partial output remains on failure.
     * Writer callbacks cannot mutate/re-enter this table. */
    int three_mm_chunk_table_write(ThreeMMChunkTable *, const ThreeMMStorageWriter *, int big_endian, int system);
#ifdef __cplusplus
}
#endif
#endif
