/* Indexed bitmap files converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BITMAP_FILE_H
#define MM_ENGINE_BITMAP_FILE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_BITMAP_PIXELS,
        MM_BITMAP_PALETTE
    };
    enum
    {
        MM_BITMAP_HEADER,
        MM_BITMAP_BITS,
        MM_BITMAP_COMPRESSION,
        MM_BITMAP_EMPTY,
        MM_BITMAP_SIZE,
        MM_BITMAP_COLOURS,
        MM_BITMAP_RLE
    };
    typedef struct MMBitmapHeader
    {
        MMI32 width, height, row_bytes, pixel_bytes, source_bytes, pixel_offset;
        MMU32 colours;
        int upside_down, rle;
    } MMBitmapHeader;
    typedef struct MMBitmapServices
    {
        void *context;
        void *(*open)(void *, void *filename), *(*create)(void *, void *filename);
        void (*release)(void *, void *);
        MMI32 (*size)(void *, void *file);
        int (*read)(void *, void *file, void *, MMI32 bytes, MMI32 offset);
        int (*write)(void *, void *file, const void *, MMI32 bytes, MMI32 offset);
        int (*allocate)(void *, MMI32 bytes, void **);
        void (*free_memory)(void *, void **);
        void *(*palette_create)(void *);
        void *(*palette_lock)(void *, void *palette);
        void (*palette_unlock)(void *, void *palette);
        void (*publish)(void *, int kind, void *);
        void (*dimensions)(void *, int valid, MMI32 width, MMI32 height, int upside_down);
        void (*warning)(void *, int reason), (*bad_bitmap)(void *);
        void (*make_temporary)(void *, void *file);
    } MMBitmapServices;
    /* Eight-bit BMP only. Rows/palette bytes retain original order; orientation is
     * reported separately. Palette admission/read is conditional on want_palette.
     * Failed open clears requested owner outputs but leaves dimensions untouched.
     * Other failures release partial owners and invalidate dimensions. Success
     * transfers requested pixel/palette ownership through publish. Callbacks must
     * not reenter or mutate the ongoing transaction. */
    int mm_bitmap_header(const unsigned char[54], MMI32 file_bytes, MMBitmapHeader *, int *reason);
    int mm_bitmap_rle8(const unsigned char *, MMI32 source_bytes, unsigned char *, MMI32 pixel_bytes,
                             MMI32 row_bytes, unsigned char transparent);
    int mm_bitmap_read(void *filename, int want_pixels, int want_palette, unsigned char transparent,
                             const MMBitmapServices *);
    int mm_bitmap_write(void *filename, const void *pixels, void *palette, MMI32 width, MMI32 height,
                              int flip_rows, const MMBitmapServices *);
#ifdef __cplusplus
}
#endif
#endif
