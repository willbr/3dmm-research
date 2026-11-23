/* Chunk lifecycle converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_LIFECYCLE_H
#define MM_ENGINE_CHUNK_LIFECYCLE_H
#include "chunk_metadata.h"
#include "chunk_loading.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_CHUNK_WRITE = 1,
        MM_CHUNK_TEMP = 2,
        MM_CHUNK_MARK = 4,
        MM_CHUNK_ADD_EXTRA = 8,
        MM_CHUNK_READ_EXTRA = 16
    };
    typedef struct MMChunkOptions
    {
        int marked, add_to_extra, read_from_extra, free_map_unread, invalid_main;
    } MMChunkOptions;
    typedef struct MMChunkOptionServices
    {
        void *context;
        MMU32 (*file_flags)(void *);
        int (*set_file_flags)(void *, MMU32 flags, MMU32 mask);
        void (*read_map)(void *); /* Noncritical; clears the host's unread flag. */
    } MMChunkOptionServices;
    /* Existing file flag encoding: write=1, deny-write=2, deny-read=4, temp=16.
     * Deny-write is present even in a zero option mask. */
    MMU32 mm_chunk_file_flags(MMU32 options);
    /* Failure changing physical flags leaves options unchanged. Map reads precede
     * mark/add/read latching, and never fail this operation. Those three options
     * only become true; physical file flag mutation remains the file service. */
    int mm_chunk_options_set(MMChunkOptions *, const MMChunkOptionServices *, MMU32 options,
                                   MMU32 mask);
    /* Returns -1 for invalid count, 0 to retain, 1 to destroy. Failure leaves output
     * unchanged; a marked zero-count container remains in the host registry. */
    int mm_chunk_release(MMI32 references, int marked, MMI32 *remaining);
    typedef struct MMChunkReopenState
    {
        void *main_file, *extra_file, *index;
        MMChunkFreeMap *main_map, *extra_map;
        MMU32 main_end, extra_end;
        int free_map_unread, invalid_main;
    } MMChunkReopenState;
    typedef struct MMChunkReopenServices
    {
        void *context;
        int (*writable)(void *, void *file);
        void (*retain_file)(void *, void *file);
        void (*release_file)(void *, void *file);
        void (*release_index)(void *, void *index);
        int (*read_index)(void *, MMChunkReopenState *);
        int (*validate)(void *);
        void (*publish)(void *, const MMChunkReopenState *); /* Optional mirror update. */
    } MMChunkReopenServices;
    /* State owns file/index references and unique maps on entry/return. A nullable
     * runtime owner supplies compact-index cache storage. The transaction explicitly
     * swaps that owner and retains main. An optional publication hook mirrors
     * the new state before index I/O and cleanup. The read callback fills state.
     * Failure restores the original owners and unread state, then disposes rejected
     * owners. Deferred-map offset/size fields outside this record retain the last
     * read values, as in legacy FReopen. Other container options are unchanged.
     * Callbacks must not re-enter or independently mutate these owners. */
    int mm_chunk_reopen(MMChunkReopenState *, MMChunkRuntime *, const MMChunkReopenServices *);
#ifdef __cplusplus
}
#endif
#endif
