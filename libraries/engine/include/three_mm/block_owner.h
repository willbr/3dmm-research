/* Block ownership policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BLOCK_OWNER_H
#define THREE_MM_ENGINE_BLOCK_OWNER_H
#include "block_io.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMBlockFile
    {
        void *file;
        ThreeMMU32 offset, length;
    } ThreeMMBlockFile;
    /* create_temp returns one owned reference to an empty file. File callbacks are
     * positional; writing can extend the file. Service/context lifetimes are borrowed.
     * No callback may edit/re-enter the block. */
    typedef struct ThreeMMBlockFileServices
    {
        void *context;
        void (*retain)(void *, void *);
        void (*release)(void *, void *);
        void *(*create_temp)(void *);
        int (*read)(void *, void *, ThreeMMU32, void *, size_t);
        int (*write)(void *, void *, ThreeMMU32, const void *, size_t);
    } ThreeMMBlockFileServices;
    /* Owns exactly one file reference or opaque handle. Initialize before use;
     * never copy/move this owning record. Views and I/O descriptors are borrowed
     * until the next mutation. A file view may extend past EOF until written. */
    typedef struct ThreeMMOwnedBlock
    {
        ThreeMMBlockView view;
        void *file;
        const ThreeMMHandleServices *handles;
        const ThreeMMBlockFileServices *files;
    } ThreeMMOwnedBlock;
    int three_mm_owned_block_init(ThreeMMOwnedBlock *, const ThreeMMHandleServices *, const ThreeMMBlockFileServices *);
    int three_mm_owned_block_check(const ThreeMMOwnedBlock *);
    int three_mm_owned_block_clear(ThreeMMOwnedBlock *);
    /* File input is borrowed and retained; it must survive release of prior state.
     * Handle input is independently owned, adopted and set to NULL on success. */
    int three_mm_owned_block_set_file(ThreeMMOwnedBlock *, const ThreeMMBlockFile *, int packed);
    int three_mm_owned_block_set_handle(ThreeMMOwnedBlock *, void **handle, int packed);
    /* Preserve packed state; try NORMAL memory below 8 MiB, then an empty temp file.
     * Failures leave the prior owner intact. Files are not extended by selection. */
    int three_mm_owned_block_set_temp(ThreeMMOwnedBlock *, ThreeMMU32 bytes, int force_file);
    int three_mm_owned_block_io(ThreeMMOwnedBlock *, ThreeMMBlockIO *);
    /* Publish one owned file reference on success. Memory materialization does not
     * redirect/consume its block. Empty/zero-memory blocks return a NULL file;
     * zero-length file blocks retain their existing file. Failure leaves out intact. */
    int three_mm_owned_block_materialize(ThreeMMOwnedBlock *, int packed_ok, ThreeMMBlockFile *out);
    int three_mm_owned_block_take_handle(ThreeMMOwnedBlock *, int packed_ok, void **out, int *shrink_failed);
    typedef int (*ThreeMMBlockTransform)(void *context, void **handle);
    /* Encoding/decoding commit returned handles and release prior files in C.
     * Transform success consumes/replaces its handle; failure preserves it. */
    int three_mm_owned_block_code(ThreeMMOwnedBlock *, int pack, void *context, ThreeMMBlockTransform,
                                  int *shrink_failed);
#ifdef __cplusplus
}
#endif
#endif
