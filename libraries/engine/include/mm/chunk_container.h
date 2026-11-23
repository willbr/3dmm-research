/* Container construction converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNK_CONTAINER_H
#define MM_ENGINE_CHUNK_CONTAINER_H
#include "chunk_lifecycle.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_CONTAINER_OPEN_FAILED,
        MM_CONTAINER_CREATE_FAILED,
        MM_CONTAINER_ALREADY_OPEN,
        MM_CONTAINER_INITIAL_OPTIONS
    };
    typedef struct MMChunkConstruction
    {
        void *context;
        void *(*find)(void *);
        void *(*allocate)(void *);
        void (*retain)(void *, void *owner);
        void (*release)(void *, void *owner);
        void *(*file)(void *, int create, MMU32 flags);
        void *(*index)(void *);
        void (*attach_file)(void *, void *owner, void *file);
        void (*attach_index)(void *, void *owner, void *index);
        void (*set_end)(void *, void *owner, MMU32 end);
        int (*resize)(void *, void *file, MMU32 end);
        int (*read_index)(void *, void *owner);
        int (*validate)(void *, void *owner);
        int (*options)(void *, void *owner, MMU32 flags);
        void (*issue)(void *, int kind);
        void (*finish)(void *, void *owner, int create); /* Optional new-owner diagnostics. */
    } MMChunkConstruction;
    /* find returns a borrowed owner. allocate returns a new reference. Attach
     * callbacks consume file/index references, even for partial construction.
     * release destroys a failed new owner and all attached resources. Successful
     * existing opens apply options before retaining. Creation rejects existing
     * owners and forces write-enable, then creates index/file and reserves 128 bytes.
     * A newly created/opened owner's initial option failure is diagnostic only, as
     * in legacy release builds; an existing owner's option failure returns NULL
     * without an open-error diagnostic. Callbacks are synchronous and may not re-enter. */
    void *mm_chunk_construct(const MMChunkConstruction *, MMU32 flags, int create);
    typedef struct MMForestConstruction
    {
        void *context;
        void *(*allocate)(void *);
        void (*release)(void *, void *owner);
        void *(*index)(void *);
        void (*attach_index)(void *, void *owner, void *index);
        void *(*temporary)(void *);
        void (*retain_file)(void *, void *file);
        void (*attach_file)(void *, void *owner, void *file);
        int (*resize)(void *, void *file, MMU32 end);
        void (*destination)(void *, void *owner, MMU32 end, int borrowed);
        int (*read)(void *, void *owner, void *source, MMU32 offset, MMU32 bytes, int copy);
        void (*finish)(void *, void *owner); /* Optional successful-owner diagnostics. */
    } MMForestConstruction;
    /* Attach callbacks consume references, including partial construction. Copy
     * imports create a temporary file and reserve its 128-byte prefix; borrowed
     * imports retain source, publish offset+bytes as end, and mark invalid-main /
     * add-to-extra before reading. Failure releases the owner and all attachments.
     * read may leave partial resources; release consumes those too. Input source
     * is borrowed. Calls are synchronous and cannot re-enter this construction. */
    void *mm_chunk_construct_forest(const MMForestConstruction *, void *source, MMU32 offset,
                                          MMU32 bytes, int copy);
    typedef struct MMChunkDisposal
    {
        void *context;
        void (*release_file)(void *, void *);
        void (*release_index)(void *, void *);
        void (*publish)(void *, const MMChunkReopenState *);
    } MMChunkDisposal;
    /* Consume all owners, including partially constructed/empty states. Publish a
     * cleared state first, then release main file/map, extra file/map, index and
     * runtime cache in legacy order. A nullable runtime supports expanded indexes.
     * Callbacks cannot re-enter; maps/caches must be unlocked. Repeated disposal of
     * the cleared state is safe. Native object allocation/destruction remains a host
     * primitive; this routine owns its associated-resource disposal sequencing. */
    int mm_chunk_dispose(MMChunkReopenState *, MMChunkRuntime *, const MMChunkDisposal *);
#ifdef __cplusplus
}
#endif
#endif
