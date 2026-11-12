/* Block/handle policy converted from Kauai file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BLOCK_IO_H
#define THREE_MM_ENGINE_BLOCK_IO_H
#include "data_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Opaque host handles. allocate/resize use NORMAL priority. Failed allocation
     * returns NULL; failed resize preserves the old handle and size. A successful
     * lock returns a stable pointer, including for a zero-byte handle. No re-entry. */
    typedef struct ThreeMMHandleServices
    {
        void *context;
        int (*allocate)(void *, ThreeMMU32, void **);
        void (*release)(void *, void *);
        ThreeMMU32 (*size)(void *, void *);
        void *(*lock)(void *, void *);
        void (*unlock)(void *, void *);
        int (*resize)(void *, void **, ThreeMMU32);
    } ThreeMMHandleServices;
    /* Borrowed view. File callbacks take absolute physical offsets. Ordinary byte
     * reads/writes and memory copies must not overlap. File copies retain forward
     * order, including the legacy allocation-dependent overlap behavior. */
    typedef struct ThreeMMBlockIO
    {
        ThreeMMBlockView view;
        const ThreeMMHandleServices *handles;
        void *context;
        int (*read_file)(void *, ThreeMMU32, void *, size_t);
        int (*write_file)(void *, ThreeMMU32, const void *, size_t);
    } ThreeMMBlockIO;
    int three_mm_stream_copy(const ThreeMMReadStream *, const ThreeMMStorageWriter *, const ThreeMMStorageAllocator *);
    int three_mm_block_read(const ThreeMMBlockIO *, ThreeMMU32 offset, void *, ThreeMMU32 bytes, int packed_ok);
    int three_mm_block_write(const ThreeMMBlockIO *, ThreeMMU32 offset, const void *, ThreeMMU32 bytes, int packed_ok);
    int three_mm_block_copy(const ThreeMMBlockIO *source, const ThreeMMBlockIO *destination, int packed_ok,
                            const ThreeMMStorageAllocator *);
    int three_mm_block_read_handle(const ThreeMMBlockIO *, ThreeMMU32 offset, ThreeMMU32 bytes, int packed_ok,
                                   void **output);
    int three_mm_block_write_handle(const ThreeMMBlockIO *, ThreeMMU32 offset, void *handle, int packed_ok);
    /* Memory take consumes the view, shifts a slice to offset zero and attempts an
     * exact shrink. A file take reads a new handle and leaves its view/reference.
     * Shrink failure retains the actual physical size, matching the legacy policy;
     * shrink_failed reports it for the host's existing diagnostic. */
    int three_mm_block_take_handle(ThreeMMBlockIO *, int packed_ok, void **output, int *shrink_failed);
    typedef int (*ThreeMMHandleCode)(void *, const void *, ThreeMMU32, void *, ThreeMMU32, ThreeMMI32 *);
    /* Query -> allocate -> convert -> unlock -> best-effort shrink -> commit.
     * Failed query/allocation/conversion retains the input handle and contents. */
    int three_mm_handle_code(const ThreeMMHandleServices *, void **owned, void *context, ThreeMMHandleCode,
                             int *shrink_failed);
    /* Pack file/full-memory/sliced-memory blocks using the existing codec callback
     * shape. A failed slice encode retains the normalized owning memory block.
     * Successful file packing leaves its prior file reference for the host to
     * release only after adopting the returned view. */
    int three_mm_block_pack(ThreeMMBlockView *, const ThreeMMBlockDecoder *, const ThreeMMHandleServices *,
                            int *shrink_failed);
#ifdef __cplusplus
}
#endif
#endif
