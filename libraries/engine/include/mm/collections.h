/* Collection algorithms converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_COLLECTIONS_H
#define MM_ENGINE_COLLECTIONS_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Borrowed views over owning C storage. Publish count/free_count/bytes back to
     * the host after mutations. Mutation inputs must not alias either storage area.
     * Failed reservations preserve logical data/counts but may resize capacity.
     * trash_unused retains the legacy debug 0xa3 poisoning; release leaves unused
     * bytes and bitmap bits alone. Result codes are MMStorageResult. */
    typedef struct MMArray
    {
        MMCollectionStorage *storage;
        MMU32 width, count;
        MMI32 free_count;
        int allocated, trash_unused;
    } MMArray;
    typedef struct MMGroup
    {
        MMCollectionStorage *storage;
        MMU32 fixed_bytes, count, bytes;
        MMI32 free_count; /* -1 dense, otherwise allocated. */
        int trash_unused;
    } MMGroup;
    typedef struct MMGroupLocation
    {
        MMI32 offset, bytes;
    } MMGroupLocation;
    enum
    {
        MM_GROUP_SLOT_INVALID,
        MM_GROUP_SLOT_FREE,
        MM_GROUP_SLOT_PRESENT
    };
    int mm_array_reserve(MMArray *, MMU32 additional, int shrink);
    int mm_array_insert(MMArray *, MMU32 index, const void *, MMU32 count);
    int mm_array_delete(MMArray *, MMU32 index, MMU32 count);
    int mm_array_set_count(MMArray *, MMU32 count);
    int mm_array_move(MMArray *, MMU32 source, MMU32 target);
    int mm_array_add(MMArray *, const void *, MMU32 *index);
    int mm_array_pop(MMArray *, void *output);
    int mm_group_reserve(MMGroup *, MMU32 additional, MMU32 variable_bytes, int shrink);
    /* Read-only lookup permits a final record with no trailing alignment bytes.
     * Out-of-range indices and allocated holes return MM_GROUP_SLOT_FREE. */
    int mm_group_read_slot(const MMGroup *, MMU32 index, MMGroupLocation *);
    int mm_group_location(const MMGroup *, MMU32 index, MMGroupLocation *);
    int mm_group_insert(MMGroup *, MMU32 index, MMU32 variable_bytes, const void *,
                              const void *fixed);
    int mm_group_add(MMGroup *, MMU32 variable_bytes, const void *, const void *fixed,
                           MMU32 *index);
    int mm_group_delete(MMGroup *, MMU32 index);
    int mm_group_move(MMGroup *, MMU32 source, MMU32 target);
    int mm_group_swap(MMGroup *, MMU32 first, MMU32 second);
    int mm_group_insert_bytes(MMGroup *, MMU32 index, MMU32 offset, MMU32 bytes,
                                    const void *);
    int mm_group_delete_bytes(MMGroup *, MMU32 index, MMU32 offset, MMU32 bytes);
    int mm_group_put(MMGroup *, MMU32 index, MMU32 bytes, const void *);
    int mm_group_move_bytes(MMGroup *, MMU32 source, MMU32 source_offset, MMU32 target,
                                  MMU32 target_offset, MMU32 bytes);
    int mm_group_merge(MMGroup *, MMU32 source, MMU32 target);
    int mm_group_copy_entries(MMGroup *destination, MMGroup *source, MMU32 source_index,
                                    MMU32 target_index, MMU32 count);
    void mm_group_swap_locations(MMGroup *);

    enum
    {
        MM_COLLECTION_ARRAY,
        MM_COLLECTION_ALLOCATED_ARRAY,
        MM_COLLECTION_GROUP,
        MM_COLLECTION_STRINGS
    };
    /* Wire records are decoded values, never native structures to write to disk.
     * Layout checks products/subtractions before arithmetic. Read preserves legacy
     * header policy, including AL's negative free counts and GG's rejection of an
     * empty allocated group. It does not impose a new payload/bitmap validator. */
    typedef struct MMCollectionHeader
    {
        int kind, big_endian, system;
        MMU32 width, count, bytes, header_bytes, first_bytes, second_bytes;
        MMI32 free_count;
    } MMCollectionHeader;
    int mm_collection_layout(MMCollectionHeader *);
    int mm_collection_header_read(const MMReadStream *, int kind, int allocated_group,
                                        MMCollectionHeader *);
    int mm_collection_header_write(const MMCollectionHeader *, unsigned char *output, size_t capacity);
    /* Given a decoded GG header and the complete bytes following it, return
     * the offset of a dense row and its variable byte count. */
    int mm_collection_group_row(const MMCollectionHeader *, const unsigned char *records, MMU32 row,
                                MMU32 *offset, MMI32 *bytes);
#ifdef __cplusplus
}
#endif
#endif
