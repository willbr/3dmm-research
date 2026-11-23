/* Block/handle policy converted from Kauai file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BLOCK_IO_H
#define MM_ENGINE_BLOCK_IO_H
#include "data_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef int (*MMBlockTransform)(void *context, void **handle);
    /* Opaque host handles. allocate/resize use NORMAL priority. Failed allocation
     * returns NULL; failed resize preserves the old handle and size. A successful
     * lock returns a stable pointer, including for a zero-byte handle. No re-entry. */
    typedef struct MMHandleServices
    {
        void *context;
        int (*allocate)(void *, MMU32, void **);
        void (*release)(void *, void *);
        MMU32 (*size)(void *, void *);
        void *(*lock)(void *, void *);
        void (*unlock)(void *, void *);
        int (*resize)(void *, void **, MMU32);
        /* Optional default decoder for handles from this provider. */
        MMBlockTransform decode;
    } MMHandleServices;
    /* Borrowed view. File callbacks take absolute physical offsets. Ordinary byte
     * reads/writes and memory copies must not overlap. File copies retain forward
     * order, including the legacy allocation-dependent overlap behavior. */
    typedef struct MMBlockIO
    {
        MMBlockView view;
        const MMHandleServices *handles;
        void *context;
        int (*read_file)(void *, MMU32, void *, size_t);
        int (*write_file)(void *, MMU32, const void *, size_t);
    } MMBlockIO;
    /* Borrow an unpacked block and its I/O descriptor for exact reads. Both
     * must stay live and unchanged until the stream is no longer in use. */
    int mm_block_io_stream(MMBlockIO *, MMReadStream *);
    /* Borrow an unpacked block and its descriptor for positional writes. */
    int mm_block_io_writer(MMBlockIO *, MMStorageWriter *);
    int mm_stream_copy(const MMReadStream *, const MMStorageWriter *, const MMStorageAllocator *);
    int mm_block_read(const MMBlockIO *, MMU32 offset, void *, MMU32 bytes, int packed_ok);
    int mm_block_write(const MMBlockIO *, MMU32 offset, const void *, MMU32 bytes, int packed_ok);
    int mm_block_copy(const MMBlockIO *source, const MMBlockIO *destination, int packed_ok,
                            const MMStorageAllocator *);
    int mm_block_read_handle(const MMBlockIO *, MMU32 offset, MMU32 bytes, int packed_ok,
                                   void **output);
    int mm_block_write_handle(const MMBlockIO *, MMU32 offset, void *handle, int packed_ok);
    /* Memory take consumes the view, shifts a slice to offset zero and attempts an
     * exact shrink. A file take reads a new handle and leaves its view/reference.
     * Shrink failure retains the actual physical size, matching the legacy policy;
     * shrink_failed reports it for the host's existing diagnostic. */
    int mm_block_take_handle(MMBlockIO *, int packed_ok, void **output, int *shrink_failed);
    typedef int (*MMHandleCode)(void *, const void *, MMU32, void *, MMU32, MMI32 *);
    /* Query -> allocate -> convert -> unlock -> best-effort shrink -> commit.
     * Failed query/allocation/conversion retains the input handle and contents. */
    int mm_handle_code(const MMHandleServices *, void **owned, void *context, MMHandleCode,
                             int *shrink_failed);
    /* Pack file/full-memory/sliced-memory blocks using the existing codec callback
     * shape. A failed slice encode retains the normalized owning memory block.
     * Successful file packing leaves its prior file reference for the host to
     * release only after adopting the returned view. */
    int mm_block_pack(MMBlockView *, const MMBlockDecoder *, const MMHandleServices *,
                            int *shrink_failed);
#ifdef __cplusplus
}
#endif
#endif
