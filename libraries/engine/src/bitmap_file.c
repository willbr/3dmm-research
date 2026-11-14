/* Indexed bitmap files converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/bitmap_file.h"
#include "three_mm/wide.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p, unsigned n)
{
    ThreeMMU32 v = 0;
    while (n)
        v = (v << 8) | p[--n];
    return v;
}
static void put(unsigned char *p, unsigned n, ThreeMMU32 v)
{
    while (n--)
    {
        *p++ = (unsigned char)v;
        v >>= 8;
    }
}
static int layout(ThreeMMI32 width, ThreeMMI32 height, ThreeMMI32 *row, ThreeMMI32 *bytes)
{
    if (width < 0 || width > 2147483644 || height < 0)
        return 0;
    *row = (width + 3) & ~3;
    if (*row && (ThreeMMU32)height > 0x7fffffffUL / (ThreeMMU32)*row)
        return 0;
    *bytes = *row * height;
    return 1;
}
int three_mm_bitmap_header(const unsigned char *b, ThreeMMI32 bytes, ThreeMMBitmapHeader *h, int *reason)
{
    ThreeMMU32 compression = word(b + 30, 4), image = word(b + 34, 4), offset = word(b + 10, 4);
    ThreeMMI32 height = three_mm_signed_word(word(b + 22, 4));
    *reason = THREE_MM_BITMAP_HEADER;
    if (bytes <= 54 || word(b, 2) != 0x4d42 || word(b + 2, 4) != (ThreeMMU32)bytes || offset < 54 ||
        offset >= (ThreeMMU32)bytes || word(b + 6, 2) || word(b + 8, 2) || word(b + 14, 4) != 40 ||
        word(b + 26, 2) != 1)
        return 0;
    *reason = THREE_MM_BITMAP_BITS;
    if (word(b + 28, 2) != 8)
        return 0;
    *reason = THREE_MM_BITMAP_COMPRESSION;
    h->rle = compression == 1;
    if ((compression != 0 && !h->rle) || (image != (ThreeMMU32)bytes - offset && (image || h->rle)))
        return 0;
    h->width = three_mm_signed_word(word(b + 18, 4));
    h->upside_down = height < 0;
    *reason = THREE_MM_BITMAP_EMPTY;
    if (height == (-2147483647 - 1) || h->width <= 0 || height == 0)
        return 0;
    h->height = height < 0 ? -height : height;
    if (!layout(h->width, h->height, &h->row_bytes, &h->pixel_bytes))
        return 0;
    *reason = THREE_MM_BITMAP_SIZE;
    if (!h->rle && image && image != (ThreeMMU32)h->pixel_bytes)
        return 0;
    h->source_bytes = (!h->rle && !image) ? h->pixel_bytes : (ThreeMMI32)image;
    h->pixel_offset = (ThreeMMI32)offset;
    h->colours = word(b + 46, 4);
    return 1;
}
int three_mm_bitmap_rle8(const unsigned char *src, ThreeMMI32 count, unsigned char *dst, ThreeMMI32 bytes,
                         ThreeMMI32 row, unsigned char transparent)
{
    ThreeMMI32 at = 0, pos = 0, x = 0, n, padded, dx, dy;
    unsigned char code;
    if (count < 0 || bytes < 0 || row <= 0 || !src || (bytes && !dst))
        return 0;
    for (;;)
    {
        if (count - at < 2)
            return 0;
        code = src[at++];
        n = src[at++];
        if (code)
        {
            if (code > bytes - pos || code > row - x)
                return 0;
            memset(dst + pos, n, code);
            pos += code;
            x += code;
            continue;
        }
        if (n == 0)
        {
            if (row - x > bytes - pos)
                return 0;
            if (row > x)
                memset(dst + pos, transparent, row - x);
            pos += row - x;
            x = 0;
        }
        else if (n == 1)
        {
            if (bytes > pos)
                memset(dst + pos, transparent, bytes - pos);
            return 1;
        }
        else if (n == 2)
        {
            if (count - at < 2)
                return 0;
            dx = src[at++];
            dy = src[at++];
            if (dx > row - x || dx > bytes - pos || dy > (bytes - pos - dx) / row)
                return 0;
            n = dx + row * dy;
            if (n)
                memset(dst + pos, transparent, n);
            pos += n;
            x += dx;
        }
        else
        {
            padded = (n + 1) & ~1;
            if (padded > count - at || n > bytes - pos || n > row - x)
                return 0;
            memcpy(dst + pos, src + at, n);
            pos += n;
            at += padded;
            x += n;
        }
    }
}
static int read_at(void *file, void *p, ThreeMMI32 n, ThreeMMI32 at, ThreeMMI32 bytes, const ThreeMMBitmapServices *s)
{
    return n >= 0 && at >= 0 && at <= bytes && n <= bytes - at && s->read(s->context, file, p, n, at);
}
int three_mm_bitmap_read(void *filename, int want_pixels, int want_palette, unsigned char transparent,
                         const ThreeMMBitmapServices *s)
{
    void *file, *pixels = NULL, *palette = NULL, *source = NULL, *data;
    ThreeMMBitmapHeader h;
    unsigned char header[54];
    ThreeMMI32 bytes;
    int reason, ok;
    if (want_pixels)
        s->publish(s->context, THREE_MM_BITMAP_PIXELS, NULL);
    if (want_palette)
        s->publish(s->context, THREE_MM_BITMAP_PALETTE, NULL);
    file = s->open(s->context, filename);
    if (!file)
        return 0;
    bytes = s->size(s->context, file);
    if (bytes <= 54 || !read_at(file, header, 54, 0, bytes, s))
        goto fail;
    if (!three_mm_bitmap_header(header, bytes, &h, &reason))
    {
        s->warning(s->context, reason);
        goto fail;
    }
    if (want_palette)
    {
        if (h.colours != 0 && h.colours != 256)
        {
            s->warning(s->context, THREE_MM_BITMAP_COLOURS);
            goto fail;
        }
        palette = s->palette_create(s->context);
        if (!palette)
            goto fail;
        s->publish(s->context, THREE_MM_BITMAP_PALETTE, palette);
        data = s->palette_lock(s->context, palette);
        ok = read_at(file, data, 1024, 54, bytes, s);
        s->palette_unlock(s->context, palette);
        if (!ok)
            goto fail;
    }
    if (want_pixels)
    {
        if (h.rle)
        {
            if (!s->allocate(s->context, h.source_bytes, &source))
                goto fail;
            if (!read_at(file, source, h.source_bytes, h.pixel_offset, bytes, s) ||
                !s->allocate(s->context, h.pixel_bytes, &pixels))
                goto rle_bad;
            s->publish(s->context, THREE_MM_BITMAP_PIXELS, pixels);
            if (!three_mm_bitmap_rle8((const unsigned char *)source, h.source_bytes, (unsigned char *)pixels,
                                      h.pixel_bytes, h.row_bytes, transparent))
                goto rle_bad;
            s->free_memory(s->context, &source);
        }
        else
        {
            if (!s->allocate(s->context, h.pixel_bytes, &pixels))
                goto fail;
            s->publish(s->context, THREE_MM_BITMAP_PIXELS, pixels);
            if (!read_at(file, pixels, h.pixel_bytes, h.pixel_offset, bytes, s))
            {
                s->free_memory(s->context, &pixels);
                s->publish(s->context, THREE_MM_BITMAP_PIXELS, NULL);
                goto fail;
            }
        }
    }
    s->dimensions(s->context, 1, h.width, h.height, h.upside_down);
    s->release(s->context, file);
    return 1;
rle_bad:
    s->free_memory(s->context, &source);
    s->warning(s->context, THREE_MM_BITMAP_RLE);
fail:
    s->bad_bitmap(s->context);
    /* Original malformed-RLE failures discarded this pointer without freeing it. */
    s->free_memory(s->context, &pixels);
    if (want_pixels)
        s->publish(s->context, THREE_MM_BITMAP_PIXELS, NULL);
    if (palette)
        s->release(s->context, palette);
    if (want_palette)
        s->publish(s->context, THREE_MM_BITMAP_PALETTE, NULL);
    s->dimensions(s->context, 0, 0, 0, 0);
    s->release(s->context, file);
    return 0;
}
int three_mm_bitmap_write(void *filename, const void *pixels, void *palette, ThreeMMI32 width, ThreeMMI32 height,
                          int flip, const ThreeMMBitmapServices *s)
{
    unsigned char header[54];
    ThreeMMI32 row, n, at = 1078, y;
    void *file, *data;
    int ok = 0;
    if (!layout(width, height, &row, &n) || n > 2147482569 || (n && !pixels))
        return 0;
    memset(header, 0, sizeof(header));
    put(header, 2, 0x4d42);
    put(header + 2, 4, (ThreeMMU32)n + 1078);
    put(header + 10, 4, 1078);
    put(header + 14, 4, 40);
    put(header + 18, 4, (ThreeMMU32)width);
    put(header + 22, 4, (ThreeMMU32)height);
    put(header + 26, 2, 1);
    put(header + 28, 2, 8);
    put(header + 46, 4, 256);
    put(header + 50, 4, 256);
    file = s->create(s->context, filename);
    if (!file)
        return 0;
    if (!s->write(s->context, file, header, 54, 0))
        goto done;
    data = s->palette_lock(s->context, palette);
    ok = s->write(s->context, file, data, 1024, 54);
    s->palette_unlock(s->context, palette);
    if (!ok)
        goto done;
    if (flip)
    {
        ok = 1;
        if (row)
            for (y = height; y > 0 && ok;)
            {
                --y;
                ok = s->write(s->context, file, (const unsigned char *)pixels + y * row, row, at);
                at += row;
            }
    }
    else
        ok = s->write(s->context, file, pixels, n, 1078);
done:
    if (!ok)
        s->make_temporary(s->context, file);
    s->release(s->context, file);
    return ok;
}
