/* Chunk metadata converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_METADATA_H
#define THREE_MM_ENGINE_CHUNK_METADATA_H
#include "chunky_file.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkRuntimeEntry
    {
        ThreeMMU32 type, number;
        ThreeMMI32 runtime;
    } ThreeMMChunkRuntimeEntry;
    typedef struct ThreeMMChunkRuntime
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 count;
        int created;
    } ThreeMMChunkRuntime;
    /* Owning sorted cache. Zero removes/does not allocate; failed insertions retain
     * logical entries but may grow capacity. Empty allocated storage is retained.
     * swap explicitly transfers ownership (e.g. failed file reopen). Do not copy. */
    int three_mm_chunk_runtime_init(ThreeMMChunkRuntime *, const ThreeMMStorageAllocator *);
    int three_mm_chunk_runtime_dispose(ThreeMMChunkRuntime *);
    int three_mm_chunk_runtime_swap(ThreeMMChunkRuntime *, ThreeMMChunkRuntime *);
    ThreeMMI32 three_mm_chunk_runtime_get(const ThreeMMChunkRuntime *, ThreeMMU32 type, ThreeMMU32 number);
    int three_mm_chunk_runtime_set(ThreeMMChunkRuntime *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 runtime);
    int three_mm_chunk_runtime_at(const ThreeMMChunkRuntime *, ThreeMMU32 position, ThreeMMChunkRuntimeEntry *);
    typedef struct ThreeMMChunkNames
    {
        void *context;
        ThreeMMU32 (*count)(void *);
        int (*entry)(void *, ThreeMMU32 position, ThreeMMChunkEntry *);
        int (*insert)(void *, ThreeMMU32 position, ThreeMMU32 offset, ThreeMMU32 bytes);
        void (*erase)(void *, ThreeMMU32 position, ThreeMMU32 offset, ThreeMMU32 bytes);
        void *(*pin)(void *, ThreeMMU32 position);
        void (*unpin)(void *);
        ThreeMMU32 max_bytes;
    } ThreeMMChunkNames;
    typedef struct ThreeMMChunkNameValue
    {
        const void *wire;
        ThreeMMU32 bytes, characters;
    } ThreeMMChunkNameValue;
    typedef struct ThreeMMChunkNameDecoder
    {
        void *context;
        int (*decode)(void *, const void *wire, ThreeMMU32 bytes);
        void (*clear)(void *);
        ThreeMMU32 (*characters)(void *);
    } ThreeMMChunkNameDecoder;
    /* Native primitives use existing C89 group storage. Input wire must not alias
     * the group. Name bytes follow 12-byte child records; shrinking precedes writing,
     * growth failure retains the old name. Pin covers the complete copy/decode.
     * A null/empty value removes a name. get returns 1 only for a nonempty result;
     * absent/invalid names clear the output. Decoder diagnostics remain host policy. */
    int three_mm_chunk_name_set(const ThreeMMChunkNames *, ThreeMMU32 position, const ThreeMMChunkNameValue *);
    int three_mm_chunk_name_get(const ThreeMMChunkNames *, ThreeMMU32 position, const ThreeMMChunkNameDecoder *);
#ifdef __cplusplus
}
#endif
#endif
