#include "mm/pixel_raster.h"
#include <stddef.h>

int mm_pixel_surface_valid(const MMPixelSurface *surface)
{
    const MMI32 depth = surface->depth;
    const MMI32 width = surface->width, pitch = surface->pitch, height = surface->height;
    if (!surface->pixels || width <= 0 || height <= 0 || pitch <= 0 ||
        (size_t)pitch != (MMU32)pitch || (size_t)height != (MMU32)height ||
        (size_t)height > (size_t)-1 / (size_t)pitch)
        return 0;
    if (depth == 1 || depth == 4)
        return (width - 1) / (8 / depth) < pitch;
    return (depth == 8 || depth == 16 || depth == 24 || depth == 32) && width <= pitch / (depth / 8);
}

MMU32 mm_pixel_read(const unsigned char *row, MMI32 x, int depth)
{
    const unsigned char *pixel;
    MMU32 raw;
    if (depth == 1)
        return (row[x / 8] >> (7 - (x & 7))) & 1;
    if (depth == 4)
        return (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
    pixel = row + (size_t)x * (depth / 8);
    raw = pixel[0];
    if (depth >= 16)
        raw |= (MMU32)pixel[1] << 8;
    if (depth >= 24)
        raw |= (MMU32)pixel[2] << 16;
    if (depth == 32)
        raw |= (MMU32)pixel[3] << 24;
    return raw;
}

void mm_pixel_write(unsigned char *row, MMI32 x, int depth, MMU32 raw)
{
    unsigned char *pixel;
    unsigned shift, mask;
    if (depth == 1 || depth == 4)
    {
        shift = depth == 1 ? 7 - (x & 7) : (x & 1) ? 0 : 4;
        mask = depth == 1 ? 1 : 15;
        pixel = row + x / (8 / depth);
        *pixel = (unsigned char)((*pixel & ~(mask << shift)) | ((raw & mask) << shift));
        return;
    }
    pixel = row + (size_t)x * (depth / 8);
    pixel[0] = (unsigned char)raw;
    if (depth >= 16)
        pixel[1] = (unsigned char)(raw >> 8);
    if (depth >= 24)
        pixel[2] = (unsigned char)(raw >> 16);
    if (depth == 32)
        pixel[3] = (unsigned char)(raw >> 24);
}

MMU32 mm_pixel_rgb(const unsigned char *colour)
{
    return ((MMU32)colour[2] << 16) | ((MMU32)colour[1] << 8) | colour[0];
}

MMU32 mm_pixel_rgb555(MMU32 rgb)
{
    return ((rgb >> 9) & 0x7c00) | ((rgb >> 6) & 0x3e0) | ((rgb >> 3) & 31);
}

unsigned char mm_pixel_nearest(const unsigned char *palette, MMI32 count, MMU32 rgb)
{
    MMI32 i, red, green, blue, distance = 2147483647L, candidate;
    unsigned char best = 0;
    for (i = 0; i < count; ++i, palette += 4)
    {
        red = (MMI32)palette[2] - (MMI32)((rgb >> 16) & 255);
        green = (MMI32)palette[1] - (MMI32)((rgb >> 8) & 255);
        blue = (MMI32)palette[0] - (MMI32)(rgb & 255);
        candidate = red * red + green * green + blue * blue;
        if (candidate < distance)
        {
            distance = candidate;
            best = (unsigned char)i;
        }
    }
    return best;
}

void mm_pixel_translate8(const unsigned char *source, MMI32 source_count, const unsigned char *target,
                         MMI32 target_count, unsigned char indices[256])
{
    MMI32 i;
    int identity = target_count == 256;
    for (i = 0; identity && i < 256; ++i)
        identity = (i < source_count ? mm_pixel_rgb(source + 4 * i) : 0) == mm_pixel_rgb(target + 4 * i);
    for (i = 0; i < 256; ++i)
        indices[i] = identity ? (unsigned char)i
                             : mm_pixel_nearest(target, target_count, i < source_count ? mm_pixel_rgb(source + 4 * i) : 0);
}
