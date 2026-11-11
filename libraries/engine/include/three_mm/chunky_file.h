/* Chunk index reading and lookup converted from the original Kauai engine.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNKY_FILE_H
#define THREE_MM_ENGINE_CHUNKY_FILE_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif

    enum ThreeMMChunkResult
    {
        THREE_MM_CHUNK_OK,
        THREE_MM_CHUNK_NOT_FOUND,
        THREE_MM_CHUNK_BAD_FILE,
        THREE_MM_CHUNK_IO,
        THREE_MM_CHUNK_MEMORY
    };
    enum
    {
        THREE_MM_CHUNK_CURRENT_VERSION = 5,
        THREE_MM_CHUNK_BACKWARD_VERSION = 4,
        THREE_MM_CHUNK_PREFIX_BYTES = 128,
        THREE_MM_CHUNK_LONER = 2,
        THREE_MM_CHUNK_PACKED = 4,
        THREE_MM_CHUNK_FOREST = 16
    };
    typedef struct ThreeMMChunkHeader
    {
        ThreeMMU32 creator, logical_end, index_offset, index_bytes, map_offset, map_bytes;
        int version, backward_version, big_endian, system;
    } ThreeMMChunkHeader;
    typedef struct ThreeMMChunkEntry
    {
        ThreeMMU32 type, number, offset, bytes, flags, child_count, parent_count, variable_bytes;
    } ThreeMMChunkEntry;
    typedef struct ThreeMMChunkChild
    {
        ThreeMMU32 type, number, slot;
    } ThreeMMChunkChild;

    /* Accessors borrow live host storage. They return nonzero only after filling a
     * complete record. Query callbacks must not mutate/re-enter the index. This
     * lets editors use the same searches as immutable file indexes without a stale
     * second cache. Sorted chunk order is (type,number); children (slot,type,number).
     * Query outputs are unchanged on failure, except insertion positions on misses. */
    typedef struct ThreeMMChunkAccess
    {
        void *context;
        ThreeMMU32 count;
        int (*entry)(void *context, ThreeMMU32 position, ThreeMMChunkEntry *entry);
        int (*child)(void *context, ThreeMMU32 parent, ThreeMMU32 position, ThreeMMChunkChild *child);
    } ThreeMMChunkAccess;

    int three_mm_chunk_at(const ThreeMMChunkAccess *access, ThreeMMU32 position, ThreeMMChunkEntry *entry);
    int three_mm_chunk_find(const ThreeMMChunkAccess *access, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *position);
    int three_mm_chunk_type_at(const ThreeMMChunkAccess *access, ThreeMMU32 type, ThreeMMU32 ordinal,
                               ThreeMMChunkEntry *entry, ThreeMMU32 *position);
    int three_mm_chunk_child_at(const ThreeMMChunkAccess *access, ThreeMMU32 parent, ThreeMMU32 position,
                                ThreeMMChunkChild *child);
    int three_mm_chunk_find_child(const ThreeMMChunkAccess *access, ThreeMMU32 parent, ThreeMMU32 slot, ThreeMMU32 type,
                                  ThreeMMU32 number, ThreeMMU32 *position);
    /* First child at this slot with type >= requested type, matching retail's
     * FGetKidChid behavior. Exact-type callers must also compare returned type. */
    int three_mm_chunk_child_type(const ThreeMMChunkAccess *access, ThreeMMU32 parent, ThreeMMU32 slot, ThreeMMU32 type,
                                  ThreeMMChunkChild *child);
    /* Retail graph policy: bounded children/reference counts, aggregate reference
     * totals, cycles and reachability from zero-reference roots. Does not invent
     * a stricter per-node incoming-reference-count rule or reorder stored data. */
    int three_mm_chunk_validate_graph(const ThreeMMChunkAccess *access, ThreeMMU32 max_references);

    typedef struct ThreeMMChunkIndex ThreeMMChunkIndex;
    typedef struct ThreeMMChunkPolicy
    {
        int expanded_runtime, native_big_endian;
        ThreeMMU32 max_name_bytes;
    } ThreeMMChunkPolicy;
    /* Index owns copied wire bytes; input is borrowed only during open. All output
     * pointers, including index, are unchanged on failure. Policy selects existing
     * runtime width and expanded-index pre-swap compatibility checks. Names remain
     * encoded bytes; pre-v3 name conversion stays with the legacy text host. */
    int three_mm_chunk_read_header(const ThreeMMReadStream *stream, ThreeMMChunkHeader *header);
    int three_mm_chunk_index_open(const ThreeMMReadStream *stream, const ThreeMMChunkPolicy *policy,
                                  ThreeMMChunkIndex **index);
    void three_mm_chunk_index_close(ThreeMMChunkIndex *index);
    ThreeMMChunkAccess three_mm_chunk_index_access(ThreeMMChunkIndex *index);
    const ThreeMMChunkHeader *three_mm_chunk_index_header(const ThreeMMChunkIndex *index);
    int three_mm_chunk_index_name(const ThreeMMChunkIndex *index, ThreeMMU32 position, const unsigned char **bytes,
                                  ThreeMMU32 *count);

#ifdef __cplusplus
}
#endif
#endif
