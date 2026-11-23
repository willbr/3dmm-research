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
    struct MMChunkContext;
    struct MMChunkLink;
    struct MMChunkOwnerRead;
    struct MMFileOpsHost;
    typedef struct MMChunkConstruction
    {
        void *context;
        const struct MMFileOpsHost *files;
        const struct MMChunkContext *archives;
        struct MMChunkLink *(*allocate)(void *);
        void (*retain)(void *, void *owner);
        void (*release)(void *, void *owner);
        void (*extra_collision)(void *owner);
        void (*issue)(void *, int kind);
        void (*finish)(void *, void *owner, int create); /* Optional new-owner diagnostics. */
    } MMChunkConstruction;
    /* Allocate returns the link of a new, registered archive with one reference
     * and an empty reader. Release disposes a failed archive and its attachments.
     * The file provider and archive reader must use the same file tokens. Successful
     * existing opens apply options before retaining. Creation rejects existing
     * owners and forces write-enable, then creates index/file and reserves 128 bytes.
     * A newly created/opened owner's initial option failure is diagnostic only, as
     * in legacy release builds; an existing owner's option failure returns NULL
     * without an open-error diagnostic. Callbacks are synchronous and may not re-enter. */
    void *mm_chunk_construct(const MMChunkConstruction *, void *name, MMU32 flags, int create);
    /* Name scratch is initialized and distinct from the optional basis. */
    void *mm_chunk_construct_temp(const MMChunkConstruction *, void *basis, void *scratch);
    typedef struct MMForestConstruction
    {
        void *context;
        struct MMChunkLink *(*allocate)(void *);
        void (*release)(void *, void *owner);
        void (*finish)(void *, void *owner); /* Optional successful-owner diagnostics. */
    } MMForestConstruction;
    /* Allocate returns the link of a new archive with an empty reader. Copy
     * imports create a temporary file and reserve its 128-byte prefix; borrowed
     * imports retain source, publish offset+bytes as end, and mark invalid-main /
     * add-to-extra before reading. Failure releases the owner, including partial
     * imports. Input source is borrowed. Calls are synchronous and may not re-enter. */
    void *mm_chunk_construct_forest(const MMForestConstruction *, void *source, MMU32 offset,
                                          MMU32 bytes, int copy);
    /* Unpack packed chunks into a temporary file and import by borrowing it. */
    void *mm_chunk_construct_forest_from_chunk(const MMForestConstruction *, const struct MMChunkOwnerRead *,
                                               MMU32 type, MMU32 number, int copy);
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
