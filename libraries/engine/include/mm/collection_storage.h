/* Collection storage converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_COLLECTION_STORAGE_H
#define MM_ENGINE_COLLECTION_STORAGE_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMStorageResult
    {
        MM_STORAGE_OK,
        MM_STORAGE_ARGUMENT,
        MM_STORAGE_MEMORY,
        MM_STORAGE_LOCKED,
        MM_STORAGE_IO
    };
    enum
    {
        MM_STORAGE_SPEED = 1,
        MM_STORAGE_NORMAL = 2,
        MM_STORAGE_LIMIT = 0x08000000
    };
    /* resize(NULL,0,n) allocates. A failed resize returns NULL and preserves the
     * old pointer, bytes and allocation size. New storage has malloc alignment.
     * release is called only for non-NULL blocks. Context lifetime is borrowed.
     * Callbacks must not re-enter the same storage. NULL allocator uses the C heap. */
    typedef struct MMStorageAllocator
    {
        void *context;
        void *(*resize)(void *, void *, MMU32 old_bytes, MMU32 new_bytes, int priority);
        void (*release)(void *, void *, MMU32 bytes);
    } MMStorageAllocator;
    typedef struct MMStorageBuffer
    {
        unsigned char *data;
        MMU32 capacity, locks;
    } MMStorageBuffer;
    /* Owns both buffers. Initialize before use; do not copy this owning record.
     * Direct pointers are borrowed until the next resizing operation. */
    typedef struct MMCollectionStorage
    {
        MMStorageAllocator allocator;
        MMStorageBuffer buffers[2];
        MMU32 min_grow[2];
    } MMCollectionStorage;
    int mm_storage_init(MMCollectionStorage *, const MMStorageAllocator *);
    int mm_storage_dispose(MMCollectionStorage *);
    /* Shrink both sections first, then grow in order. Speculative growth uses SPEED
     * before exact NORMAL allocation. Failure growing the second section retains
     * successful changes to the first. A failed shrink is best effort: retains its
     * actual capacity/data and does not fail the operation. Locked resizes fail. */
    int mm_storage_ensure(MMCollectionStorage *, MMU32 first, MMU32 second, int shrink);
    int mm_storage_resize_exact(MMCollectionStorage *, unsigned section, MMU32 bytes, int priority);
    int mm_storage_lock(MMCollectionStorage *, unsigned section);
    int mm_storage_unlock(MMCollectionStorage *, unsigned section);
    /* Independent copies use only the requested prefixes and inherit growth hints
     * after success. A failed copy can grow/shrink destination buffers, but copies
     * no payload bytes and leaves its growth hints unchanged. */
    int mm_storage_copy(MMCollectionStorage *destination, const MMCollectionStorage *source,
                              MMU32 first, MMU32 second);
    typedef struct MMStorageWriter
    {
        void *context;
        MMU32 length;
        int (*write_at)(void *, MMU32 offset, const void *, size_t bytes);
    } MMStorageWriter;
    /* DataBlock wire ordering: header, first prefix, second prefix. Exact length
     * required for writing. Reads/writes lock only the active section, release that
     * lock on callback failure, and retain completed transfers on I/O failure.
     * Host callbacks provide unpacked I/O and must not mutate the target storage. */
    int mm_storage_write(MMCollectionStorage *, const MMStorageWriter *, const void *header,
                               MMU32 header_bytes, MMU32 first, MMU32 second);
    int mm_storage_read(MMCollectionStorage *, const MMReadStream *, MMU32 offset,
                              MMU32 first, MMU32 second);
#ifdef __cplusplus
}
#endif
#endif
