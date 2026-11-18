/* Masked bitmap policy converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MASKED_BITMAP_H
#define MM_ENGINE_MASKED_BITMAP_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMaskedRect
    {
        MMI32 left, top, right, bottom;
    } MMMaskedRect;
    typedef struct MMMaskedBitmap
    {
        void *data;
        MMI32 bytes, row_bytes;
    } MMMaskedBitmap;
    typedef struct MMMaskedSource
    {
        void *pixels;
        MMI32 width, height;
        int upside_down;
    } MMMaskedSource;
    enum
    {
        MM_MASKED_EMPTY,
        MM_MASKED_WRONG_SIZE
    };
    typedef struct MMMaskedServices
    {
        void *context;
        int host_big_endian;
        MMU32 system;
        void *(*create)(void *);
        MMMaskedBitmap *(*state)(void *, void *);
        void (*release)(void *, void *);
        int (*allocate)(void *, MMI32, void **);
        void (*free_memory)(void *, void **);
        void (*diagnostic)(void *, int);
        int (*unpack)(void *, void *);
        MMI32 (*size)(void *, void *, int packed);
        int (*read)(void *, void *, void *, MMI32 bytes);
        int (*write)(void *, void *, const void *, MMI32 bytes);
        int (*is_memory)(void *, void *);
        void (*consume)(void *, void *);
        void (*publish_fetch)(void *, int valid, void *, MMI32 bytes);
        int (*bitmap_read)(void *, void *filename, unsigned char transparent, MMMaskedSource *);
    } MMMaskedServices;
    /* C owns aligned, contiguous host-order header/row-table/RLE storage. Initialize
     * state to zero; never copy an owning state. Flags are upside-down=1 and mask=2.
     * Reinitialization commits after success. Row lengths must fit signed 16 bits.
     * Immutable views survive until successful reinitialization or disposal. */
    int mm_masked_init(MMMaskedBitmap *, const unsigned char *, MMI32 row, MMI32 height,
                             const MMMaskedRect *, MMI32 x, MMI32 y, unsigned char transparent,
                             unsigned flags, unsigned char fill, const MMMaskedServices *);
    void *mm_masked_new(const unsigned char *, MMI32 row, MMI32 height, const MMMaskedRect *,
                              MMI32 x, MMI32 y, unsigned char transparent, unsigned flags, unsigned char fill,
                              const MMMaskedServices *);
    void mm_masked_dispose(MMMaskedBitmap *, const MMMaskedServices *);
    /* Validate/normalize host/foreign wire bytes in place. Empty height normalizes
     * both extents to the original left/top. Bad runs and trailing bytes fail in all
     * builds. On failure this scratch record can have partially normalized bytes. */
    int mm_masked_validate(MMMaskedBitmap *, int host_big);
    void mm_masked_bounds(const MMMaskedBitmap *, int host_big, MMMaskedRect *);
    int mm_masked_hit(const MMMaskedBitmap *, int host_big, MMI32 x, MMI32 y);
    /* Copy and normalize a decoded wire stream. A successful read transfers
     * ownership to the empty output. Failure leaves the output unchanged. */
    int mm_masked_read_stream(const MMReadStream *, MMMaskedBitmap *, const MMMaskedServices *);
    /* Optional region iterator. begin receives the visible rectangle. next
     * returns a row's sorted, disjoint spans in absolute destination coordinates,
     * bounded by that rectangle. advance starts the next scan line. */
    typedef struct MMMaskedSpans
    {
        void *context;
        void (*begin)(void *, const MMMaskedRect *);
        int (*next)(void *, MMI32 *left, MMI32 *right);
        void (*advance)(void *);
    } MMMaskedSpans;
    /* Draw validated pixels at the supplied destination origin. The stride is
     * also the drawable width. Transparent runs leave the destination intact. */
    void mm_masked_blit8(const MMMaskedBitmap *, int host_big, unsigned char *pixels, MMI32 stride, MMI32 height,
                         MMI32 left, MMI32 top, const MMMaskedRect *clip, const MMMaskedSpans *);
    /* One bit per pixel, most significant bit first. Within the clip and bitmap
     * bounds, opaque runs set bits and transparent runs clear them. */
    void mm_masked_blit_mask(const MMMaskedBitmap *, int host_big, unsigned char *pixels, MMI32 stride, MMI32 height,
                             MMI32 left, MMI32 top, const MMMaskedRect *clip);
    /* Successful byte transfer consumes memory-backed blocks before validation or
     * object creation; file-backed blocks stay intact. Allocation/read failure leaves
     * the source block intact. Fetch estimates do not load/consume. Callbacks must
     * not reenter or mutate the same transaction. */
    void *mm_masked_read(void *block, const MMMaskedServices *);
    int mm_masked_fetch(void *block, int want_object, const MMMaskedServices *);
    int mm_masked_write(MMMaskedBitmap *, void *block, const MMMaskedServices *);
    void *mm_masked_import(void *filename, unsigned char transparent, MMI32 x, MMI32 y, unsigned flags,
                                 unsigned char fill, const MMMaskedServices *);
#ifdef __cplusplus
}
#endif
#endif
