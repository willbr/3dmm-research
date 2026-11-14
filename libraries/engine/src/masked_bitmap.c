/* Masked bitmap policy converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/masked_bitmap.h"
#include "three_mm/wide.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static ThreeMMU32 word(const unsigned char *p, unsigned n, int big)
{
    ThreeMMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[big ? i : n - i - 1];
    return v;
}
static void put(unsigned char *p, unsigned n, ThreeMMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        p[big ? n - i - 1 : i] = (unsigned char)v;
        v >>= 8;
    }
}
static int subtract(ThreeMMI32 a, ThreeMMI32 b, ThreeMMI32 *out)
{
    if ((b > 0 && a < MINIMUM + b) || (b < 0 && a > MAXIMUM + b))
        return 0;
    *out = a - b;
    return 1;
}
static int row_encode(const unsigned char *src, ThreeMMI32 width, unsigned char transparent, int mask,
                      unsigned char *out)
{
    ThreeMMI32 i = 0, run = 0, n = 0, j;
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
static void canonical(unsigned char *b, ThreeMMI32 n, const ThreeMMMaskedServices *s)
{
    put(b, 2, 1, s->host_big_endian);
    put(b + 2, 2, s->system, s->host_big_endian);
    put(b + 6, 2, 0, s->host_big_endian);
    put(b + 24, 4, (ThreeMMU32)n, s->host_big_endian);
}
void three_mm_masked_dispose(ThreeMMMaskedBitmap *m, const ThreeMMMaskedServices *s)
{
    s->free_memory(s->context, &m->data);
    m->bytes = m->row_bytes = 0;
}
int three_mm_masked_init(ThreeMMMaskedBitmap *m, const unsigned char *pixels, ThreeMMI32 row, ThreeMMI32 height,
                         const ThreeMMMaskedRect *extract, ThreeMMI32 xref, ThreeMMI32 yref, unsigned char transparent,
                         unsigned flags, unsigned char fill, const ThreeMMMaskedServices *s)
{
    ThreeMMMaskedRect r;
    ThreeMMI32 x, y, sy, n, table, at, left = MAXIMUM, top = MAXIMUM, right = 0, bottom = 0;
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
        s->diagnostic(s->context, THREE_MM_MASKED_EMPTY);
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
    if (!s->allocate(s->context, n, &data))
        return 0;
    b = (unsigned char *)data;
    memset(b, 0, 28);
    canonical(b, n, s);
    b[4] = (unsigned char)mask;
    b[5] = fill;
    put(b + 8, 4, (ThreeMMU32)r.left, s->host_big_endian);
    put(b + 12, 4, (ThreeMMU32)r.top, s->host_big_endian);
    put(b + 16, 4, (ThreeMMU32)r.right, s->host_big_endian);
    put(b + 20, 4, (ThreeMMU32)r.bottom, s->host_big_endian);
    at = 28 + table;
    for (y = top; y < bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        count = row_encode(pixels + sy * row + left, right - left, transparent, mask, b + at);
        put(b + 28 + (y - top) * 2, 2, (ThreeMMU32)count, s->host_big_endian);
        at += count;
    }
    three_mm_masked_dispose(m, s);
    m->data = data;
    m->bytes = n;
    m->row_bytes = table;
    return 1;
}
void *three_mm_masked_new(const unsigned char *pixels, ThreeMMI32 row, ThreeMMI32 height, const ThreeMMMaskedRect *r,
                          ThreeMMI32 x, ThreeMMI32 y, unsigned char transparent, unsigned flags, unsigned char fill,
                          const ThreeMMMaskedServices *s)
{
    void *o = s->create(s->context);
    if (o && !three_mm_masked_init(s->state(s->context, o), pixels, row, height, r, x, y, transparent, flags, fill, s))
    {
        s->release(s->context, o);
        o = NULL;
    }
    return o;
}
void three_mm_masked_bounds(const ThreeMMMaskedBitmap *m, int big, ThreeMMMaskedRect *r)
{
    const unsigned char *b = (const unsigned char *)m->data;
    r->left = three_mm_signed_word(word(b + 8, 4, big));
    r->top = three_mm_signed_word(word(b + 12, 4, big));
    r->right = three_mm_signed_word(word(b + 16, 4, big));
    r->bottom = three_mm_signed_word(word(b + 20, 4, big));
}
int three_mm_masked_validate(ThreeMMMaskedBitmap *m, int big)
{
    unsigned char *b = (unsigned char *)m->data;
    ThreeMMMaskedRect r;
    ThreeMMU32 marker, width, height, i, offset;
    ThreeMMI32 n = m->bytes, rows, at, end, pos, dxp, k, len;
    int source = big;
    if (n < 28 || !b)
        return 0;
    marker = word(b, 2, big);
    if (marker == 256)
        source = !big;
    else if (marker != 1)
        return 0;
    if (word(b + 6, 2, source) || word(b + 24, 4, source) != (ThreeMMU32)n)
        return 0;
    if (source != big)
    {
        for (i = 0; i < 4; i += 2)
            put(b + i, 2, word(b + i, 2, source), big);
        for (i = 8; i < 28; i += 4)
            put(b + i, 4, word(b + i, 4, source), big);
        put(b, 2, 1, big);
    }
    three_mm_masked_bounds(m, big, &r);
    if (r.bottom == r.top)
    {
        if (n != 28)
            return 0;
        put(b + 16, 4, (ThreeMMU32)r.left, big);
        m->row_bytes = 0;
        return 1;
    }
    if (r.bottom < r.top || r.right <= r.left)
        return 0;
    width = (ThreeMMU32)r.right - (ThreeMMU32)r.left;
    height = (ThreeMMU32)r.bottom - (ThreeMMU32)r.top;
    if (width > MAXIMUM || height > (ThreeMMU32)(n - 28) / 2)
        return 0;
    rows = (ThreeMMI32)height * 2;
    at = 28 + rows;
    for (offset = 28; offset < (ThreeMMU32)(28 + rows); offset += 2)
    {
        len = (ThreeMMI32)word(b + offset, 2, source);
        if (source != big)
            put(b + offset, 2, (ThreeMMU32)len, big);
        if (len > 32767 || len > n - at)
            return 0;
        end = at + len;
        pos = at;
        dxp = 0;
        while (pos < end)
        {
            k = b[pos++];
            if ((ThreeMMU32)k > width - (ThreeMMU32)dxp)
                return 0;
            dxp += k;
            if (pos == end)
                break;
            k = b[pos++];
            if ((ThreeMMU32)k > width - (ThreeMMU32)dxp)
                return 0;
            dxp += k;
            if (!b[4])
            {
                if (k > end - pos)
                    return 0;
                pos += k;
            }
        }
        at = end;
    }
    if (at != n)
        return 0;
    m->row_bytes = rows;
    return 1;
}
int three_mm_masked_hit(const ThreeMMMaskedBitmap *m, int big, ThreeMMI32 x, ThreeMMI32 y)
{
    const unsigned char *b = (const unsigned char *)m->data;
    ThreeMMMaskedRect r;
    ThreeMMI32 row, at, end, len, pos, skip, count;
    if (!b || m->bytes < 28 || m->row_bytes < 0 || m->row_bytes > m->bytes - 28)
        return 0;
    three_mm_masked_bounds(m, big, &r);
    if (x < r.left || x >= r.right || y < r.top || y >= r.bottom || !subtract(x, r.left, &x) ||
        !subtract(y, r.top, &y) || y >= m->row_bytes / 2)
        return 0;
    at = 28 + m->row_bytes;
    for (row = 0; row <= y; ++row)
    {
        len = (ThreeMMI32)word(b + 28 + row * 2, 2, big);
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
void *three_mm_masked_read(void *block, const ThreeMMMaskedServices *s)
{
    ThreeMMMaskedBitmap m;
    void *o;
    memset(&m, 0, sizeof(m));
    if (!s->unpack(s->context, block))
        return NULL;
    m.bytes = s->size(s->context, block, 0);
    if (m.bytes < 28 || !s->allocate(s->context, m.bytes, &m.data))
        return NULL;
    if (!s->read(s->context, block, m.data, m.bytes))
        goto fail;
    if (s->is_memory(s->context, block))
        s->consume(s->context, block);
    if (!three_mm_masked_validate(&m, s->host_big_endian))
        goto fail;
    o = s->create(s->context);
    if (!o)
        goto fail;
    *s->state(s->context, o) = m;
    return o;
fail:
    three_mm_masked_dispose(&m, s);
    return NULL;
}
int three_mm_masked_fetch(void *block, int want, const ThreeMMMaskedServices *s)
{
    ThreeMMI32 n = s->size(s->context, block, 1);
    void *o;
    if (!want)
    {
        s->publish_fetch(s->context, 1, NULL, n);
        return 1;
    }
    if (!s->unpack(s->context, block))
        goto fail;
    n = s->size(s->context, block, 0);
    o = three_mm_masked_read(block, s);
    if (!o)
        goto fail;
    s->publish_fetch(s->context, 1, o, n);
    return 1;
fail:
    s->publish_fetch(s->context, 0, NULL, 0);
    return 0;
}
int three_mm_masked_write(ThreeMMMaskedBitmap *m, void *block, const ThreeMMMaskedServices *s)
{
    if (!m->data || m->bytes < 28)
        return 0;
    canonical((unsigned char *)m->data, m->bytes, s);
    if (m->bytes != s->size(s->context, block, 0))
    {
        s->diagnostic(s->context, THREE_MM_MASKED_WRONG_SIZE);
        return 0;
    }
    return s->write(s->context, block, m->data, m->bytes);
}
void *three_mm_masked_import(void *filename, unsigned char transparent, ThreeMMI32 x, ThreeMMI32 y, unsigned flags,
                             unsigned char fill, const ThreeMMMaskedServices *s)
{
    ThreeMMMaskedSource input;
    ThreeMMMaskedRect r;
    void *o = NULL;
    memset(&input, 0, sizeof(input));
    if (!s->bitmap_read(s->context, filename, transparent, &input))
        return NULL;
    r.left = r.top = 0;
    r.right = input.width;
    r.bottom = input.height;
    if (input.width > 0 && input.width <= MAXIMUM - 3)
        o = three_mm_masked_new((const unsigned char *)input.pixels, (input.width + 3) & ~3, input.height, &r, x, y,
                                transparent, input.upside_down ? flags : flags ^ 1U, fill, s);
    s->free_memory(s->context, &input.pixels);
    return o;
}
