/* Container construction converted from Kauai chunk.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_CHUNK_CONTAINER_H
#define THREE_MM_ENGINE_CHUNK_CONTAINER_H
#include "chunk_lifecycle.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_CONTAINER_OPEN_FAILED,
        THREE_MM_CONTAINER_CREATE_FAILED,
        THREE_MM_CONTAINER_ALREADY_OPEN,
        THREE_MM_CONTAINER_INITIAL_OPTIONS
    };
    typedef struct ThreeMMChunkConstruction
    {
        void *context;
        void *(*find)(void *);
        void *(*allocate)(void *);
        void (*retain)(void *, void *owner);
        void (*release)(void *, void *owner);
        void *(*file)(void *, int create, ThreeMMU32 flags);
        void *(*index)(void *);
        void (*attach_file)(void *, void *owner, void *file);
        void (*attach_index)(void *, void *owner, void *index);
        void (*set_end)(void *, void *owner, ThreeMMU32 end);
        int (*resize)(void *, void *file, ThreeMMU32 end);
        int (*read_index)(void *, void *owner);
        int (*validate)(void *, void *owner);
        int (*options)(void *, void *owner, ThreeMMU32 flags);
        void (*issue)(void *, int kind);
        void (*finish)(void *, void *owner, int create); /* Optional new-owner diagnostics. */
    } ThreeMMChunkConstruction;
    /* find returns a borrowed owner. allocate returns a new reference. Attach
     * callbacks consume file/index references, even for partial construction.
     * release destroys a failed new owner and all attached resources. Successful
     * existing opens apply options before retaining. Creation rejects existing
     * owners and forces write-enable, then creates index/file and reserves 128 bytes.
     * A newly created/opened owner's initial option failure is diagnostic only, as
     * in legacy release builds; an existing owner's option failure returns NULL
     * without an open-error diagnostic. Callbacks are synchronous and may not re-enter. */
    void *three_mm_chunk_construct(const ThreeMMChunkConstruction *, ThreeMMU32 flags, int create);
    typedef struct ThreeMMChunkLookup
    {
        void *(*next)(void *);
        void *(*main_file)(void *);
        void *(*extra_file)(void *);
        int (*invalid_main)(void *);
        void (*extra_collision)(void *);
    } ThreeMMChunkLookup;
    /* Borrowed registry/file tokens; invalid-main wrappers are skipped. A matching
     * extra file emits the optional diagnostic and continues the legacy search. */
    void *three_mm_chunk_find_container(void *first, void *file, const ThreeMMChunkLookup *);
    typedef struct ThreeMMChunkDisposal
    {
        void *context;
        void (*release_file)(void *, void *);
        void (*release_index)(void *, void *);
        void (*publish)(void *, const ThreeMMChunkReopenState *);
    } ThreeMMChunkDisposal;
    /* Consume all owners, including partially constructed/empty states. Publish a
     * cleared state first, then release main file/map, extra file/map, index and
     * runtime cache in legacy order. A nullable runtime supports expanded indexes.
     * Callbacks cannot re-enter; maps/caches must be unlocked. Repeated disposal of
     * the cleared state is safe. Native object allocation/destruction remains a host
     * primitive; this routine owns its associated-resource disposal sequencing. */
    int three_mm_chunk_dispose(ThreeMMChunkReopenState *, ThreeMMChunkRuntime *, const ThreeMMChunkDisposal *);
#ifdef __cplusplus
}
#endif
#endif
