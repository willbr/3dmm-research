/* Owned pixel buffers converted from Kauai pixel_buffer.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/pixel_storage.h"
#include <stdlib.h>

#define PIXEL_SIZE_LIMIT 2147483647UL

int mm_pixel_layout(MMI32 width, MMI32 height, MMI32 depth, MMI32 alignment, MMI32 *pitch, size_t *bytes)
{
    MMU32 row, count, unit;
    if (width < 0 || height < 0 || (alignment != 4 && alignment != 8))
        return 0;
    if (depth == 1 || depth == 4)
        row = width ? (MMU32)((width - 1) / (8 / depth) + 1) : 0;
    else
    {
        if (depth != 8 && depth != 16 && depth != 24 && depth != 32)
            return 0;
        unit = (MMU32)(depth / 8);
        if ((MMU32)width > PIXEL_SIZE_LIMIT / unit)
            return 0;
        row = (MMU32)width * unit;
    }
    row = (row + (MMU32)(alignment - 1)) & ~(MMU32)(alignment - 1);
    if (row > PIXEL_SIZE_LIMIT || (height && row > PIXEL_SIZE_LIMIT / (MMU32)height))
        return 0;
    count = row * (MMU32)height;
    if ((size_t)count != count)
        return 0;
    *pitch = (MMI32)row;
    *bytes = (size_t)count;
    return 1;
}

int mm_pixel_buffer_init(MMPixelBuffer *owner, MMI32 width, MMI32 height, MMI32 depth, MMI32 alignment)
{
    MMPixelBuffer replacement;
    if (width <= 0 || height <= 0 ||
        !mm_pixel_layout(width, height, depth, alignment, &replacement.surface.pitch, &replacement.bytes))
        return 0;
    replacement.surface.pixels = calloc(1, replacement.bytes);
    if (!replacement.surface.pixels)
        return 0;
    replacement.surface.width = width;
    replacement.surface.height = height;
    replacement.surface.depth = depth;
    free(owner->surface.pixels);
    *owner = replacement;
    return 1;
}

void mm_pixel_buffer_reset(MMPixelBuffer *owner)
{
    static const MMPixelBuffer empty = {{NULL, 0, 0, 0, 0}, 0};
    free(owner->surface.pixels);
    *owner = empty;
}
