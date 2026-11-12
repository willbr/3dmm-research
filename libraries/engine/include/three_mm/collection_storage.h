/* Collection storage converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_COLLECTION_STORAGE_H
#define THREE_MM_ENGINE_COLLECTION_STORAGE_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ThreeMMStorageResult
    {
        THREE_MM_STORAGE_OK,
        THREE_MM_STORAGE_ARGUMENT,
        THREE_MM_STORAGE_MEMORY,
        THREE_MM_STORAGE_LOCKED,
        THREE_MM_STORAGE_IO
    };
    enum
    {
        THREE_MM_STORAGE_SPEED = 1,
        THREE_MM_STORAGE_NORMAL = 2,
        THREE_MM_STORAGE_LIMIT = 0x08000000
    };
    /* resize(NULL,0,n) allocates. A failed resize returns NULL and preserves the
     * old pointer, bytes and allocation size. New storage has malloc alignment.
     * release is called only for non-NULL blocks. Context lifetime is borrowed.
     * Callbacks must not re-enter the same storage. NULL allocator uses the C heap. */
    typedef struct ThreeMMStorageAllocator
    {
        void *context;
        void *(*resize)(void *, void *, ThreeMMU32 old_bytes, ThreeMMU32 new_bytes, int priority);
        void (*release)(void *, void *, ThreeMMU32 bytes);
    } ThreeMMStorageAllocator;
    typedef struct ThreeMMStorageBuffer
    {
        unsigned char *data;
        ThreeMMU32 capacity, locks;
    } ThreeMMStorageBuffer;
    /* Owns both buffers. Initialize before use; do not copy this owning record.
     * Direct pointers are borrowed until the next resizing operation. */
    typedef struct ThreeMMCollectionStorage
    {
        ThreeMMStorageAllocator allocator;
        ThreeMMStorageBuffer buffers[2];
        ThreeMMU32 min_grow[2];
    } ThreeMMCollectionStorage;
    int three_mm_storage_init(ThreeMMCollectionStorage *, const ThreeMMStorageAllocator *);
    int three_mm_storage_dispose(ThreeMMCollectionStorage *);
    /* Shrink both sections first, then grow in order. Speculative growth uses SPEED
     * before exact NORMAL allocation. Failure growing the second section retains
     * successful changes to the first. A failed shrink is best effort: retains its
     * actual capacity/data and does not fail the operation. Locked resizes fail. */
    int three_mm_storage_ensure(ThreeMMCollectionStorage *, ThreeMMU32 first, ThreeMMU32 second, int shrink);
    int three_mm_storage_resize_exact(ThreeMMCollectionStorage *, unsigned section, ThreeMMU32 bytes, int priority);
    int three_mm_storage_lock(ThreeMMCollectionStorage *, unsigned section);
    int three_mm_storage_unlock(ThreeMMCollectionStorage *, unsigned section);
    /* Independent copies use only the requested prefixes and inherit growth hints
     * after success. A failed copy can grow/shrink destination buffers, but copies
     * no payload bytes and leaves its growth hints unchanged. */
    int three_mm_storage_copy(ThreeMMCollectionStorage *destination, const ThreeMMCollectionStorage *source,
                              ThreeMMU32 first, ThreeMMU32 second);
    typedef struct ThreeMMStorageWriter
    {
        void *context;
        ThreeMMU32 length;
        int (*write_at)(void *, ThreeMMU32 offset, const void *, size_t bytes);
    } ThreeMMStorageWriter;
    /* DataBlock wire ordering: header, first prefix, second prefix. Exact length
     * required for writing. Reads/writes lock only the active section, release that
     * lock on callback failure, and retain completed transfers on I/O failure.
     * Host callbacks provide unpacked I/O and must not mutate the target storage. */
    int three_mm_storage_write(ThreeMMCollectionStorage *, const ThreeMMStorageWriter *, const void *header,
                               ThreeMMU32 header_bytes, ThreeMMU32 first, ThreeMMU32 second);
    int three_mm_storage_read(ThreeMMCollectionStorage *, const ThreeMMReadStream *, ThreeMMU32 offset,
                              ThreeMMU32 first, ThreeMMU32 second);
#ifdef __cplusplus
}
#endif
#endif
