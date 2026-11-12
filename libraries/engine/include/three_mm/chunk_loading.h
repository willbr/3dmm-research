/* Chunk loading converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_LOADING_H
#define THREE_MM_ENGINE_CHUNK_LOADING_H
#include "chunk_heap.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMChunkFreeMap
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 count;
    } ThreeMMChunkFreeMap;
    /* Unique owning pointer; allocator owns both record and entry storage. No copy,
     * retain or implicit sharing. close(NULL) is allowed. Allocation/read failures
     * leave output unchanged. Reads preserve raw ranges without new validation. */
    int three_mm_free_map_create(const ThreeMMStorageAllocator *, ThreeMMU32 reserve, ThreeMMChunkFreeMap **);
    void three_mm_free_map_close(ThreeMMChunkFreeMap *);
    int three_mm_free_map_get(const ThreeMMChunkFreeMap *, ThreeMMU32, ThreeMMChunkFreeRange *);
    int three_mm_free_map_put(ThreeMMChunkFreeMap *, ThreeMMU32, const ThreeMMChunkFreeRange *);
    int three_mm_free_map_insert(ThreeMMChunkFreeMap *, ThreeMMU32, const ThreeMMChunkFreeRange *);
    int three_mm_free_map_erase(ThreeMMChunkFreeMap *, ThreeMMU32);
    int three_mm_free_map_read(const ThreeMMReadStream *, const ThreeMMStorageAllocator *, ThreeMMChunkFreeMap **);
    /* Clear unread before any I/O, including failure or an already existing map.
     * Failed lazy reads are noncritical and are not retried. Zero bytes omits I/O. */
    int three_mm_free_map_load(ThreeMMChunkFreeMap **, int *unread, const ThreeMMReadStream *, ThreeMMU32 offset,
                               ThreeMMU32 bytes, const ThreeMMStorageAllocator *);
    ThreeMMU32 three_mm_free_map_wire_size(const ThreeMMChunkFreeMap *);
    /* Header then one complete body write; omit empty body. Writer is exact-sized.
     * Current native byte order emits directly; opposite order uses a C scratch
     * buffer. Partial output remains on callback failure; ownership stays unchanged. */
    int three_mm_free_map_write(ThreeMMChunkFreeMap *, const ThreeMMStorageWriter *, int big_endian, int system);
    typedef struct ThreeMMChunkMaterializer
    {
        void *context;
        /* Input is bounded and zero-terminated by C. Host converts historical SZS
         * text into the existing String wire form, without normalizing encodings. */
        int (*legacy_name)(void *, const char *, void *wire, ThreeMMU32 capacity, ThreeMMU32 *bytes);
        int (*insert)(void *, ThreeMMU32 position, const ThreeMMChunkEntry *, const void *variable, ThreeMMU32 bytes);
    } ThreeMMChunkMaterializer;
    /* Owns reusable scratch storage. Child records are native U32 type/number/slot;
     * names retain encoded bytes. Pre-v3 names >=256 bytes are discarded. Failed
     * insertion may leave a partial destination for host disposal. Source is borrowed
     * and unchanged, scratch pointers expire after each synchronous callback. */
    int three_mm_chunk_materialize(ThreeMMChunkIndex *, const ThreeMMChunkMaterializer *,
                                   const ThreeMMStorageAllocator *);
#ifdef __cplusplus
}
#endif
#endif
