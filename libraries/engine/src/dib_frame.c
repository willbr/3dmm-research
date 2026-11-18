/* DIB frame policy converted from Kauai dib_frame.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/dib_frame.h"
#include <string.h>

#define DIB_HEADER_BYTES 40

int mm_dib_info(const void *data, size_t available, MMDibInfo *info)
{
    const unsigned char *header = (const unsigned char *)data;
    MMU32 width, raw_height, depth, height, pitch, bytes, decoded, declared;
    if (!header || available < DIB_HEADER_BYTES || mm_pixel_read(header, 0, 32) != DIB_HEADER_BYTES)
        return 0;
    width = mm_pixel_read(header, 1, 32);
    raw_height = mm_pixel_read(header, 2, 32);
    depth = mm_pixel_read(header + 14, 0, 16);
    if (!width || width > 2147483647UL / 4 || !raw_height || raw_height == 0x80000000UL ||
        header[12] != 1 || header[13] || (depth != 24 && depth != 32) || mm_pixel_read(header, 4, 32) ||
        mm_pixel_read(header, 8, 32))
        return 0;
    height = raw_height & 0x80000000UL ? 0U - raw_height : raw_height;
    if (height > 2147483647UL / (width * 4))
        return 0;
    decoded = width * 4 * height;
    pitch = (width * (depth / 8) + 3) & ~(MMU32)3;
    bytes = pitch * height;
    declared = mm_pixel_read(header, 5, 32);
    if ((size_t)decoded != decoded || (size_t)bytes > (size_t)-1 - DIB_HEADER_BYTES || (declared && declared < bytes))
        return 0;

    info->width = (MMI32)width;
    info->height = (MMI32)height;
    info->depth = (MMI32)depth;
    info->pitch = (MMI32)pitch;
    info->bottom_up = (raw_height & 0x80000000UL) == 0;
    info->bytes = (size_t)bytes + DIB_HEADER_BYTES;
    return 1;
}

int mm_dib_decode(const void *data, size_t bytes, const MMPixelSurface *target)
{
    MMDibInfo info;
    const unsigned char *pixels, *row;
    unsigned char *output;
    MMI32 y, x, source_y;
    if (!mm_dib_info(data, bytes, &info) || bytes < info.bytes || target->depth != 32 ||
        target->width != info.width || target->height != info.height || !mm_pixel_surface_valid(target))
        return 0;

    pixels = (const unsigned char *)data + DIB_HEADER_BYTES;
    for (y = 0; y < info.height; ++y)
    {
        source_y = info.bottom_up ? info.height - y - 1 : y;
        row = pixels + (size_t)source_y * (size_t)info.pitch;
        output = (unsigned char *)target->pixels + (size_t)y * (size_t)target->pitch;
        if (info.depth == 32)
            memcpy(output, row, (size_t)info.width * 4);
        else
            for (x = 0; x < info.width; ++x)
                mm_pixel_write(output, x, 32, mm_pixel_read(row, x, 24));
    }
    return 1;
}
