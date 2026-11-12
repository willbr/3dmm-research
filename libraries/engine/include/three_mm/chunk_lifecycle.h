/* Chunk lifecycle converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_LIFECYCLE_H
#define THREE_MM_ENGINE_CHUNK_LIFECYCLE_H
#include "chunk_metadata.h"
#include "chunk_loading.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_CHUNK_WRITE = 1,
        THREE_MM_CHUNK_TEMP = 2,
        THREE_MM_CHUNK_MARK = 4,
        THREE_MM_CHUNK_ADD_EXTRA = 8,
        THREE_MM_CHUNK_READ_EXTRA = 16
    };
    typedef struct ThreeMMChunkOptions
    {
        int marked, add_to_extra, read_from_extra, free_map_unread, invalid_main;
    } ThreeMMChunkOptions;
    typedef struct ThreeMMChunkOptionServices
    {
        void *context;
        ThreeMMU32 (*file_flags)(void *);
        int (*set_file_flags)(void *, ThreeMMU32 flags, ThreeMMU32 mask);
        void (*read_map)(void *); /* Noncritical; clears the host's unread flag. */
    } ThreeMMChunkOptionServices;
    /* Existing file flag encoding: write=1, deny-write=2, deny-read=4, temp=16.
     * Deny-write is present even in a zero option mask. */
    ThreeMMU32 three_mm_chunk_file_flags(ThreeMMU32 options);
    /* Failure changing physical flags leaves options unchanged. Map reads precede
     * mark/add/read latching, and never fail this operation. Those three options
     * only become true; physical file flag mutation remains the file service. */
    int three_mm_chunk_options_set(ThreeMMChunkOptions *, const ThreeMMChunkOptionServices *, ThreeMMU32 options,
                                   ThreeMMU32 mask);
    /* Returns -1 for invalid count, 0 to retain, 1 to destroy. Failure leaves output
     * unchanged; a marked zero-count container remains in the host registry. */
    int three_mm_chunk_release(ThreeMMI32 references, int marked, ThreeMMI32 *remaining);
    typedef struct ThreeMMChunkRegistry
    {
        void *(*next)(void *);
        int (*marked)(void *);
        ThreeMMI32 (*references)(void *);
        void (*clear_mark)(void *);
        void (*destroy)(void *);
    } ThreeMMChunkRegistry;
    int three_mm_chunk_clear_marks(void *first, const ThreeMMChunkRegistry *);
    int three_mm_chunk_close_unmarked(void *first, const ThreeMMChunkRegistry *);
    typedef struct ThreeMMChunkReopenState
    {
        void *main_file, *extra_file, *index;
        ThreeMMChunkFreeMap *main_map, *extra_map;
        ThreeMMU32 main_end, extra_end;
        int free_map_unread, invalid_main;
    } ThreeMMChunkReopenState;
    typedef struct ThreeMMChunkReopenServices
    {
        void *context;
        int (*writable)(void *, void *file);
        void (*retain_file)(void *, void *file);
        void (*release_file)(void *, void *file);
        void (*release_index)(void *, void *index);
        int (*read_index)(void *, ThreeMMChunkReopenState *);
        int (*validate)(void *);
        void (*publish)(void *, const ThreeMMChunkReopenState *);
    } ThreeMMChunkReopenServices;
    /* State owns file/index references and unique maps on entry/return. A nullable
     * runtime owner supplies compact-index cache storage. The transaction explicitly
     * swaps that owner, retains main, then publishes before index I/O and cleanup.
     * Read callback mirrors newly loaded native owners into state.
     * Failure restores the original owners and unread state, then disposes rejected
     * owners. Deferred-map offset/size fields outside this record retain the last
     * read values, as in legacy FReopen. Other container options are unchanged.
     * Callbacks must not re-enter or independently mutate these owners. */
    int three_mm_chunk_reopen(ThreeMMChunkReopenState *, ThreeMMChunkRuntime *, const ThreeMMChunkReopenServices *);
#ifdef __cplusplus
}
#endif
#endif
