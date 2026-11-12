/* Collection algorithms converted from Kauai groups.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_COLLECTIONS_H
#define THREE_MM_ENGINE_COLLECTIONS_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Borrowed views over owning C storage. Publish count/free_count/bytes back to
     * the host after mutations. Mutation inputs must not alias either storage area.
     * Failed reservations preserve logical data/counts but may resize capacity.
     * trash_unused retains the legacy debug 0xa3 poisoning; release leaves unused
     * bytes and bitmap bits alone. Result codes are ThreeMMStorageResult. */
    typedef struct ThreeMMArray
    {
        ThreeMMCollectionStorage *storage;
        ThreeMMU32 width, count;
        ThreeMMI32 free_count;
        int allocated, trash_unused;
    } ThreeMMArray;
    typedef struct ThreeMMGroup
    {
        ThreeMMCollectionStorage *storage;
        ThreeMMU32 fixed_bytes, count, bytes;
        ThreeMMI32 free_count; /* -1 dense, otherwise allocated. */
        int trash_unused;
    } ThreeMMGroup;
    typedef struct ThreeMMGroupLocation
    {
        ThreeMMI32 offset, bytes;
    } ThreeMMGroupLocation;
    int three_mm_array_reserve(ThreeMMArray *, ThreeMMU32 additional, int shrink);
    int three_mm_array_insert(ThreeMMArray *, ThreeMMU32 index, const void *, ThreeMMU32 count);
    int three_mm_array_delete(ThreeMMArray *, ThreeMMU32 index, ThreeMMU32 count);
    int three_mm_array_set_count(ThreeMMArray *, ThreeMMU32 count);
    int three_mm_array_move(ThreeMMArray *, ThreeMMU32 source, ThreeMMU32 target);
    int three_mm_array_add(ThreeMMArray *, const void *, ThreeMMU32 *index);
    int three_mm_array_pop(ThreeMMArray *, void *output);
    int three_mm_group_reserve(ThreeMMGroup *, ThreeMMU32 additional, ThreeMMU32 variable_bytes, int shrink);
    int three_mm_group_location(const ThreeMMGroup *, ThreeMMU32 index, ThreeMMGroupLocation *);
    int three_mm_group_insert(ThreeMMGroup *, ThreeMMU32 index, ThreeMMU32 variable_bytes, const void *,
                              const void *fixed);
    int three_mm_group_add(ThreeMMGroup *, ThreeMMU32 variable_bytes, const void *, const void *fixed,
                           ThreeMMU32 *index);
    int three_mm_group_delete(ThreeMMGroup *, ThreeMMU32 index);
    int three_mm_group_move(ThreeMMGroup *, ThreeMMU32 source, ThreeMMU32 target);
    int three_mm_group_swap(ThreeMMGroup *, ThreeMMU32 first, ThreeMMU32 second);
    int three_mm_group_insert_bytes(ThreeMMGroup *, ThreeMMU32 index, ThreeMMU32 offset, ThreeMMU32 bytes,
                                    const void *);
    int three_mm_group_delete_bytes(ThreeMMGroup *, ThreeMMU32 index, ThreeMMU32 offset, ThreeMMU32 bytes);
    int three_mm_group_put(ThreeMMGroup *, ThreeMMU32 index, ThreeMMU32 bytes, const void *);
    int three_mm_group_move_bytes(ThreeMMGroup *, ThreeMMU32 source, ThreeMMU32 source_offset, ThreeMMU32 target,
                                  ThreeMMU32 target_offset, ThreeMMU32 bytes);
    int three_mm_group_merge(ThreeMMGroup *, ThreeMMU32 source, ThreeMMU32 target);
    int three_mm_group_copy_entries(ThreeMMGroup *destination, ThreeMMGroup *source, ThreeMMU32 source_index,
                                    ThreeMMU32 target_index, ThreeMMU32 count);
    void three_mm_group_swap_locations(ThreeMMGroup *);

    enum
    {
        THREE_MM_COLLECTION_ARRAY,
        THREE_MM_COLLECTION_ALLOCATED_ARRAY,
        THREE_MM_COLLECTION_GROUP,
        THREE_MM_COLLECTION_STRINGS
    };
    /* Wire records are decoded values, never native structures to write to disk.
     * Layout checks products/subtractions before arithmetic. Read preserves legacy
     * header policy, including AL's negative free counts and GG's rejection of an
     * empty allocated group. It does not impose a new payload/bitmap validator. */
    typedef struct ThreeMMCollectionHeader
    {
        int kind, big_endian, system;
        ThreeMMU32 width, count, bytes, header_bytes, first_bytes, second_bytes;
        ThreeMMI32 free_count;
    } ThreeMMCollectionHeader;
    int three_mm_collection_layout(ThreeMMCollectionHeader *);
    int three_mm_collection_header_read(const ThreeMMReadStream *, int kind, int allocated_group,
                                        ThreeMMCollectionHeader *);
    int three_mm_collection_header_write(const ThreeMMCollectionHeader *, unsigned char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
