/* Chunk index persistence and lookup converted from the original Kauai engine.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNKY_FILE_H
#define MM_ENGINE_CHUNKY_FILE_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif

    enum MMChunkResult
    {
        MM_CHUNK_OK,
        MM_CHUNK_NOT_FOUND,
        MM_CHUNK_BAD_FILE,
        MM_CHUNK_IO,
        MM_CHUNK_MEMORY
    };
    enum
    {
        MM_CHUNK_CURRENT_VERSION = 5,
        MM_CHUNK_BACKWARD_VERSION = 4,
        MM_CHUNK_PREFIX_BYTES = 128,
        MM_CHUNK_LONER = 2,
        MM_CHUNK_PACKED = 4,
        MM_CHUNK_FOREST = 16
    };
    typedef struct MMChunkHeader
    {
        MMU32 creator, logical_end, index_offset, index_bytes, map_offset, map_bytes;
        int version, backward_version, big_endian, system;
    } MMChunkHeader;
    typedef struct MMChunkEntry
    {
        MMU32 type, number, offset, bytes, flags, child_count, parent_count, variable_bytes;
    } MMChunkEntry;
    typedef struct MMChunkChild
    {
        MMU32 type, number, slot;
    } MMChunkChild;

    /* Stored payloads sit between the prefix and the index, with (0, 0) allowed
     * for empty chunks. */
    int mm_chunk_stored_extent(const MMChunkHeader *, const MMChunkEntry *);

    /* Accessors borrow live host storage. They return nonzero only after filling a
     * complete record. Query callbacks must not mutate/re-enter the index. This
     * lets editors use the same searches as immutable file indexes without a stale
     * second cache. Sorted chunk order is (type,number); children (slot,type,number).
     * Query outputs are unchanged on failure, except insertion positions on misses. */
    typedef struct MMChunkAccess
    {
        void *context;
        MMU32 count;
        int (*entry)(void *context, MMU32 position, MMChunkEntry *entry);
        int (*child)(void *context, MMU32 parent, MMU32 position, MMChunkChild *child);
    } MMChunkAccess;

    int mm_chunk_at(const MMChunkAccess *access, MMU32 position, MMChunkEntry *entry);
    int mm_chunk_keys_ordered(const MMChunkAccess *);
    int mm_chunk_find(const MMChunkAccess *access, MMU32 type, MMU32 number, MMU32 *position);
    int mm_chunk_type_at(const MMChunkAccess *access, MMU32 type, MMU32 ordinal,
                               MMChunkEntry *entry, MMU32 *position);
    int mm_chunk_child_at(const MMChunkAccess *access, MMU32 parent, MMU32 position,
                                MMChunkChild *child);
    int mm_chunk_child_compare(const MMChunkChild *, const MMChunkChild *);
    int mm_chunk_find_child(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                                  MMU32 number, MMU32 *position);
    /* First child at this slot with type >= requested type, matching retail's
     * FGetKidChid behavior. Exact-type callers must also compare returned type. */
    int mm_chunk_child_type(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                                  MMChunkChild *child);
    /* Exact slot and type, with the first child winning if the slot and type
     * are repeated. These resolve the link only. The consumer reads its target. */
    int mm_chunk_child_exact(const MMChunkAccess *, MMU32 parent, MMU32 slot, MMU32 type, MMChunkChild *);
    int mm_chunk_link_number(const MMChunkAccess *, MMU32 parent_type, MMU32 parent_number, MMU32 slot,
                             MMU32 child_type, MMU32 *number);
    /* Retail graph policy: bounded children/reference counts, aggregate reference
     * totals, cycles and reachability from zero-reference roots. Does not invent
     * a stricter per-node incoming-reference-count rule or reorder stored data. */
    int mm_chunk_validate_graph(const MMChunkAccess *access, MMU32 max_references);

    typedef struct MMChunkIndex MMChunkIndex;
    typedef struct MMChunkPolicy
    {
        int expanded_runtime, native_big_endian;
        MMU32 max_name_bytes;
    } MMChunkPolicy;
    /* Index owns copied wire bytes; input is borrowed only during open. All output
     * pointers, including index, are unchanged on failure. Policy selects existing
     * runtime width and expanded-index pre-swap compatibility checks. GG index
     * rows may omit alignment padding. Names remain encoded bytes; pre-v3 name
     * conversion stays with the legacy text host. */
    int mm_chunk_read_header(const MMReadStream *stream, MMChunkHeader *header);
    typedef struct MMChunkSave
    {
        MMU32 creator, index_offset, index_bytes, map_bytes;
        int big_endian, system;
    } MMChunkSave;
    typedef struct MMChunkWriter
    {
        void *context;
        int (*index)(void *, MMU32 offset, MMU32 bytes);
        int (*map)(void *, MMU32 offset, MMU32 bytes);
        int (*prefix)(void *, const void *bytes, size_t count);
    } MMChunkWriter;
    /* Commit the existing serialized index and optional free map, then publish a
     * zero-reserved CHN2 v5/back-v4 prefix. No truncation, flush or rollback is
     * implied: failed callbacks can leave partial body/prefix writes, as in retail.
     * Validation precedes I/O. Callbacks are exact, synchronous and may not mutate
     * or re-enter this request. Zero map_bytes omits the map callback. Optional
     * committed output is unchanged on failure. Filesystem naming/ownership stays
     * with the host; index/map serialization uses the existing collection backend. */
    int mm_chunk_write_index(const MMChunkSave *save, const MMChunkWriter *writer,
                                   MMChunkHeader *committed);
    int mm_chunk_index_open(const MMReadStream *stream, const MMChunkPolicy *policy,
                                  MMChunkIndex **index);
    /* A retained index can outlive the source stream and its original owner. */
    MMChunkIndex *mm_chunk_index_retain(const MMChunkIndex *index);
    void mm_chunk_index_close(MMChunkIndex *index);
    MMChunkAccess mm_chunk_index_access(const MMChunkIndex *index);
    const MMChunkHeader *mm_chunk_index_header(const MMChunkIndex *index);
    int mm_chunk_index_name(const MMChunkIndex *index, MMU32 position, const unsigned char **bytes,
                                  MMU32 *count);
    typedef int (*MMChunkIndexWrite)(void *, MMU32 offset, const unsigned char *bytes, MMU32 count);
    /* Rewrite chunk numbers and incoming child links using one new number per
     * old index row. The writer receives the complete replacement index once
     * after planning succeeds. Chunk bodies and the free map need no rewrite. */
    int mm_chunk_index_rekey(const MMChunkIndex *, const MMU32 *numbers, MMChunkIndexWrite, void *context);

#ifdef __cplusplus
}
#endif
#endif
