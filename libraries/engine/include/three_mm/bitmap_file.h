/* Indexed bitmap files converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BITMAP_FILE_H
#define THREE_MM_ENGINE_BITMAP_FILE_H
#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_BITMAP_PIXELS,
        THREE_MM_BITMAP_PALETTE
    };
    enum
    {
        THREE_MM_BITMAP_HEADER,
        THREE_MM_BITMAP_BITS,
        THREE_MM_BITMAP_COMPRESSION,
        THREE_MM_BITMAP_EMPTY,
        THREE_MM_BITMAP_SIZE,
        THREE_MM_BITMAP_COLOURS,
        THREE_MM_BITMAP_RLE
    };
    typedef struct ThreeMMBitmapHeader
    {
        ThreeMMI32 width, height, row_bytes, pixel_bytes, source_bytes, pixel_offset;
        ThreeMMU32 colours;
        int upside_down, rle;
    } ThreeMMBitmapHeader;
    typedef struct ThreeMMBitmapServices
    {
        void *context;
        void *(*open)(void *, void *filename), *(*create)(void *, void *filename);
        void (*release)(void *, void *);
        ThreeMMI32 (*size)(void *, void *file);
        int (*read)(void *, void *file, void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        int (*write)(void *, void *file, const void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        int (*allocate)(void *, ThreeMMI32 bytes, void **);
        void (*free_memory)(void *, void **);
        void *(*palette_create)(void *);
        void *(*palette_lock)(void *, void *palette);
        void (*palette_unlock)(void *, void *palette);
        void (*publish)(void *, int kind, void *);
        void (*dimensions)(void *, int valid, ThreeMMI32 width, ThreeMMI32 height, int upside_down);
        void (*warning)(void *, int reason), (*bad_bitmap)(void *);
        void (*make_temporary)(void *, void *file);
    } ThreeMMBitmapServices;
    /* Eight-bit BMP only. Rows/palette bytes retain original order; orientation is
     * reported separately. Palette admission/read is conditional on want_palette.
     * Failed open clears requested owner outputs but leaves dimensions untouched.
     * Other failures release partial owners and invalidate dimensions. Success
     * transfers requested pixel/palette ownership through publish. Callbacks must
     * not reenter or mutate the ongoing transaction. */
    int three_mm_bitmap_header(const unsigned char[54], ThreeMMI32 file_bytes, ThreeMMBitmapHeader *, int *reason);
    int three_mm_bitmap_rle8(const unsigned char *, ThreeMMI32 source_bytes, unsigned char *, ThreeMMI32 pixel_bytes,
                             ThreeMMI32 row_bytes, unsigned char transparent);
    int three_mm_bitmap_read(void *filename, int want_pixels, int want_palette, unsigned char transparent,
                             const ThreeMMBitmapServices *);
    int three_mm_bitmap_write(void *filename, const void *pixels, void *palette, ThreeMMI32 width, ThreeMMI32 height,
                              int flip_rows, const ThreeMMBitmapServices *);
#ifdef __cplusplus
}
#endif
#endif
