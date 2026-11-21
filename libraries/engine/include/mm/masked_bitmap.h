/* Masked bitmap policy converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MASKED_BITMAP_H
#define MM_ENGINE_MASKED_BITMAP_H
#include "resource_cache.h"
#include "read_stream.h"
#include "pixel_raster.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef MMPixelRect MMMaskedRect;
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
        int (*allocate)(void *, MMI32, void **);
        void (*free_memory)(void *, void **);
        void (*diagnostic)(void *, int);
        MMBlockTransform decode;
        int (*bitmap_read)(void *, void *filename, unsigned char transparent, MMMaskedSource *);
    } MMMaskedServices;
    /* C owns aligned, contiguous host-order header/row-table/RLE storage. Initialize
     * state to zero; never copy an owning state. Flags are upside-down=1 and mask=2.
     * Reinitialization commits after success. Row lengths must fit signed 16 bits.
     * Immutable views survive until successful reinitialization or disposal. */
    int mm_masked_init(MMMaskedBitmap *, const unsigned char *, MMI32 row, MMI32 height,
                             const MMMaskedRect *, MMI32 x, MMI32 y, unsigned char transparent,
                             unsigned flags, unsigned char fill, const MMMaskedServices *);
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
    /* Draw validated pixels at the supplied destination origin. The stride is
     * also the drawable width. Transparent runs leave the destination intact. */
    void mm_masked_blit8(const MMMaskedBitmap *, int host_big, unsigned char *pixels, MMI32 stride, MMI32 height,
                         MMI32 left, MMI32 top, const MMMaskedRect *clip, const struct MMRegion *);
    /* One bit per pixel, most significant bit first. Within the clip and bitmap
     * bounds, opaque runs set bits and transparent runs clear them. */
    void mm_masked_blit_mask(const MMMaskedBitmap *, int host_big, unsigned char *pixels, MMI32 stride, MMI32 height,
                             MMI32 left, MMI32 top, const MMMaskedRect *clip);
    typedef struct MMMaskedRle
    {
        const unsigned char *data;
        size_t bytes;
        const void *row_bytes; /* One native-endian 16-bit length per row. */
        MMI32 width, height;
        int solid;
        unsigned char fill;
    } MMMaskedRle;
    typedef struct MMMaskedTransfer
    {
        MMU32 colours[256], mask_white, mask_black;
        unsigned char background;
    } MMMaskedTransfer;
    int mm_masked_transfer_resolve(MMI32 depth, const unsigned char *source, MMI32 source_count,
                                   const unsigned char *target, MMI32 target_count, MMMaskedTransfer *);
    /* Validate all rows before writing. Borrowed source, palette and transfer
     * data must not overlap destination storage. With transfer unset, write
     * covered pixels directly to an indexed or native XRGB surface. A resolved
     * transfer applies mask OR and colour AND, including transparent cells.
     * BLACKONWHITE shrink combines samples before palette translation. */
    int mm_masked_stretch(const MMPixelSurface *, const MMMaskedRle *, const MMMaskedRect *destination,
                          const MMMaskedRect *clip, const struct MMRegion *, const unsigned char *palette, MMI32 count,
                          const MMMaskedTransfer *, int and_shrink);
    int mm_masked_rle_view(const MMMaskedBitmap *, int host_big, MMMaskedRle *);
    int mm_masked_native_big_endian(void);
    int mm_masked_write(const MMMaskedBitmap *, const MMStorageWriter *, const MMMaskedServices *);
    typedef struct MMMaskedOwner MMMaskedOwner;
    typedef struct MMMaskedOwnerHost
    {
        MMMaskedServices bitmap;
        const MMStorageAllocator *objects;
        void (*mark)(MMMaskedOwner *);
    } MMMaskedOwnerHost;
    /* The host and allocators are borrowed for the owner's lifetime. A null
     * host uses native byte order and the C heap. The bitmap view is borrowed. */
    MMMaskedOwner *mm_masked_owner_new(const unsigned char *, MMI32 row, MMI32 height, const MMMaskedRect *,
                                        MMI32 x, MMI32 y, unsigned char transparent, unsigned flags, unsigned char fill,
                                        const MMMaskedOwnerHost *);
    MMMaskedOwner *mm_masked_owner_import(void *filename, unsigned char transparent, MMI32 x, MMI32 y,
                                           unsigned flags, unsigned char fill, const MMMaskedOwnerHost *);
    MMMaskedOwner *mm_masked_owner_read_stream(const MMReadStream *, const MMMaskedOwnerHost *);
    /* A successful byte transfer consumes memory-backed blocks before wire validation
     * or object allocation. File-backed blocks stay intact. Allocation/read failure
     * leaves the source block intact. Size probes neither decode nor transfer. */
    MMMaskedOwner *mm_masked_owner_read_block(MMOwnedBlock *, const MMMaskedOwnerHost *);
    int mm_masked_owner_fetch(MMOwnedBlock *, void **, MMI32 *, const MMMaskedOwnerHost *);
    const MMMaskedBitmap *mm_masked_owner_bitmap(const MMMaskedOwner *);
    const MMResourceObject *mm_masked_owner_binding(const MMMaskedOwner *);
    const MMResourceObjectServices *mm_masked_owner_services(void);
    void mm_masked_owner_retain(MMMaskedOwner *);
    void mm_masked_owner_release(MMMaskedOwner *);
    void mm_masked_owner_priority(MMMaskedOwner *, MMI32);
    int mm_masked_owner_write(const MMMaskedOwner *, const MMStorageWriter *);
#ifdef __cplusplus
}
#endif
#endif
