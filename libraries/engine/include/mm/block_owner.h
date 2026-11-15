/* Block ownership policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BLOCK_OWNER_H
#define MM_ENGINE_BLOCK_OWNER_H
#include "block_io.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMBlockFile
    {
        void *file;
        MMU32 offset, length;
    } MMBlockFile;
    /* create_temp returns one owned reference to an empty file. File callbacks are
     * positional; writing can extend the file. Service/context lifetimes are borrowed.
     * No callback may edit/re-enter the block. */
    typedef struct MMBlockFileServices
    {
        void *context;
        void (*retain)(void *, void *);
        void (*release)(void *, void *);
        void *(*create_temp)(void *);
        int (*read)(void *, void *, MMU32, void *, size_t);
        int (*write)(void *, void *, MMU32, const void *, size_t);
    } MMBlockFileServices;
    /* Owns exactly one file reference or opaque handle. Initialize before use;
     * never copy/move this owning record. Views and I/O descriptors are borrowed
     * until the next mutation. A file view may extend past EOF until written. */
    typedef struct MMOwnedBlock
    {
        MMBlockView view;
        void *file;
        const MMHandleServices *handles;
        const MMBlockFileServices *files;
    } MMOwnedBlock;
    int mm_owned_block_init(MMOwnedBlock *, const MMHandleServices *, const MMBlockFileServices *);
    int mm_owned_block_check(const MMOwnedBlock *);
    int mm_owned_block_clear(MMOwnedBlock *);
    /* File input is borrowed and retained; it must survive release of prior state.
     * Handle input is independently owned, adopted and set to NULL on success. */
    int mm_owned_block_set_file(MMOwnedBlock *, const MMBlockFile *, int packed);
    int mm_owned_block_set_handle(MMOwnedBlock *, void **handle, int packed);
    /* Preserve packed state; try NORMAL memory below 8 MiB, then an empty temp file.
     * Failures leave the prior owner intact. Files are not extended by selection. */
    int mm_owned_block_set_temp(MMOwnedBlock *, MMU32 bytes, int force_file);
    int mm_owned_block_io(MMOwnedBlock *, MMBlockIO *);
    /* Publish one owned file reference on success. Memory materialization does not
     * redirect/consume its block. Empty/zero-memory blocks return a NULL file;
     * zero-length file blocks retain their existing file. Failure leaves out intact. */
    int mm_owned_block_materialize(MMOwnedBlock *, int packed_ok, MMBlockFile *out);
    int mm_owned_block_take_handle(MMOwnedBlock *, int packed_ok, void **out, int *shrink_failed);
    typedef int (*MMBlockTransform)(void *context, void **handle);
    /* Encoding/decoding commit returned handles and release prior files in C.
     * Transform success consumes/replaces its handle; failure preserves it. */
    int mm_owned_block_code(MMOwnedBlock *, int pack, void *context, MMBlockTransform,
                                  int *shrink_failed);
#ifdef __cplusplus
}
#endif
#endif
