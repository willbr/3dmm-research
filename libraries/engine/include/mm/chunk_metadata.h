/* Chunk metadata converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_METADATA_H
#define MM_ENGINE_CHUNK_METADATA_H
#include "chunky_file.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkRuntimeEntry
    {
        MMU32 type, number;
        MMI32 runtime;
    } MMChunkRuntimeEntry;
    typedef struct MMChunkRuntime
    {
        MMCollectionStorage storage;
        MMU32 count;
        int created;
    } MMChunkRuntime;
    /* Owning sorted cache. Zero removes/does not allocate; failed insertions retain
     * logical entries but may grow capacity. Empty allocated storage is retained.
     * swap explicitly transfers ownership (e.g. failed file reopen). Do not copy. */
    int mm_chunk_runtime_init(MMChunkRuntime *, const MMStorageAllocator *);
    int mm_chunk_runtime_dispose(MMChunkRuntime *);
    int mm_chunk_runtime_swap(MMChunkRuntime *, MMChunkRuntime *);
    MMI32 mm_chunk_runtime_get(const MMChunkRuntime *, MMU32 type, MMU32 number);
    int mm_chunk_runtime_set(MMChunkRuntime *, MMU32 type, MMU32 number, MMI32 runtime);
    int mm_chunk_runtime_at(const MMChunkRuntime *, MMU32 position, MMChunkRuntimeEntry *);
    typedef struct MMChunkNames
    {
        void *context;
        MMU32 (*count)(void *);
        int (*entry)(void *, MMU32 position, MMChunkEntry *);
        int (*insert)(void *, MMU32 position, MMU32 offset, MMU32 bytes);
        void (*erase)(void *, MMU32 position, MMU32 offset, MMU32 bytes);
        void *(*pin)(void *, MMU32 position);
        void (*unpin)(void *);
        MMU32 max_bytes;
    } MMChunkNames;
    typedef struct MMChunkNameValue
    {
        const void *wire;
        MMU32 bytes, characters;
    } MMChunkNameValue;
    typedef struct MMChunkNameDecoder
    {
        void *context;
        int (*decode)(void *, const void *wire, MMU32 bytes);
        void (*clear)(void *);
        MMU32 (*characters)(void *);
    } MMChunkNameDecoder;
    /* Native primitives use existing C89 group storage. Input wire must not alias
     * the group. Name bytes follow 12-byte child records; shrinking precedes writing,
     * growth failure retains the old name. Pin covers the complete copy/decode.
     * A null/empty value removes a name. get returns 1 only for a nonempty result;
     * absent/invalid names clear the output. Decoder diagnostics remain host policy. */
    int mm_chunk_name_set(const MMChunkNames *, MMU32 position, const MMChunkNameValue *);
    int mm_chunk_name_get(const MMChunkNames *, MMU32 position, const MMChunkNameDecoder *);
#ifdef __cplusplus
}
#endif
#endif
