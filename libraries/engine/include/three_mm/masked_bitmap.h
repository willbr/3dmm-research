/* Masked bitmap policy converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MASKED_BITMAP_H
#define THREE_MM_ENGINE_MASKED_BITMAP_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMaskedRect
    {
        ThreeMMI32 left, top, right, bottom;
    } ThreeMMMaskedRect;
    typedef struct ThreeMMMaskedBitmap
    {
        void *data;
        ThreeMMI32 bytes, row_bytes;
    } ThreeMMMaskedBitmap;
    typedef struct ThreeMMMaskedSource
    {
        void *pixels;
        ThreeMMI32 width, height;
        int upside_down;
    } ThreeMMMaskedSource;
    enum
    {
        THREE_MM_MASKED_EMPTY,
        THREE_MM_MASKED_WRONG_SIZE
    };
    typedef struct ThreeMMMaskedServices
    {
        void *context;
        int host_big_endian;
        ThreeMMU32 system;
        void *(*create)(void *);
        ThreeMMMaskedBitmap *(*state)(void *, void *);
        void (*release)(void *, void *);
        int (*allocate)(void *, ThreeMMI32, void **);
        void (*free_memory)(void *, void **);
        void (*diagnostic)(void *, int);
        int (*unpack)(void *, void *);
        ThreeMMI32 (*size)(void *, void *, int packed);
        int (*read)(void *, void *, void *, ThreeMMI32 bytes);
        int (*write)(void *, void *, const void *, ThreeMMI32 bytes);
        int (*is_memory)(void *, void *);
        void (*consume)(void *, void *);
        void (*publish_fetch)(void *, int valid, void *, ThreeMMI32 bytes);
        int (*bitmap_read)(void *, void *filename, unsigned char transparent, ThreeMMMaskedSource *);
    } ThreeMMMaskedServices;
    /* C owns aligned, contiguous host-order header/row-table/RLE storage. Initialize
     * state to zero; never copy an owning state. Flags are upside-down=1 and mask=2.
     * Reinitialization commits after success. Row lengths must fit signed 16 bits.
     * Immutable views survive until successful reinitialization or disposal. */
    int three_mm_masked_init(ThreeMMMaskedBitmap *, const unsigned char *, ThreeMMI32 row, ThreeMMI32 height,
                             const ThreeMMMaskedRect *, ThreeMMI32 x, ThreeMMI32 y, unsigned char transparent,
                             unsigned flags, unsigned char fill, const ThreeMMMaskedServices *);
    void *three_mm_masked_new(const unsigned char *, ThreeMMI32 row, ThreeMMI32 height, const ThreeMMMaskedRect *,
                              ThreeMMI32 x, ThreeMMI32 y, unsigned char transparent, unsigned flags, unsigned char fill,
                              const ThreeMMMaskedServices *);
    void three_mm_masked_dispose(ThreeMMMaskedBitmap *, const ThreeMMMaskedServices *);
    /* Validate/normalize host/foreign wire bytes in place. Empty height normalizes
     * both extents to the original left/top. Bad runs and trailing bytes fail in all
     * builds. On failure this scratch record can have partially normalized bytes. */
    int three_mm_masked_validate(ThreeMMMaskedBitmap *, int host_big);
    void three_mm_masked_bounds(const ThreeMMMaskedBitmap *, int host_big, ThreeMMMaskedRect *);
    int three_mm_masked_hit(const ThreeMMMaskedBitmap *, int host_big, ThreeMMI32 x, ThreeMMI32 y);
    /* Successful byte transfer consumes memory-backed blocks before validation or
     * object creation; file-backed blocks stay intact. Allocation/read failure leaves
     * the source block intact. Fetch estimates do not load/consume. Callbacks must
     * not reenter or mutate the same transaction. */
    void *three_mm_masked_read(void *block, const ThreeMMMaskedServices *);
    int three_mm_masked_fetch(void *block, int want_object, const ThreeMMMaskedServices *);
    int three_mm_masked_write(ThreeMMMaskedBitmap *, void *block, const ThreeMMMaskedServices *);
    void *three_mm_masked_import(void *filename, unsigned char transparent, ThreeMMI32 x, ThreeMMI32 y, unsigned flags,
                                 unsigned char fill, const ThreeMMMaskedServices *);
#ifdef __cplusplus
}
#endif
#endif
