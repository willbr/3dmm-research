/* Masked bitmap policy converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/masked_bitmap.h"
#include "mm/region.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static MMU32 word(const unsigned char *p, unsigned n, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[big ? i : n - i - 1];
    return v;
}
static void put(unsigned char *p, unsigned n, MMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        p[big ? n - i - 1 : i] = (unsigned char)v;
        v >>= 8;
    }
}
static int subtract(MMI32 a, MMI32 b, MMI32 *out)
{
    if ((b > 0 && a < MINIMUM + b) || (b < 0 && a > MAXIMUM + b))
        return 0;
    *out = a - b;
    return 1;
}
static int row_encode(const unsigned char *src, MMI32 width, unsigned char transparent, int mask,
                      unsigned char *out)
{
    MMI32 i = 0, run = 0, n = 0, j;
    int trans = 1, opaque = 0;
    for (j = 0; j < width; ++j)
        if (src[j] != transparent)
        {
            opaque = 1;
            break;
        }
    if (!opaque)
        return 0;
    for (;;)
    {
        if (i == width || trans != (src[i] == transparent) || run == 255)
        {
            if (trans && i == width)
                break;
            if (n == 32767)
                return -1;
            if (out)
                out[n] = (unsigned char)run;
            ++n;
            if (!trans && !mask)
            {
                if (run > 32767 - n)
                    return -1;
                if (out && run)
                    memcpy(out + n, src + i - run, (size_t)run);
                n += run;
            }
            if (i == width)
                break;
            run = 0;
            trans = !trans;
        }
        else
        {
            ++run;
            ++i;
        }
    }
    return (int)n;
}
static void canonical(unsigned char *b, MMI32 n, const MMMaskedServices *s)
{
    put(b, 2, 1, s->host_big_endian);
    put(b + 2, 2, s->system, s->host_big_endian);
    put(b + 6, 2, 0, s->host_big_endian);
    put(b + 24, 4, (MMU32)n, s->host_big_endian);
}
static int allocate_data(const MMMaskedServices *s, MMI32 bytes, void **out)
{
    if (s && s->allocate)
        return s->allocate(s->context, bytes, out);
    if ((size_t)bytes != (MMU32)bytes)
        return 0;
    *out = malloc((size_t)bytes);
    return *out != NULL;
}
static void free_data(const MMMaskedServices *s, void **data)
{
    if (s && s->free_memory)
        s->free_memory(s->context, data);
    else
    {
        free(*data);
        *data = NULL;
    }
}
static void diagnostic(const MMMaskedServices *s, int code)
{
    if (s->diagnostic)
        s->diagnostic(s->context, code);
}
void mm_masked_dispose(MMMaskedBitmap *m, const MMMaskedServices *s)
{
    free_data(s, &m->data);
    m->bytes = m->row_bytes = 0;
}
int mm_masked_init(MMMaskedBitmap *m, const unsigned char *pixels, MMI32 row, MMI32 height,
                         const MMMaskedRect *extract, MMI32 xref, MMI32 yref, unsigned char transparent,
                         unsigned flags, unsigned char fill, const MMMaskedServices *s)
{
    MMMaskedRect r;
    MMI32 x, y, sy, n, table, at, left = MAXIMUM, top = MAXIMUM, right = 0, bottom = 0;
    int count, mask = (flags & 2) != 0;
    unsigned char *b;
    void *data = NULL;
    if (!pixels || !extract || row <= 0 || height <= 0 || height > MAXIMUM / row || extract->left < 0 ||
        extract->top < 0 || extract->left >= extract->right || extract->top >= extract->bottom ||
        extract->right > row || extract->bottom > height)
        return 0;
    for (y = extract->top; y < extract->bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        for (x = extract->left; x < extract->right; ++x)
            if (pixels[sy * row + x] != transparent)
            {
                if (x < left)
                    left = x;
                if (x + 1 > right)
                    right = x + 1;
                if (y < top)
                    top = y;
                bottom = y + 1;
            }
    }
    if (left == MAXIMUM)
    {
        left = top = right = bottom = 0;
        diagnostic(s, MM_MASKED_EMPTY);
    }
    if (!subtract(left, xref, &r.left) || !subtract(right, xref, &r.right) || !subtract(top, yref, &r.top) ||
        !subtract(bottom, yref, &r.bottom))
        return 0;
    if (bottom - top > (MAXIMUM - 28) / 2)
        return 0;
    table = (bottom - top) * 2;
    n = 28 + table;
    for (y = top; y < bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        count = row_encode(pixels + sy * row + left, right - left, transparent, mask, NULL);
        if (count < 0 || count > MAXIMUM - n)
            return 0;
        n += count;
    }
    if (!allocate_data(s, n, &data))
        return 0;
    b = (unsigned char *)data;
    memset(b, 0, 28);
    canonical(b, n, s);
    b[4] = (unsigned char)mask;
    b[5] = fill;
    put(b + 8, 4, (MMU32)r.left, s->host_big_endian);
    put(b + 12, 4, (MMU32)r.top, s->host_big_endian);
    put(b + 16, 4, (MMU32)r.right, s->host_big_endian);
    put(b + 20, 4, (MMU32)r.bottom, s->host_big_endian);
    at = 28 + table;
    for (y = top; y < bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        count = row_encode(pixels + sy * row + left, right - left, transparent, mask, b + at);
        put(b + 28 + (y - top) * 2, 2, (MMU32)count, s->host_big_endian);
        at += count;
    }
    mm_masked_dispose(m, s);
    m->data = data;
    m->bytes = n;
    m->row_bytes = table;
    return 1;
}
void mm_masked_bounds(const MMMaskedBitmap *m, int big, MMMaskedRect *r)
{
    const unsigned char *b = (const unsigned char *)m->data;
    r->left = mm_signed_word(word(b + 8, 4, big));
    r->top = mm_signed_word(word(b + 12, 4, big));
    r->right = mm_signed_word(word(b + 16, 4, big));
    r->bottom = mm_signed_word(word(b + 20, 4, big));
}
static int decode_row(const unsigned char *row, size_t length, MMI32 width, int solid,
                      unsigned char fill, unsigned char *indices, unsigned char *opaque)
{
    size_t pos = 0;
    MMI32 x = 0, count;
    if (opaque)
        memset(opaque, 0, (size_t)width);
    while (pos < length)
    {
        count = row[pos++];
        if (count > width - x)
            return 0;
        x += count;
        if (pos == length)
            break;
        count = row[pos++];
        if (count > width - x || (!solid && (size_t)count > length - pos))
            return 0;
        if (opaque)
        {
            memset(opaque + x, 1, (size_t)count);
            if (solid)
                memset(indices + x, fill, (size_t)count);
            else
                memcpy(indices + x, row + pos, (size_t)count);
        }
        x += count;
        if (!solid)
            pos += (size_t)count;
    }
    return 1;
}
int mm_masked_validate(MMMaskedBitmap *m, int big)
{
    unsigned char *b = (unsigned char *)m->data;
    MMMaskedRect r;
    MMU32 marker, width, height, i, offset;
    MMI32 n = m->bytes, rows, at, len;
    int source = big;
    if (n < 28 || !b)
        return 0;
    marker = word(b, 2, big);
    if (marker == 256)
        source = !big;
    else if (marker != 1)
        return 0;
    if (word(b + 6, 2, source) || word(b + 24, 4, source) != (MMU32)n)
        return 0;
    if (source != big)
    {
        for (i = 0; i < 4; i += 2)
            put(b + i, 2, word(b + i, 2, source), big);
        for (i = 8; i < 28; i += 4)
            put(b + i, 4, word(b + i, 4, source), big);
        put(b, 2, 1, big);
    }
    mm_masked_bounds(m, big, &r);
    if (r.bottom == r.top)
    {
        if (n != 28)
            return 0;
        put(b + 16, 4, (MMU32)r.left, big);
        m->row_bytes = 0;
        return 1;
    }
    if (r.bottom < r.top || r.right <= r.left)
        return 0;
    width = (MMU32)r.right - (MMU32)r.left;
    height = (MMU32)r.bottom - (MMU32)r.top;
    if (width > MAXIMUM || height > (MMU32)(n - 28) / 2)
        return 0;
    rows = (MMI32)height * 2;
    at = 28 + rows;
    for (offset = 28; offset < (MMU32)(28 + rows); offset += 2)
    {
        len = (MMI32)word(b + offset, 2, source);
        if (source != big)
            put(b + offset, 2, (MMU32)len, big);
        if (len > 32767 || len > n - at || !decode_row(b + at, (size_t)len, (MMI32)width, b[4], 0, NULL, NULL))
            return 0;
        at += len;
    }
    if (at != n)
        return 0;
    m->row_bytes = rows;
    return 1;
}
int mm_masked_hit(const MMMaskedBitmap *m, int big, MMI32 x, MMI32 y)
{
    const unsigned char *b = (const unsigned char *)m->data;
    MMMaskedRect r;
    MMI32 row, at, end, len, pos, skip, count;
    if (!b || m->bytes < 28 || m->row_bytes < 0 || m->row_bytes > m->bytes - 28)
        return 0;
    mm_masked_bounds(m, big, &r);
    if (x < r.left || x >= r.right || y < r.top || y >= r.bottom || !subtract(x, r.left, &x) ||
        !subtract(y, r.top, &y) || y >= m->row_bytes / 2)
        return 0;
    at = 28 + m->row_bytes;
    for (row = 0; row <= y; ++row)
    {
        len = (MMI32)word(b + 28 + row * 2, 2, big);
        if (len > 32767 || len > m->bytes - at)
            return 0;
        if (row == y)
            break;
        at += len;
    }
    end = at + len;
    pos = at;
    while (pos < end)
    {
        skip = b[pos++];
        if (x < skip || pos == end)
            return 0;
        x -= skip;
        count = b[pos++];
        if (!b[4] && count > end - pos)
            return 0;
        if (x < count)
            return 1;
        x -= count;
        if (!b[4])
            pos += count;
    }
    return 0;
}
static int visible_span(MMI32 origin, MMI32 extent, MMI32 clip_min, MMI32 clip_max, MMI32 *first, MMI32 *end)
{
    MMU32 start, stop;
    if (origin >= clip_max || clip_min >= clip_max)
        return 0;
    start = origin < clip_min ? (MMU32)clip_min - (MMU32)origin : 0;
    stop = (MMU32)clip_max - (MMU32)origin;
    if (start >= (MMU32)extent)
        return 0;
    *first = (MMI32)start;
    *end = stop < (MMU32)extent ? (MMI32)stop : extent;
    return 1;
}

static void fill_bits(unsigned char *line, MMI32 left, MMI32 right, int on)
{
    MMI32 first = left / 8, last = (right - 1) / 8;
    unsigned char leading = (unsigned char)(0xffU >> (left & 7));
    unsigned char trailing = (unsigned char)(0xffU << (7 - ((right - 1) & 7)));
    if (first == last)
        leading &= trailing;
    else
    {
        if (last > first + 1)
            memset(line + first + 1, on ? 255 : 0, (size_t)(last - first - 1));
        line[last] = (unsigned char)(on ? line[last] | trailing : line[last] & ~trailing);
    }
    line[first] = (unsigned char)(on ? line[first] | leading : line[first] & ~leading);
}

static int next_span(MMRegionCursor *cursor, MMI32 origin, MMI32 *left, MMI32 *right)
{
    if (!cursor || !mm_region_cursor_pair(cursor, left, right))
        return 0;
    *left -= origin;
    *right -= origin;
    return 1;
}

static void blit_row(const unsigned char *run, const unsigned char *row_end, unsigned char *line,
                     MMI32 origin, MMI32 left, MMI32 right, int solid, unsigned char fill, int mask,
                     MMRegionCursor *cursor)
{
    MMI32 x = 0, count, run_end, start, end;
    if (cursor && !next_span(cursor, origin, &left, &right))
        return;
    if (mask)
        fill_bits(line, origin + left, origin + right, 0);
    while (run < row_end)
    {
        x += *run++;
        if (run == row_end)
            break;
        count = *run++;
        run_end = x + count;
        while (left < run_end)
        {
            start = x < left ? left : x;
            end = run_end < right ? run_end : right;
            if (end > start)
            {
                if (mask)
                    fill_bits(line, origin + start, origin + end, 1);
                else if (solid)
                    memset(line + (origin + start), fill, (size_t)(end - start));
                else
                    memcpy(line + (origin + start), run + (start - x), (size_t)(end - start));
            }
            if (right > run_end)
                break;
            if (!next_span(cursor, origin, &left, &right))
                return;
        }
        if (!solid)
            run += count;
        x = run_end;
    }
}

static void blit(const MMMaskedBitmap *m, int big, unsigned char *pixels, MMI32 stride, MMI32 height,
                 MMI32 left, MMI32 top, const MMMaskedRect *clip, int mask, const struct MMRegion *region)
{
    MMRegionCursor cursor = {0};
    const unsigned char *b = (const unsigned char *)m->data;
    MMMaskedRect r, target;
    const unsigned char *encoded, *row_end;
    unsigned char *line;
    MMI32 x0, x1, y0, y1, row;
    if (stride <= 0 || height <= 0 || (size_t)stride != (MMU32)stride || (size_t)height != (MMU32)height ||
        (size_t)height > (size_t)-1 / (size_t)stride || (mask && stride > MAXIMUM / 8))
        return;
    target.left = target.top = 0;
    target.right = mask ? stride * 8 : stride;
    target.bottom = height;
    if (clip)
    {
        if (clip->left > target.left)
            target.left = clip->left;
        if (clip->top > target.top)
            target.top = clip->top;
        if (clip->right < target.right)
            target.right = clip->right;
        if (clip->bottom < target.bottom)
            target.bottom = clip->bottom;
    }
    mm_masked_bounds(m, big, &r);
    if (!visible_span(left, r.right - r.left, target.left, target.right, &x0, &x1) ||
        !visible_span(top, r.bottom - r.top, target.top, target.bottom, &y0, &y1))
        return;
    if (region)
    {
        target.left = left + x0;
        target.right = left + x1;
        target.top = top + y0;
        target.bottom = top + y1;
        mm_region_cursor_open(&cursor, region, &target);
    }
    encoded = b + 28 + m->row_bytes;
    for (row = 0; row < y0; ++row)
        encoded += word(b + 28 + row * 2, 2, big);
    for (; row < y1; ++row)
    {
        line = pixels + (size_t)(top + row) * (size_t)stride;
        row_end = encoded + word(b + 28 + row * 2, 2, big);
        blit_row(encoded, row_end, line, left, x0, x1, b[4], b[5], mask, region ? &cursor : NULL);
        encoded = row_end;
        if (region && row + 1 < y1)
            mm_region_scan_advance(&cursor.scan, 1);
    }
    mm_region_cursor_close(&cursor);
}

void mm_masked_blit8(const MMMaskedBitmap *m, int big, unsigned char *pixels, MMI32 stride, MMI32 height,
                     MMI32 left, MMI32 top, const MMMaskedRect *clip, const struct MMRegion *region)
{
    blit(m, big, pixels, stride, height, left, top, clip, 0, region);
}

void mm_masked_blit_mask(const MMMaskedBitmap *m, int big, unsigned char *pixels, MMI32 stride, MMI32 height,
                         MMI32 left, MMI32 top, const MMMaskedRect *clip)
{
    blit(m, big, pixels, stride, height, left, top, clip, 1, NULL);
}

int mm_masked_transfer_resolve(MMI32 depth, const unsigned char *source, MMI32 source_count,
                               const unsigned char *target, MMI32 target_count, MMMaskedTransfer *transfer)
{
    static const unsigned char monochrome[8] = {0, 0, 0, 0, 255, 255, 255, 0};
    unsigned char indices[256];
    MMI32 i;
    MMU32 rgb;
    if (!source || source_count < 1 || source_count > 256)
        return 0;
    switch (depth)
    {
    case 1:
        if ((!target && target_count != 0) || (target && target_count != 2))
            return 0;
        if (!target)
            target = monochrome;
        target_count = 2;
        break;
    case 4:
    case 8:
        if (!target || target_count < 1 || target_count > (1 << depth))
            return 0;
        break;
    case 16:
    case 24:
        break;
    default:
        return 0;
    }
    transfer->mask_black = 0;
    transfer->mask_white = depth == 1 ? 1 : depth == 16 ? 0x7fff : 0xffffffUL;
    if (depth == 4 || depth == 8)
    {
        transfer->mask_black = mm_pixel_nearest(target, target_count, 0);
        transfer->mask_white = mm_pixel_nearest(target, target_count, 0xffffffUL);
    }
    transfer->background = mm_pixel_nearest(source, source_count, 0xffffffUL);
    if (depth == 8)
    {
        mm_pixel_translate8(source, source_count, target, target_count, indices);
        for (i = 0; i < 256; ++i)
            transfer->colours[i] = indices[i];
    }
    else
        for (i = 0; i < 256; ++i)
        {
            rgb = i < source_count ? mm_pixel_rgb(source + 4 * i) : 0;
            transfer->colours[i] = depth <= 4 ? mm_pixel_nearest(target, target_count, rgb)
                                             : depth == 16 ? mm_pixel_rgb555(rgb) : rgb;
        }
    return 1;
}

typedef struct MaskedRows
{
    size_t *offsets;
    unsigned char *scratch;
    MMI32 decoded;
} MaskedRows;

static int prepare_rows(const MMMaskedRle *source, MaskedRows *rows, unsigned planes)
{
    const unsigned char *lengths = (const unsigned char *)source->row_bytes;
    const unsigned short one = 1;
    const int big = *(const unsigned char *)&one == 0;
    size_t offset = 0, length;
    MMI32 y;
    if (!lengths || (!source->data && source->bytes) || source->width <= 0 || source->height <= 0 ||
        (size_t)source->width != (MMU32)source->width || (size_t)source->height != (MMU32)source->height ||
        (size_t)source->width > (size_t)-1 / planes || (size_t)source->height >= (size_t)-1 / sizeof(size_t))
        return 0;
    rows->offsets = (size_t *)malloc(((size_t)source->height + 1) * sizeof(size_t));
    rows->scratch = (unsigned char *)malloc((size_t)source->width * planes);
    if (!rows->offsets || !rows->scratch)
        return 0;
    for (y = 0; y < source->height; ++y, lengths += 2)
    {
        length = (size_t)word(lengths, 2, big);
        rows->offsets[y] = offset;
        if (length > 32767 || length > source->bytes - offset ||
            (length && !decode_row(source->data + offset, length, source->width, source->solid, 0, NULL, NULL)))
            return 0;
        offset += length;
    }
    rows->offsets[y] = offset;
    return offset == source->bytes;
}

static void load_row(const MMMaskedRle *source, MaskedRows *rows, MMI32 y)
{
    size_t offset, length;
    if (rows->decoded == y)
        return;
    offset = rows->offsets[y];
    length = rows->offsets[y + 1] - offset;
    decode_row(length ? source->data + offset : NULL, length, source->width, source->solid, source->fill,
               rows->scratch, rows->scratch + source->width);
    rows->decoded = y;
}

static MMI32 first_sample(MMI32 position, MMI32 source, MMI32 destination, MMI32 last, int reduce)
{
    if (!reduce || source <= destination)
        return last;
    return position ? mm_pixel_sample(position - 1, source, destination, 0) + 1 : 0;
}

static void combine_rows(const MMMaskedRle *source, MaskedRows *rows, MMI32 first, MMI32 last, unsigned char background)
{
    const size_t width = (size_t)source->width;
    const unsigned char *indices = rows->scratch, *opaque = indices + width;
    unsigned char *colours = rows->scratch + width * 2, *covered = colours + width;
    size_t x;
    MMI32 y;
    memset(colours, 255, width);
    memset(covered, 1, width);
    for (y = first; y <= last; ++y)
    {
        load_row(source, rows, y);
        for (x = 0; x < width; ++x)
        {
            covered[x] &= opaque[x];
            colours[x] &= opaque[x] ? indices[x] : background;
        }
    }
}

int mm_masked_stretch(const MMPixelSurface *surface, const MMMaskedRle *source, const MMMaskedRect *destination,
                      const MMMaskedRect *clip, const struct MMRegion *region, const unsigned char *palette, MMI32 count,
                      const MMMaskedTransfer *transfer, int and_shrink)
{
    MMRegionCursor cursor = {0};
    MaskedRows rows = {NULL, NULL, -1};
    MMMaskedRect bounds;
    MMI32 width, height, x, y, left, right, sx, sy, i, delta;
    unsigned char *line, *indices, *opaque, colour, covered;
    MMU32 value;
    int near, reduce, result = 0;
    if (!mm_pixel_surface_valid(surface) || !mm_pixel_rect_extent(destination, &width, &height) ||
        width <= 0 || height <= 0 ||
        (transfer ? surface->depth == 32 :
                    surface->depth != 8 && (surface->depth != 32 || !palette || count < 1 || count > 256)))
        return 0;
    if (!prepare_rows(source, &rows, transfer ? 4 : 2))
        goto done;
    near = transfer && width - source->width >= -1 && width - source->width <= 1 &&
           height - source->height >= -1 && height - source->height <= 1;
    reduce = transfer && and_shrink && !near;
    indices = rows.scratch + (transfer ? (size_t)source->width * 2 : 0);
    opaque = indices + source->width;
    bounds.left = bounds.top = 0;
    bounds.right = surface->width;
    bounds.bottom = surface->height;
    mm_pixel_rect_intersect(&bounds, destination);
    if (clip)
        mm_pixel_rect_intersect(&bounds, clip);
    result = 1;
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom)
        goto done;
    if (region)
        mm_region_cursor_open(&cursor, region, &bounds);
    for (y = bounds.top; y < bounds.bottom; ++y)
    {
        left = bounds.left;
        right = bounds.right;
        if (!region || mm_region_cursor_pair(&cursor, &left, &right))
        {
            line = (unsigned char *)surface->pixels + (size_t)y * (size_t)surface->pitch;
            delta = y - destination->top;
            sy = mm_pixel_sample(delta, source->height, height, near);
            if (transfer)
                combine_rows(source, &rows, first_sample(delta, source->height, height, sy, reduce), sy,
                             transfer->background);
            else
                load_row(source, &rows, sy);
            do
            {
                for (x = left; x < right; ++x)
                {
                    delta = x - destination->left;
                    sx = mm_pixel_sample(delta, source->width, width, near);
                    if (transfer)
                    {
                        colour = 255;
                        covered = 1;
                        for (i = first_sample(delta, source->width, width, sx, reduce); i <= sx; ++i)
                        {
                            colour &= indices[i];
                            covered &= opaque[i];
                        }
                        value = mm_pixel_read(line, x, surface->depth);
                        value = (value | (covered ? transfer->mask_white : transfer->mask_black)) &
                                transfer->colours[colour];
                        mm_pixel_write(line, x, surface->depth, value);
                    }
                    else if (opaque[sx])
                    {
                        colour = indices[sx];
                        if (surface->depth == 8)
                            line[x] = colour;
                        else if (colour < count)
                        {
                            value = mm_pixel_rgb(palette + 4 * colour);
                            memcpy(line + (size_t)x * 4, &value, 4);
                        }
                    }
                }
            } while (region && mm_region_cursor_pair(&cursor, &left, &right));
        }
        if (region && y + 1 < bounds.bottom)
            mm_region_scan_advance(&cursor.scan, 1);
    }
done:
    mm_region_cursor_close(&cursor);
    free(rows.scratch);
    free(rows.offsets);
    return result;
}

static int read_masked_bytes(const MMReadStream *stream, MMMaskedBitmap *m, const MMMaskedServices *s)
{
    if (stream->length < 28 || stream->length > MAXIMUM || (size_t)stream->length != stream->length)
        return 0;
    m->bytes = (MMI32)stream->length;
    if (!allocate_data(s, m->bytes, &m->data))
        return 0;
    if (!mm_read_exact(stream, 0, m->data, (size_t)m->bytes))
    {
        mm_masked_dispose(m, s);
        return 0;
    }
    return 1;
}
int mm_masked_read_stream(const MMReadStream *stream, MMMaskedBitmap *out, const MMMaskedServices *s)
{
    MMMaskedBitmap m = {0};
    if (!read_masked_bytes(stream, &m, s))
        return 0;
    if (!mm_masked_validate(&m, s->host_big_endian))
    {
        mm_masked_dispose(&m, s);
        return 0;
    }
    *out = m;
    return 1;
}

int mm_masked_rle_view(const MMMaskedBitmap *bitmap, int big, MMMaskedRle *out)
{
    const unsigned char *header = bitmap->data;
    MMMaskedRect bounds;
    MMU32 width, height;
    if (!header || bitmap->bytes < 28)
        return 0;
    mm_masked_bounds(bitmap, big, &bounds);
    width = (MMU32)bounds.right - (MMU32)bounds.left;
    height = (MMU32)bounds.bottom - (MMU32)bounds.top;
    memset(out, 0, sizeof(*out));
    if (!width || !height)
        return 1;
    if (width > MAXIMUM || height > (MAXIMUM - 28) / 2 || bitmap->row_bytes != (MMI32)height * 2 ||
        bitmap->row_bytes > bitmap->bytes - 28)
        return 0;
    out->data = header + 28 + bitmap->row_bytes;
    out->bytes = (size_t)(bitmap->bytes - 28 - bitmap->row_bytes);
    out->row_bytes = header + 28;
    out->width = (MMI32)width;
    out->height = (MMI32)height;
    out->solid = header[4] != 0;
    out->fill = header[5];
    return 1;
}

int mm_masked_native_big_endian(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

struct MMMaskedOwner
{
    MMResourceObject binding;
    MMReference references;
    const MMMaskedOwnerHost *host;
    MMMaskedBitmap bitmap;
};

static const MMMaskedServices *bitmap_services(const MMMaskedOwnerHost *host, MMMaskedServices *heap)
{
    if (host)
        return &host->bitmap;
    memset(heap, 0, sizeof(*heap));
    heap->host_big_endian = mm_masked_native_big_endian();
    return heap;
}

static MMMaskedOwner *create(const MMMaskedOwnerHost *host)
{
    const MMStorageAllocator *allocator = host ? host->objects : NULL;
    MMMaskedOwner *owner = allocator ? allocator->resize(allocator->context, NULL, 0, sizeof(*owner), MM_STORAGE_NORMAL)
                                    : malloc(sizeof(*owner));
    if (owner)
    {
        memset(owner, 0, sizeof(*owner));
        mm_reference_init(&owner->references);
        owner->host = host;
    }
    return owner;
}

static MMResourceObject *binding(void *object) { return &((MMMaskedOwner *)object)->binding; }
static MMReference *references(void *object) { return &((MMMaskedOwner *)object)->references; }

static void destroy(void *object)
{
    MMMaskedOwner *owner = object;
    const MMMaskedOwnerHost *host = owner->host;
    const MMStorageAllocator *allocator = host ? host->objects : NULL;
    mm_masked_dispose(&owner->bitmap, host ? &host->bitmap : NULL);
    mm_resource_object_dispose(owner, mm_masked_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, sizeof(*owner));
    else
        free(owner);
}

static void mark(void *object)
{
    MMMaskedOwner *owner = object;
    if (owner->host && owner->host->mark)
        owner->host->mark(owner);
}

const MMResourceObjectServices *mm_masked_owner_services(void)
{
    static const MMResourceObjectServices services = {binding, references, destroy, NULL, mark};
    return &services;
}

const MMMaskedBitmap *mm_masked_owner_bitmap(const MMMaskedOwner *owner) { return &owner->bitmap; }
const MMResourceObject *mm_masked_owner_binding(const MMMaskedOwner *owner) { return &owner->binding; }

void mm_masked_owner_retain(MMMaskedOwner *owner)
{
    if (owner)
        mm_reference_add(&owner->references);
}

void mm_masked_owner_release(MMMaskedOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_masked_owner_services());
}

void mm_masked_owner_priority(MMMaskedOwner *owner, MMI32 priority)
{
    mm_resource_object_priority(owner, priority, mm_masked_owner_services());
}

MMMaskedOwner *mm_masked_owner_new(const unsigned char *pixels, MMI32 row, MMI32 height, const MMMaskedRect *r,
                                   MMI32 x, MMI32 y, unsigned char transparent, unsigned flags, unsigned char fill,
                                   const MMMaskedOwnerHost *host)
{
    MMMaskedServices heap;
    MMMaskedOwner *owner = create(host);
    if (owner && !mm_masked_init(&owner->bitmap, pixels, row, height, r, x, y, transparent, flags, fill,
                                 bitmap_services(host, &heap)))
    {
        mm_masked_owner_release(owner);
        return NULL;
    }
    return owner;
}

static MMMaskedOwner *adopt(MMMaskedBitmap *bitmap, const MMMaskedOwnerHost *host)
{
    MMMaskedOwner *owner = create(host);
    if (owner)
        owner->bitmap = *bitmap;
    else
        mm_masked_dispose(bitmap, host ? &host->bitmap : NULL);
    return owner;
}

MMMaskedOwner *mm_masked_owner_read_stream(const MMReadStream *stream, const MMMaskedOwnerHost *host)
{
    MMMaskedServices heap;
    MMMaskedBitmap bitmap = {0};
    if (!mm_masked_read_stream(stream, &bitmap, bitmap_services(host, &heap)))
        return NULL;
    return adopt(&bitmap, host);
}

static MMMaskedOwner *read_masked_block(MMOwnedBlock *block, const MMMaskedOwnerHost *host,
                                        const MMMaskedServices *s)
{
    MMMaskedBitmap bitmap = {0};
    MMBlockIO io;
    MMReadStream stream;
    if (mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_io_stream(&io, &stream) != MM_DATA_OK ||
        !read_masked_bytes(&stream, &bitmap, s))
        return NULL;
    if (io.view.kind == MM_BLOCK_MEMORY)
        mm_owned_block_clear(block);
    if (mm_masked_validate(&bitmap, s->host_big_endian))
        return adopt(&bitmap, host);
    mm_masked_dispose(&bitmap, s);
    return NULL;
}

MMMaskedOwner *mm_masked_owner_read_block(MMOwnedBlock *block, const MMMaskedOwnerHost *host)
{
    MMMaskedServices heap;
    const MMMaskedServices *s = bitmap_services(host, &heap);
    if (block->view.packed && mm_owned_block_code(block, 0, s->context, s->decode, NULL) != MM_DATA_OK)
        return NULL;
    return read_masked_block(block, host, s);
}

int mm_masked_owner_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMMaskedOwnerHost *host)
{
    MMMaskedOwner *owner;
    if (!out)
    {
        *bytes = (MMI32)block->view.length;
        return 1;
    }
    owner = mm_masked_owner_read_block(block, host);
    if (!owner)
        return 0;
    *out = owner;
    *bytes = owner->bitmap.bytes;
    return 1;
}

int mm_masked_write(const MMMaskedBitmap *m, const MMStorageWriter *writer, const MMMaskedServices *s)
{
    if (!m->data || m->bytes < 28)
        return 0;
    canonical((unsigned char *)m->data, m->bytes, s);
    if ((MMU32)m->bytes != writer->length)
    {
        diagnostic(s, MM_MASKED_WRONG_SIZE);
        return 0;
    }
    return writer->write_at && writer->write_at(writer->context, 0, m->data, (size_t)m->bytes);
}

int mm_masked_owner_write(const MMMaskedOwner *owner, const MMStorageWriter *writer)
{
    MMMaskedServices heap;
    return mm_masked_write(&owner->bitmap, writer, bitmap_services(owner->host, &heap));
}

MMMaskedOwner *mm_masked_owner_import(void *filename, unsigned char transparent, MMI32 x, MMI32 y,
                                      unsigned flags, unsigned char fill, const MMMaskedOwnerHost *host)
{
    const MMMaskedServices *s;
    MMMaskedSource input;
    MMMaskedRect r;
    MMMaskedOwner *owner = NULL;
    if (!host || !host->bitmap.bitmap_read)
        return NULL;
    s = &host->bitmap;
    memset(&input, 0, sizeof(input));
    if (!s->bitmap_read(s->context, filename, transparent, &input))
        return NULL;
    r.left = r.top = 0;
    r.right = input.width;
    r.bottom = input.height;
    if (input.width > 0 && input.width <= MAXIMUM - 3)
        owner = mm_masked_owner_new((const unsigned char *)input.pixels, (input.width + 3) & ~3, input.height,
                                    &r, x, y, transparent, input.upside_down ? flags : flags ^ 1U, fill, host);
    free_data(s, &input.pixels);
    return owner;
}
