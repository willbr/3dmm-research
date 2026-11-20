/* Chunk loading converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_LOADING_H
#define MM_ENGINE_CHUNK_LOADING_H
#include "chunk_heap.h"
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMChunkFreeMap
    {
        MMCollectionStorage storage;
        MMU32 count;
    } MMChunkFreeMap;
    /* Unique owning pointer; allocator owns both record and entry storage. No copy,
     * retain or implicit sharing. close(NULL) is allowed. Allocation/read failures
     * leave output unchanged. Reads preserve raw ranges without new validation. */
    int mm_free_map_create(const MMStorageAllocator *, MMU32 reserve, MMChunkFreeMap **);
    void mm_free_map_close(MMChunkFreeMap *);
    int mm_free_map_get(const MMChunkFreeMap *, MMU32, MMChunkFreeRange *);
    int mm_free_map_put(MMChunkFreeMap *, MMU32, const MMChunkFreeRange *);
    int mm_free_map_insert(MMChunkFreeMap *, MMU32, const MMChunkFreeRange *);
    int mm_free_map_erase(MMChunkFreeMap *, MMU32);
    int mm_free_map_read(const MMReadStream *, const MMStorageAllocator *, MMChunkFreeMap **);
    /* Clear unread before any I/O, including failure or an already existing map.
     * Failed lazy reads are noncritical and are not retried. Zero bytes omits I/O. */
    int mm_free_map_load(MMChunkFreeMap **, int *unread, const MMReadStream *, MMU32 offset,
                               MMU32 bytes, const MMStorageAllocator *);
    MMU32 mm_free_map_wire_size(const MMChunkFreeMap *);
    /* Header then one complete body write; omit empty body. Writer is exact-sized.
     * Current native byte order emits directly; opposite order uses a C scratch
     * buffer. Partial output remains on callback failure; ownership stays unchanged. */
    int mm_free_map_write(MMChunkFreeMap *, const MMStorageWriter *, int big_endian, int system);
    typedef int (*MMChunkLegacyName)(void *, const char *, void *wire, MMU32 capacity, MMU32 *bytes);
    enum { MM_CHUNK_NAME_WIRE_BYTES = 516 };
    /* Names borrow index bytes or the caller's scratch for pre-v3 conversion. */
    int mm_chunk_serialized_name(const MMChunkIndex *, MMU32 position, MMChunkLegacyName, void *context,
                                  unsigned char scratch[MM_CHUNK_NAME_WIRE_BYTES],
                                  const unsigned char **bytes, MMU32 *count);
    typedef struct MMChunkMaterializer
    {
        void *context;
        /* Input is bounded and zero-terminated by C. Host converts historical SZS
         * text into the existing String wire form, without normalizing encodings. */
        MMChunkLegacyName legacy_name;
        int (*insert)(void *, MMU32 position, const MMChunkEntry *, const void *variable, MMU32 bytes);
    } MMChunkMaterializer;
    /* Owns reusable scratch storage. Child records are native U32 type/number/slot;
     * names retain encoded bytes. Pre-v3 names >=256 bytes are discarded. Failed
     * insertion may leave a partial destination for host disposal. Source is borrowed
     * and unchanged, scratch pointers expire after each synchronous callback. */
    int mm_chunk_materialize(MMChunkIndex *, const MMChunkMaterializer *,
                                   const MMStorageAllocator *);
#ifdef __cplusplus
}
#endif
#endif
